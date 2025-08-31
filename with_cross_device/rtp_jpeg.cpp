#include "rtp_jpeg.h"

// RFC 2435 参照:
// - Main header: Type-specific(1), Fragment Offset(3), Type(1), Q(1), Width(1), Height(1)
// - Quantization table header (Q=128..255 の時は最初のパケットに挿入)
// - 各パケットの timestamp は同一、最後のパケットで Marker=1
// ref: Type/Q/Width/Height/Marker/Fragment offset 等の定義と振る舞い
//   → https://datatracker.ietf.org/doc/html/rfc2435 【3.1.1〜3.1.6 / 4.1 / 4.2 / 4.3】

namespace rtpjpeg {

static const uint8_t SOI[2] = {0xFF, 0xD8};
static const uint8_t EOI[2] = {0xFF, 0xD9};

static bool read_be16(const uint8_t* p, size_t len, size_t off, uint16_t& v){
  if(off+1>=len) return false; v = (uint16_t(p[off])<<8) | p[off+1]; return true;
}
static bool is_marker(const uint8_t* p, size_t off, uint8_t m){
  return p[off]==0xFF && p[off+1]==m;
}

// DQT を集め、SOS 後から EOI 直前までを scan として返す（8bitテーブルのみ対応）
bool extract_qtables_and_scan(const uint8_t* b, size_t L,
                              const uint8_t*& scan, size_t& scan_len,
                              Qtables& qt){
  scan = nullptr; scan_len = 0; qt.have = false;
  if(L<4 || b[0]!=SOI[0] || b[1]!=SOI[1]) return false;

  size_t i = 2;
  bool haveL = false, haveC = false;
  // 走査
  while(i+4<=L){
    if(b[i]!=0xFF){ i++; continue; } // フィル
    uint8_t m = b[i+1];
    if(m==0xDA){ // SOS
      uint16_t seglen; if(!read_be16(b,L,i+2,seglen)) return false;
      size_t sos_hdr_end = i+2+seglen;
      if(sos_hdr_end>=L) return false;
      // scan 開始
      size_t s = sos_hdr_end;
      // EOI 探索
      size_t e = s;
      while(e+2<=L){
        if(b[e]==0xFF){
          uint8_t n = b[e+1];
          if(n==0x00){ e+=2; continue; } // スタッフィング
          if(n>=0xD0 && n<=0xD7){ e+=2; continue; } // RSTn
          if(n==0xD9){ // EOI
            scan = b + s; scan_len = e - s; 
            qt.have = (haveL && haveC);
            return true;
          }
        }
        e++;
      }
      return false;
    } else if(m==0xDB){ // DQT
      uint16_t seglen; if(!read_be16(b,L,i+2,seglen)) return false;
      size_t p = i+4; size_t end = i+2+seglen;
      while(p<end){
        uint8_t pq_tq = b[p++]; 
        uint8_t pq = (pq_tq>>4)&0x0F; 
        uint8_t tq = pq_tq&0x0F;
        if(pq!=0){ // 16bitは未対応（Baseline想定）
          return false;
        }
        if(p+64>end) return false;
        if(tq==0){ memcpy(qt.lqt, b+p, 64); haveL=true; }
        else if(tq==1){ memcpy(qt.cqt, b+p, 64); haveC=true; }
        p+=64;
      }
      i = end; 
    } else {
      // 一般セグメント（長さあり）
      if(m==0xD8 || m==0xD9){ i+=2; continue; } // SOI/EOI（長さなし）
      uint16_t seglen; if(!read_be16(b,L,i+2,seglen)) return false;
      i += 2 + seglen;
    }
  }
  return false;
}

static inline uint8_t clamp_u8(int v){ return (v<0)?0:((v>255)?255:v); }

// emit() へ渡す1パケット分の RTP/JPEG「payload」（RTPヘッダを含まない）を組み立てる
bool packetize(const uint8_t* jpg, size_t jpg_len,
               uint16_t width, uint16_t height,
               JpegType type,
               uint8_t type_specific,
               uint32_t /*ts90k*/,
               size_t max_payload,
               std::function<bool(const uint8_t*, size_t, bool)> emit)
{
  if(!jpg || jpg_len<4 || !emit) return false;

  const uint8_t* scan=nullptr; size_t scan_len=0;
  Qtables qt{};
  if(!extract_qtables_and_scan(jpg, jpg_len, scan, scan_len, qt)){
    return false;
  }

  // Main JPEG header (8 bytes)
  // [0] Type-specific
  // [1..3] Fragment Offset (24bit) → 送出時に上書き
  // [4] Type (0:4:2:2, 1:4:2:0)
  // [5] Q (=255: 以降に Quantization Table header を付ける)
  // [6] Width  (8px 単位)
  // [7] Height (8px 単位)
  uint8_t mainhdr[8];
  mainhdr[0] = type_specific; // 0=progressive scanned（フルフレーム）
  mainhdr[1] = mainhdr[2] = mainhdr[3] = 0; // offset は後で
  mainhdr[4] = (type==JpegType::YUV420)?1:0;
  mainhdr[5] = 255; // Q=255: in-band quantization tables (dynamic)
  mainhdr[6] = uint8_t((width + 7) / 8);
  mainhdr[7] = uint8_t((height + 7) / 8);

  // Quantization Table header（Q=255 のため必須）
  // MBZ(1)=0, Precision(1)=0(8bit x2 tables), Length(2)=130 (=1+64 + 1+64)
  uint8_t qthdr[4 + 1 + 64 + 1 + 64];
  size_t  qthdr_len = sizeof(qthdr);
  qthdr[0]=0; qthdr[1]=0; qthdr[2]=0; qthdr[3]=130;
  qthdr[4]=0; memcpy(&qthdr[5],  qt.lqt, 64);
  qthdr[69]=1; memcpy(&qthdr[70], qt.cqt, 64);

  // 1パケット内に入る scan データ量を計算
  //   先頭パケット:  mainhdr(8) + qthdr(130) + scanChunk <= max_payload
  //   以降パケット:  mainhdr(8) + scanChunk <= max_payload
  size_t first_overhead = sizeof(mainhdr) + qthdr_len;
  size_t next_overhead  = sizeof(mainhdr);

  size_t off = 0;
  while(off < scan_len){
    const bool first = (off==0);
    const size_t overhead = first ? first_overhead : next_overhead;
    if(max_payload <= overhead) return false; // MTU が小さすぎる
    size_t chunk = scan_len - off;
    size_t max_chunk = max_payload - overhead;
    if(chunk > max_chunk) chunk = max_chunk;

    // payload バッファ（RTPヘッダは UdpAgent 側で付ける）
    static uint8_t payload[1500]; // 十分な大きさ（注意: スタックでなく静的）
    size_t pos = 0;

    // Main JPEG header（Fragment offset を埋めて push）
    uint8_t mh[8]; memcpy(mh, mainhdr, 8);
    // Fragment Offset は "JPEG frame data 内の位置"（=ここでは scan 内のオフセット）
    uint32_t fo = (uint32_t)off;
    mh[1] = uint8_t((fo >> 16) & 0xFF);
    mh[2] = uint8_t((fo >>  8) & 0xFF);
    mh[3] = uint8_t((fo      ) & 0xFF);
    memcpy(payload + pos, mh, 8); pos += 8;

    if(first){
      memcpy(payload + pos, qthdr, qthdr_len); pos += qthdr_len;
    }

    memcpy(payload + pos, scan + off, chunk); pos += chunk;

    const bool marker = (off + chunk) >= scan_len; // 最終パケット
    if(!emit(payload, pos, marker)) return false;

    off += chunk;
  }
  return true;
}

} // namespace

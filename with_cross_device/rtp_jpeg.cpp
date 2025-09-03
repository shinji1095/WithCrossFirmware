#include "rtp_jpeg.h"
#include "NetDebug.h"
#include <string.h>

namespace rtpjpeg {

static bool be16(const uint8_t* p, size_t len, size_t off, uint16_t& out){
  if (off + 1 >= len) return false;
  out = (uint16_t(p[off]) << 8) | p[off + 1];
  return true;
}

/*** DQT + SOS..EOI 抽出 **************************************************/
bool extract_qtables_and_scan(const uint8_t* b, size_t L,
                              const uint8_t*& scan, size_t& scan_len,
                              Qtables& qt)
{
  scan = nullptr; scan_len = 0; qt.have = false;
  memset(qt.lqt, 0, 64);
  memset(qt.cqt, 0, 64);

  if (!b || L < 4) return false;
  if (!(b[0] == 0xFF && b[1] == 0xD8)) return false; // SOI

  size_t i = 2;
  while (i + 3 < L) {
    if (b[i] != 0xFF) { i++; continue; }
    uint8_t m = b[i+1];

    // no-length markers
    if (m == 0xD8 /*SOI*/ || (m >= 0xD0 && m <= 0xD7) || m == 0x01) { i += 2; continue; }
    if (m == 0xD9 /*EOI*/) break;

    uint16_t seglen;
    if (!be16(b, L, i+2, seglen)) return false;
    size_t seg_start = i + 4;
    size_t seg_end   = i + 2 + seglen;
    if (seg_end > L) return false;

    if (m == 0xDB /*DQT*/) {
      // may contain multiple tables (we only support 8-bit)
      size_t p = seg_start;
      while (p < seg_end) {
        if (p + 1 >= seg_end) break;
        uint8_t pq_tq = b[p++]; // Pq(4), Tq(4)
        uint8_t pq = (pq_tq >> 4) & 0x0F;
        uint8_t tq = pq_tq & 0x0F;
        if (pq != 0) return false; // 16-bit tables not supported
        if (p + 64 > seg_end) return false;
        if (tq == 0) memcpy(qt.lqt, b + p, 64);
        else if (tq == 1) memcpy(qt.cqt, b + p, 64);
        p += 64;
      }
      // robust: allow one-sided DQT and clone
      bool hasL=false, hasC=false;
      for (int k=0; k<64; ++k){ hasL |= (qt.lqt[k]!=0); hasC |= (qt.cqt[k]!=0); }
      if (hasL && !hasC) memcpy(qt.cqt, qt.lqt, 64);
      if (!hasL && hasC) memcpy(qt.lqt, qt.cqt, 64);
      qt.have = (hasL || hasC);
      if (!qt.have) {
        LOGW("RTP/JPEG","skip frame: no DQT");
        return false;                // Q=255でQTable無しになるフレームは送らない
      }

    } else if (m == 0xDA /*SOS*/) {
      // scan = bytes after SOS segment until EOI (FFD9), RSTn are data
      size_t s = seg_end;
      size_t p = s;
      while (p + 1 < L) {
        if (b[p] == 0xFF) {
          uint8_t n = b[p+1];
          if (n == 0x00) { p += 2; continue; }             // stuffed 0xFF00
          if (n >= 0xD0 && n <= 0xD7) { p += 2; continue; } // RSTn inside scan
          if (n == 0xD9 /*EOI*/) {
            scan = b + s;
            scan_len = p - s;
            return (scan_len > 0);
          }
          p += 2; // skip unexpected marker
        } else {
          p++;
        }
      }
      return false; // no EOI
    }

    i = seg_end;
  }

  return false;
}

/*** SOF0 のサンプリング係数→RTP/JPEG Type 自動判定 *********************/
struct Samp { uint8_t yH=1,yV=1, cbH=1,cbV=1, crH=1,crV=1; bool ok=false; };
static bool parse_sof0_sampling(const uint8_t* b, size_t L, Samp& s){
  if (!b || L < 4) return false;
  size_t i = 2;
  while (i + 3 < L) {
    if (b[i] != 0xFF) { i++; continue; }
    uint8_t m = b[i+1];
    if (m == 0xD8 || (m>=0xD0 && m<=0xD7) || m==0x01) { i += 2; continue; }

    uint16_t seglen; if(!be16(b, L, i+2, seglen)) return false;
    size_t seg_start = i + 4, seg_end = i + 2 + seglen;
    if (seg_end > L) return false;

    if (m == 0xC0 /*SOF0: baseline*/) {
      if (seg_end - seg_start < 8) return false;
      uint8_t nf = b[seg_start + 5];
      size_t p = seg_start + 6;
      if (nf < 3 || p + nf*3 > seg_end) return false;
      for (int k=0;k<nf;k++){
        uint8_t cid = b[p++];
        uint8_t hv  = b[p++];
        (void)b[p++]; // tq
        uint8_t H = (hv>>4)&0x0F, V = hv&0x0F;
        if (cid == 1) { s.yH=H; s.yV=V; }
        else if (cid == 2){ s.cbH=H; s.cbV=V; }
        else if (cid == 3){ s.crH=H; s.crV=V; }
      }
      s.ok = true; return true;
    }
    if (m == 0xDA /*SOS*/) break;
    i = seg_end;
  }
  return false;
}

/*** DRI の有無（0xFFDD） ************************************************/
static bool find_dri(const uint8_t* b, size_t L, uint16_t& dri){
  dri = 0;
  if (!b || L<6 || !(b[0]==0xFF && b[1]==0xD8)) return false;
  size_t i=2;
  while (i+3<L){
    if (b[i]!=0xFF){ i++; continue; }
    uint8_t m=b[i+1];
    if (m==0xD8 || (m>=0xD0 && m<=0xD7) || m==0x01){ i+=2; continue; }
    uint16_t seglen; if(!be16(b,L,i+2,seglen)) return false;
    size_t seg_end=i+2+seglen; if(seg_end>L) return false;
    if (m==0xDD && seglen==4){
      uint16_t v; if(!be16(b,L,i+4,v)) return false;
      dri = v; return true;
    }
    if (m==0xDA) break;
    i=seg_end;
  }
  return false;
}

static inline size_t umin(size_t a, size_t b){ return (a<b)?a:b; }

/*** 公開API: packetize **************************************************/
bool packetize(const uint8_t* jpg, size_t jpg_len,
               uint16_t width, uint16_t height,
               JpegType hintType,
               uint8_t type_specific,
               uint32_t /*ts90k*/,
               size_t max_payload,
               std::function<bool(const uint8_t*, size_t, bool)> emit)
{
  if (!jpg || jpg_len<4 || max_payload<8 || !emit) return false;

  // 1) Q-table + scan
  const uint8_t* scan=nullptr; size_t scan_len=0;
  Qtables qt{};
  if (!extract_qtables_and_scan(jpg, jpg_len, scan, scan_len, qt)) return false;
  if (!qt.have) {
    LOGW("RTP/JPEG","skip frame: no DQT");
    return false; // Q=255運用でテーブル無しフレームは送らない
  }

  // 2) Type を SOF0 から自動判定（ヒントは後方互換用）
  JpegType effType = hintType;
  Samp sp{};
  if (parse_sof0_sampling(jpg, jpg_len, sp) && sp.ok) {
    if (sp.yH==2 && sp.yV==1 && sp.cbH==1 && sp.cbV==1 && sp.crH==1 && sp.crV==1)
      effType = JpegType::YUV422;
    else if (sp.yH==2 && sp.yV==2 && sp.cbH==1 && sp.cbV==1 && sp.crH==1 && sp.crV==1)
      effType = JpegType::YUV420;
  }

  // 3) DRI
  uint16_t dri_val=0; bool has_dri = (find_dri(jpg, jpg_len, dri_val) && dri_val>0);

  // 4) Main JPEG header（各パケット）
  uint8_t main8[8];
  main8[0] = type_specific;
  main8[1] = main8[2] = main8[3] = 0;
  uint8_t type_field = (effType == JpegType::YUV420) ? 1 : 0; // 0:422, 1:420
  if (has_dri) type_field |= 0x40;                            // +DRI
  main8[4] = type_field;
  main8[5] = 255;                                             // Q=255（先頭にQTableを送る）
  main8[6] = (uint8_t)((width  + 7)/8);
  main8[7] = (uint8_t)((height + 7)/8);

  // 5) 先頭パケット用：Restart(4B) → QTable(134B)
  uint8_t rmhdr[4]; size_t rm_len = 0;
  if (has_dri) {
    rmhdr[0] = (uint8_t)(dri_val>>8);
    rmhdr[1] = (uint8_t)(dri_val    );
    rmhdr[2] = 0xC0 | 0x3F;  // F=1,L=1, RestartCount上位6bit=0x3F
    rmhdr[3] = 0xFF;         // RestartCount下位8bit
    rm_len = 4;
  }
  
  // RFC2435: MBZ(1)=0, Precision(1)=0, Length(2)=128,
  // then Luma(64B) + Chroma(64B) – no table-ID bytes.
  uint8_t qthdr[4 + 64 + 64];
  qthdr[0] = 0;           // MBZ
  qthdr[1] = 0;           // Precision (all 8-bit)
  qthdr[2] = 0; qthdr[3] = 128;  // Length = 128
  memcpy(&qthdr[4],  qt.lqt, 64);
  memcpy(&qthdr[68], qt.cqt, 64);
  const size_t q_len = sizeof(qthdr);

  // 6) 断片化送出（最後のパケットのみ marker=true）
  size_t off = 0;
  while (off < scan_len) {
    const bool first = (off == 0);

    if (max_payload <= 8) return false;
    size_t overhead = 8 + (first?rm_len:0) + (first?q_len:0);
    if (overhead >= max_payload) return false;

    size_t room  = max_payload - overhead;
    size_t chunk = (scan_len - off < room) ? (scan_len - off) : room;

    // ★ emit前に「今回が最後か」を確定
    bool is_last = (off + chunk) >= scan_len;

    // ★ Fragment Offset は “今回の off” を固定して使う
    uint32_t fo_val = (uint32_t)off;
    if (first && fo_val != 0) fo_val = 0; // 安全弁（先頭は必ず0）

    uint8_t payload[1800];
    size_t pos = 0;

    // main8B with Fragment Offset
    uint8_t mh[8]; memcpy(mh, main8, 8);
    mh[1] = (uint8_t)(fo_val>>16);
    mh[2] = (uint8_t)(fo_val>>8);
    mh[3] = (uint8_t)(fo_val);
    memcpy(payload+pos, mh, 8); pos += 8;

    // Restart（DRIあり時、先頭のみ）
    if (first && rm_len) { memcpy(payload+pos, rmhdr, rm_len); pos += rm_len; }
    // QTable（Q=255：先頭のみ）
    if (first) { memcpy(payload+pos, qthdr, q_len); pos += q_len; }

    // scan chunk
    memcpy(payload+pos, scan+off, chunk); pos += chunk;

    if (pos > max_payload) return false;
    if (!emit(payload, pos, is_last)) return false;

    off += chunk;     // ★ 最後に増やす
  }
  return true;
}

} // namespace rtpjpeg

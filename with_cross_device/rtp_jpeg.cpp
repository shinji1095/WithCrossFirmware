#include "rtp_jpeg.h"
#include <string.h>

namespace rtpjpeg {

static const uint8_t SOI[2] = {0xFF, 0xD8};
static const uint8_t EOI[2] = {0xFF, 0xD9};

static inline uint16_t be16(const uint8_t* p){ return (uint16_t(p[0])<<8) | p[1]; }

// ======= オプション（必要に応じてONにする） =============================
// SOF2を見つけたら送出中止（Baselineのみ許可）
#ifndef RFC2435_ENFORCE_BASELINE
#define RFC2435_ENFORCE_BASELINE 1
#endif
// DHT(ハフマン)を検出したときに「標準テーブルのみ許可」チェックを行う
// 0: 無効（従来挙動） / 1: 形式的チェック（4本の表: DC0,DC1,AC0,AC1 が揃うかだけ確認）
// ※FFmpegのような「中身まで完全一致」検証はフラッシュ増が大きいので省略（必要なら後日対応）。
#ifndef RFC2435_CHECK_STD_HUFFMAN_LIGHT
#define RFC2435_CHECK_STD_HUFFMAN_LIGHT 0
#endif
// ======================================================================

// ---- 量子化テーブル（DQT）読み出し（8bitのみ） -------------------------
static bool read_dqt(const uint8_t* seg, size_t len, Qtables& qt){
  size_t p = 0;
  while(p < len){
    if(p >= len) break;
    uint8_t pq_tq = seg[p++];               // Pq(4)/Tq(4)
    uint8_t pq = (pq_tq>>4)&0x0F;
    uint8_t tq = pq_tq & 0x0F;
    if(pq != 0) return false;               // 16bit精度は非対応（RFC2435慣例）
    if(p + 64 > len) return false;


    // RFC2435: RTP側のQuantization Tableヘッダは JFIFのDQTと同じzig-zag並び。
    // そのまま64バイトをコピーする（並び替え不要）。
    if(tq==0) memcpy(qt.lqt, &seg[p], 64);
    else if(tq==1) memcpy(qt.cqt, &seg[p], 64);
    // tq==2,3 は無視（RFC2435はLuma/Chromaの2表が想定）

    p += 64;
  }
  qt.have = true;
  return true;
}

// ---- DHT（標準ハフマン表）形式チェック（軽量） --------------------------
// RFC2435は標準DHT前提。ここでは「DC(Y)=0, DC(C)=1, AC(Y)=0x10, AC(C)=0x11 の4表」
// がJPEGヘッダに存在するかだけを見る軽量チェック（中身一致までは検証しない）。
static bool looks_like_std_huffman_light(const uint8_t* b, size_t L){
#if RFC2435_CHECK_STD_HUFFMAN_LIGHT
  if(L<4) return true;
  size_t i = 2;
  bool have_dc0=false, have_dc1=false, have_ac0=false, have_ac1=false;

  while(i+4<=L){
    if(b[i]!=0xFF){ i++; continue; }
    uint8_t m = b[i+1];
    if(m == 0xC4){ // DHT
      uint16_t seglen = be16(&b[i+2]); // len includes the 2 bytes of length
      size_t p = i + 4;
      size_t end = i + 2 + seglen;
      if(end > L) return false;

      while(p < end){
        if(p+17 > end) break;
        uint8_t tc_th = b[p++];     // Tc(4):0=DC,1=AC / Th(4):table id
        uint8_t tc = (tc_th>>4)&0x0F;
        uint8_t th = (tc_th   )&0x0F;

        // bits[16]
        int count = 0;
        for(int k=0;k<16;k++){ if(p>=end) return false; count += b[p++]; }
        // values[count]
        if(p+count > end) return false;
        p += count;

        if(tc==0 && th==0) have_dc0 = true;
        else if(tc==0 && th==1) have_dc1 = true;
        else if(tc==1 && th==0) have_ac0 = true;
        else if(tc==1 && th==1) have_ac1 = true;
        // 他IDの表は無視
      }
      i = end;
    }
    else if(m==0xDA /*SOS*/ || m==0xD9 /*EOI*/){
      break; // ヘッダ領域終了
    }
    else {
      uint16_t seglen = be16(&b[i+2]);
      if(i + 2 + seglen > L) return false;
      i += 2 + seglen;
    }
  }
  // 4本すべて揃っていると“標準っぽい”
  return have_dc0 && have_dc1 && have_ac0 && have_ac1;
#else
  (void)b; (void)L; return true;
#endif
}

// ---- SOS..EOI の scan を抽出（RSTn/FF00はそのまま）＋ DQT 収集 ---------
bool extract_qtables_and_scan(const uint8_t* b, size_t L,
                              const uint8_t*& scan, size_t& scan_len,
                              Qtables& qt){
  scan = nullptr; scan_len = 0; qt = Qtables{};
  if(L<4 || b[0]!=SOI[0] || b[1]!=SOI[1]) return false;

  size_t i = 2;
  while(i+4<=L){
    if(b[i]!=0xFF){ i++; continue; }
    uint8_t m = b[i+1];

    if(m == 0xDA){ // SOS
      uint16_t seglen = be16(&b[i+2]);
      size_t   sos_hdr_end = i + 2 + seglen;
      if(sos_hdr_end >= L) return false;

      size_t s = sos_hdr_end; // scan start
      size_t e = s;
      while(e+2 <= L){
        if(b[e]==0xFF){
          uint8_t n = b[e+1];
          if(n==0x00){ e+=2; continue; }            // stuffed
          if(n>=0xD0 && n<=0xD7){ e+=2; continue; } // RSTn
          if(n==0xD9){                               // EOI
            scan = b + s;
            scan_len = e - s;                        // EOIは含めない（FFmpeg踏襲）
            if(!qt.have){ memcpy(qt.cqt, qt.lqt, 64); qt.have=true; }
            return true;
          }
        }
        e++;
      }
      return false;
    }
    else if(m == 0xDB){ // DQT
      uint16_t seglen = be16(&b[i+2]);
      if(i + 2 + seglen > L) return false;
      const uint8_t* seg = &b[i+4];
      size_t segpayload = seglen - 2;
      if(!read_dqt(seg, segpayload, qt)) return false;
      i += 2 + seglen;
    }
    else if(m==0xD8 || m==0xD9){ // SOI/EOI
      i += 2;
    }
    else {
      uint16_t seglen = be16(&b[i+2]);
      if(i + 2 + seglen > L) return false;
      i += 2 + seglen;
    }
  }
  return false;
}

// ---- SOF0/DRI 解析（422H/422V/420/444/411 へ厳密写像） -------------------
bool detect_params(const uint8_t* b, size_t L, JpegParams& out){
  out.type = JpegType::JPEG_422H;
  out.dri  = 0;
  out.has_sof0 = false;

  if(L<4 || b[0]!=SOI[0] || b[1]!=SOI[1]) return false;
  size_t i = 2;

  struct Comp { uint8_t id=0, H=1, V=1; };
  Comp comps[3]; int ncomp_found = 0;

  while(i+4<=L){
    if(b[i]!=0xFF){ i++; continue; }
    uint8_t m = b[i+1];

#if RFC2435_ENFORCE_BASELINE
    if(m == 0xC2){ // SOF2 Progressive → Baseline前提に反する
      return false; // 以降はフォールバック（type_hint）か、上位で破棄判断
    }
#endif

    if(m == 0xC0){ // SOF0 Baseline
      uint16_t seglen = be16(&b[i+2]);
      size_t p = i + 4;
      if(i + 2 + seglen > L) return false;

      /* P */ p++;
      /* Y */ p+=2;
      /* X */ p+=2;
      uint8_t nf = b[p++];
      ncomp_found = (nf>3)?3:nf;

      for(int k=0;k<ncomp_found;k++){
        if(p+3 > i+2+seglen) break;
        uint8_t cid = b[p++];
        uint8_t hv  = b[p++];
        /* tq */    p++;
        comps[k].id = cid;
        comps[k].H  = (hv>>4)&0x0F;
        comps[k].V  = (hv   )&0x0F;
      }

      // 最大(H*V)をYとみなす
      int yix = 0;
      for(int k=1;k<ncomp_found;k++){
        int a = comps[k].H*comps[k].V;
        int b_ = comps[yix].H*comps[yix].V;
        if(a>b_) yix=k;
      }
      int ix1 = (yix==0)?1:0;
      int ix2 = (yix==2)?1:2;

      if(ncomp_found < 3){
        out.has_sof0 = false; // 非YCbCrっぽい
      }else{
        auto is11=[](uint8_t h,uint8_t v){return h==1 && v==1;};
        uint8_t yh=comps[yix].H, yv=comps[yix].V;
        uint8_t c1h=comps[ix1].H,c1v=comps[ix1].V;
        uint8_t c2h=comps[ix2].H,c2v=comps[ix2].V;

        if(yh==2&&yv==1&&is11(c1h,c1v)&&is11(c2h,c2v)){ out.type=JpegType::JPEG_422H; out.has_sof0=true; }
        else if(yh==2&&yv==2&&is11(c1h,c1v)&&is11(c2h,c2v)){ out.type=JpegType::JPEG_420 ; out.has_sof0=true; }
        else if(yh==4&&yv==1&&is11(c1h,c1v)&&is11(c2h,c2v)){ out.type=JpegType::JPEG_411 ; out.has_sof0=true; }
        else if(yh==1&&yv==1&&is11(c1h,c1v)&&is11(c2h,c2v)){ out.type=JpegType::JPEG_444 ; out.has_sof0=true; }
        else if(yh==1&&yv==2&&is11(c1h,c1v)&&is11(c2h,c2v)){ out.type=JpegType::JPEG_422V; out.has_sof0=true; }
        else { out.has_sof0=false; }
      }
      i += 2 + seglen;
    }
    else if(m == 0xDD){ // DRI
      if(i+6 > L) return false;
      uint16_t seglen = be16(&b[i+2]);
      if(seglen<4 || i+2+seglen > L) return false;
      out.dri = be16(&b[i+4]);
      i += 2 + seglen;
    }
    else if(m==0xDA){ i += 2; }
    else if(m==0xD8 || m==0xD9){ i += 2; }
    else {
      uint16_t seglen = be16(&b[i+2]);
      if(i + 2 + seglen > L) return false;
      i += 2 + seglen;
    }
  }
  return true;
}

// ---- Restart header（RFC2435 3.1.5） --------------------------------------
static void write_rsthdr(uint8_t* p, uint16_t dri){
  p[0]=(dri>>8)&0xFF; p[1]=dri&0xFF;
  uint16_t flc = 0xC000 | 0x3FFF; // F=1,L=1,Count=0x3FFF
  p[2]=(flc>>8)&0xFF; p[3]=flc&0xFF;
}

// ---- パケット化 ------------------------------------------------------------
bool packetize(const uint8_t* jpg, size_t jpg_len,
               uint16_t width, uint16_t height,
               JpegType type_hint, uint8_t type_specific,
               uint32_t /*ts90k*/, size_t max_payload,
               std::function<bool(const uint8_t*, size_t, bool)> emit)
{
  if(!jpg || jpg_len<4 || !emit) return false;

  // 0) 送信前の軽量チェック（オプション）
  if(!looks_like_std_huffman_light(jpg, jpg_len)) {
    // 標準ハフマンでない可能性 → RFC2435上は非推奨
    // 運用上は false を返して上位で RAW-UDP や WS /stream に切替えるのが安全
    return false;
  }

  // 1) SOS..EOI の scan と DQT を抽出
  const uint8_t* scan=nullptr; size_t scan_len=0;
  Qtables qt{};
  if(!extract_qtables_and_scan(jpg, jpg_len, scan, scan_len, qt)) return false;

  // 2) SOF0/DRI 解析（Baseline以外は detect_params 内のガードで抑止可）
  JpegParams prm{};
  if(!detect_params(jpg, jpg_len, prm) || !prm.has_sof0){
    // SOF0不明など → 従来通りヒントへフォールバック（後方互換）
    prm.type = type_hint;
    prm.dri  = 0;
  }

  // 3) Main JPEG header（8B）
  uint8_t mainhdr[8];
  mainhdr[0] = type_specific;
  mainhdr[1] = mainhdr[2] = mainhdr[3] = 0; // Fragment offset（都度上書き）
  uint8_t baseType = static_cast<uint8_t>(prm.type);
  mainhdr[4] = prm.dri ? uint8_t(baseType + 64) : baseType;
  mainhdr[5] = 255;                                    // Q=255: Quant header 同梱
  mainhdr[6] = uint8_t((width  + 7) / 8);              // 8px ブロック単位（FFmpeg同等）
  mainhdr[7] = uint8_t((height + 7) / 8);

  // 4) Quantization Table header（Luma/Chroma の2表=130B）
  uint8_t qthdr[4 + 1 + 64 + 1 + 64];
  qthdr[0]=0; qthdr[1]=0; qthdr[2]=0; qthdr[3]=130;    // Length=130
  qthdr[4]=0; memcpy(&qthdr[5],  qt.lqt, 64);
  qthdr[69]=1; memcpy(&qthdr[70], qt.cqt, 64);
  const size_t qthdr_len = sizeof(qthdr);

  // 5) Restart header（DRI>0 のときのみ）
  uint8_t rsthdr[4]; size_t rst_len = 0;
  if(prm.dri){ write_rsthdr(rsthdr, prm.dri); rst_len = 4; }

  // 6) 分割送出
  const size_t first_overhead = sizeof(mainhdr) + rst_len + qthdr_len;
  const size_t next_overhead  = sizeof(mainhdr);

  size_t off = 0;
  while(off < scan_len){
    const bool first = (off==0);
    const size_t overhead = first ? first_overhead : next_overhead;
    if(max_payload <= overhead) return false;

    size_t chunk = scan_len - off;
    size_t max_chunk = max_payload - overhead;
    if(chunk > max_chunk) chunk = max_chunk;

    static uint8_t payload[1800]; // 1200+ヘッダに十分
    size_t pos = 0;

    // Main header（フラグメントオフセット埋め）
    uint8_t mh[8]; memcpy(mh, mainhdr, 8);
    uint32_t fo = (uint32_t)off;
    mh[1] = uint8_t((fo>>16)&0xFF);
    mh[2] = uint8_t((fo>>8 )&0xFF);
    mh[3] = uint8_t((fo    )&0xFF);
    memcpy(payload+pos, mh, 8); pos += 8;

    if(first && rst_len){ memcpy(payload+pos, rsthdr, rst_len); pos += rst_len; }
    if(first){ memcpy(payload+pos, qthdr, qthdr_len); pos += qthdr_len; }

    memcpy(payload+pos, scan+off, chunk); pos += chunk;

    const bool marker = (off + chunk) >= scan_len; // 最後のパケットのみ M=1
    if(!emit(payload, pos, marker)) return false;

    off += chunk;
  }
  return true;
}

} // namespace rtpjpeg

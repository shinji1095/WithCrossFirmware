#pragma once
#include <Arduino.h>
#include <functional>

namespace rtpjpeg {

// Quant tables container (zig-zag order)
struct Qtables {
  bool    have = false;
  uint8_t lqt[64] = {0};  // Luma   (ID=0)
  uint8_t cqt[64] = {0};  // Chroma (ID=1)
};

// RFC2435 Type（数値は RFC2435 と一致）
// 旧コード互換のため、YUV422/YUV420 という“旧名”も同じ値で定義しておく
enum class JpegType : uint8_t {
  // --- 旧名（後方互換用） ---
  YUV422   = 0,  // = JPEG_422H
  YUV420   = 1,  // = JPEG_420

  // --- 正式名（RFC2435に対応） ---
  JPEG_422H = YUV422, // 4:2:2 horizontal  Y=2x1, Cb/Cr=1x1
  JPEG_420  = YUV420, // 4:2:0            Y=2x2, Cb/Cr=1x1
  JPEG_444  = 2,      // 4:4:4            Y=1x1, Cb/Cr=1x1
  JPEG_411  = 3,      // 4:1:1            Y=4x1, Cb/Cr=1x1
  JPEG_422V = 4       // 4:2:2 vertical   Y=1x2, Cb/Cr=1x1
};

// Parameters detected from SOF0/DRI
struct JpegParams {
  JpegType type;      // sampling type
  uint16_t dri;       // restart interval (MCUs), 0 if none
  bool     has_sof0;  // true if we could read SOF0 & map a known type
};

// Extract DQT (convert to zig-zag order) and get scan(SOS..EOI) region.
bool extract_qtables_and_scan(const uint8_t* jpg, size_t len,
                              const uint8_t*& scan, size_t& scan_len,
                              Qtables& qt);

// Parse SOF0/DRI and fill JpegParams (returns true on parse success).
bool detect_params(const uint8_t* jpg, size_t len, JpegParams& out);

// Packetize JPEG to RFC2435 RTP/JPEG payloads.
// - Q=255: put Quantization Table header in the first payload.
// - If DRI>0, add Restart Header in the first payload and set Type += 64.
bool packetize(const uint8_t* jpg, size_t jpg_len,
               uint16_t width, uint16_t height,
               JpegType type_hint,                 // fallback when unknown
               uint8_t type_specific,              // usually 0
               uint32_t ts90k,                     // RTP timestamp (90kHz)
               size_t max_payload,                 // MTU for payload
               std::function<bool(const uint8_t*, size_t, bool)> emit);

} // namespace rtpjpeg

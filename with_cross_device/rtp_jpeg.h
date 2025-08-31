#pragma once
#include <Arduino.h>
#include <functional>

// JPEG から DQT(量子化テーブル) を抽出し、SOS〜EOI の「単一スキャンのエントロピー符号部」を分割して
// RTP/JPEG ペイロードを生成（Q=255: Quantization Table header を同梱）
namespace rtpjpeg {

struct Qtables {
  bool    have = false;
  uint8_t lqt[64] = {0};  // table 0
  uint8_t cqt[64] = {0};  // table 1
};

// JPEG (JFIForMJPEG) から DQT と scan 部を取り出す（Baseline 前提）
bool extract_qtables_and_scan(const uint8_t* jpg, size_t len,
                              const uint8_t*& scan, size_t& scan_len,
                              Qtables& qt);

// JPEG type: 0=4:2:2, 1=4:2:0 (RFC2435 3.1.3と4.1の定義)
enum class JpegType : uint8_t { YUV422 = 0, YUV420 = 1 };

// RTP/JPEG の各ペイロード（RTPヘッダ以外）を順次 emit する。
// emit(payload_ptr, payload_size, marker_last_packet)
bool packetize(const uint8_t* jpg, size_t jpg_len,
               uint16_t width, uint16_t height,
               JpegType type,
               uint8_t type_specific,         // 通常 0（プログレッシブ）
               uint32_t ts90k,                // 同一フレームは同一TS
               size_t max_payload,            // RTPヘッダを除く上限（例:1200）
               std::function<bool(const uint8_t*, size_t, bool)> emit);
} // namespace

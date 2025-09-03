#pragma once
#include <Arduino.h>
#include <functional>
#include <stddef.h>
#include <stdint.h>

namespace rtpjpeg {

struct Qtables {
  bool    have = false;
  uint8_t lqt[64] = {0};  // table 0 (luma)
  uint8_t cqt[64] = {0};  // table 1 (chroma)
};

// Extract DQT (quantization tables) and the entropy-coded scan data range (SOS..EOI).
// Returns true on success. 'scan' points to the first byte after SOS header,
// and 'scan_len' is the number of bytes before the EOI marker (FFD9).
bool extract_qtables_and_scan(const uint8_t* jpg, size_t len,
                              const uint8_t*& scan, size_t& scan_len,
                              Qtables& qt);

// JPEG type: 0=4:2:2, 1=4:2:0 (RFC2435 3.1.3/4.1)
enum class JpegType : uint8_t { YUV422 = 0, YUV420 = 1 };

// Build RTP/JPEG payloads (without RTP header) and emit them one by one.
// emit(payload_ptr, payload_size, marker_is_last_packet)
bool packetize(const uint8_t* jpg, size_t jpg_len,
               uint16_t width, uint16_t height,
               JpegType type,
               uint8_t type_specific,         // usually 0 (progressive)
               uint32_t ts90k,                // same timestamp for all packets of a frame
               size_t max_payload,            // max payload size excluding 12B RTP header
               std::function<bool(const uint8_t*, size_t, bool)> emit);

} // namespace rtpjpeg

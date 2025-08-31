#pragma once
#include <Arduino.h>
#include <lwip/sockets.h>
#include <netinet/in.h>
#include "config.h"

class UdpAgent {
public:
  enum class Mode { RAW_JPEG_DATAGRAM, RTP_JPEG };

  bool begin(const char* dst_ip, uint16_t dst_port,
             Mode mode = Mode::RTP_JPEG);
  bool ready() const { return _sock >= 0; }

  // 旧来互換（RAW用）
  bool sendFrame(const uint8_t* jpg, size_t len, uint32_t backoffMs=0);

  // RTP/JPEG（RFC2435）
  bool sendRtpJpegFrame(const uint8_t* jpg, size_t len,
                        uint16_t w, uint16_t h,
                        uint32_t backoffMs=0);

  // 統計
  void tick1sReport(); // 1秒毎にログ出力

private:
  int         _sock = -1;
  sockaddr_in _peer{};
  Mode        _mode = Mode::RTP_JPEG;

  // RTP state
  uint16_t _seq = 1;
  uint32_t _ts  = 0;
  uint32_t _pkt_in_1s = 0;
  uint32_t _drop_in_1s = 0;
  uint32_t _t_last_report = 0;

  bool sendRtpPacket(const uint8_t* payload, size_t payload_len, bool marker);
};

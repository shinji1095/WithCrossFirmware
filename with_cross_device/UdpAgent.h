#pragma once
#include <WiFi.h>
#include <WiFiUdp.h>

class UdpAgent {
public:
  // Set destination and open an ephemeral local port.
  bool begin(const char* host, uint16_t port);

  // True when destination is set and Wi-Fi is connected.
  bool ready() const;

  // For symmetry with other agents (currently no-op).
  void loop();

  // Send one JPEG frame via a single datagram.
  bool sendFrame(const uint8_t* buf, size_t len, uint32_t backoffMs = 0);

private:
  WiFiUDP   udp_;
  IPAddress dst_;
  uint16_t  dport_ = 0;
  bool      begun_ = false;
  uint32_t  next_ok_after_ = 0;
  uint16_t  consec_fail_ = 0;
  uint16_t  frame_id_ = 0;

  bool sendOne(const uint8_t* data, size_t len);

  // --- (future extension; unused now) simple fragment header -------------
  // struct FragHeader { uint16_t frame_id, total, idx, len; } __attribute__((packed));
};

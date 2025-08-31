#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "UdpAgent.h"
#include "config.h"

class RtspServer {
public:
  bool begin(uint16_t port = RTSP_PORT);
  void loop(); // accept / parse / keepalive
  bool isPlaying() const { return _playing; }

  // カメラフレームを送る（内部の UdpAgent が RTP/JPEG 送出）
  bool sendJpegFrame(const uint8_t* jpg, size_t len, uint16_t w, uint16_t h);

private:
  WiFiServer  _srv{RTSP_PORT};
  WiFiClient  _cli;
  bool        _playing = false;
  String      _session = "0001abcd";
  String      _reqbuf;

  // client RTP/RTCP
  IPAddress   _cli_ip;
  uint16_t    _cli_rtp = 0, _cli_rtcp = 0;

  UdpAgent    _udp;

  void handleRequest(const String& req);
  String makeSdp(uint16_t w, uint16_t h) const;
};

#include "UdpAgent.h"
#include "NetDebug.h"
#include "rtp_jpeg.h"
#include <string.h>

bool UdpAgent::begin(const char* ip, uint16_t port, Mode mode){
  if(_sock>=0) { close(_sock); _sock=-1; }
  _mode = mode;

  _sock = socket(AF_INET, SOCK_DGRAM, 0);
  if(_sock<0){ LOGE("UDP","socket() fail"); return false; }

  // Low-latency hint
  int tos = 0x10; // IPTOS_LOWDELAY
  setsockopt(_sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));

  memset(&_peer, 0, sizeof(_peer));
  _peer.sin_family = AF_INET;
  _peer.sin_port   = htons(port);
  _peer.sin_addr.s_addr = inet_addr(ip);

  _seq = 1;
  _ts  = 0;
  _t_last_report = millis();
  _pkt_in_1s = _drop_in_1s = 0;

  LOGI("UDP","dst=%s:%u mode=%s", ip, (unsigned)port,
       (_mode==Mode::RTP_JPEG) ? "RTP/JPEG" : "RAW-JPEG");
  return true;
}

bool UdpAgent::sendFrame(const uint8_t* jpg, size_t len, uint32_t){
  if(_sock<0) return false;
  if(_mode==Mode::RTP_JPEG){
    // Protect against misuse
    return sendRtpJpegFrame(jpg, len, CAM_WIDTH, CAM_HEIGHT);
  }
  ssize_t n = sendto(_sock, (const char*)jpg, len, 0, (sockaddr*)&_peer, sizeof(_peer));
  if(n<0){ _drop_in_1s++; return false; }
  _pkt_in_1s++; return true;
}

bool UdpAgent::sendRtpJpegFrame(const uint8_t* jpg, size_t len,
                                uint16_t w, uint16_t h, uint32_t){
  if(_sock<0) return false;

  if(_ts==0){
    // First TS arbitrary. Then advance by 90k/fps each frame.
    _ts = millis() * 90;
  }else{
    _ts += (uint32_t)(90000 / CAM_FPS);
  }

  auto emit = [&](const uint8_t* payload, size_t paylen, bool marker_last)->bool {
    uint8_t rtp[12];
    rtp[0] = 0x80;                              // V=2,P=0,X=0,CC=0
    rtp[1] = (uint8_t)((marker_last ? 0x80 : 0) | 26);
    rtp[2] = (uint8_t)(_seq >> 8);
    rtp[3] = (uint8_t)(_seq & 0xFF);
    rtp[4] = (uint8_t)(_ts >> 24);
    rtp[5] = (uint8_t)(_ts >> 16);
    rtp[6] = (uint8_t)(_ts >> 8);
    rtp[7] = (uint8_t)(_ts);
    // SSRC: fixed or configurable
    uint32_t ssrc = 0x13572468u;
    rtp[8]  = (uint8_t)((ssrc >> 24) & 0xFF);
    rtp[9]  = (uint8_t)((ssrc >> 16) & 0xFF);
    rtp[10] = (uint8_t)((ssrc >> 8)  & 0xFF);
    rtp[11] = (uint8_t)((ssrc      ) & 0xFF);

    uint8_t buf[1600];
    if (12 + paylen > sizeof(buf)) { _drop_in_1s++; return false; }
    memcpy(buf, rtp, 12);
    memcpy(buf+12, payload, paylen);

    ssize_t n = sendto(_sock, (const char*)buf, 12 + paylen, 0, (sockaddr*)&_peer, sizeof(_peer));
    if(n<0){ _drop_in_1s++; return false; }
    _seq++; _pkt_in_1s++; return true;
  };

  // OV2640 is typically 4:2:2 → Type=0
  return rtpjpeg::packetize(jpg, len, w, h,
                            rtpjpeg::JpegType::YUV422,
                            /*type_specific=*/0,
                            _ts, RTP_PAYLOAD_MTU, emit);
}

void UdpAgent::tick1sReport(){
  if(millis() - _t_last_report >= 1000){
    LOGI("RTP","fps~%u, pkt=%u, drop=%u",
         (unsigned)CAM_FPS, (unsigned)_pkt_in_1s, (unsigned)_drop_in_1s);
    _pkt_in_1s=_drop_in_1s=0;
    _t_last_report = millis();
  }
}

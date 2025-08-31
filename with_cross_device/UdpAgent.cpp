#include "UdpAgent.h"
#include "NetDebug.h"
#include "rtp_jpeg.h"

bool UdpAgent::begin(const char* ip, uint16_t port, Mode mode){
  if(_sock>=0) { close(_sock); _sock=-1; }
  _mode = mode;

  _sock = socket(AF_INET, SOCK_DGRAM, 0);
  if(_sock<0){ LOGE("UDP","socket() fail"); return false; }

  // 低遅延: IP_TOS=LOWDELAY(0x10)
  int tos = 0x10;
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
       (_mode==Mode::RTP_JPEG)?"RTP/JPEG":"RAW-JPEG");
  return true;
}

bool UdpAgent::sendFrame(const uint8_t* jpg, size_t len, uint32_t){
  if(_sock<0) return false;
  if(_mode==Mode::RTP_JPEG){
    // 誤用防止
    return sendRtpJpegFrame(jpg, len, CAM_WIDTH, CAM_HEIGHT);
  }
  ssize_t n = sendto(_sock, jpg, len, 0, (sockaddr*)&_peer, sizeof(_peer));
  if(n<0){ _drop_in_1s++; return false; }
  _pkt_in_1s++; return true;
}

bool UdpAgent::sendRtpJpegFrame(const uint8_t* jpg, size_t len,
                                uint16_t w, uint16_t h, uint32_t){
  if(_sock<0) return false;

  if(_ts==0){
    // 第1フレームTSは任意。以降は 90000/fps ずつ増分
    _ts = millis() * 90; // 近似（次フレームで上書き）
  }else{
    _ts += (uint32_t)(90000 / CAM_FPS); // RFC2435: 90kHz TS
  }

  auto emit = [&](const uint8_t* payload, size_t paylen, bool marker)->bool{
    // RTP header (12 bytes)
    uint8_t rtp[12];
    rtp[0] = 0x80;                           // V=2,P=0,X=0,CC=0
    rtp[1] = (marker?0x80:0x00) | (RTP_PT_JPEG & 0x7F); // M/PT
    rtp[2] = (_seq>>8)&0xFF; rtp[3] = _seq & 0xFF;
    rtp[4] = (_ts>>24)&0xFF; rtp[5] = (_ts>>16)&0xFF;
    rtp[6] = (_ts>> 8)&0xFF; rtp[7] = (_ts    )&0xFF;
    rtp[8] = (RTP_SSRC>>24)&0xFF; rtp[9] = (RTP_SSRC>>16)&0xFF;
    rtp[10]= (RTP_SSRC>> 8)&0xFF; rtp[11]= (RTP_SSRC    )&0xFF;

    // 送信バッファに連結（RTPヘッダ + JPEGペイロード）
    static uint8_t buf[1600];
    if(12 + paylen > sizeof(buf)) { _drop_in_1s++; return false; }
    memcpy(buf, rtp, 12);
    memcpy(buf+12, payload, paylen);

    ssize_t n = sendto(_sock, buf, 12+paylen, 0, (sockaddr*)&_peer, sizeof(_peer));
    if(n<0){ _drop_in_1s++; return false; }
    _seq++; _pkt_in_1s++; return true;
  };

  // XIAO の OV2640 は 4:2:2（一般的）→ Type=0
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

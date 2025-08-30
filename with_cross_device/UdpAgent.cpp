#include "UdpAgent.h"
#include "NetDebug.h"

bool UdpAgent::begin(const char* host, uint16_t port){
  if(!dst_.fromString(host)){
    LOGE("NET","UDP invalid dst IP: %s", host);
    return false;
  }
  dport_ = port;
  if(!udp_.begin(0)){                  // ephemeral local port
    LOGE("NET","UDP begin() failed");
    return false;
  }
  begun_ = true;
  consec_fail_ = 0;
  next_ok_after_ = 0;
  LOGI("UDP","dst=%s:%u", host, port);
  return true;
}

bool UdpAgent::ready() const {
  return begun_ && (WiFi.status() == WL_CONNECTED);
}

void UdpAgent::loop(){
  // no-op (kept for symmetry/future use)
}

bool UdpAgent::sendOne(const uint8_t* data, size_t len){
  if(!udp_.beginPacket(dst_, dport_)){
    return false;
  }
  size_t n = udp_.write(data, len);
  bool ok = udp_.endPacket();
  return ok && (n == len);
}

bool UdpAgent::sendFrame(const uint8_t* buf, size_t len, uint32_t backoffMs){
  if(!ready()) return false;
  if(millis() < next_ok_after_) return false;

  // current spec: 1 datagram = 1 JPEG frame (no fragmentation)
  bool ok = sendOne(buf, len);
  if(!ok){
    consec_fail_++;
    LOGW("NET","UDP send fail (%u) len=%u", (unsigned)consec_fail_, (unsigned)len);
    if(consec_fail_ >= 10){
      LOGW("NET","UDP fail x10 -> WiFi.reconnect()");
      WiFi.reconnect();
      consec_fail_ = 0;
    }
    next_ok_after_ = millis() + backoffMs;
    return false;
  }
  consec_fail_ = 0;
  if(backoffMs) next_ok_after_ = millis() + backoffMs;
  return true;
}

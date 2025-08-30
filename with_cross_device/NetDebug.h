/**
 * NetDebug.h : simple logging helpers
 *  - LOGD/LOGI/LOGW/LOGE(tag, fmt, ...)
 *  - Wi-Fi event hook
 */
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>

static const char* reasonStr(uint8_t r){
  switch(r){
    case  1: return "UNSPECIFIED";
    case  2: return "AUTH_EXPIRE";
    case 15: return "4WAY_TIMEOUT";
    case 200: return "BEACON_TIMEOUT";
    case 201: return "NO_AP_FOUND";
    case 202: return "AUTH_FAIL";
    case 203: return "ASSOC_FAIL";
    case 204: return "HS_TIMEOUT";
    default: return "OTHER";
  }
}


namespace NetDebug {
  enum Level { DEBUG_L, INFO_L, WARN_L, ERROR_L };
  inline const char* lvlStr(Level l){
    switch(l){case DEBUG_L:return "D";case INFO_L:return "I";
              case WARN_L:return "W";default:return "E";}
  }

  inline void vlog(Level lvl, const char* tag, const char* fmt, va_list ap){
    char msg[256];
    vsnprintf(msg, sizeof(msg), fmt, ap);
    uint64_t us  = esp_timer_get_time();
    size_t   hf  = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    Serial.printf("[%llu us][%s][%s][heap:%u][core:%d] %s\n",
                  (unsigned long long)us, lvlStr(lvl), tag,
                  (unsigned)hf, xPortGetCoreID(), msg);
  }

  inline void log(Level lvl, const char* tag, const char* fmt, ...){
    va_list ap; va_start(ap, fmt); 
    vlog(lvl, tag, fmt, ap); va_end(ap);
  }
}

#define LOGD(tag, fmt, ...) NetDebug::log(NetDebug::DEBUG_L, tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) NetDebug::log(NetDebug::INFO_L,  tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) NetDebug::log(NetDebug::WARN_L,  tag, fmt, ##__VA_ARGS__)
#define LOGE(tag, fmt, ...) NetDebug::log(NetDebug::ERROR_L, tag, fmt, ##__VA_ARGS__)

/* -------- Wi-Fi event hook ------------------------------------------ */
static void WiFiEvt(WiFiEvent_t e, WiFiEventInfo_t info){
  switch(e){
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      LOGI("WiFiEvt","AP connected"); break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      LOGI("WiFiEvt","GOT IP:%s GW:%s",
           WiFi.localIP().toString().c_str(),
           WiFi.gatewayIP().toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      LOGW("WiFiEvt","DISCONNECTED, reason=%u(%s)", 
      info.wifi_sta_disconnected.reason, 
      reasonStr(info.wifi_sta_disconnected.reason));
      // if(retry++ < 3){
      //     esp_wifi_connect();
      // }else{
      //     retry = 0;
      //     to(S::BLE_WAIT);
      // }
      break;
    default: LOGD("WiFiEvt","event=%d", e); break;
  }
}
inline void registerWiFiDebug(){ WiFi.onEvent(WiFiEvt); }

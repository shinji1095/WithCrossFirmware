#include "CameraStreamer.h"
#include "NetDebug.h" 

bool CameraStreamer::begin() {
    camera_config_t cfg{};
    initCameraConfig(cfg);
    esp_err_t err = esp_camera_init(&cfg);
    LOGI("CAM","esp_camera_init=%d", (int)err);            /// LOG
    return err == ESP_OK;
}

void CameraStreamer::stream(WsAgent& ws)
{
    if (millis() < _nextOkAfter) return;
    if (millis() - _tLast < _interval || !ws.ready()) return;

    uint64_t t0 = esp_timer_get_time();
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb){
        LOGW("CAM","fb null");
        return;
    }
    uint64_t t1 = esp_timer_get_time();

    bool ok = ws.sendFrame(fb->buf, fb->len, 80);  
    esp_camera_fb_return(fb);

    if (ok) _tLast = millis();       

    LOGD("CAM","cap %uB in %llu us, send %s",
         (unsigned)fb->len,
         (unsigned long long)(t1 - t0),
         ok ? "ok" : "drop");
}

// 既存の stream(WsAgent& ws) の下あたりに追加
void CameraStreamer::stream(UdpAgent& udp)
{
    if (millis() < _nextOkAfter) return;

    // 一定のフレーム間隔とUDP準備完了をチェック
    if (millis() - _tLast < _interval || !udp.ready()) {
        static uint32_t lastNote = 0;
        if (!udp.ready() && millis() - lastNote > 1000) {
            LOGD("NET","UDP not ready yet");
            lastNote = millis();
        }
        return;
    }

    uint64_t t0 = esp_timer_get_time();
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb){
        LOGW("CAM","fb null");
        _nextOkAfter = millis() + 80;
        return;
    }
    uint64_t t1 = esp_timer_get_time();

    // --- デバッグ: 最初の数フレームだけ JPEG 先頭/末尾2Bを出力（FF D8 … FF D9想定）
    static int dumpN = 3;  // 3フレームだけ
    if (dumpN > 0 && fb->len >= 2) {
        const uint8_t *p = fb->buf; size_t n = fb->len;
        LOGI("UDP","JPEG head/tail = %02X %02X … %02X %02X",
             p[0], p[1], p[n-2], p[n-1]);
        dumpN--;
    }

    // --- 純粋 JPEG を 1 datagram で送信（ヘッダ付与なし）---
    bool ok = udp.sendFrame(fb->buf, fb->len, 80);
    esp_camera_fb_return(fb);

    if (ok) {
        _tLast = millis();
        LOGD("UDP","sent %u bytes (cap %llu us)",
             (unsigned)fb->len,
             (unsigned long long)(t1 - t0));
    } else {
        LOGW("UDP","drop %u bytes", (unsigned)fb->len);
    }
}



void CameraStreamer::initCameraConfig(camera_config_t& config) {
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_VGA;
  config.pixel_format = PIXFORMAT_JPEG; 
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 40;
  config.fb_count = 1;

  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 36;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      config.frame_size = FRAMESIZE_VGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }
}
#include "CameraStreamer.h"
#include "NetDebug.h"
#include "config.h"
#include "UdpAgent.h"

bool CameraStreamer::begin() {
    camera_config_t cfg{};
    initCameraConfig(cfg);
    esp_err_t err = esp_camera_init(&cfg);
    _interval = 1000 / CAM_FPS;                 // FPS に追従
    LOGI("CAM","esp_camera_init=%d", (int)err);
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

void CameraStreamer::stream(UdpAgent& udp){
    if (millis() - _tLast < _interval || !udp.ready()) return;

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb){ LOGW("CAM","fb null"); return; }

#if STREAM_MODE==3
    bool ok = udp.sendFrame(fb->buf, fb->len);
#else
    bool ok = udp.sendRtpJpegFrame(fb->buf, fb->len, fb->width, fb->height);
#endif
    esp_camera_fb_return(fb);

    if (ok) _tLast = millis();
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

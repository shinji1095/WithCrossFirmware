#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <time.h>

#define BUTTON_PIN 1
#define CAMERA_MODEL_XIAO_ESP32S3
#include "camera_pins.h"

// ---- Wifi ----
const char *ssid = "moto g(50) 5G_2485";
const char *password = "44bpwpzj6s8rkxc";

// ---- WebSocket ----
const char* WS_HOST = "10.32.59.91";
const uint16_t WS_PORT = 4000;
const char* WS_PATH = "/stream";

WebSocketsClient wsStream;

// ---- NTP ----
const char *ntpServer = "ntp.nict.jp";
const long gmtOffset_sec = 9 * 3600;
const int daylightOffset_sec = 0;

// ---- Setup ----
void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLDOWN);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.begin(115200);
  Serial.setDebugOutput(true);

  // ---- Camera ----
  camera_config_t config;
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
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG; 
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    while(1) delay(1000);
  }

  // ---- WiFi ----
  WiFi.begin(ssid, password);
  WiFi.setTxPower(WIFI_POWER_17dBm);
  WiFi.setSleep(false);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  // ---- NTP ----
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer, "pool.ntp.org");

  // ---- WebSocket ----
  wsStream.begin(WS_HOST, WS_PORT, WS_PATH);
  wsStream.setReconnectInterval(3000);
}

void loop() {
  wsStream.loop();  // WebSocketを維持

  if (digitalRead(BUTTON_PIN) == HIGH) {
    Serial.println("[INFO] Button pressed. Capturing and sending photo...");

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[ERROR] Camera capture failed");
      delay(1000);
      return;
    }

    // SDカード保存（元コードのまま）
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    char filename[64];
    snprintf(filename, sizeof(filename), "/photo/%04d%02d%02d_%02d%02d%02d.jpg",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);

    File file = SD.open(filename, FILE_WRITE);
    if (file) {
      file.write(fb->buf, fb->len);
      file.close();
      Serial.printf("[INFO] Saved photo: %s\n", filename);
    } else {
      Serial.printf("[ERROR] Failed to write to SD: %s\n", filename);
    }

    // 📡 WebSocketで送信
    bool ok = wsStream.sendBIN(fb->buf, fb->len);
    Serial.printf(ok ? "[INFO] Sent %d bytes over WebSocket\n" : "[ERROR] WebSocket send failed\n", fb->len);

    esp_camera_fb_return(fb);
    delay(1000);
  }

  delay(10);
}

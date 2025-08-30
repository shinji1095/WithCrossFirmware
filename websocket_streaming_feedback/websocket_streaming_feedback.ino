/*********************************************************************
 * Xiao ESP32-S3 Sense → WebSocket Push Camera + Feedback
 *  - Sends JPEG stream to ws://<SERVER_IP>:<PORT>/stream
 *  - Receives control feedback via ws://<SERVER_IP>:<PORT>/ctrl
 *  - Vibrates when instructed
 *********************************************************************/
#include "esp_camera.h"
#include <WiFi.h>
#include <WebSocketsClient.h>
#define CAMERA_MODEL_XIAO_ESP32S3
#include "camera_pins.h"   // Board-specific pin map

/**************  USER CONFIG  *************************/
const char* WIFI_SSID     = "moto g(50) 5G_2485";//"aterm-cd7784-a";//
const char* WIFI_PASS     = "44bpwpzj6s8rkxc";//"9d1d50c097fc7";//

const char* WS_HOST       = "192.168.216.91"; 
const uint16_t WS_PORT    = 4000;
const char* WS_PATH       = "/stream";
const char* CTRL_PATH     = "/ctrl";

#define VIBRATION_PIN 2                 // GPIO for vibration motor
#define FRAME_SIZE   FRAMESIZE_VGA
#define JPEG_QUALITY 12
#define SEND_INTERVAL_MS 100

#define BUTTON_PIN 1
bool lastButtonState = LOW;
/****************************************/

WebSocketsClient wsStream;
WebSocketsClient wsCtrl;

unsigned long lastSend = 0;

/* ---------- Wi-Fi ---------- */
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".\n");
  }
  Serial.printf("\n✔ WiFi connected: %s\n", WiFi.localIP().toString().c_str());
}

/* ---------- Camera ---------- */
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size   = FRAME_SIZE;
  config.pixel_format = PIXFORMAT_JPEG;
  config.jpeg_quality = JPEG_QUALITY;
  config.fb_count     = 2;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  s->set_vflip(s, 1);  // optional flip
  return true;
}

/* ---------- Stream Channel ---------- */
void onStreamEvent(WStype_t type, uint8_t* payload, size_t len) {
  if (type == WStype_CONNECTED)
    Serial.println("[STREAM] Connected");
  else if (type == WStype_DISCONNECTED)
    Serial.println("[STREAM] Disconnected");
}

void connectStream() {
  wsStream.begin(WS_HOST, WS_PORT, "/stream");
  wsStream.onEvent(onStreamEvent);
  wsStream.setReconnectInterval(3000);
  wsStream.enableHeartbeat(30000, 5000, 2);
}

/* ---------- Control Channel ---------- */
void onCtrlEvent(WStype_t type, uint8_t* payload, size_t len) {
  if (type == WStype_CONNECTED) {
    Serial.println("[CTRL] Connected");
    delay(10);
    wsCtrl.sendTXT("XIAO");  // Identify to server
  } else if (type == WStype_DISCONNECTED) {
    Serial.println("[CTRL] Disconnected");
  } else if (type == WStype_BIN && len == 1) {
    bool vibrate = payload[0] == 0x01;
    digitalWrite(VIBRATION_PIN, vibrate ? HIGH : LOW);
    Serial.printf("[CTRL] Vibrate: %s\n", vibrate ? "ON" : "OFF");
  }
}

void connectCtrl() {
  wsCtrl.begin(WS_HOST, WS_PORT, "/ctrl");
  wsCtrl.onEvent(onCtrlEvent);
  wsCtrl.setReconnectInterval(3000);
  wsCtrl.enableHeartbeat(30000, 5000, 2);
}


/* ---------- Setup ---------- */
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(VIBRATION_PIN, OUTPUT);
  digitalWrite(VIBRATION_PIN, LOW);

  pinMode(BUTTON_PIN, INPUT);
  lastButtonState = digitalRead(BUTTON_PIN);

  if (!initCamera()) {
    Serial.println("Camera init failed. Halting.");
    while (true) delay(1000);
  }

  connectWiFi();
  connectStream();
  connectCtrl();
}

/* ---------- Main Loop ---------- */
void loop() {
  wsStream.loop();
  wsCtrl.loop();

  if (!wsStream.isConnected()) return;
  if (millis() - lastSend < SEND_INTERVAL_MS) return;

  lastSend = millis();
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  bool ok = wsStream.sendBIN(fb->buf, fb->len);
  if (!ok) {
    Serial.println("WebSocket send failed");
  }

  bool currentButtonState = digitalRead(BUTTON_PIN);
  if (lastButtonState == LOW && currentButtonState == HIGH) {
    Serial.println("Button pressed!");
    if (wsCtrl.isConnected()) {
      wsCtrl.sendTXT("pressed"); 
    }
  }
  lastButtonState = currentButtonState;

  esp_camera_fb_return(fb);
}
#include "esp_camera.h"
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

#define CAMERA_MODEL_XIAO_ESP32S3
#include "camera_pins.h"

// ====== Wi-Fi & WebSocket ======
const char* WIFI_SSID = "pi";//"moto g(50) 5G_2485";//"己のiPhone";//"aterm-cd7784-a";//
const char* WIFI_PASS = "wadalab5540";//"44bpwpzj6s8rkxc";//"9d1d50c097fc7";//"abcd4213";//
const char* WS_HOST = "10.42.0.243";//"192.168.10.101";  // React/Server PC
const uint16_t WS_PORT = 4000;
const char* CTRL_PATH = "/ctrl";

#define VIBRATION_PIN 2
#define BUTTON_PIN 1
#define SEND_INTERVAL_MS 100

WebSocketsClient wsCtrl;

bool lastButtonState = LOW;
unsigned long lastSend = 0;

// ==== 時刻同期 ====
uint64_t reactServerTime = 0;  // ← 型を変更
unsigned long localMillisAtSync = 0;

uint64_t getSynchronizedTimestamp() {
  if (reactServerTime == 0) return millis(); // fallback
  return reactServerTime + (millis() - localMillisAtSync);
}

// ==== WebSocket Event ====
void onCtrlEvent(WStype_t type, uint8_t* payload, size_t len) {
  if (type == WStype_CONNECTED) {
    Serial.println("[CTRL] Connected");
    wsCtrl.sendTXT("XIAO"); // identify as XIAO
  }
  else if (type == WStype_TEXT) {
  Serial.printf("[CTRL] TEXT: %s\n", payload);

  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) {
    Serial.print("❌ JSON parse error: ");
    Serial.println(err.c_str());
    return;
  }

  if (doc["type"] == "server_time") {
    reactServerTime = doc["ts"].as<uint64_t>();
    localMillisAtSync = millis();
    Serial.printf("⏱️ Synced: reactServerTime = %llu, localMillis = %lu\n", reactServerTime, localMillisAtSync);
  }

}
}


void connectCtrl() {
  wsCtrl.begin(WS_HOST, WS_PORT, CTRL_PATH);
  wsCtrl.onEvent(onCtrlEvent);
  wsCtrl.setReconnectInterval(3000);
  wsCtrl.enableHeartbeat(30000, 5000, 2);
}

// ==== Setup ====
void setup() {
  Serial.begin(115200);
  pinMode(VIBRATION_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT);
  digitalWrite(VIBRATION_PIN, LOW);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n✔ Connected: %s\n", WiFi.localIP().toString().c_str());

  connectCtrl();
  lastButtonState = digitalRead(BUTTON_PIN);
}

// ==== Loop ====
void loop() {
  wsCtrl.loop();

  bool currentButton = digitalRead(BUTTON_PIN);
  if (lastButtonState == LOW && currentButton == HIGH) {
    uint64_t timestamp = getSynchronizedTimestamp();
    String msg = "pressed|" + String(timestamp);
    Serial.println(msg);
    if (wsCtrl.isConnected()) {
      wsCtrl.sendTXT(msg);
    }
  }
  lastButtonState = currentButton;

}

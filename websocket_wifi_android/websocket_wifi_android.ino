#include <WiFi.h>
#include <WebSocketsClient.h>
#include "esp_camera.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <esp_log.h>
#include <time.h>

#define CAMERA_MODEL_XIAO_ESP32S3
#include "camera_pins.h"

/* ---------- Wi-Fi / WebSocket 設定 ---------- */
const char *ssid = "moto g(50) 5G_2485";//"aterm-cd7784-a";
const char *password = "44bpwpzj6s8rkxc";//"9d1d50c097fc7";
const char* websocket_host = "192.168.10.101";
const uint16_t websocket_port = 4000;
const char* websocket_path = "/stream";

/* ---------- グローバル ---------- */
#define BUTTON_PIN 1
static const char *TAG = "main";
int prevState = 0, currState = 0;
bool camera_ok = false;
bool ntp_ok = false;
bool sd_sign = false;
WebSocketsClient webSocket;
const char* LOG_FILE_PATH = "/log/log.txt";

/* ---------- ログ出力 ---------- */
void appendLog(fs::FS &fs, const char *path, const char *message)
{
  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.printf("Failed to open %s for appending\n", path);
    return;
  }
  file.print(message);
  file.close();
}

void writeFile(fs::FS &fs, const char * path, uint8_t * data, size_t len)
{
  Serial.printf("Writing file: %s\r\n", path);
  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.write(data, len) == len) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

/* ---------- NTP 初期化 ---------- */
void initTime()
{
  configTime(9 * 3600, 0, "ntp.nict.jp", "pool.ntp.org");
  Serial.print("Syncing NTP");
  for (int i = 0; i < 10; ++i) {
    time_t now = time(nullptr);
    if (now > 1700000000) {
      Serial.println(" ✓");
      ntp_ok = true;
      appendLog(SD, LOG_FILE_PATH, "[INFO] NTP synchronized successfully.\n");
      return;
    }
    Serial.print(".");
    delay(500);
  }
  Serial.println(" ×");
  appendLog(SD, LOG_FILE_PATH, "[WARN] NTP synchronization failed.\n");
}

/* ---------- SD 初期化 ---------- */
bool initSD()
{
  if (!SD.exists("/log")) {
    SD.mkdir("/log");
  }
  
  if (!SD.begin(21)) {
    Serial.println("Card Mount Failed");
    appendLog(SD, LOG_FILE_PATH, "[ERROR] Card mount failed.\n");
    return false;
  }

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    appendLog(SD, LOG_FILE_PATH, "[ERROR] No SD card attached.\n");
    return false;
  }

  Serial.print("SD Card Type: ");
  String typeStr;
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
    typeStr = "MMC";
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
    typeStr = "SDSC";
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
    typeStr = "SDHC";
  } else {
    Serial.println("UNKNOWN");
    typeStr = "UNKNOWN";
  }

  appendLog(SD, LOG_FILE_PATH, ("[INFO] SD Card initialized: " + typeStr + "\n").c_str());
  sd_sign = true;
  return true;
}

/* ---------- カメラ初期化 ---------- */
void startCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;   config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;   config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;   config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;   config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM; config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 10000000;
  config.frame_size = FRAMESIZE_VGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 10;
  config.fb_count = psramFound() ? 2 : 1;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (esp_camera_init(&config) != ESP_OK) {
    ESP_LOGE(TAG, "Camera init failed");
    appendLog(SD, LOG_FILE_PATH, "[ERROR] Camera initialization failed.\n");
    return;
  }
  camera_ok = true;
  appendLog(SD, LOG_FILE_PATH, "[INFO] Camera initialized successfully.\n");
}

/* ---------- 画像送信 ---------- */
void sendImage()
{
  if (!camera_ok) {
    appendLog(SD, LOG_FILE_PATH, "[WARN] sendImage called but camera is not OK.\n");
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    ESP_LOGE(TAG, "Capture failed");
    appendLog(SD, LOG_FILE_PATH, "[ERROR] Camera capture failed.\n");
    return;
  }

  uint32_t t0 = millis();
  bool ok = webSocket.sendBIN(fb->buf, fb->len);
  uint32_t dur = millis() - t0;

  ESP_LOGI(TAG, "sendBIN %s: %u bytes in %u ms", ok ? "OK" : "NG", fb->len, dur);

  char logbuf[128];
  if (ntp_ok) {
    time_t now = time(nullptr);
    struct tm tm; localtime_r(&now, &tm);
    snprintf(logbuf, sizeof(logbuf),
      "[TX] %04d-%02d-%02d %02d:%02d:%02d,TX,%u,%u,%s\n",
      tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
      tm.tm_hour, tm.tm_min, tm.tm_sec,
      fb->len, dur, ok ? "OK" : "NG");
  } else {
    snprintf(logbuf, sizeof(logbuf), "[TX] %lu,TX,%u,%u,%s\n", millis(), fb->len, dur, ok ? "OK" : "NG");
  }
  appendLog(SD, LOG_FILE_PATH, logbuf);

  esp_camera_fb_return(fb);
}

/* ---------- setup ---------- */
void setup()
{
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLDOWN);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  initSD();       // SD 初期化・ログファイル作成

  // Wi-Fi 接続
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n ✓");
  appendLog(SD, LOG_FILE_PATH, "[INFO] WiFi connected.\n");

  initTime();     // NTP → 時刻取得
  startCamera();  // カメラ初期化

  // WebSocket 開始
  webSocket.begin(websocket_host, websocket_port, websocket_path);
  webSocket.onEvent([](WStype_t type, uint8_t* , size_t ){
    if (type == WStype_CONNECTED) {
      ESP_LOGI(TAG, "WS connected");
      appendLog(SD, LOG_FILE_PATH, "[INFO] WebSocket connected.\n");
    }
    if (type == WStype_DISCONNECTED) {
      ESP_LOGW(TAG, "WS disconnected");
      appendLog(SD, LOG_FILE_PATH, "[WARN] WebSocket disconnected.\n");
    }
  });
  webSocket.setReconnectInterval(5000);
  digitalWrite(LED_BUILTIN, HIGH); // 準備完了サイン
  appendLog(SD, LOG_FILE_PATH, "[INFO] System ready.\n");
}

/* ---------- loop ---------- */
void loop()
{
  webSocket.loop();

  currState = digitalRead(BUTTON_PIN);
  if (currState == 1 && prevState == 0) {
    uint32_t startTime = millis();
    appendLog(SD, LOG_FILE_PATH, "[INFO] Button pressed, capturing and sending image.\n");
    sendImage();
    char buf[64];
    snprintf(buf, sizeof(buf), "[INFO] Send time: %lu ms\n", millis() - startTime);
    appendLog(SD, LOG_FILE_PATH, buf);
  }
  prevState = currState;

  digitalWrite(LED_BUILTIN, currState == 1 ? HIGH : LOW);
  delay(500);
}

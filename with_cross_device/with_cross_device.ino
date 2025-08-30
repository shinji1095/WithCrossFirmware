/**
 * with_cross_device.ino – XIAO ESP32-S3 Sense 〈function‐oriented, full source〉
 * ---------------------------------------------------------------------------
 *  • FSM  : BLE_WAIT → GET_DEV_INFO → WS_WAIT → HOME ↔ (SIGNAL | STRAIGHT | OBJECT)
 *  • BLE  : 128-bit UUID, 10 s timeout & re-advertise loop
 *  • Wi-Fi: creds via BLE → WebSocket (/stream /control /mode)
 *  • Camera: JPEG 640×480 → /stream (10 FPS)
 *  • Buttons: prev / next / ok / back
 *  • Motor : GPIO 33  /control
 *  • Serial: verbose state & event logs
 *  • Build : Arduino IDE 2.x / PlatformIO, 115 200 baud
 */
#include "NetDebug.h"

#include "AppStateMachine.h"
AppStateMachine fsm;

void setup(){
  Serial.begin(115200);
  LOGI("MAIN","setup start");
  registerWiFiDebug();                          /// LOG
  AppStateMachine::instance().begin();
  LOGI("MAIN","setup done");
}
void loop(){
  AppStateMachine::instance().loop();
  static uint32_t last=0;
  if(millis()-last>5000){
    LOGD("MAIN","alive");
    last = millis();
  }}
#pragma once
#include "BleAgent.h"
#include "WsAgent.h"
#include "CameraStreamer.h"
#include "Hardware.h"
#include "UdpAgent.h"
#include "Buttons.h" 

class AppStateMachine {
public:
    static AppStateMachine& instance() {
        static AppStateMachine inst;
        return inst;
    }
    void begin();
    void loop();

private:
    /* ── 状態定義 ─────────────────────────────────────────────── */
    enum class S : uint8_t { BLE_WAIT, GET_INFO, WS_WAIT, HOME,
                             SIG, STRAIGHT, OBJ };

    /* ── モード循環テーブル ─────────────────────────────────── */
    static constexpr uint8_t  MODE_CNT = 3;
    static constexpr uint16_t kModes[MODE_CNT] = { 0x0001, 0x0010, 0x0011 };

    Buttons  buttons;
    static constexpr uint8_t kBtnPins[4] = { BTN_PREV, BTN_NEXT, BTN_BACK, BTN_OK };

    /* ── 変数 ──────────────────────────────────────────────── */
    S         st       = S::BLE_WAIT;
    uint8_t   modeIdx  = 0;              // 0-based index
    uint16_t  mode     = kModes[0];      // 現在選択中
    BleAgent  ble;
    WsAgent   ws;
    UdpAgent  udp;
    CameraStreamer cam;

    BleAgent::Creds wifiCreds;
    bool     wifiStarted = false;
    bool     bleActive   = true;
    bool     btnActivate = false;

    uint32_t tLed   = 0; bool ledOn = false; uint16_t ledInt = 500;

    /* --- OK長押し(300ms) 即時判定用 --- */
    uint32_t  okPressStart = 0;
    bool      okHolding    = false;
    bool      okLongFired  = false;
    static constexpr uint32_t OK_LONG_MS = 300;  // ← 要件
    static constexpr uint32_t OK_SUPPRESS_MS = 100;
    uint32_t okSuppressUntil = 0;
    bool okIgnoreUntilRelease = false;

    /* --- モータ100msパルス（ノンブロッキング） --- */
    uint32_t motorPulseUntil = 0;
    inline void startMotorPulse100ms() {
        digitalWrite(PIN_MOTOR, HIGH);
        motorPulseUntil = millis() + 100;
    }

    /* ── helpers ───────────────────────────────────────────── */
    const char* toStr(S s) const {
        switch (s){
            case S::BLE_WAIT:  return "BLE_WAIT";
            case S::GET_INFO:  return "GET_INFO";
            case S::WS_WAIT:   return "WS_WAIT";
            case S::HOME:      return "HOME";
            case S::SIG:       return "SIG";
            case S::STRAIGHT:  return "STRAIGHT";
            case S::OBJ:       return "OBJ";
        } return "?";
    }

    void to(S n);
    void ledTask();
    void buttonTask();

    /* ── Core分離: UI(core0) / NET+CAM(core1) ───────────────── */
    static void uiTask(void* arg);
    static void netcamTask(void* arg);
    TaskHandle_t hUiTask    = nullptr;
    TaskHandle_t hNetTask   = nullptr;

    /* ── WS送信用キュー（WS操作の一本化） ────────────────── */
    enum class WsCmdType : uint8_t { MODE, MOTOR };
    struct WsCmd { WsCmdType type; uint16_t val; };
    static constexpr int WS_Q_LEN = 16;
    QueueHandle_t wsQ = nullptr;

    inline void sendModeAsync(uint16_t v){
        if(!wsQ) return;
        WsCmd c{WsCmdType::MODE, v};
        xQueueSend(wsQ, &c, 0);
    }
    inline void sendMotorAsync(uint16_t v){
        if(!wsQ) return;
        WsCmd c{WsCmdType::MOTOR, v};
        xQueueSend(wsQ, &c, 0);
    }
};

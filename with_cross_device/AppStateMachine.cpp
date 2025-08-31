#include "AppStateMachine.h"
#include <WiFi.h>
#include "NetDebug.h"
#include <esp_wifi.h>
#include "config.h"

/* ===== 初期化 ======================================================== */
void AppStateMachine::begin() {
    initHardware();
    buttons.begin(kBtnPins, 4); 
    LOGI("FSM","begin()");

    /* BLE 受信 → Wi-Fi creds を取得したら WS_WAIT へ */
    ble.begin([this](const BleAgent::Creds& c) {
        wifiCreds = c;
        LOGI("FSM","BLE creds received: ssid=%s ip=%s port=%u",
             c.ssid.c_str(), c.ip.c_str(), c.port);
        to(S::WS_WAIT);
    });

    bool ok = cam.begin();
    LOGI("FSM","camera init=%d", ok);

    /* WS送信用キュー作成（WS操作をcore1に集約） */
    wsQ = xQueueCreate(WS_Q_LEN, sizeof(WsCmd));

    /* Core分離: UI(core0) / NET+CAM(core1) */
    xTaskCreatePinnedToCore(uiTask,    "uiTask",    4096, this, 2, &hUiTask, 0);
    xTaskCreatePinnedToCore(netcamTask,"netcamTask",6144, this, 2, &hNetTask, 1);
}

/* Arduino の loop は最小化（実処理は FreeRTOS タスクへ） */
void AppStateMachine::loop() {
    vTaskDelay(1);
}

/* ===== UI Task: buttons/LED/BLE/motor ================================ */
void AppStateMachine::uiTask(void* arg){
    auto* self = static_cast<AppStateMachine*>(arg);
    for(;;){
        if(self->btnActivate){
            self->buttons.update();
            self->buttonTask();
        }
        self->ledTask();

        /* モータ100msパルス終了処理（非ブロッキング） */
        if (self->motorPulseUntil && millis() >= self->motorPulseUntil) {
            digitalWrite(PIN_MOTOR, LOW);
            self->motorPulseUntil = 0;
        }

        if (self->bleActive) self->ble.loop();
        vTaskDelay(1);
    }
}

/* ===== NET+CAM Task: Wi-Fi/WS/Stream/WS送信 ========================== */
void AppStateMachine::netcamTask(void* arg){
    auto* self = static_cast<AppStateMachine*>(arg);

    for(;;){
        // --- WS_WAIT ---
        if (self->st == S::WS_WAIT) {
            if (!self->wifiStarted && self->wifiCreds.ssid.length()) {
                LOGI("WiFi","begin SSID=%s", self->wifiCreds.ssid.c_str());
                WiFi.mode(WIFI_STA);
                WiFi.setSleep(false);
                esp_wifi_set_ps(WIFI_PS_NONE);
                WiFi.begin(self->wifiCreds.ssid.c_str(), self->wifiCreds.psk.c_str());
                self->wifiStarted = true;
            }

            if (self->wifiStarted && WiFi.status() == WL_CONNECTED && !self->ws.ready()) {
                if (self->ws.begin(self->wifiCreds.ip.c_str(), self->wifiCreds.port)) {
                    LOGI("WS","ws.begin(%s:%u)",
                         self->wifiCreds.ip.c_str(), self->wifiCreds.port);

                #if STREAM_MODE==3
                    const uint16_t udp_port = self->wifiCreds.port;   
                    self->udp.begin(self->wifiCreds.ip.c_str(), udp_port,
                                    UdpAgent::Mode::RAW_JPEG_DATAGRAM);
                #else
                    const uint16_t udp_port = RTP_PORT;                
                    self->udp.begin(self->wifiCreds.ip.c_str(), udp_port,
                                    UdpAgent::Mode::RTP_JPEG);
                #endif
                    LOGI("UDP","udp.begin(%s:%u)",
                    self->wifiCreds.ip.c_str(), udp_port);

                    static bool bleStopped = false;
                    if (!bleStopped) {
                        self->ble.stop();
                        self->bleActive  = false;
                        bleStopped = true;
                        LOGI("BLE","Stopped after Wi-Fi up");
                    }
                }
            }

            if (self->ws.ready() && self->st == S::WS_WAIT) {
                LOGI("WS","CONNECTED → HOME");
                self->btnActivate = true;
                self->to(S::HOME);
            }
        }

        // --- WS ループ & 送信キュー ---
        self->ws.loop();
        AppStateMachine::WsCmd cmd;
        while (self->wsQ && xQueueReceive(self->wsQ, &cmd, 0) == pdTRUE) {
            if (cmd.type == WsCmdType::MODE) self->ws.sendMode(cmd.val);
            else                             self->ws.sendMotor(cmd.val);
        }

        // --- 実行モード中：画像ストリーム ---
        if (self->st == S::SIG || self->st == S::STRAIGHT || self->st == S::OBJ) {
        #if   STREAM_MODE==0   // RTP/UDP
            self->cam.stream(self->udp);
        #elif STREAM_MODE==1   // RTSP(UDP)
            // if(self->rtsp.isPlaying()) self->cam.stream(self->udp); // rtsp.loop() 側で begin 済み
        #elif STREAM_MODE==2   // Legacy WS
            self->cam.stream(self->ws);
        #else                  // Legacy RAW UDP
            self->cam.stream(self->udp);
        #endif
        }

        // RTP 1秒ごとの統計ログ
        self->udp.tick1sReport();

        vTaskDelay(1);
    }
}

/* ===== 状態遷移 ====================================================== */
void AppStateMachine::to(S n) {
    S prev = st;
    LOGI("FSM","%s → %s", toStr(st), toStr(n));
    st = n;

    /* --- SIG/STRAIGHT/OBJ → HOME の共通処理 --- */
    if (st == S::HOME && (prev == S::SIG || prev == S::STRAIGHT || prev == S::OBJ)) {
        sendModeAsync(0x1111);          // ← 要件：HOME遷移で0x1111送信
        startMotorPulse100ms();         // 既要件：100msモータHIGH
    }

    switch (st) {
        case S::BLE_WAIT:  ledInt = 500;                           break;
        case S::GET_INFO:  ledInt = 200;                           break;
        case S::WS_WAIT:   ledInt = 100; wifiStarted = false;      break;

        case S::HOME:      ledInt = 0;     sendModeAsync(mode);    break; 
        case S::SIG:       okSuppressUntil = millis() + OK_SUPPRESS_MS; okIgnoreUntilRelease = true; sendModeAsync(0x1001); break;
        case S::STRAIGHT:  okSuppressUntil = millis() + OK_SUPPRESS_MS; okIgnoreUntilRelease = true; sendModeAsync(0x1010); break;
        case S::OBJ:       okSuppressUntil = millis() + OK_SUPPRESS_MS; okIgnoreUntilRelease = true; sendModeAsync(0x1011); break;
     }
 }

/* ===== LED 点滅 ====================================================== */
void AppStateMachine::ledTask() {
    if (!ledInt) { digitalWrite(LED_PIN, LOW); return; }
    if (millis() - tLed >= ledInt) {
        ledOn = !ledOn;
        digitalWrite(LED_PIN, ledOn);
        tLed = millis();
    }
}

/* ===== ボタンハンドラ（HOME：短押し巡回 / 長押し遷移） =============== */
void AppStateMachine::buttonTask() {
    /* --------- HOME でのモード選択 -------------------------------- */
    if(st == S::HOME){
        if(buttons.rising(BTN_NEXT)){
            modeIdx = (modeIdx + 1) % MODE_CNT;
            mode    = kModes[modeIdx];
            sendModeAsync(mode);    // 候補のみ通知
        }
        if(buttons.rising(BTN_PREV)){
            modeIdx = (modeIdx + MODE_CNT - 1) % MODE_CNT;
            mode    = kModes[modeIdx];
            sendModeAsync(mode);
        }

        /* OK: 短押し=候補巡回 / 長押し(>=300ms)＝その候補へ遷移 */
        if (buttons.pressed(BTN_OK)) {
            if(!okHolding){
                okHolding   = true;
                okLongFired = false;
                okPressStart = millis();
            } else if(!okLongFired && (millis() - okPressStart >= OK_LONG_MS)){
                okLongFired = true;                    // 長押し即時判定
                okSuppressUntil = millis() + OK_SUPPRESS_MS;
                (void)buttons.rising(BTN_OK);
                okIgnoreUntilRelease = true; 
                switch(modeIdx){
                    case 0: to(S::SIG);       break;
                    case 1: to(S::STRAIGHT);  break;
                    case 2: to(S::OBJ);       break;
                }
            }
        } else if (okHolding) {  // リリース
            uint32_t held = millis() - okPressStart;
            bool wasLong = okLongFired;
            okHolding = false; okLongFired = false;
            if (!wasLong && held < OK_LONG_MS) {
                // ← 短押し：候補だけ巡回（遷移はしない）
                modeIdx = (modeIdx + 1) % MODE_CNT;
                mode    = kModes[modeIdx];
                sendModeAsync(mode);
            }
        }

    /* ------ 実行中：BTN_OK単押し/ BACK で HOME ------ */
    }else if (st == S::SIG || st == S::STRAIGHT || st == S::OBJ)
    {
        // BACKは常時有効
        if (buttons.rising(BTN_BACK)) {
            sendModeAsync(0x1000);
            to(S::HOME);
            return;
        }

        // 実行状態に入った直後は、OKを一度“離す”まで無視
        if (okIgnoreUntilRelease) {
            if (!buttons.pressed(BTN_OK)) {
                okIgnoreUntilRelease = false;
                (void)buttons.rising(BTN_OK); // リリース時にrising残留を捨てる
            }
            return; // まだ離していない／今離した直後は処理しない
        }

        // OK単押し（立ち上がり）で HOME へ
        if (millis() >= okSuppressUntil && buttons.rising(BTN_OK)) {
            sendModeAsync(0x1000);   // 既存挙動維持（HOMEインジケータ）
            to(S::HOME);             // ← to() 内で 0x1111 も送出＆モータ100ms
            return;
        }
    }
}

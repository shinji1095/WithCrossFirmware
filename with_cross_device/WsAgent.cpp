#include "WsAgent.h"
#include "NetDebug.h"
#include "Hardware.h"

static WsAgent* gSelf = nullptr;
static bool motorState = false;

void wsCb(WStype_t t, uint8_t* p, size_t l)
{
    if (!gSelf) return;                  
    switch (t) {
        case WStype_CONNECTED:
            LOGI("WS", "CONNECTED");
            gSelf->_busy = false; 
            gSelf->_connecting = false;
            gSelf->start(gSelf->_host.c_str(), gSelf->_port);
            break;

        case WStype_DISCONNECTED:
        case WStype_ERROR:
            LOGW("WS", "DISC/ERR, code=%u", (l >= 2) ? ((p[0] << 8) | p[1]) : 0);
            gSelf->_busy = false;  
            gSelf->_connecting = false;
            gSelf->_needReconnect = true;
            break;

        default:
            break;
    }
}

void ctrlCb(WStype_t t, uint8_t* p, size_t l)
{
    if (t != WStype_BIN || l < 2) return;
    uint16_t cmd = (uint16_t(p[0]) << 8) | uint16_t(p[1]);
    if(cmd==0x0001 && !motorState){  
        motorState = true;
        digitalWrite(PIN_MOTOR, HIGH);
        LOGI("CTRL","Motor ON");
    }
    else if(cmd==0x0000 && motorState){
        motorState = false;
        digitalWrite(PIN_MOTOR, LOW);
        LOGI("CTRL","Motor OFF");
    }
}

bool WsAgent::begin(const char* host, uint16_t port)
{
    if (_busy || _connecting || _stream.isConnected()) {
        // LOGD("WS","begin() skipped – busy/connected");
        return false;
    }

    if (millis() - _lastTry < 5000) return false;  // クールダウン
    _lastTry = millis();
    _busy    = true;
    _connecting = true;

    LOGI("WS","host=%s port=%u", host, port);

    WiFiClient probe;
    if (!probe.connect(host, port, 1000)) {
        LOGE("WS","TCP port unreachable");
        _needReconnect = true;
        _busy = false; _connecting = false;
        return false;
    }
    probe.stop();

    if (_stream.isConnected()) _stream.disconnect();

    _host = host;  _port = port;
    gSelf = this;

    _stream.onEvent(wsCb);
    _stream.begin(host, port, "/stream");
    _stream.setReconnectInterval(3000);
    return true;
}


void WsAgent::start(const char* host, uint16_t port)
{
    _ctrl.begin(host, port, "/control");
    _ctrl.onEvent(ctrlCb);
    _mode.begin(host, port, "/mode");
}

void WsAgent::loop()
{
    _stream.loop(); _ctrl.loop(); _mode.loop();

    if (_needReconnect && !_connecting && (millis() - _lastTry) > 5000) {
        _needReconnect = false;
        begin(_host.c_str(), _port);      // ガード付き再試行
    }
}

void WsAgent::sendMode(uint16_t v){
    uint8_t b[2]={uint8_t(v>>8),uint8_t(v)};
    LOGD("WS","sendMode=0x%04X", v);                      /// LOG
    _mode.sendBIN(b,2);
}

void WsAgent::sendMotor(uint16_t v){
    uint8_t b[2]={uint8_t(v>>8),uint8_t(v)};
    LOGD("WS","sendMotor=0x%04X", v);                     /// LOG
    _ctrl.sendBIN(b,2);
}

bool WsAgent::sendFrame(const uint8_t* buf, size_t len, uint32_t backoffMs)
{
    if(!_stream.isConnected()) return false;

    if(!_stream.sendBIN(buf, len)){        // キュー満杯
        LOGW("WS","queue full – drop");
        _nextOkAfter = millis() + backoffMs;   // 50〜100 ms など
        return false;
    }
    return true;
}
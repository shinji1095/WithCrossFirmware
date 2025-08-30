#pragma once
#include <WebSocketsClient.h>
#include <WiFi.h>

class WsAgent {
public:
    bool  begin(const char* host, uint16_t port);   // ガード付き
    void  loop();                                   // 再試行管理
    bool  ready() { return _stream.isConnected(); }

    void  sendMode(uint16_t);
    void  sendMotor(uint16_t);
    bool sendFrame(const uint8_t* buf, size_t len, uint32_t backoffMs = 0);

private:
    void  start(const char* host, uint16_t port);   // 実際の begin()

    WebSocketsClient _stream, _ctrl, _mode;
    String  _host;  
    uint16_t _port = 0;
    uint32_t _lastTry = 0;
    uint32_t _nextOkAfter = 0;
    bool     _busy     = false;
    bool     _connecting = false;
    bool  _needReconnect = false;
    
    friend void wsCb(WStype_t, uint8_t*, size_t);
};

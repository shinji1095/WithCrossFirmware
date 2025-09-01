#pragma once
#include <Arduino.h>
#include "config.h"
#include "esp_camera.h"

// ESP32-RTSPServer
// API: init(), deinit(), reinit(), readyToSendFrame(), sendRTSPFrame(), setCredentials()
// 参考: README の API Reference と Usage セクション
#include <ESP32-RTSPServer.h>

class RtspAgent {
public:
  // 外部の呼び出しシグネチャは維持（AppStateMachine などの既存コードは変更不要）
  bool begin(uint16_t port = RTSP_PORT,
             const char* user = RTSP_USER,
             const char* pass = RTSP_PASS) {
    if (_started) return true;

    // 認証（空文字なら無効のままでOK）
    if (user && pass) {
      _srv.setCredentials(user, pass);
    }

    // 複数クライアント
    _srv.maxRTSPClients = RTSP_MAX_CLIENTS;

    // 送出パラメータ設定（動画のみ / UDP 優先）
    _srv.transport = RTSPServer::VIDEO_ONLY;
    _srv.rtspPort  = static_cast<int>(port);

    // v1.3.5 は begin() ではなく init() を使用
    // 必要最低限: transport, rtspPort, sampleRate(=0)
    if (!_srv.init(RTSPServer::VIDEO_ONLY, port, 0 /* sampleRate */)) {
      return false;
    }

    // カメラの画質を取得（無ければ60）
    sensor_t* s = esp_camera_sensor_get();
    _quality = s ? static_cast<int>(s->status.quality) : 30;

    _started = true;
    return true;
  }

  bool ready() const {
    return _started && _srv.readyToSendFrame();
  }

  // camera_fb_t そのまま渡す版
  bool sendFrame(camera_fb_t* fb) {
    if (!_started || !fb) return false;
    _srv.sendRTSPFrame(fb->buf, fb->len, _quality, fb->width, fb->height);
    return true;
  }

  // バッファ個別指定版
  bool sendFrame(const uint8_t* jpg, size_t len, int q, int w, int h) {
    if (!_started) return false;
    _srv.sendRTSPFrame(jpg, len, q, w, h);
    return true;
  }

  void stop() {
    if (_started) {
      _srv.deinit();   // v1.3.5 の停止API
      _started = false;
    }
  }

private:
  RTSPServer _srv;
  bool _started = false;
  int  _quality = 60;
};

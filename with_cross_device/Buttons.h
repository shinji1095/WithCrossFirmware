#pragma once
#include <Arduino.h>
#include "NetDebug.h"          // LOGD 用

class Buttons {
  static constexpr uint32_t DEBOUNCE_MS = 20;
  static constexpr size_t   MAX_BTN     = 8;
  struct Info { uint8_t pin; bool prev; bool rise; };

public:
  void begin(const uint8_t* pins, size_t n) {
    _cnt = (n > MAX_BTN) ? MAX_BTN : n;
    for (size_t i = 0; i < _cnt; ++i) {
      _btn[i].pin = pins[i];
      pinMode(pins[i], INPUT_PULLDOWN);       // ★ まず設定
      _btn[i].prev = digitalRead(pins[i]);    // その後読み取る
      _btn[i].rise = false;
    }
    _lastScan = millis();
  }

  /* loop() から毎回呼び出し */
  void update() {
    if (millis() - _lastScan < DEBOUNCE_MS) return;
    _lastScan = millis();

    for (size_t i = 0; i < _cnt; ++i) {
      bool now = digitalRead(_btn[i].pin);
      if (!_btn[i].prev && now) {             // LOW→HIGH
        _btn[i].rise = true;
        LOGD("BTN", "Rising pin=%u", _btn[i].pin);
      }
      _btn[i].prev = now;
    }
  }

  /* 立ち上がりがあった瞬間だけ true（読み出し時に自動クリア） */
  bool rising(uint8_t pin) {
    for (size_t i = 0; i < _cnt; ++i)
      if (_btn[i].pin == pin) {
        bool r = _btn[i].rise;
        _btn[i].rise = false;
        return r;
      }
    return false;
  }

  bool falling(uint8_t pin){
    for(size_t i=0;i<_cnt;++i)
      if(_btn[i].pin==pin){
        bool f = (_btn[i].prev && !digitalRead(pin));
        return f;
      }
    return false;
}

  bool pressed(uint8_t pin) const {
    for (size_t i = 0; i < _cnt; ++i)
      if (_btn[i].pin == pin) return _btn[i].prev;
    return false;
  }

private:
  Info     _btn[MAX_BTN];
  size_t   _cnt      = 0;
  uint32_t _lastScan = 0;
};

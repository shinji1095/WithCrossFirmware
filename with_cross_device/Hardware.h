#pragma once
#include <Arduino.h>

/* GPIO */
#define BTN_PREV   1
#define BTN_NEXT   8
#define BTN_BACK   9
#define BTN_OK     7
#define PIN_MOTOR  2
#define LED_PIN    LED_BUILTIN

inline void initHardware() {
    pinMode(LED_PIN, OUTPUT);
    pinMode(PIN_MOTOR, OUTPUT); 
    digitalWrite(PIN_MOTOR, LOW);
    pinMode(BTN_PREV, INPUT_PULLDOWN);
    pinMode(BTN_NEXT, INPUT_PULLDOWN);
    pinMode(BTN_BACK, INPUT_PULLDOWN);
    pinMode(BTN_OK , INPUT_PULLDOWN);
}
inline bool btn(uint8_t p) { return digitalRead(p)==HIGH; }
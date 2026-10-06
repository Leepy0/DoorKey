#pragma once
#include <Arduino.h>
#include "config.h"

// 보드 RGB LED 상태 표시 (loop 태스크 전용)
namespace Led {

inline uint32_t& offAt() {
  static uint32_t v = 0;
  return v;
}

inline void set(uint8_t r, uint8_t g, uint8_t b, uint32_t holdMs = 0) {
#if STATUS_LED_PIN >= 0
  rgbLedWrite(STATUS_LED_PIN, r, g, b);
  offAt() = holdMs ? millis() + holdMs : 0;
#endif
}

inline void loop(bool enrolling) {
#if STATUS_LED_PIN >= 0
  static uint32_t lastBlink = 0;
  static bool on = false;
  uint32_t now = millis();
  if (offAt() && (int32_t)(now - offAt()) >= 0) {
    rgbLedWrite(STATUS_LED_PIN, 0, 0, 0);
    offAt() = 0;
  }
  // 등록 모드: 파란색 점멸
  if (enrolling && !offAt() && now - lastBlink > 500) {
    lastBlink = now;
    on = !on;
    rgbLedWrite(STATUS_LED_PIN, 0, 0, on ? 40 : 0);
  }
  if (!enrolling && on) {
    on = false;
    if (!offAt()) rgbLedWrite(STATUS_LED_PIN, 0, 0, 0);
  }
#endif
}

}  // namespace Led

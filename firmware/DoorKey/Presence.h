#pragma once
#include <Arduino.h>
#include "Ble.h"
#include "config.h"

// 기기별 재실/외출 판정 (loop 태스크 전용)
//
//  UNKNOWN ──감지──> HOME
//  HOME ──absentSec 동안 미감지──┬─ 직전 최대 RSSI >= exitRssi ──> AWAY  (현관으로 나감 → 무장)
//                               └─ 그 외 ─────────────────────> LOST  (집 안에서 신호만 끊김 → 무장 안 함)
//  LOST ──감지──> HOME
//  AWAY ──RSSI >= arriveRssi 감지 ×confirmCount──> HOME + 문 열기
//         (단, 짧은 외출 / 이전 주소 재등장 / 비활성 시간대면 문 열기 생략)
namespace Presence {

enum class St : uint8_t { Unknown, Home, Lost, Away };

struct Rt {
  St st = St::Unknown;
  uint32_t stSinceMs = 0;
  uint32_t lastSeenMs = 0;
  uint32_t awaySinceMs = 0;
  int8_t lastRssi = -127;
  float ema = -127;
  float itvEma = 0;        // 광고 수신 간격 평균 (ms)
  uint32_t maxGapMs = 0;   // HOME 상태에서 가장 길었던 미수신 간격
  uint32_t advCount = 0;
  uint8_t lastAddr[6] = {0};
  uint8_t recent[16][6];   // 최근 본 주소 (재생 공격 방지)
  uint8_t recentN = 0, recentIdx = 0;
  int8_t sec[300];         // 초 단위 최대 RSSI (-127 = 없음)
  uint32_t secHead = 0;
  int8_t lastExitPeak = -127;
  uint8_t streak = 0;
  uint32_t streakFirstMs = 0;
  char lastEvent[128] = {0};
};

void begin();
void onDet(const Ble::Det& d);
void tick();

void resetSlot(int slot);
bool forceState(int slot, bool away);  // 웹 UI 수동 전환

const Rt& rt(int slot);
const char* stName(St s);
void spark(int slot, int8_t out[60]);  // 최근 60초 RSSI

}  // namespace Presence

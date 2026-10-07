#pragma once
#include <Arduino.h>
#include "Ble.h"
#include "config.h"

// 폰별 재실/외출 판정 (loop 태스크 전용)
//
// 두 신호의 역할을 나눈다.
//   BLE            : "폰이 지금 현관 가까이 있는가" — 초 단위, 집 안에서는 끊길 수 있음
//   SmartThings 위치: "사람이 정말 집 밖으로 나갔는가" — 분 단위, 반경이 넓음
//
//  미확인 ──감지──> 재실
//  재실 ──absentSec 동안 미감지──> 미감지
//  미감지 ──감지──> 재실 (문 안 열림)
//  미감지 ──외출 확인──> 외출
//     위치 기기 ID 있음: SmartThings가 '외출'을 2회 연속 보여야 함 (이번 외출에 해당하는 것만)
//                       조회가 안 되면 → 설정에 따라 BLE 이탈 신호로 판단하거나, 외출로 바꾸지 않음
//     위치 기기 ID 없음: 사라지기 직전 최대 RSSI >= exitRssi (현관으로 나간 것으로 봄)
//  외출 ──RSSI >= arriveRssi 감지 ×confirmCount──> 재실 + 문 열기
//     단, 짧은 외출 / 외출 전 BLE 주소 재등장 / 자동 열기 꺼짐 / 허용 시간대 아님 / 쿨다운이면 열지 않음
namespace Presence {

enum class St : uint8_t { Unknown, Home, Unseen, Away };

// 미감지 상태에서 외출 확인이 어디까지 됐는가 (UI 표시용)
enum class Chk : uint8_t {
  None,      // 해당 없음 (미감지 아님)
  BleOnly,   // 위치 기기 ID 없음 → BLE 이탈 신호가 약해 집 안으로 봄
  Waiting,   // SmartThings 조회 중 (아직 '집')
  Partial,   // SmartThings '외출' 1회 — 한 번 더 확인 중
  Stale,     // SmartThings 조회가 안 됨
};

struct Rt {
  St st = St::Unknown;
  uint32_t stSinceMs = 0;
  uint32_t lastSeenMs = 0;
  uint32_t awaySinceMs = 0;    // 외출 시작 (BLE 마지막 감지 시각)
  bool awayExact = false;      // 위 시각이 실제 BLE 마지막 감지인가 (부팅 직후·수동 전환은 false)
  int8_t lastRssi = -127;
  float ema = -127;
  float itvEma = 0;            // 광고 수신 간격 평균 (ms)
  uint32_t maxGapMs = 0;       // 재실 상태에서 가장 길었던 미수신 간격
  uint32_t advCount = 0;
  uint8_t lastAddr[6] = {0};
  uint8_t recent[16][6];       // 재실 중 본 주소 (재생 공격 방지)
  uint8_t recentN = 0, recentIdx = 0;
  int8_t sec[300];             // 초 단위 최대 RSSI (-127 = 없음)
  uint32_t secHead = 0;
  int8_t lastExitPeak = -127;  // 사라지기 직전 exitWindowSec 동안의 최대 RSSI
  uint8_t streak = 0;
  uint32_t streakFirstMs = 0;
  bool staleNotified = false;  // 이번 미감지 동안 '조회 안 됨' 알림을 보냈는가
  char lastEvent[128] = {0};
};

void begin();
void onDet(const Ble::Det& d);
void tick();

void resetSlot(int slot);
bool forceState(int slot, bool away);  // 웹 UI 수동 전환

const Rt& rt(int slot);
const char* stName(St s);
Chk check(int slot);                   // 미감지 상태의 외출 확인 진행
const char* chkName(Chk c);
void spark(int slot, int8_t out[60]);  // 최근 60초 RSSI
void presPollNow();                    // 모든 폰의 SmartThings 위치를 바로 조회

}  // namespace Presence

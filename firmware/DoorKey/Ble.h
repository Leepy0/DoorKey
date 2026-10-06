#pragma once
#include <Arduino.h>

namespace Ble {

// 등록된 폰의 광고 1건 (BLE 태스크 → loop 태스크)
struct Det {
  uint8_t slot;
  int8_t rssi;
  uint8_t addr[6];  // 리틀엔디언
  uint32_t ms;
};

enum class EnrollState : uint8_t { Idle, Waiting, Connected, Done, Failed };

struct EnrollInfo {
  EnrollState st = EnrollState::Idle;
  char name[40] = {0};
  char msg[160] = {0};
  uint32_t startedMs = 0;
  int slot = -1;
};

struct Stats {
  uint32_t advTotal;
  uint32_t rpaTotal;
  uint32_t cacheHit;
  uint32_t queueDrop;
  bool scanning;
  // 갤럭시 앱 비컨 진단
  uint32_t appBeaconMs;  // 마지막 감지 millis (0 = 없음)
  int8_t appBeaconRssi;
  int8_t appBeaconSlot;  // -1 이면 IRK로 해석 안 됨
};

void begin();
void loop();             // loop 태스크에서 자주 호출
void rebuildIrkTable();  // 기기 추가/삭제/활성 변경 후 호출
bool popDet(Det& d);

bool startEnroll(const char* name, String& err);
void cancelEnroll();
EnrollInfo enrollInfo();

Stats stats();

}  // namespace Ble

#pragma once
#include <Arduino.h>

// GitHub Releases에서 서명된 펌웨어를 확인·설치하고, 새 버전이 정상 동작하지 않으면 이전 슬롯으로 되돌린다.
// check/install/periodic은 net 태스크에서만 호출한다 (네트워크 작업 직렬화).
namespace Ota {

enum class St : uint8_t { Idle, Checking, Available, Latest, Downloading, Error };

struct Info {
  St st;
  char latest[16];     // 서명 확인된 최신 버전
  char notes[128];     // 릴리스 요약
  uint32_t checkedMs;  // 마지막 확인 (millis, 0 = 아직)
  int progress;        // 다운로드 0~100
  char err[96];
  bool verifying;      // 업데이트 직후 정상 동작 확인 중
  char rolled[64];     // 직전 업데이트가 롤백된 이유 (없으면 빈 문자열)
};

void bootCheck();   // setup() 맨 앞
void loop();        // loop()에서 호출: 새 버전 정상 확인
void periodic();    // net 태스크: 주기 확인
void check();       // net 태스크
void install();     // net 태스크
void markPending(); // 새 펌웨어를 쓴 직후 (웹 업로드 포함)
Info info();
const char* stName(St s);

}  // namespace Ota

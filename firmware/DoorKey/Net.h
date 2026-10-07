#pragma once
#include <Arduino.h>

// SmartThings(OAuth·문 열기)와 알림을 처리하는 전용 태스크.
// 다른 태스크는 요청만 큐에 넣고 즉시 반환한다.
namespace Net {

struct Status {
  bool hasToken;
  bool authBroken;         // 리프레시 토큰 무효 → 재인증 필요
  uint32_t expiresAt;      // epoch, 0 = 모름
  uint32_t lastRefreshMs;  // millis, 0 = 이번 부팅에 갱신 안 함
  int lastRefreshCode;
  // 마지막 명령
  int cmdCode;             // 0 = 없음
  uint32_t cmdHttpMs;      // HTTP 요청 소요
  uint32_t cmdTotalMs;     // 감지부터 응답까지
  uint32_t cmdAtEpoch;
  uint32_t cmdAtMs;
  char cmdWho[40];
  char cmdBody[160];
  // 연결 유지
  bool warm;
  uint32_t warmConnects;
  uint32_t warmAvgLifeSec;
  uint32_t lastTlsMs;      // 마지막 TLS 연결 소요
  char lastError[128];
  uint32_t checkAtMs;      // 마지막 연결 확인 시각
  int checkCode;
  char checkBody[160];
};

// SmartThings 폰 위치(presenceSensor) 조회 결과
struct Pres {
  int8_t val;            // -1 모름, 0 외출(not present), 1 집(present)
  uint32_t since;        // 그 값이 된 시각 (SmartThings timestamp, epoch). 0 = 모름
  uint32_t okMs;         // 마지막 조회 성공 (millis, 0 = 없음)
  uint32_t tryMs;        // 마지막 조회 시도 (millis, 0 = 없음)
  uint32_t awaySeenMs;   // 마지막으로 '외출'을 본 조회 시각 (millis, 0 = 없음)
  uint32_t awaySince;    // 그때 SmartThings가 알려준 외출 시작 시각 (epoch)
  int code;              // 마지막 HTTP 코드
  char err[96];          // 마지막 조회 오류 (성공하면 빈 문자열)
};

void begin();

bool requestUnlock(const char* who, uint32_t detectMs, bool test);
void requestCodeExchange(const String& code);
void requestRefresh();
void requestCheck();  // 대상 기기 상태 조회로 토큰·deviceId 확인
void notify(const char* msg, const char* click = nullptr);  // click: 알림을 누르면 열 URL
void requestTestNotify();
void requestUpdCheck();    // 펌웨어 업데이트 확인
void requestUpdInstall();  // 확인된 새 버전 설치 (1분 정도 다른 네트워크 작업 대기)

// 폰 위치 조회 요청. 이미 조회 중이거나 Wi-Fi·토큰이 없거나 큐가 붐비면 false (다음에 다시 시도)
bool requestPresence(int slot, const char* name, const char* deviceId);
void presReset(int slot);  // 기기 ID가 바뀌거나 슬롯이 초기화될 때
Pres pres(int slot);

void setAnyAway(bool v);
Status status();

}  // namespace Net

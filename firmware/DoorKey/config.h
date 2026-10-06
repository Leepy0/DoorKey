#pragma once
// ============================================================================
// DoorKey 기본 설정
// - 여기 값은 "최초 부팅 시 기본값"이다.
// - 한 번 부팅한 뒤에는 웹 UI에서 바꾼 값(NVS 저장)이 우선한다.
// - 공장 초기화: 웹 UI > 시스템 > 설정 초기화
// ============================================================================

#define FW_VERSION "1.0.3"

// ---- Wi-Fi (비워두면 설정용 AP로 부팅, ESP32는 2.4GHz만 접속 가능) ----
#define DEFAULT_WIFI_SSID ""
#define DEFAULT_WIFI_PASS ""

// ---- 웹 UI / OTA 로그인 ----
#define ADMIN_USER         "admin"
#define DEFAULT_ADMIN_PASS "doorkey"   // 첫 접속 후 반드시 변경 (웹 UI > 시스템)

// ---- 네트워크 ----
#define HOSTNAME      "doorkey"        // http://doorkey.local
#define SETUP_AP_SSID "DoorKey-Setup"  // Wi-Fi 미설정/접속 실패 시 뜨는 AP
#define SETUP_AP_PASS "doorkey1234"    // 8자 이상
#define TZ_INFO       "KST-9"

// ---- 보드 ----
#define STATUS_LED_PIN 48  // 보드 RGB LED(WS2812) 핀. 없으면 -1

// ---- BLE ----
#define BLE_ENROLL_NAME   "DoorKey"  // 등록 모드에서 폰 블루투스 목록에 보이는 이름
#define BLE_ENROLL_SEC    120        // 등록 모드 유지 시간
#define BLE_SCAN_ITVL_MS  100        // 스캔 주기
#define BLE_SCAN_WIN_MS   50         // 스캔 창 (주기 대비 비율만큼 BLE가 무선을 점유, 나머지는 Wi-Fi). 80이면 웹 응답이 끊김
#define MAX_DEVICES       8

// 갤럭시 광고 앱이 쏘는 서비스 UUID (앱과 동일해야 함, 진단 표시용)
#define APP_BEACON_UUID "6d0f5a1e-3c4b-4e8a-9b2f-7a1c0d4e5f60"

// ---- SmartThings 기본 대상 (웹 UI에서 변경 가능) ----
#define DEFAULT_ST_DEVICE_ID  "a0b713ec-957f-46ad-b998-7786d6e2d52b"
#define DEFAULT_ST_COMPONENT  "main"
#define DEFAULT_ST_CAPABILITY "absoluteweather46907.lock"
#define DEFAULT_ST_COMMAND    "unlock"
#define DEFAULT_ST_ARGS       "[]"
#define DEFAULT_ST_REDIRECT   "https://httpbin.org/get"

// ---- 펌웨어 업데이트 (GitHub Releases, 서명 확인 후 웹 UI에서 설치) ----
#define OTA_REPO         "Leepy0/DoorKey"
#define OTA_MANIFEST_URL "https://github.com/" OTA_REPO "/releases/latest/download/manifest.json"
#define OTA_SIG_URL      "https://github.com/" OTA_REPO "/releases/latest/download/manifest.sig"
#define OTA_CHECK_HOURS  12

// ============================================================================
// DoorKey — BLE 귀가 감지 → SmartThings 도어락 열기 (ESP32-S3)
//
// 보드 설정 (Arduino IDE › 도구)
//   보드: ESP32S3 Dev Module
//   Flash Size: 16MB (128Mb)
//   PSRAM: OPI PSRAM
//   Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)   ← OTA 가능
//   USB CDC On Boot: 보드의 "USB" 포트로 시리얼을 보려면 Enabled, "COM" 포트면 Disabled
//
// 필요 라이브러리 (라이브러리 매니저)
//   NimBLE-Arduino (h2zero) 2.x
//   ArduinoJson (Benoit Blanchon) 7.x
// ============================================================================
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <time.h>
#include "config.h"
#include "Ble.h"
#include "Led.h"
#include "Log.h"
#include "Net.h"
#include "Ota.h"
#include "Presence.h"
#include "Store.h"
#include "Web.h"

static bool apOn = false;
static uint32_t staUpSinceMs = 0;
static uint32_t staDownSinceMs = 0;

static void startAp(const char* why) {
  if (apOn) return;
  WiFi.mode(WIFI_AP_STA);  // STA는 계속 재접속 시도
  WiFi.softAP(SETUP_AP_SSID, SETUP_AP_PASS);
  apOn = true;
  Log::printf("설정 AP 시작 (%s): %s / %s → http://%s", why, SETUP_AP_SSID, SETUP_AP_PASS,
              WiFi.softAPIP().toString().c_str());
}

static void wifiBegin() {
  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  // 주의: BLE와 함께 쓰므로 WiFi.setSleep(false) 금지 (모뎀 슬립이 켜져 있어야 공존 가능)
  String ssid = Store::wifiSsid();
  if (ssid.isEmpty()) {
    startAp("Wi-Fi 미설정");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), Store::wifiPass().c_str());
  uint32_t t0 = millis();
  while (!WiFi.isConnected() && millis() - t0 < 20000) delay(200);
  if (WiFi.isConnected()) {
    Log::printf("Wi-Fi 연결: %s (%d dBm) → http://%s.local / http://%s", ssid.c_str(), WiFi.RSSI(), HOSTNAME,
                WiFi.localIP().toString().c_str());
  } else {
    startAp("접속 실패");
  }
}

static void wifiLoop() {
  uint32_t now = millis();
  if (WiFi.isConnected()) {
    staDownSinceMs = 0;
    if (!staUpSinceMs) {
      staUpSinceMs = now;
      Log::printf("Wi-Fi 연결됨: %s", WiFi.localIP().toString().c_str());
    }
    // 정상 접속이 1분 유지되면 설정 AP를 끈다
    if (apOn && now - staUpSinceMs > 60000) {
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      apOn = false;
      Log::printf("설정 AP 종료");
    }
  } else {
    if (staUpSinceMs) Log::printf("Wi-Fi 끊김");
    staUpSinceMs = 0;
    if (!staDownSinceMs) staDownSinceMs = now;
    // 2분 넘게 못 붙으면 설정 AP를 띄워 둔다 (공유기 교체 등)
    if (!apOn && now - staDownSinceMs > 120000) startAp("장시간 접속 실패");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Log::begin();
  Log::printf("DoorKey %s 부팅", FW_VERSION);
  Ota::bootCheck();  // 업데이트 직후 반복 재부팅이면 여기서 이전 버전으로 되돌린다
  Led::set(20, 20, 20, 1000);

  Store::begin();
  wifiBegin();
  configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com", "kr.pool.ntp.org");

  Presence::begin();
  Ble::begin();
  Net::begin();
  Web::begin();

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(Store::adminPass().c_str());
  ArduinoOTA.onStart([] { Log::printf("OTA 시작"); });
  ArduinoOTA.onError([](ota_error_t e) { Log::printf("OTA 오류 %d", (int)e); });
  ArduinoOTA.begin();
  MDNS.addService("http", "tcp", 80);
}

void loop() {
  Web::loop();
  ArduinoOTA.handle();
  Ble::loop();

  Ble::Det d;
  for (int n = 0; n < 32 && Ble::popDet(d); n++) Presence::onDet(d);
  Presence::tick();

  wifiLoop();
  Ota::loop();
  Ble::EnrollInfo ei = Ble::enrollInfo();
  Led::loop(ei.st == Ble::EnrollState::Waiting || ei.st == Ble::EnrollState::Connected);
  delay(2);
}

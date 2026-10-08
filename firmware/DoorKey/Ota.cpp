#include "Ota.h"
#include <WiFi.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>
#include "config.h"
#include "Log.h"
#include "Net.h"
#include "OtaKey.h"

namespace Ota {

static const char* NS = "doorota";  // Store와 별도 NVS 영역 (설정 초기화로 지워지지 않음)
static const uint32_t VERIFY_OK_MS = 90000;        // Wi-Fi 연결 상태로 이만큼 지나면 정상
static const uint32_t VERIFY_TIMEOUT_MS = 300000;  // 이 안에 정상 확인 못 하면 롤백
static const uint8_t MAX_BOOTS = 3;                // 확인 전 재부팅 허용 횟수

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static Info I = {};
static String manVersion, manSha, manBin;
static uint32_t manSize = 0;
static bool rolledNotified = false;

const char* stName(St s) {
  switch (s) {
    case St::Checking: return "checking";
    case St::Available: return "available";
    case St::Latest: return "latest";
    case St::Downloading: return "downloading";
    case St::Error: return "error";
    default: return "idle";
  }
}

Info info() {
  portENTER_CRITICAL(&mux);
  Info c = I;
  portEXIT_CRITICAL(&mux);
  return c;
}

static void setSt(St s, const char* err = nullptr) {
  portENTER_CRITICAL(&mux);
  I.st = s;
  if (err) strlcpy(I.err, err, sizeof(I.err));
  else I.err[0] = 0;
  portEXIT_CRITICAL(&mux);
  if (err) Log::utf8Fix(I.err);
}

static void fail(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void fail(const char* fmt, ...) {
  char m[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(m, sizeof(m), fmt, ap);
  va_end(ap);
  setSt(St::Error, m);
  Log::printf("업데이트: %s", m);
}

static uint32_t verNum(const char* s) {
  int a = 0, b = 0, c = 0;
  sscanf(s, "%d.%d.%d", &a, &b, &c);
  return ((uint32_t)a << 20) | ((uint32_t)b << 10) | (uint32_t)c;
}

// ---------------------------------------------------------------- 서명

// manifest.json 원문에 대한 ECDSA P-256 / SHA-256 서명(base64 DER) 확인
static bool verifySig(const String& msg, const String& sigB64) {
  uint8_t sig[160];
  size_t sl = 0;
  String s = sigB64;
  s.trim();
  if (mbedtls_base64_decode(sig, sizeof(sig), &sl, (const uint8_t*)s.c_str(), s.length()) != 0) return false;
  uint8_t hash[32];
  if (mbedtls_sha256((const uint8_t*)msg.c_str(), msg.length(), hash, 0) != 0) return false;
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  int rc = mbedtls_pk_parse_public_key(&pk, (const uint8_t*)OTA_PUBKEY_PEM, strlen(OTA_PUBKEY_PEM) + 1);
  if (rc == 0) rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), sig, sl);
  mbedtls_pk_free(&pk);
  return rc == 0;
}

// ---------------------------------------------------------------- HTTP

// 무결성은 서명·해시로 확인하므로 TLS 인증서 검증은 생략
static int httpGet(const char* url, String& out, int maxLen) {
  NetworkClientSecure c;
  c.setInsecure();
  HTTPClient h;
  h.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  h.setConnectTimeout(8000);
  h.setTimeout(10000);
  h.setUserAgent("DoorKey/" FW_VERSION);
  if (!h.begin(c, url)) return -100;
  int code = h.GET();
  if (code == 200) {
    int n = h.getSize();
    if (n > maxLen) code = -101;
    else out = h.getString();
    if ((int)out.length() > maxLen) code = -101;
  }
  h.end();
  return code;
}

// ---------------------------------------------------------------- 확인

void check() {
  if (!WiFi.isConnected()) return;
  setSt(St::Checking);
  String man, sig;
  int c1 = httpGet(OTA_MANIFEST_URL, man, 2048);
  if (c1 == 404) return fail("릴리스가 아직 없습니다");
  if (c1 != 200) return fail("업데이트 정보 받기 실패 (HTTP %d)", c1);
  int c2 = httpGet(OTA_SIG_URL, sig, 512);
  if (c2 != 200) return fail("서명 받기 실패 (HTTP %d)", c2);
  if (!verifySig(man, sig)) return fail("서명이 맞지 않아 무시했습니다");

  JsonDocument d;
  if (deserializeJson(d, man)) return fail("업데이트 정보 형식 오류");
  String v = d["version"] | "";
  String sha = d["sha256"] | "";
  String bin = d["bin"] | "";
  uint32_t size = d["size"] | 0;
  String notes = d["notes"] | "";
  if (v.isEmpty() || sha.length() != 64 || !bin.startsWith("https://") || size < 100000 || size > 0x300000)
    return fail("업데이트 정보 값 오류");
  sha.toLowerCase();
  manVersion = v;
  manSha = sha;
  manBin = bin;
  manSize = size;

  bool newer = verNum(v.c_str()) > verNum(FW_VERSION);
  portENTER_CRITICAL(&mux);
  strlcpy(I.latest, v.c_str(), sizeof(I.latest));
  strlcpy(I.notes, notes.c_str(), sizeof(I.notes));
  I.checkedMs = millis() | 1;
  I.st = newer ? St::Available : St::Latest;
  I.err[0] = 0;
  portEXIT_CRITICAL(&mux);
  Log::utf8Fix(I.notes);

  if (!newer) return;
  Log::printf("새 펌웨어 %s 있음 (현재 %s)", v.c_str(), FW_VERSION);
  // 알림은 보내지 않는다 (웹 UI 배너로만 표시). 설치는 사람이 고르는 일이라 급하지 않다
}

void periodic() {
  static uint32_t next = 180000;  // 부팅 3분 뒤 첫 확인
  if ((int32_t)(millis() - next) < 0) return;
  next = millis() + OTA_CHECK_HOURS * 3600000UL;
  St s = info().st;
  if (s == St::Downloading || s == St::Checking) return;
  check();
}

// ---------------------------------------------------------------- 설치

void install() {
  if (info().st != St::Available || manBin.isEmpty()) return fail("설치할 새 버전이 없습니다. 먼저 확인하세요");
  Log::printf("업데이트 %s 다운로드 시작", manVersion.c_str());
  portENTER_CRITICAL(&mux);
  I.st = St::Downloading;
  I.progress = 0;
  I.err[0] = 0;
  portEXIT_CRITICAL(&mux);

  NetworkClientSecure c;
  c.setInsecure();
  HTTPClient h;
  h.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  h.setConnectTimeout(8000);
  h.setTimeout(15000);
  h.setUserAgent("DoorKey/" FW_VERSION);
  if (!h.begin(c, manBin)) return fail("다운로드 주소 오류");
  int code = h.GET();
  if (code != 200) {
    h.end();
    return fail("다운로드 실패 (HTTP %d)", code);
  }
  int len = h.getSize();
  if (len > 0 && (uint32_t)len != manSize) {
    h.end();
    return fail("파일 크기가 다릅니다 (%d / %lu)", len, (unsigned long)manSize);
  }
  if (!Update.begin(manSize)) {
    h.end();
    return fail("설치 준비 실패: %s", Update.errorString());
  }

  uint8_t* buf = (uint8_t*)malloc(4096);
  if (!buf) {
    Update.abort();
    h.end();
    return fail("메모리 부족");
  }
  mbedtls_sha256_context sc;
  mbedtls_sha256_init(&sc);
  mbedtls_sha256_starts(&sc, 0);
  NetworkClient* s = h.getStreamPtr();
  uint32_t got = 0, lastData = millis();
  bool ok = true;
  while (got < manSize) {
    int avail = s->available();
    if (avail > 0) {
      size_t want = min((size_t)avail, min((size_t)4096, (size_t)(manSize - got)));
      int n = s->read(buf, want);
      if (n <= 0) continue;
      if (Update.write(buf, n) != (size_t)n) {
        ok = false;
        fail("쓰기 실패: %s", Update.errorString());
        break;
      }
      mbedtls_sha256_update(&sc, buf, n);
      got += n;
      lastData = millis();
      portENTER_CRITICAL(&mux);
      I.progress = (int)((uint64_t)got * 100 / manSize);
      portEXIT_CRITICAL(&mux);
    } else {
      if (millis() - lastData > 15000) {
        ok = false;
        fail("다운로드가 멈췄습니다 (%lu/%lu)", (unsigned long)got, (unsigned long)manSize);
        break;
      }
      delay(5);
    }
  }
  free(buf);
  h.end();
  uint8_t hash[32];
  mbedtls_sha256_finish(&sc, hash);
  mbedtls_sha256_free(&sc);
  if (!ok) {
    Update.abort();
    return;
  }

  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", hash[i]);
  if (manSha != hex) {
    Update.abort();
    return fail("파일 해시가 서명된 값과 다릅니다 — 설치 안 함");
  }
  if (!Update.end(true)) return fail("설치 실패: %s", Update.errorString());

  markPending();
  Log::printf("업데이트 %s 설치 완료 — 재부팅", manVersion.c_str());
  delay(1000);
  ESP.restart();
}

// ---------------------------------------------------------------- 롤백

void markPending() {
  const esp_partition_t* run = esp_ota_get_running_partition();
  Preferences p;
  p.begin(NS, false);
  p.putString("prev", run ? run->label : "");
  p.putUChar("boots", 0);
  p.putBool("pend", true);
  p.end();
}

static void rollback(const char* why) {
  Preferences p;
  p.begin(NS, false);
  String prev = p.getString("prev", "");
  p.putBool("pend", false);
  p.putString("rolled", why);
  p.end();
  const esp_partition_t* t = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, prev.c_str());
  if (!t || esp_ota_set_boot_partition(t) != ESP_OK) {
    Log::printf("롤백 실패 (%s): 이전 슬롯 없음", why);
    return;
  }
  Log::printf("새 펌웨어 이상 (%s) — 이전 버전으로 되돌림", why);
  delay(300);
  ESP.restart();
}

void bootCheck() {
  Preferences p;
  p.begin(NS, false);
  String rolled = p.getString("rolled", "");
  if (rolled.length()) {
    p.remove("rolled");
    strlcpy(I.rolled, rolled.c_str(), sizeof(I.rolled));
    Log::printf("직전 업데이트가 롤백됨: %s", I.rolled);
  }
  if (!p.getBool("pend", false)) {
    p.end();
    return;
  }
  const esp_partition_t* run = esp_ota_get_running_partition();
  if (run && p.getString("prev", "") == run->label) {
    // 새 이미지로 부팅하지 못하고 원래 슬롯에서 시작됨
    p.putBool("pend", false);
    p.end();
    return;
  }
  uint8_t boots = p.getUChar("boots", 0) + 1;
  p.putUChar("boots", boots);
  p.end();
  if (boots > MAX_BOOTS) {
    rollback("확인 전 반복 재부팅");
    return;
  }
  I.verifying = true;
  Log::printf("새 펌웨어 %s 동작 확인 중 (부팅 %u회)", FW_VERSION, boots);
}

void loop() {
  if (I.rolled[0] && !rolledNotified && WiFi.isConnected()) {
    rolledNotified = true;
    char m[140];
    snprintf(m, sizeof(m), "DoorKey 업데이트 실패 — 이전 버전 %s로 되돌림 (%s)", FW_VERSION, I.rolled);
    Net::notify(m);
  }
  if (!I.verifying) return;
  uint32_t up = millis();
  if (WiFi.isConnected() && up >= VERIFY_OK_MS) {
    Preferences p;
    p.begin(NS, false);
    p.putBool("pend", false);
    p.end();
    I.verifying = false;
    Log::printf("새 펌웨어 %s 정상 확인", FW_VERSION);  // 정상이면 알림 없음 (되돌렸을 때만 알림)
  } else if (up >= VERIFY_TIMEOUT_MS) {
    rollback("Wi-Fi 연결 실패");
  }
}

}  // namespace Ota

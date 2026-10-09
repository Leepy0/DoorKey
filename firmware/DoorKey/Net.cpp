#include "Net.h"
#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <time.h>
#include "Log.h"
#include "Ota.h"
#include "Store.h"

namespace Net {

enum class JobType : uint8_t { Unlock, TestUnlock, Exchange, Refresh, Check, Notify, TestNotify, UpdCheck, UpdInstall, Presence, Heartbeat, CmdPoll };

struct Job {
  JobType type;
  int8_t slot;     // Presence: 기기 슬롯
  uint16_t gen;    // Presence: 요청 당시 세대 (도중에 ID가 바뀌면 결과 버림)
  uint32_t detectMs;
  char who[40];
  char text[200];
  char link[48];   // Notify: 알림을 누르면 열 URL
};

static const char* API_HOST = "api.smartthings.com";

static QueueHandle_t q = nullptr;
static SemaphoreHandle_t sMtx = nullptr;
static Status S;
static volatile bool anyAway = false;

static NetworkClientSecure* api = nullptr;  // SmartThings 전용, 연결을 재사용한다

static uint32_t nextRefreshMs = 15000;  // 부팅 15초 뒤 첫 갱신
static uint32_t refreshBackoffMs = 60000;

static bool warmWas = false;
static uint32_t warmSinceMs = 0, lastWarmTryMs = 0, warmLifeN = 0;
static uint64_t warmLifeSum = 0;
static bool warmFailLogged = false;

// 폰 위치 조회 결과 (sMtx로 보호)
static Pres PR[MAX_DEVICES];
static uint16_t presGen[MAX_DEVICES];
static volatile bool presBusy[MAX_DEVICES];

struct SLock {
  SLock() { xSemaphoreTake(sMtx, portMAX_DELAY); }
  ~SLock() { xSemaphoreGive(sMtx); }
};

static uint32_t epochNow() {
  time_t t = time(nullptr);
  return t > 1700000000 ? (uint32_t)t : 0;
}

static String enc(const String& s) {
  static const char* hex = "0123456789ABCDEF";
  String o;
  o.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      o += (char)c;
    } else {
      o += '%';
      o += hex[c >> 4];
      o += hex[c & 15];
    }
  }
  return o;
}

static void setError(const String& e) {
  SLock l;
  strlcpy(S.lastError, e.c_str(), sizeof(S.lastError));
  Log::utf8Fix(S.lastError);
}

// ---------------------------------------------------------------- HTTP

// SmartThings API 호출. 유지 중인 TLS 연결이 있으면 그대로 쓴다.
static int apiRequest(const char* method, const String& path, const String& body, const char* ctype,
                      const String& auth, String& resp, bool retry = true) {
  HTTPClient http;
  http.setReuse(true);
  http.setConnectTimeout(5000);
  http.setTimeout(8000);
  if (!http.begin(*api, String("https://") + API_HOST + path)) return -100;
  if (auth.length()) http.addHeader("Authorization", auth);
  if (ctype) http.addHeader("Content-Type", ctype);
  http.addHeader("Accept", "application/json");
  int code = http.sendRequest(method, (uint8_t*)body.c_str(), body.length());
  if (code > 0) {
    resp = http.getString();
  } else {
    resp = HTTPClient::errorToString(code);
  }
  http.end();
  if (code <= 0 && retry) {
    // 서버가 유휴 연결을 닫은 경우 → 새로 연결해서 한 번 더
    api->stop();
    return apiRequest(method, path, body, ctype, auth, resp, false);
  }
  return code;
}

// ---------------------------------------------------------------- OAuth

static void scheduleRefresh(uint32_t expiresIn) {
  // 만료 시간의 절반 뒤 갱신 (30분 ~ 12시간 사이로 제한)
  uint32_t sec = expiresIn / 2;
  if (sec < 1800) sec = 1800;
  if (sec > 43200) sec = 43200;
  nextRefreshMs = millis() + sec * 1000UL;
  if (!nextRefreshMs) nextRefreshMs = 1;
}

static int tokenRequest(const String& form, const String& oldRefresh, String& err) {
  StConf c = Store::getSt();
  if (c.clientId.isEmpty() || c.clientSecret.isEmpty()) {
    err = "Client ID/Secret 미설정";
    return -1;
  }
  String resp;
  int code = apiRequest("POST", "/oauth/token", form, "application/x-www-form-urlencoded",
                        "Basic " + base64::encode(c.clientId + ":" + c.clientSecret), resp);
  if (code != 200) {
    err = "HTTP " + String(code) + " " + resp.substring(0, 100);
    return code;
  }
  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    err = "토큰 응답 파싱 실패";
    return -2;
  }
  StTokens t;
  t.access = doc["access_token"] | "";
  t.refresh = doc["refresh_token"] | oldRefresh.c_str();
  uint32_t expiresIn = doc["expires_in"] | 86400;
  uint32_t now = epochNow();
  t.expiresAt = now ? now + expiresIn : 0;
  if (t.access.isEmpty()) {
    err = "access_token 없음";
    return -3;
  }
  Store::setTokens(t);  // 새 리프레시 토큰은 즉시 저장
  scheduleRefresh(expiresIn);
  {
    SLock l;
    S.hasToken = true;
    S.authBroken = false;
    S.expiresAt = t.expiresAt;
    S.lastRefreshMs = millis();
    S.lastRefreshCode = 200;
  }
  refreshBackoffMs = 60000;
  return 200;
}

static void sendNotify(const char* msg, const char* click = nullptr);

static bool refreshTokens(const char* why) {
  StTokens t = Store::getTokens();
  StConf c = Store::getSt();
  if (t.refresh.isEmpty()) return false;
  String form = "grant_type=refresh_token&client_id=" + enc(c.clientId) + "&client_secret=" + enc(c.clientSecret) +
                "&refresh_token=" + enc(t.refresh);
  String err;
  int code = tokenRequest(form, t.refresh, err);
  if (code == 200) {
    Log::printf("SmartThings 토큰 갱신 (%s)", why);
    return true;
  }
  {
    SLock l;
    S.lastRefreshCode = code;
  }
  setError("토큰 갱신 실패: " + err);
  Log::printf("토큰 갱신 실패 (%s): %s", why, err.c_str());
  if (code == 400 || code == 401) {
    // 리프레시 토큰 만료/무효 → 사람이 다시 인증해야 한다
    bool first;
    {
      SLock l;
      first = !S.authBroken;
      S.authBroken = true;
    }
    nextRefreshMs = 0;
    if (first) sendNotify("SmartThings 인증이 만료됐습니다. 웹 UI에서 다시 인증하세요.");
  } else {
    nextRefreshMs = millis() + refreshBackoffMs;
    if (!nextRefreshMs) nextRefreshMs = 1;
    refreshBackoffMs = min<uint32_t>(refreshBackoffMs * 2, 30 * 60000UL);
  }
  return false;
}

static void doExchange(const char* input) {
  // 코드만 붙여넣거나, 리다이렉트된 전체 URL/JSON을 붙여넣어도 된다
  String in = input;
  String code = in;
  if (in.indexOf("code=") >= 0) {
    code = in.substring(in.indexOf("code=") + 5);
  } else if (in.indexOf("\"code\"") >= 0) {
    int q1 = in.indexOf('"', in.indexOf(':', in.indexOf("\"code\"")) + 1);
    int q2 = in.indexOf('"', q1 + 1);
    if (q1 > 0 && q2 > q1) code = in.substring(q1 + 1, q2);
  }
  int amp = code.indexOf('&');
  if (amp >= 0) code = code.substring(0, amp);
  code.trim();
  if (code.isEmpty()) {
    setError("인증 코드가 비어 있습니다");
    return;
  }
  StConf c = Store::getSt();
  String form = "grant_type=authorization_code&code=" + enc(code) + "&redirect_uri=" + enc(c.redirect) +
                "&client_id=" + enc(c.clientId) + "&client_secret=" + enc(c.clientSecret);
  String err;
  int rc = tokenRequest(form, "", err);
  if (rc == 200) {
    Log::printf("SmartThings 인증 완료");
    setError("");
  } else {
    setError("인증 실패: " + err);
    Log::printf("SmartThings 인증 실패: %s", err.c_str());
  }
}

// ---------------------------------------------------------------- 명령

static String buildCommand(const StConf& c) {
  JsonDocument doc;
  JsonObject cmd = doc["commands"].to<JsonArray>().add<JsonObject>();
  cmd["component"] = c.component;
  cmd["capability"] = c.capability;
  cmd["command"] = c.command;
  JsonDocument args;
  if (!deserializeJson(args, c.args) && args.is<JsonArray>()) {
    cmd["arguments"] = args.as<JsonArray>();
  } else {
    cmd["arguments"].to<JsonArray>();
  }
  String body;
  serializeJson(doc, body);
  return body;
}

static void doUnlock(const Job& j) {
  StConf c = Store::getSt();
  StTokens t = Store::getTokens();
  uint32_t t0 = millis();
  int code;
  String resp;
  if (t.access.isEmpty()) {
    code = -1;
    resp = "토큰 없음 (SmartThings 인증 필요)";
  } else {
    String body = buildCommand(c);
    String path = "/v1/devices/" + c.deviceId + "/commands";
    code = apiRequest("POST", path, body, "application/json", "Bearer " + t.access, resp);
    if (code == 401 && refreshTokens("401 응답")) {
      t = Store::getTokens();
      code = apiRequest("POST", path, body, "application/json", "Bearer " + t.access, resp);
    }
  }
  uint32_t httpMs = millis() - t0;
  uint32_t totalMs = millis() - j.detectMs;
  bool ok = code >= 200 && code < 300;
  {
    SLock l;
    S.cmdCode = code;
    S.cmdHttpMs = httpMs;
    S.cmdTotalMs = totalMs;
    S.cmdAtEpoch = epochNow();
    S.cmdAtMs = millis();
    strlcpy(S.cmdWho, j.who, sizeof(S.cmdWho));
    strlcpy(S.cmdBody, resp.c_str(), sizeof(S.cmdBody));
    Log::utf8Fix(S.cmdBody);
  }
  Log::printf("문 열기%s [%s] HTTP %d — 요청 %lums, 감지 후 %lums", j.type == JobType::TestUnlock ? "(테스트)" : "",
              j.who, code, (unsigned long)httpMs, (unsigned long)totalMs);
  if (!ok) Log::printf("응답: %.100s", resp.c_str());

  if (j.type == JobType::Unlock && !ok) {  // 실패만 알림. 열림은 로그에만
    char m[160];
    snprintf(m, sizeof(m), "%s 귀가 감지 — 문 열기 실패 (HTTP %d)", j.who, code);
    sendNotify(m);
  }
}

static void doCheck() {
  StConf c = Store::getSt();
  StTokens t = Store::getTokens();
  String resp;
  int code = -1;
  if (t.access.isEmpty()) {
    resp = "토큰 없음";
  } else {
    code = apiRequest("GET", "/v1/devices/" + c.deviceId + "/status", "", nullptr, "Bearer " + t.access, resp);
    if (code == 401 && refreshTokens("401 응답")) {
      t = Store::getTokens();
      code = apiRequest("GET", "/v1/devices/" + c.deviceId + "/status", "", nullptr, "Bearer " + t.access, resp);
    }
  }
  SLock l;
  S.checkAtMs = millis();
  S.checkCode = code;
  strlcpy(S.checkBody, resp.c_str(), sizeof(S.checkBody));
  Log::utf8Fix(S.checkBody);
}

// ---------------------------------------------------------------- 폰 위치 (presenceSensor)

// "2026-10-07T05:12:33.123Z" → epoch (UTC). 실패하면 0
static uint32_t parseIso(const char* s) {
  int Y, M, D, h, m, sec;
  if (!s || sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &Y, &M, &D, &h, &m, &sec) != 6 || Y < 2020) return 0;
  Y -= M <= 2;  // 그레고리력 날짜 → 1970-01-01 기준 일수
  int era = Y / 400, yoe = Y - era * 400;
  int doy = (153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int64_t days = (int64_t)era * 146097 + doe - 719468;
  return (uint32_t)(days * 86400 + h * 3600 + m * 60 + sec);
}

static void doPresence(const Job& j) {
  StTokens t = Store::getTokens();
  String path = String("/v1/devices/") + j.text + "/components/main/capabilities/presenceSensor/status";
  String resp;
  int code = apiRequest("GET", path, "", nullptr, "Bearer " + t.access, resp);
  if (code == 401 && refreshTokens("401 응답")) {
    t = Store::getTokens();
    code = apiRequest("GET", path, "", nullptr, "Bearer " + t.access, resp);
  }

  int8_t val = -1;
  uint32_t since = 0;
  char err[96] = "";
  if (code == 200) {
    JsonDocument doc;
    if (deserializeJson(doc, resp)) {
      strlcpy(err, "응답을 읽지 못함", sizeof(err));
    } else {
      const char* v = doc["presence"]["value"] | "";
      if (!strcmp(v, "present")) val = 1;
      else if (!strcmp(v, "not present")) val = 0;
      else strlcpy(err, "위치 값 없음 — 폰 위치 기기 ID가 맞는지 확인", sizeof(err));
      since = parseIso(doc["presence"]["timestamp"] | "");
    }
  } else if (code == 403 || code == 404) {
    snprintf(err, sizeof(err), "HTTP %d — 이 토큰으로 볼 수 없는 기기 (ID 확인)", code);
  } else {
    snprintf(err, sizeof(err), "HTTP %d", code);
  }

  int slot = j.slot;
  int8_t prevVal;
  bool prevErr;
  {
    SLock l;
    presBusy[slot] = false;
    if (j.gen != presGen[slot]) return;  // 조회 중에 ID가 바뀜
    Pres& p = PR[slot];
    prevVal = p.val;
    prevErr = p.err[0] != 0;
    p.tryMs = millis();
    p.code = code;
    strlcpy(p.err, err, sizeof(p.err));
    if (val >= 0) {
      p.val = val;
      p.since = since;
      p.okMs = millis();
      if (val == 0) {
        p.awaySeenMs = millis();
        p.awaySince = since;
        if (p.awayRun < 255) p.awayRun++;
      } else {
        p.awayRun = 0;
      }
    }
  }
  if (val >= 0 && val != prevVal) {
    String at = since ? Log::stamp(since, 0) : String("시각 모름");
    Log::printf("[%s] SmartThings 위치: %s (%s부터)", j.who, val ? "집" : "외출", at.c_str());
  } else if (val < 0 && !prevErr) {
    Log::printf("[%s] SmartThings 위치 조회 실패: %s", j.who, err);
  }
}

// ---------------------------------------------------------------- 알림 (ntfy 등 HTTP POST)

static void sendNotify(const char* msg, const char* click) {
  String url = Store::ntfyUrl();
  if (url.isEmpty()) return;
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(6000);
  NetworkClientSecure sc;
  NetworkClient plain;
  bool began;
  if (url.startsWith("https://")) {
    sc.setInsecure();  // 인증서 검증 생략 (CA 번들은 코어 버전별 API 차이·IRAM 부담)
    began = http.begin(sc, url);
  } else {
    began = http.begin(plain, url);
  }
  if (!began) {
    Log::printf("알림 URL 오류");
    return;
  }
  http.addHeader("Title", "DoorKey");
  if (click && *click) http.addHeader("Click", click);  // ntfy: 알림을 누르면 이 주소를 연다
  http.addHeader("Content-Type", "text/plain; charset=utf-8");
  int code = http.POST((uint8_t*)msg, strlen(msg));
  http.end();
  if (code < 200 || code >= 300) Log::printf("알림 전송 실패 (HTTP %d)", code);
}

// ---------------------------------------------------------------- Heartbeat

static uint32_t nextHbMs = 0;

// 설정된 URL을 GET. 응답이 2xx면 성공. 실패해도 로그는 첫 실패·복구 때만 남긴다
static void sendHeartbeat() {
  String url = Store::hbUrl();
  if (url.isEmpty()) return;
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(6000);
  NetworkClientSecure sc;
  NetworkClient plain;
  bool began;
  if (url.startsWith("https://")) {
    sc.setInsecure();
    began = http.begin(sc, url);
  } else {
    began = http.begin(plain, url);
  }
  int code = began ? http.GET() : -100;
  http.end();
  bool ok = code >= 200 && code < 300;
  uint32_t fails;
  {
    SLock l;
    S.hbAtMs = millis();
    S.hbCode = code;
    fails = ok ? 0 : S.hbFails + 1;
    bool wasFailing = S.hbFails > 0;
    S.hbFails = fails;
    if (!ok && fails == 1) Log::printf("Heartbeat 전송 실패 (HTTP %d)", code);
    if (ok && wasFailing) Log::printf("Heartbeat 전송 복구");
  }
}

static void heartbeatLoop() {
  if ((int32_t)(millis() - nextHbMs) < 0) return;
  nextHbMs = millis() + HB_INTERVAL_SEC * 1000UL;
  sendHeartbeat();
}

// ---------------------------------------------------------------- ntfy 명령 토픽

static uint32_t nextCmdMs = 0;

static void setCmdLast(const char* s) {
  SLock l;
  strlcpy(S.ntfyLast, s, sizeof(S.ntfyLast));
  Log::utf8Fix(S.ntfyLast);
}

// 명령 한 건 실행. 문 열기는 일부러 받지 않는다 (토픽 이름만 알면 누구나 보낼 수 있으므로)
static void runCmd(const String& msg) {
  String c = msg;
  c.trim();
  c.toLowerCase();
  Log::printf("ntfy 명령: %s", c.c_str());
  if (c == "update") {
    Ota::check();
    Ota::Info i = Ota::info();
    if (i.st == Ota::St::Available) {
      char m[120];
      snprintf(m, sizeof(m), "DoorKey v%s 설치 시작 (현재 %s) — 끝나면 재부팅", i.latest, FW_VERSION);
      sendNotify(m);
      setCmdLast("update → 설치 중");
      api->stop();
      Ota::install();  // 성공하면 재부팅하므로 아래로 내려오면 실패
      i = Ota::info();
      snprintf(m, sizeof(m), "DoorKey 설치 실패: %s", i.err);
      sendNotify(m);
      setCmdLast("update → 설치 실패");
    } else if (i.st == Ota::St::Latest) {
      sendNotify("DoorKey " FW_VERSION " — 이미 최신 버전");
      setCmdLast("update → 최신");
    } else {
      char m[140];
      snprintf(m, sizeof(m), "DoorKey 업데이트 확인 실패: %s", i.err);
      sendNotify(m);
      setCmdLast("update → 확인 실패");
    }
  } else if (c == "check") {
    Ota::check();
    Ota::Info i = Ota::info();
    char m[140];
    if (i.st == Ota::St::Available) snprintf(m, sizeof(m), "DoorKey 새 버전 v%s 있음 (현재 %s) — 'update'를 보내면 설치", i.latest, FW_VERSION);
    else if (i.st == Ota::St::Latest) snprintf(m, sizeof(m), "DoorKey %s — 최신 버전", FW_VERSION);
    else snprintf(m, sizeof(m), "DoorKey 업데이트 확인 실패: %s", i.err);
    sendNotify(m);
    setCmdLast("check");
  } else if (c == "reboot") {
    sendNotify("DoorKey 재부팅");
    setCmdLast("reboot");
    delay(500);
    ESP.restart();
  } else if (c == "status") {
    char m[160];
    snprintf(m, sizeof(m), "DoorKey %s · http://%s · Wi-Fi %d dBm · 가동 %lu분", FW_VERSION, WiFi.localIP().toString().c_str(),
             WiFi.RSSI(), (unsigned long)(millis() / 60000));
    sendNotify(m);
    setCmdLast("status");
  } else {
    setCmdLast(("모르는 명령: " + c).c_str());
  }
}

// 토픽의 새 메시지를 가져온다 (poll=1: 쌓인 것만 받고 바로 끊음). 한 번에 여러 줄(JSON Lines)이 올 수 있다
static void pollCmd() {
  String url = Store::cmdUrl();
  if (url.isEmpty()) return;
  String since = Store::cmdSince();
  if (url.endsWith("/")) url.remove(url.length() - 1);
  url += "/json?poll=1&since=" + (since.isEmpty() ? String("30s") : since);  // 처음엔 최근 30초치만

  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(8000);
  NetworkClientSecure sc;
  NetworkClient plain;
  bool began;
  if (url.startsWith("https://")) {
    sc.setInsecure();
    began = http.begin(sc, url);
  } else {
    began = http.begin(plain, url);
  }
  int code = began ? http.GET() : -100;
  String body = code == 200 ? http.getString() : String();
  http.end();
  {
    SLock l;
    S.ntfyAtMs = millis();
    S.ntfyCode = code;
  }
  if (code != 200) return;

  // 줄마다 {"id":..,"event":"message","message":".."}
  int pos = 0;
  String lastId;
  String pending;
  while (pos < (int)body.length()) {
    int nl = body.indexOf('\n', pos);
    if (nl < 0) nl = body.length();
    String line = body.substring(pos, nl);
    pos = nl + 1;
    line.trim();
    if (line.isEmpty()) continue;
    JsonDocument doc;
    if (deserializeJson(doc, line)) continue;
    const char* ev = doc["event"] | "";
    if (strcmp(ev, "message") != 0) continue;
    lastId = doc["id"] | "";
    pending = doc["message"] | "";  // 여러 개가 쌓였으면 마지막 것만 실행
  }
  if (lastId.isEmpty()) return;
  Store::setCmdSince(lastId);  // 실행 전에 저장: 재부팅 명령이 반복되지 않도록
  runCmd(pending);
}

static void cmdLoop() {
  if ((int32_t)(millis() - nextCmdMs) < 0) return;
  nextCmdMs = millis() + CMD_POLL_SEC * 1000UL;
  pollCmd();
}

// ---------------------------------------------------------------- 주기 작업

static void refreshLoop() {
  bool broken;
  {
    SLock l;
    broken = S.authBroken;
  }
  if (broken || !nextRefreshMs || (int32_t)(millis() - nextRefreshMs) < 0) return;
  if (Store::getTokens().refresh.isEmpty()) {
    nextRefreshMs = 0;
    return;
  }
  refreshTokens("주기 갱신");
}

// 외출 중인 사람이 있으면 TLS 연결을 미리 열어둔다 → 귀가 시 핸드셰이크 생략
static void warmLoop() {
  bool connected = api->connected();
  if (warmWas && !connected) {
    warmLifeSum += (millis() - warmSinceMs) / 1000;
    warmLifeN++;
  }
  warmWas = connected;

  bool hasToken, broken;
  {
    SLock l;
    S.warm = connected;
    S.warmAvgLifeSec = warmLifeN ? (uint32_t)(warmLifeSum / warmLifeN) : 0;
    hasToken = S.hasToken;
    broken = S.authBroken;
  }
  bool want = Store::params().keepWarm && anyAway && hasToken && !broken;
  if (!want || connected) return;
  if (millis() - lastWarmTryMs < 30000) return;
  lastWarmTryMs = millis();

  uint32_t t0 = millis();
  api->stop();
  if (api->connect(API_HOST, 443)) {
    warmSinceMs = millis();
    warmWas = true;
    warmFailLogged = false;
    SLock l;
    S.warmConnects++;
    S.lastTlsMs = millis() - t0;
  } else if (!warmFailLogged) {
    warmFailLogged = true;
    Log::printf("SmartThings 연결 유지 실패 (이후 30초마다 재시도)");
  }
}

static void handle(const Job& j) {
  switch (j.type) {
    case JobType::Unlock:
    case JobType::TestUnlock: doUnlock(j); break;
    case JobType::Exchange: doExchange(j.text); break;
    case JobType::Refresh:
      {
        SLock l;
        S.authBroken = false;
      }
      if (!refreshTokens("수동")) Log::printf("수동 갱신 실패");
      break;
    case JobType::Check: doCheck(); break;
    case JobType::Notify: sendNotify(j.text, j.link); break;
    case JobType::TestNotify: sendNotify("DoorKey 알림 테스트"); break;
    case JobType::UpdCheck: Ota::check(); break;
    case JobType::UpdInstall:
      api->stop();  // TLS 메모리 확보
      Ota::install();
      break;
    case JobType::Presence: doPresence(j); break;
    case JobType::Heartbeat:
      sendHeartbeat();
      nextHbMs = millis() + HB_INTERVAL_SEC * 1000UL;
      break;
    case JobType::CmdPoll:
      pollCmd();
      nextCmdMs = millis() + CMD_POLL_SEC * 1000UL;
      break;
  }
}

static void task(void*) {
  api = new NetworkClientSecure();
  api->setInsecure();  // 인증서 검증 생략 (암호화는 유지)
  api->setHandshakeTimeout(10);
  {
    StTokens t = Store::getTokens();
    SLock l;
    S.hasToken = !t.refresh.isEmpty();
    S.expiresAt = t.expiresAt;
  }
  for (;;) {
    Job j;
    if (xQueueReceive(q, &j, pdMS_TO_TICKS(500)) == pdTRUE) {
      if (WiFi.isConnected()) {
        handle(j);
      } else if (j.type == JobType::Presence) {
        presBusy[j.slot] = false;  // 주기 조회라 조용히 건너뜀
      } else {
        Log::printf("Wi-Fi 끊김 — 네트워크 작업 %d 건너뜀", (int)j.type);
      }
    }
    if (WiFi.isConnected()) {
      refreshLoop();
      warmLoop();
      heartbeatLoop();
      cmdLoop();
      if (!anyAway) Ota::periodic();  // 외출 중엔 업데이트 확인을 미룬다 (귀가 순간 문 열기와 겹치지 않게)
    }
  }
}

// ---------------------------------------------------------------- 외부 API

static bool push(const Job& j, bool front = false) {
  if (!q) return false;
  BaseType_t r = front ? xQueueSendToFront(q, &j, 0) : xQueueSend(q, &j, 0);
  if (r != pdTRUE) Log::printf("네트워크 큐 가득 참 — 요청 버림");
  return r == pdTRUE;
}

bool requestUnlock(const char* who, uint32_t detectMs, bool test) {
  Job j = {};
  j.type = test ? JobType::TestUnlock : JobType::Unlock;
  j.detectMs = detectMs;
  strlcpy(j.who, who, sizeof(j.who));
  return push(j, true);  // 문 열기는 맨 앞으로
}

void requestCodeExchange(const String& code) {
  Job j = {};
  j.type = JobType::Exchange;
  strlcpy(j.text, code.c_str(), sizeof(j.text));
  push(j);
}

void requestRefresh() {
  Job j = {};
  j.type = JobType::Refresh;
  push(j);
}

void requestCheck() {
  Job j = {};
  j.type = JobType::Check;
  push(j);
}

void notify(const char* msg, const char* click) {
  Job j = {};
  j.type = JobType::Notify;
  strlcpy(j.link, click ? click : "", sizeof(j.link));
  strlcpy(j.text, msg, sizeof(j.text));
  push(j);
}

void requestTestNotify() {
  Job j = {};
  j.type = JobType::TestNotify;
  push(j);
}

void requestHeartbeat() {
  Job j = {};
  j.type = JobType::Heartbeat;
  push(j);
}

void requestCmdPoll() {
  Job j = {};
  j.type = JobType::CmdPoll;
  push(j);
}

void requestUpdCheck() {
  Job j = {};
  j.type = JobType::UpdCheck;
  push(j);
}

void requestUpdInstall() {
  Job j = {};
  j.type = JobType::UpdInstall;
  push(j);
}

bool requestPresence(int slot, const char* name, const char* deviceId) {
  if (slot < 0 || slot >= MAX_DEVICES || !q || presBusy[slot] || !WiFi.isConnected()) return false;
  if (uxQueueSpacesAvailable(q) < 3) return false;  // 문 열기 자리를 남겨 둔다
  Job j = {};
  {
    SLock l;
    if (!S.hasToken || S.authBroken) return false;
    j.gen = presGen[slot];
  }
  j.type = JobType::Presence;
  j.slot = slot;
  strlcpy(j.who, name, sizeof(j.who));
  strlcpy(j.text, deviceId, sizeof(j.text));
  presBusy[slot] = true;
  if (xQueueSend(q, &j, 0) != pdTRUE) {
    presBusy[slot] = false;
    return false;
  }
  return true;
}

static void presClear(int slot) {
  memset(&PR[slot], 0, sizeof(Pres));
  PR[slot].val = -1;
  presGen[slot]++;
}

void presReset(int slot) {
  if (slot < 0 || slot >= MAX_DEVICES) return;
  if (!sMtx) return presClear(slot);  // Net::begin 전
  SLock l;
  presClear(slot);
}

void presNewEpisode(int slot) {
  if (slot < 0 || slot >= MAX_DEVICES || !sMtx) return;
  SLock l;
  PR[slot].awayRun = 0;
}

Pres pres(int slot) {
  SLock l;
  return PR[slot];
}

void setAnyAway(bool v) { anyAway = v; }

Status status() {
  SLock l;
  return S;
}

void begin() {
  memset(&S, 0, sizeof(S));
  for (int i = 0; i < MAX_DEVICES; i++) presClear(i);
  sMtx = xSemaphoreCreateMutex();
  q = xQueueCreate(8, sizeof(Job));
  // loop(웹 서버)와 같은 우선순위 → TLS 계산 중에도 번갈아 실행돼 웹이 멈추지 않음
  xTaskCreatePinnedToCore(task, "net", 16384, nullptr, 1, nullptr, 1);
}

}  // namespace Net

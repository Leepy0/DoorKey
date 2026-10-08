#include "Web.h"
#include <WebServer.h>
#include <WiFi.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "config.h"
#include "Ble.h"
#include "Irk.h"
#include "Log.h"
#include "Net.h"
#include "Ota.h"
#include "Presence.h"
#include "Store.h"
#include "WebPage.h"

#ifndef FW_VERSION
#define FW_VERSION "1.0.0"
#endif

namespace Web {

static WebServer server(80);
static String adminPass;
static bool uploadAuthOk = false;

// BLE 광고 수신 속도 계산
static uint32_t rateLastMs = 0, rateLastCnt = 0, rate = 0;

static bool auth() {
  if (server.authenticate(ADMIN_USER, adminPass.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "DoorKey");
  return false;
}

static void sendJson(JsonDocument& doc, int code = 200) {
  String s;
  serializeJson(doc, s);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", s);
}

static void ok(const char* msg = nullptr) {
  JsonDocument d;
  d["ok"] = true;
  if (msg) d["msg"] = msg;
  sendJson(d);
}

static void fail(const String& err, int code = 400) {
  JsonDocument d;
  d["ok"] = false;
  d["err"] = err;
  sendJson(d, code);
}

static bool body(JsonDocument& doc) {
  if (deserializeJson(doc, server.arg("plain"))) {
    fail("JSON 오류");
    return false;
  }
  return true;
}

static const char* enrollName(Ble::EnrollState s) {
  switch (s) {
    case Ble::EnrollState::Waiting: return "waiting";
    case Ble::EnrollState::Connected: return "connected";
    case Ble::EnrollState::Done: return "done";
    case Ble::EnrollState::Failed: return "failed";
    default: return "idle";
  }
}

static int32_t agoSec(uint32_t ms) { return ms ? (int32_t)((millis() - ms) / 1000) : -1; }

// ---------------------------------------------------------------- GET

static void hStatus() {
  if (!auth()) return;
  uint32_t now = millis();
  JsonDocument d;
  time_t t = time(nullptr);
  d["now"] = t > 1700000000 ? (uint32_t)t : 0;
  d["uptime"] = now / 1000;
  d["fw"] = FW_VERSION;
  bool ap = WiFi.getMode() & WIFI_AP;
  d["ap"] = ap;
  d["ip"] = WiFi.isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  d["ssid"] = WiFi.isConnected() ? WiFi.SSID() : String(SETUP_AP_SSID);
  d["rssi"] = WiFi.isConnected() ? WiFi.RSSI() : 0;
  d["heap"] = ESP.getFreeHeap();
  d["minHeap"] = ESP.getMinFreeHeap();
  d["psram"] = ESP.getFreePsram();
  d["auto"] = Store::params().autoEnabled;
  d["defaultPass"] = adminPass == DEFAULT_ADMIN_PASS;
  d["logId"] = Log::lastId();

  JsonObject pp = d["params"].to<JsonObject>();
  pp["arriveRssi"] = Store::params().arriveRssi;
  pp["exitRssi"] = Store::params().exitRssi;

  Ble::Stats bs = Ble::stats();
  JsonObject b = d["ble"].to<JsonObject>();
  b["scanning"] = bs.scanning;
  b["adv"] = bs.advTotal;
  b["rpa"] = bs.rpaTotal;
  b["hit"] = bs.cacheHit;
  b["drop"] = bs.queueDrop;
  b["rate"] = rate;
  JsonObject app = b["app"].to<JsonObject>();
  app["ago"] = agoSec(bs.appBeaconMs);
  app["rssi"] = bs.appBeaconRssi;
  app["slot"] = bs.appBeaconSlot;
  app["name"] = bs.appBeaconSlot >= 0 ? Store::devices()[bs.appBeaconSlot].name : "";

  Ble::EnrollInfo ei = Ble::enrollInfo();
  JsonObject en = d["enroll"].to<JsonObject>();
  en["st"] = enrollName(ei.st);
  en["name"] = ei.name;
  en["msg"] = ei.msg;
  int32_t left = (int32_t)BLE_ENROLL_SEC - (int32_t)((now - ei.startedMs) / 1000);
  en["left"] = left < 0 ? 0 : left;

  JsonArray devs = d["devices"].to<JsonArray>();
  Device* D = Store::devices();
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (!D[i].used) continue;
    const Presence::Rt& r = Presence::rt(i);
    JsonObject o = devs.add<JsonObject>();
    char hex[33], addr[18];
    Irk::toHex(D[i].irk, hex);
    Irk::addrToStr(r.lastAddr, addr);
    o["slot"] = i;
    o["name"] = D[i].name;
    o["enabled"] = D[i].enabled;
    o["irk"] = hex;
    o["idAddr"] = D[i].idAddr;
    o["st"] = Presence::stName(r.st);
    o["stFor"] = (now - r.stSinceMs) / 1000;
    o["seenAgo"] = agoSec(r.lastSeenMs);
    o["rssi"] = r.lastRssi;
    o["ema"] = (int)roundf(r.ema);
    o["itv"] = (int)r.itvEma;
    o["maxGap"] = r.maxGapMs / 1000;
    o["exitPeak"] = r.lastExitPeak;
    o["addr"] = addr;
    o["event"] = r.lastEvent;
    o["presId"] = D[i].presId;
    o["chk"] = Presence::chkName(Presence::check(i));  // 미감지 상태의 외출 확인 진행
    if (D[i].presId[0]) {
      Net::Pres p = Net::pres(i);
      JsonObject pr = o["pres"].to<JsonObject>();
      pr["val"] = p.val;
      pr["since"] = p.since;
      pr["ok"] = agoSec(p.okMs);
      pr["try"] = agoSec(p.tryMs);
      pr["code"] = p.code;
      pr["err"] = p.err;
      pr["run"] = p.awayRun;
    }
    int8_t sp[60];
    Presence::spark(i, sp);
    JsonArray a = o["spark"].to<JsonArray>();
    for (int k = 0; k < 60; k++) a.add(sp[k]);
  }

  Net::Status ns = Net::status();
  JsonObject st = d["st"].to<JsonObject>();
  st["hasToken"] = ns.hasToken;
  st["broken"] = ns.authBroken;
  st["expIn"] = (ns.expiresAt && t > 1700000000) ? (int32_t)(ns.expiresAt - (uint32_t)t) : -1;
  st["refAgo"] = agoSec(ns.lastRefreshMs);
  st["refCode"] = ns.lastRefreshCode;
  st["warm"] = ns.warm;
  st["warmN"] = ns.warmConnects;
  st["warmLife"] = ns.warmAvgLifeSec;
  st["tls"] = ns.lastTlsMs;
  st["err"] = ns.lastError;
  JsonObject cmd = st["cmd"].to<JsonObject>();
  cmd["code"] = ns.cmdCode;
  cmd["http"] = ns.cmdHttpMs;
  cmd["total"] = ns.cmdTotalMs;
  cmd["ago"] = agoSec(ns.cmdAtMs);
  cmd["who"] = ns.cmdWho;
  cmd["body"] = ns.cmdBody;
  JsonObject ck = st["check"].to<JsonObject>();
  ck["code"] = ns.checkCode;
  ck["ago"] = agoSec(ns.checkAtMs);
  ck["body"] = ns.checkBody;
  JsonObject hb = st["hb"].to<JsonObject>();
  hb["on"] = !Store::hbUrl().isEmpty();
  hb["ago"] = agoSec(ns.hbAtMs);
  hb["code"] = ns.hbCode;
  hb["fails"] = ns.hbFails;

  Ota::Info oi = Ota::info();
  JsonObject u = d["upd"].to<JsonObject>();
  u["st"] = Ota::stName(oi.st);
  u["latest"] = oi.latest;
  u["notes"] = oi.notes;
  u["checkedAgo"] = agoSec(oi.checkedMs);
  u["progress"] = oi.progress;
  u["err"] = oi.err;
  u["verifying"] = oi.verifying;
  u["rolled"] = oi.rolled;

  sendJson(d);
}

static void hLog() {
  if (!auth()) return;
  uint32_t since = server.arg("since").toInt();
  const int MAXN = 60;
  Log::Entry* buf = (Log::Entry*)ps_malloc(sizeof(Log::Entry) * MAXN);
  if (!buf) buf = (Log::Entry*)malloc(sizeof(Log::Entry) * MAXN);
  if (!buf) return fail("메모리 부족", 500);
  int n = Log::copySince(since, buf, MAXN);
  JsonDocument d;
  JsonArray a = d.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject o = a.add<JsonObject>();
    o["id"] = buf[i].id;
    o["t"] = Log::stamp(buf[i].epoch, buf[i].ms);
    o["m"] = buf[i].msg;
  }
  free(buf);
  sendJson(d);
}

static void hConfig() {
  if (!auth()) return;
  JsonDocument d;
  Store::paramsToJson(d["params"].to<JsonObject>());
  StConf c = Store::getSt();
  JsonObject st = d["st"].to<JsonObject>();
  st["clientId"] = c.clientId;
  st["hasSecret"] = !c.clientSecret.isEmpty();
  st["redirect"] = c.redirect;
  st["deviceId"] = c.deviceId;
  st["component"] = c.component;
  st["capability"] = c.capability;
  st["command"] = c.command;
  st["args"] = c.args;
  d["ntfy"] = Store::ntfyUrl();
  d["hb"] = Store::hbUrl();
  d["wifiSsid"] = Store::wifiSsid();
  sendJson(d);
}

static void hExport() {
  if (!auth()) return;
  JsonDocument d;
  Store::exportJson(d);
  String s;
  serializeJsonPretty(d, s);
  server.sendHeader("Content-Disposition", "attachment; filename=doorkey-backup.json");
  server.send(200, "application/json", s);
}

// ---------------------------------------------------------------- POST

static void hParams() {
  if (!auth()) return;
  JsonDocument d;
  if (!body(d)) return;
  String err;
  if (!Store::paramsFromJson(d.as<JsonObjectConst>(), err)) return fail(err);
  Store::saveParams();
  Log::printf("설정 변경");
  ok("저장됨");
}

static void hSt() {
  if (!auth()) return;
  JsonDocument d;
  if (!body(d)) return;
  StConf c = Store::getSt();
  c.clientId = d["clientId"] | c.clientId;
  String sec = d["clientSecret"] | "";
  if (sec.length()) c.clientSecret = sec;
  c.redirect = d["redirect"] | c.redirect;
  c.deviceId = d["deviceId"] | c.deviceId;
  c.component = d["component"] | c.component;
  c.capability = d["capability"] | c.capability;
  c.command = d["command"] | c.command;
  c.args = d["args"] | c.args;
  JsonDocument chk;
  if (deserializeJson(chk, c.args) || !chk.is<JsonArray>()) return fail("Arguments는 JSON 배열이어야 합니다 (예: [])");
  if (c.deviceId.length() < 8) return fail("Device ID 확인");
  Store::setSt(c);
  ok("저장됨");
}

static void hCode() {
  if (!auth()) return;
  JsonDocument d;
  if (!body(d)) return;
  String code = d["code"] | "";
  if (code.isEmpty()) return fail("코드가 비어 있습니다");
  Net::requestCodeExchange(code);
  ok("토큰 요청 중 — 상태를 확인하세요");
}

static void hEnroll() {
  if (!auth()) return;
  JsonDocument d;
  if (!body(d)) return;
  String err;
  if (!Ble::startEnroll(d["name"] | "", err)) return fail(err);
  ok("등록 모드 시작");
}

static void hDeviceAdd() {
  if (!auth()) return;
  JsonDocument d;
  if (!body(d)) return;
  uint8_t irk[16];
  if (!Irk::fromHex(d["irk"] | "", irk)) return fail("IRK는 16진수 32자");
  int slot = Store::addDevice(d["name"] | "기기", irk);
  if (slot < 0) return fail("기기 수 초과");
  Presence::resetSlot(slot);
  Ble::rebuildIrkTable();
  Log::printf("IRK 수동 추가: %s", Store::devices()[slot].name);
  ok("추가됨");
}

static void hDeviceUpdate() {
  if (!auth()) return;
  JsonDocument d;
  if (!body(d)) return;
  int slot = d["slot"] | -1;
  bool en = d["enabled"] | true;
  bool changed = slot >= 0 && slot < MAX_DEVICES && Store::devices()[slot].enabled != en;
  if (!Store::updateDevice(slot, d["name"] | "", en)) return fail("기기 없음");
  if (changed) Presence::resetSlot(slot);  // 비활성 동안의 낡은 상태로 오판하지 않도록
  Ble::rebuildIrkTable();
  ok("저장됨");
}

static void hDeviceDelete() {
  if (!auth()) return;
  JsonDocument d;
  if (!body(d)) return;
  int slot = d["slot"] | -1;
  String name = (slot >= 0 && slot < MAX_DEVICES) ? Store::devices()[slot].name : "";
  if (!Store::deleteDevice(slot)) return fail("기기 없음");
  Presence::resetSlot(slot);
  Ble::rebuildIrkTable();
  Log::printf("기기 삭제: %s", name.c_str());
  ok("삭제됨");
}

static void hDeviceState() {
  if (!auth()) return;
  JsonDocument d;
  if (!body(d)) return;
  if (!Presence::forceState(d["slot"] | -1, d["away"] | false)) return fail("기기 없음");
  ok();
}

// SmartThings 기기 ID 형식 (8-4-4-4-12 16진수)
static bool validUuid(const String& s) {
  if (s.length() != 36) return false;
  for (int i = 0; i < 36; i++) {
    bool dash = i == 8 || i == 13 || i == 18 || i == 23;
    if (dash ? s[i] != '-' : !isxdigit((unsigned char)s[i])) return false;
  }
  return true;
}

// 폰 위치 기기 ID 저장: {"items":[{"slot":0,"id":"..."}]}  (빈 id = 위치 확인 끔)
static void hPres() {
  if (!auth()) return;
  JsonDocument d;
  if (!body(d)) return;
  JsonArrayConst items = d["items"].as<JsonArrayConst>();
  for (JsonObjectConst it : items) {
    int slot = it["slot"] | -1;
    String id = it["id"] | "";
    id.trim();
    id.toLowerCase();
    if (slot < 0 || slot >= MAX_DEVICES || !Store::devices()[slot].used) return fail("기기 없음");
    if (id.length() && !validUuid(id)) return fail(String(Store::devices()[slot].name) + ": ID 형식이 아닙니다 (8-4-4-4-12자리)");
  }
  for (JsonObjectConst it : items) {
    int slot = it["slot"];
    String id = it["id"] | "";
    id.trim();
    id.toLowerCase();
    if (Store::setPresId(slot, id.c_str())) {
      Net::presReset(slot);
      Log::printf("[%s] 위치 기기 %s", Store::devices()[slot].name, id.length() ? "설정" : "해제");
    }
  }
  if (d["presFallback"].is<bool>()) {
    Store::params().presFallback = d["presFallback"].as<bool>();
    Store::saveParams();
  }
  Presence::presPollNow();
  ok("저장됨 — 위치를 조회합니다");
}

static void hNtfy() {
  if (!auth()) return;
  JsonDocument d;
  if (!body(d)) return;
  String u = d["url"] | "";
  if (u.length() && !u.startsWith("http://") && !u.startsWith("https://")) return fail("http(s):// 로 시작해야 합니다");
  Store::setNtfyUrl(u);
  ok("저장됨");
}

static void hHb() {
  if (!auth()) return;
  JsonDocument d;
  if (!body(d)) return;
  String u = d["url"] | "";
  if (u.length() && !u.startsWith("http://") && !u.startsWith("https://")) return fail("http(s):// 로 시작해야 합니다");
  Store::setHbUrl(u);
  if (u.length()) Net::requestHeartbeat();
  ok(u.length() ? "저장됨 — 지금 한 번 보냅니다" : "저장됨 — Heartbeat 끔");
}

static void hWifi() {
  if (!auth()) return;
  JsonDocument d;
  if (!body(d)) return;
  String ssid = d["ssid"] | "";
  if (ssid.isEmpty()) return fail("SSID가 비어 있습니다");
  Store::setWifi(ssid, d["pass"] | "");
  ok("저장됨 — 재부팅합니다");
  delay(500);
  ESP.restart();
}

static void hAdmin() {
  if (!auth()) return;
  JsonDocument d;
  if (!body(d)) return;
  String p = d["pass"] | "";
  if (p.length() < 6) return fail("6자 이상");
  Store::setAdminPass(p);
  adminPass = p;
  Log::printf("관리자 비밀번호 변경");
  ok("변경됨");
}

static void hImport() {
  if (!auth()) return;
  JsonDocument d;
  if (!body(d)) return;
  String err;
  if (!Store::importJson(d, err)) return fail(err);
  for (int i = 0; i < MAX_DEVICES; i++) Presence::resetSlot(i);
  Ble::rebuildIrkTable();
  Log::printf("설정 가져오기 완료");
  ok("가져옴");
}

static void hUpdateDone() {
  if (!uploadAuthOk) {
    server.requestAuthentication(BASIC_AUTH, "DoorKey");
    return;
  }
  bool good = !Update.hasError();
  server.send(200, "application/json", good ? "{\"ok\":true}" : "{\"ok\":false}");
  if (good) {
    Ota::markPending();  // 새 펌웨어가 이상하면 이전 버전으로 되돌리도록
    Log::printf("펌웨어 업데이트 완료 — 재부팅");
    delay(800);
    ESP.restart();
  }
}

static void hUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadAuthOk = server.authenticate(ADMIN_USER, adminPass.c_str());
    if (!uploadAuthOk) return;
    Log::printf("펌웨어 업로드 시작: %s", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Log::printf("업데이트 시작 실패: %s", Update.errorString());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadAuthOk && Update.write(up.buf, up.currentSize) != up.currentSize) Log::printf("쓰기 실패: %s", Update.errorString());
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadAuthOk && !Update.end(true)) Log::printf("업데이트 실패: %s", Update.errorString());
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
  }
}

// ---------------------------------------------------------------- 등록

void begin() {
  adminPass = Store::adminPass();

  server.on("/", HTTP_GET, [] {
    if (!auth()) return;
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/api/status", HTTP_GET, hStatus);
  server.on("/api/log", HTTP_GET, hLog);
  server.on("/api/config", HTTP_GET, hConfig);
  server.on("/api/export", HTTP_GET, hExport);

  server.on("/api/params", HTTP_POST, hParams);
  server.on("/api/st", HTTP_POST, hSt);
  server.on("/api/st/code", HTTP_POST, hCode);
  server.on("/api/st/refresh", HTTP_POST, [] {
    if (!auth()) return;
    Net::requestRefresh();
    ok("갱신 요청");
  });
  server.on("/api/st/check", HTTP_POST, [] {
    if (!auth()) return;
    Net::requestCheck();
    ok("확인 요청 — 상태를 보세요");
  });
  server.on("/api/st/test", HTTP_POST, [] {
    if (!auth()) return;
    Net::requestUnlock("테스트", millis(), true);
    ok("문 열기 요청");
  });
  server.on("/api/enroll", HTTP_POST, hEnroll);
  server.on("/api/enroll/cancel", HTTP_POST, [] {
    if (!auth()) return;
    Ble::cancelEnroll();
    ok("취소");
  });
  server.on("/api/device/add", HTTP_POST, hDeviceAdd);
  server.on("/api/device/update", HTTP_POST, hDeviceUpdate);
  server.on("/api/device/delete", HTTP_POST, hDeviceDelete);
  server.on("/api/device/state", HTTP_POST, hDeviceState);
  server.on("/api/pres", HTTP_POST, hPres);
  server.on("/api/pres/check", HTTP_POST, [] {
    if (!auth()) return;
    Presence::presPollNow();
    ok("위치 조회 중");
  });
  server.on("/api/ntfy", HTTP_POST, hNtfy);
  server.on("/api/ntfy/test", HTTP_POST, [] {
    if (!auth()) return;
    Net::requestTestNotify();
    ok("전송 요청");
  });
  server.on("/api/hb", HTTP_POST, hHb);
  server.on("/api/wifi", HTTP_POST, hWifi);
  server.on("/api/admin", HTTP_POST, hAdmin);
  server.on("/api/import", HTTP_POST, hImport);
  server.on("/api/reboot", HTTP_POST, [] {
    if (!auth()) return;
    ok("재부팅");
    delay(500);
    ESP.restart();
  });
  server.on("/api/factory", HTTP_POST, [] {
    if (!auth()) return;
    Store::factoryReset();
    ok("초기화 — 재부팅");
    delay(500);
    ESP.restart();
  });
  server.on("/api/upd/check", HTTP_POST, [] {
    if (!auth()) return;
    Net::requestUpdCheck();
    ok("업데이트 확인 중");
  });
  server.on("/api/upd/install", HTTP_POST, [] {
    if (!auth()) return;
    if (Ota::info().st != Ota::St::Available) return fail("설치할 새 버전이 없습니다. 먼저 확인하세요");
    Log::printf("업데이트 설치 요청 (웹)");
    Net::requestUpdInstall();
    ok("다운로드 시작 — 1분 정도 걸립니다");
  });
  server.on("/update", HTTP_POST, hUpdateDone, hUpdateUpload);
  server.onNotFound([] { server.send(404, "text/plain", "not found"); });

  server.begin();
}

void loop() {
  server.handleClient();
  uint32_t now = millis();
  if (now - rateLastMs >= 2000) {
    uint32_t cnt = Ble::stats().advTotal;
    rate = (cnt - rateLastCnt) * 1000 / (now - rateLastMs);
    rateLastCnt = cnt;
    rateLastMs = now;
  }
}

}  // namespace Web

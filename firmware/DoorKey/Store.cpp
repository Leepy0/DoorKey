#include "Store.h"
#include <Preferences.h>
#include "Irk.h"
#include "Log.h"

static void setName(char* dst, size_t n, const char* src) {
  strlcpy(dst, (src && *src) ? src : "기기", n);
  Log::utf8Fix(dst);
}

namespace Store {

static Preferences prefs;
static SemaphoreHandle_t mtx = nullptr;
static Params P;
static Device D[MAX_DEVICES];

struct Lock {
  Lock() { xSemaphoreTake(mtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mtx); }
};

// ---------------------------------------------------------------- 파라미터

void paramsToJson(JsonObject o) {
  o["autoEnabled"] = P.autoEnabled;
  o["arriveRssi"] = P.arriveRssi;
  o["confirmCount"] = P.confirmCount;
  o["exitRssi"] = P.exitRssi;
  o["exitWindowSec"] = P.exitWindowSec;
  o["absentSec"] = P.absentSec;
  o["minAwaySec"] = P.minAwaySec;
  o["requireNewAddr"] = P.requireNewAddr;
  o["cooldownSec"] = P.cooldownSec;
  o["activeFrom"] = P.activeFrom;
  o["activeTo"] = P.activeTo;
  o["keepWarm"] = P.keepWarm;
  o["notifyUnlock"] = P.notifyUnlock;
  o["presFallback"] = P.presFallback;
}

// 범위 검사 후 반영. 없는 키는 유지
template <typename T>
static bool takeInt(JsonObjectConst o, const char* key, T& dst, long lo, long hi, String& err) {
  if (!o[key].is<long>()) return true;
  long v = o[key].as<long>();
  if (v < lo || v > hi) {
    err = String(key) + " 범위 " + lo + "~" + hi;
    return false;
  }
  dst = (T)v;
  return true;
}

static void takeBool(JsonObjectConst o, const char* key, bool& dst) {
  if (o[key].is<bool>()) dst = o[key].as<bool>();
}

bool paramsFromJson(JsonObjectConst o, String& err) {
  Params n = P;
  takeBool(o, "autoEnabled", n.autoEnabled);
  takeBool(o, "requireNewAddr", n.requireNewAddr);
  takeBool(o, "keepWarm", n.keepWarm);
  takeBool(o, "notifyUnlock", n.notifyUnlock);
  takeBool(o, "presFallback", n.presFallback);
  if (!takeInt(o, "arriveRssi", n.arriveRssi, -100, -30, err)) return false;
  if (!takeInt(o, "confirmCount", n.confirmCount, 1, 5, err)) return false;
  if (!takeInt(o, "exitRssi", n.exitRssi, -100, -30, err)) return false;
  if (!takeInt(o, "exitWindowSec", n.exitWindowSec, 10, 300, err)) return false;
  if (!takeInt(o, "absentSec", n.absentSec, 20, 1800, err)) return false;
  if (!takeInt(o, "minAwaySec", n.minAwaySec, 0, 7200, err)) return false;
  if (!takeInt(o, "cooldownSec", n.cooldownSec, 5, 600, err)) return false;
  if (!takeInt(o, "activeFrom", n.activeFrom, 0, 23, err)) return false;
  if (!takeInt(o, "activeTo", n.activeTo, 1, 24, err)) return false;
  P = n;
  return true;
}

Params& params() { return P; }

void saveParams() {
  JsonDocument doc;
  paramsToJson(doc.to<JsonObject>());
  String s;
  serializeJson(doc, s);
  Lock l;
  prefs.putString("params", s);
}

static void loadParams() {
  String s = prefs.getString("params", "");
  if (s.isEmpty()) return;
  JsonDocument doc;
  if (deserializeJson(doc, s)) return;
  String err;
  paramsFromJson(doc.as<JsonObjectConst>(), err);
}

// ---------------------------------------------------------------- 기기

Device* devices() { return D; }

void saveDevices() {
  JsonDocument doc;
  JsonArray a = doc.to<JsonArray>();
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (!D[i].used) continue;
    char hex[33];
    Irk::toHex(D[i].irk, hex);
    JsonObject o = a.add<JsonObject>();
    o["slot"] = i;
    o["name"] = D[i].name;
    o["irk"] = hex;
    o["enabled"] = D[i].enabled;
    o["id"] = D[i].idAddr;
    o["pres"] = D[i].presId;
  }
  String s;
  serializeJson(doc, s);
  Lock l;
  prefs.putString("devices", s);
}

static void loadDevices() {
  String s = prefs.getString("devices", "");
  if (s.isEmpty()) return;
  JsonDocument doc;
  if (deserializeJson(doc, s)) return;
  for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
    int slot = o["slot"] | -1;
    if (slot < 0 || slot >= MAX_DEVICES) continue;
    uint8_t irk[16];
    if (!Irk::fromHex(o["irk"] | "", irk)) continue;
    Device& d = D[slot];
    d.used = true;
    d.enabled = o["enabled"] | true;
    setName(d.name, sizeof(d.name), o["name"] | "기기");
    strlcpy(d.idAddr, o["id"] | "", sizeof(d.idAddr));
    strlcpy(d.presId, o["pres"] | "", sizeof(d.presId));
    memcpy(d.irk, irk, 16);
  }
}

int addDevice(const char* name, const uint8_t irkBE[16], const char* idAddr) {
  int slot = -1;
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (D[i].used && memcmp(D[i].irk, irkBE, 16) == 0) {
      slot = i;  // 같은 폰 재등록 → 이름만 갱신
      break;
    }
  }
  if (slot < 0) {
    for (int i = 0; i < MAX_DEVICES; i++) {
      if (!D[i].used) {
        slot = i;
        break;
      }
    }
  }
  if (slot < 0) return -1;
  Device& d = D[slot];
  d.used = true;
  d.enabled = true;
  setName(d.name, sizeof(d.name), name);
  strlcpy(d.idAddr, idAddr ? idAddr : "", sizeof(d.idAddr));
  memcpy(d.irk, irkBE, 16);
  saveDevices();
  return slot;
}

bool updateDevice(int slot, const char* name, bool enabled) {
  if (slot < 0 || slot >= MAX_DEVICES || !D[slot].used) return false;
  if (name && *name) setName(D[slot].name, sizeof(D[slot].name), name);
  D[slot].enabled = enabled;
  saveDevices();
  return true;
}

bool setPresId(int slot, const char* id) {
  if (slot < 0 || slot >= MAX_DEVICES || !D[slot].used) return false;
  if (strcmp(D[slot].presId, id ? id : "") == 0) return false;
  strlcpy(D[slot].presId, id ? id : "", sizeof(D[slot].presId));
  saveDevices();
  return true;
}

bool deleteDevice(int slot) {
  if (slot < 0 || slot >= MAX_DEVICES || !D[slot].used) return false;
  D[slot] = Device();
  saveDevices();
  return true;
}

// ---------------------------------------------------------------- SmartThings

StConf getSt() {
  Lock l;
  StConf c;
  c.clientId = prefs.getString("st_cid", "");
  c.clientSecret = prefs.getString("st_csec", "");
  c.redirect = prefs.getString("st_redir", DEFAULT_ST_REDIRECT);
  c.deviceId = prefs.getString("st_dev", DEFAULT_ST_DEVICE_ID);
  c.component = prefs.getString("st_comp", DEFAULT_ST_COMPONENT);
  c.capability = prefs.getString("st_cap", DEFAULT_ST_CAPABILITY);
  c.command = prefs.getString("st_cmd", DEFAULT_ST_COMMAND);
  c.args = prefs.getString("st_args", DEFAULT_ST_ARGS);
  return c;
}

void setSt(const StConf& c) {
  Lock l;
  prefs.putString("st_cid", c.clientId);
  prefs.putString("st_csec", c.clientSecret);
  prefs.putString("st_redir", c.redirect);
  prefs.putString("st_dev", c.deviceId);
  prefs.putString("st_comp", c.component);
  prefs.putString("st_cap", c.capability);
  prefs.putString("st_cmd", c.command);
  prefs.putString("st_args", c.args);
}

StTokens getTokens() {
  Lock l;
  StTokens t;
  t.access = prefs.getString("st_at", "");
  t.refresh = prefs.getString("st_rt", "");
  t.expiresAt = prefs.getUInt("st_exp", 0);
  return t;
}

void setTokens(const StTokens& t) {
  Lock l;
  // 리프레시 토큰은 갱신 때마다 바뀌므로 받은 즉시 저장해야 한다
  prefs.putString("st_rt", t.refresh);
  prefs.putString("st_at", t.access);
  prefs.putUInt("st_exp", t.expiresAt);
}

void clearTokens() {
  Lock l;
  prefs.remove("st_rt");
  prefs.remove("st_at");
  prefs.remove("st_exp");
}

// ---------------------------------------------------------------- 기타

String wifiSsid() {
  Lock l;
  return prefs.getString("wifi_ssid", DEFAULT_WIFI_SSID);
}
String wifiPass() {
  Lock l;
  return prefs.getString("wifi_pass", DEFAULT_WIFI_PASS);
}
void setWifi(const String& ssid, const String& pass) {
  Lock l;
  prefs.putString("wifi_ssid", ssid);
  prefs.putString("wifi_pass", pass);
}

String adminPass() {
  Lock l;
  return prefs.getString("adm_pass", DEFAULT_ADMIN_PASS);
}
void setAdminPass(const String& p) {
  Lock l;
  prefs.putString("adm_pass", p);
}

String ntfyUrl() {
  Lock l;
  return prefs.getString("ntfy", "");
}
void setNtfyUrl(const String& u) {
  Lock l;
  prefs.putString("ntfy", u);
}

String hbUrl() {
  Lock l;
  return prefs.getString("hb", "");
}
void setHbUrl(const String& u) {
  Lock l;
  prefs.putString("hb", u);
}

void exportJson(JsonDocument& doc) {
  JsonObject root = doc.to<JsonObject>();
  root["version"] = 1;
  paramsToJson(root["params"].to<JsonObject>());
  JsonArray a = root["devices"].to<JsonArray>();
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (!D[i].used) continue;
    char hex[33];
    Irk::toHex(D[i].irk, hex);
    JsonObject o = a.add<JsonObject>();
    o["name"] = D[i].name;
    o["irk"] = hex;
    o["enabled"] = D[i].enabled;
    o["id"] = D[i].idAddr;
    o["pres"] = D[i].presId;
  }
  StConf c = getSt();
  JsonObject st = root["st"].to<JsonObject>();
  st["clientId"] = c.clientId;
  st["clientSecret"] = c.clientSecret;
  st["redirect"] = c.redirect;
  st["deviceId"] = c.deviceId;
  st["component"] = c.component;
  st["capability"] = c.capability;
  st["command"] = c.command;
  st["args"] = c.args;
  root["ntfy"] = ntfyUrl();
  root["hb"] = hbUrl();
}

bool importJson(JsonDocument& doc, String& err) {
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (!root["params"].is<JsonObjectConst>() || !root["devices"].is<JsonArrayConst>()) {
    err = "형식이 맞지 않습니다";
    return false;
  }
  Params backup = P;
  if (!paramsFromJson(root["params"].as<JsonObjectConst>(), err)) {
    P = backup;
    return false;
  }
  // 기기는 전부 교체
  Device nd[MAX_DEVICES];
  int n = 0;
  for (JsonObjectConst o : root["devices"].as<JsonArrayConst>()) {
    if (n >= MAX_DEVICES) break;
    uint8_t irk[16];
    if (!Irk::fromHex(o["irk"] | "", irk)) {
      P = backup;
      err = "IRK 형식 오류";
      return false;
    }
    nd[n].used = true;
    nd[n].enabled = o["enabled"] | true;
    setName(nd[n].name, sizeof(nd[n].name), o["name"] | "기기");
    strlcpy(nd[n].idAddr, o["id"] | "", sizeof(nd[n].idAddr));
    strlcpy(nd[n].presId, o["pres"] | "", sizeof(nd[n].presId));
    memcpy(nd[n].irk, irk, 16);
    n++;
  }
  memcpy(D, nd, sizeof(D));
  saveParams();
  saveDevices();

  if (root["st"].is<JsonObjectConst>()) {
    JsonObjectConst st = root["st"];
    StConf c = getSt();
    c.clientId = st["clientId"] | c.clientId;
    c.clientSecret = st["clientSecret"] | c.clientSecret;
    c.redirect = st["redirect"] | c.redirect;
    c.deviceId = st["deviceId"] | c.deviceId;
    c.component = st["component"] | c.component;
    c.capability = st["capability"] | c.capability;
    c.command = st["command"] | c.command;
    c.args = st["args"] | c.args;
    setSt(c);
  }
  if (root["ntfy"].is<const char*>()) setNtfyUrl(root["ntfy"].as<const char*>());
  if (root["hb"].is<const char*>()) setHbUrl(root["hb"].as<const char*>());
  return true;
}

void factoryReset() {
  Lock l;
  prefs.clear();
}

void begin() {
  mtx = xSemaphoreCreateMutex();
  prefs.begin("doorkey", false);
  loadParams();
  loadDevices();
}

}  // namespace Store

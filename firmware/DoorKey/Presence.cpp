#include "Presence.h"
#include <time.h>
#include "Irk.h"
#include "Led.h"
#include "Log.h"
#include "Net.h"
#include "Store.h"

namespace Presence {

static Rt R[MAX_DEVICES];
static uint32_t lastUnlockReqMs = 0;
static uint32_t presNextMs[MAX_DEVICES];  // 다음 SmartThings 위치 조회 시각 (millis)

static const char* devName(int slot) { return Store::devices()[slot].name; }
static bool hasPres(int slot) { return Store::devices()[slot].presId[0] != 0; }

const char* stName(St s) {
  switch (s) {
    case St::Home: return "home";
    case St::Unseen: return "unseen";
    case St::Away: return "away";
    default: return "unknown";
  }
}

const char* chkName(Chk c) {
  switch (c) {
    case Chk::BleOnly: return "ble";
    case Chk::Waiting: return "waiting";
    case Chk::Partial: return "partial";
    case Chk::Stale: return "stale";
    default: return "";
  }
}

static void setSt(int slot, St s, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
static void setSt(int slot, St s, const char* fmt, ...) {
  Rt& r = R[slot];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(r.lastEvent, sizeof(r.lastEvent), fmt, ap);
  va_end(ap);
  Log::utf8Fix(r.lastEvent);
  if (s == St::Home && r.st != St::Home) r.maxGapMs = 0;
  if (s == St::Unseen && r.st != St::Unseen) {
    r.staleNotified = false;
    Net::presNewEpisode(slot);    // 이전 외출의 '외출' 횟수를 이어 세지 않도록
    presNextMs[slot] = millis();  // 미감지가 되면 바로 위치 조회
  }
  r.st = s;
  r.stSinceMs = millis();
  Log::printf("[%s] %s", devName(slot), r.lastEvent);
}

// ---------------------------------------------------------------- 초 단위 RSSI 버퍼

static void secAdvance(Rt& r, uint32_t s) {
  if (r.secHead == 0) {
    memset(r.sec, 0x81, sizeof(r.sec));  // -127
    r.secHead = s;
    return;
  }
  if (s <= r.secHead) return;
  uint32_t n = s - r.secHead;
  if (n > 300) n = 300;
  for (uint32_t k = 1; k <= n; k++) r.sec[(r.secHead + k) % 300] = -127;
  r.secHead = s;
}

static bool secValid(const Rt& r, uint32_t s) {
  return r.secHead && s <= r.secHead && s + 300 > r.secHead;
}

static int8_t peak(const Rt& r, uint32_t endSec, uint16_t window) {
  int8_t best = -127;
  uint32_t from = endSec + 1 > window ? endSec + 1 - window : 0;
  for (uint32_t s = from; s <= endSec; s++) {
    if (secValid(r, s) && r.sec[s % 300] > best) best = r.sec[s % 300];
  }
  return best;
}

void spark(int slot, int8_t out[60]) {
  const Rt& r = R[slot];
  uint32_t nowSec = millis() / 1000;
  for (int i = 0; i < 60; i++) {
    uint32_t s = nowSec - 59 + i;
    out[i] = secValid(r, s) ? r.sec[s % 300] : -127;
  }
}

// ---------------------------------------------------------------- 주소 이력

static bool inRecent(const Rt& r, const uint8_t a[6]) {
  for (int i = 0; i < r.recentN; i++)
    if (memcmp(r.recent[i], a, 6) == 0) return true;
  return false;
}

static void rememberAddr(Rt& r, const uint8_t a[6]) {
  if (inRecent(r, a)) return;
  memcpy(r.recent[r.recentIdx], a, 6);
  r.recentIdx = (r.recentIdx + 1) % 16;
  if (r.recentN < 16) r.recentN++;
}

// ---------------------------------------------------------------- SmartThings 폰 위치

// SmartThings '외출'이 이번 외출(BLE 마지막 감지 이후)에 해당하는가
static bool awayMatches(const Rt& r, const Net::Pres& p, uint32_t now) {
  if (!p.awaySeenMs || (int32_t)(p.awaySeenMs - r.awaySinceMs) < 0) return false;
  // 귀가 후 SmartThings 반영이 늦어 남아 있던 예전 '외출'은 제외
  if (r.awayExact && p.awaySince) {
    time_t t = time(nullptr);
    if (t > 1700000000) {
      uint32_t awayStart = (uint32_t)t - (now - r.awaySinceMs) / 1000;
      if (p.awaySince + PRES_EARLY_SEC < awayStart) return false;
    }
  }
  return true;
}

// 미감지가 된 뒤(또는 마지막 조회 성공 뒤) PRES_STALE_SEC 넘게 조회 성공이 없는가
static bool presStale(const Rt& r, const Net::Pres& p, uint32_t now) {
  uint32_t ref = r.stSinceMs;
  if (p.okMs && (int32_t)(p.okMs - ref) > 0) ref = p.okMs;
  return now - ref > PRES_STALE_SEC * 1000UL;
}

Chk check(int slot) {
  if (slot < 0 || slot >= MAX_DEVICES) return Chk::None;
  const Rt& r = R[slot];
  if (r.st != St::Unseen) return Chk::None;
  if (!hasPres(slot)) return Chk::BleOnly;
  Net::Pres p = Net::pres(slot);
  uint32_t now = millis();
  if (presStale(r, p, now)) return Chk::Stale;
  if (p.val == 0 && awayMatches(r, p, now)) return Chk::Partial;
  return Chk::Waiting;
}

void presPollNow() {
  for (int i = 0; i < MAX_DEVICES; i++) presNextMs[i] = millis();
}

// 미감지 중 30초, 그 외 5분마다 조회 (조회는 net 태스크가 한다)
static void presPoll(int slot, const Rt& r, uint32_t now) {
  const Device& dv = Store::devices()[slot];
  if (!dv.presId[0] || (int32_t)(now - presNextMs[slot]) < 0) return;
  if (Net::requestPresence(slot, dv.name, dv.presId))
    presNextMs[slot] = now + (r.st == St::Unseen ? PRES_POLL_FAST_SEC : PRES_POLL_SLOW_SEC) * 1000UL;
}

// ---------------------------------------------------------------- 판정

static bool inActiveHours(const Params& P) {
  if (P.activeFrom == 0 && P.activeTo == 24) return true;
  time_t t = time(nullptr);
  if (t < 1700000000) return true;  // 시간 미동기 → 허용
  struct tm tmv;
  localtime_r(&t, &tmv);
  int h = tmv.tm_hour;
  if (P.activeFrom < P.activeTo) return h >= P.activeFrom && h < P.activeTo;
  return h >= P.activeFrom || h < P.activeTo;  // 자정을 넘기는 구간
}

static uint32_t awaySecAt(const Rt& r, uint32_t ms) {
  // 큐에 먼저 들어온 감지가 수동 전환보다 늦게 처리될 수 있으므로 음수는 0으로
  int32_t d = (int32_t)(ms - r.awaySinceMs);
  return d > 0 ? (uint32_t)d / 1000 : 0;
}

static void handleArrival(int slot, Rt& r, const Ble::Det& d, const Params& P) {
  if (d.rssi < P.arriveRssi) {
    r.streak = 0;
    return;
  }
  if (r.streak == 0 || d.ms - r.streakFirstMs > 5000) {
    r.streak = 0;
    r.streakFirstMs = d.ms;
  }
  if (++r.streak < P.confirmCount) return;
  r.streak = 0;

  uint32_t awaySec = awaySecAt(r, d.ms);
  if (awaySec < P.minAwaySec) {
    setSt(slot, St::Home, "귀가 — 외출 %lu분으로 짧아 열지 않음", (unsigned long)(awaySec / 60));
  } else if (P.requireNewAddr && inRecent(r, d.addr)) {
    setSt(slot, St::Home, "귀가 — 외출 전 BLE 주소가 다시 나타나 열지 않음 (재생 방지)");
  } else if (!P.autoEnabled) {
    setSt(slot, St::Home, "귀가 — 자동 열기가 꺼져 있어 열지 않음");
  } else if (!inActiveHours(P)) {
    setSt(slot, St::Home, "귀가 — 허용 시간대가 아니라 열지 않음");
  } else if (lastUnlockReqMs && d.ms - lastUnlockReqMs < P.cooldownSec * 1000UL) {
    setSt(slot, St::Home, "귀가 — 방금 열려서 다시 열지 않음");
  } else {
    lastUnlockReqMs = d.ms;
    Net::requestUnlock(devName(slot), d.ms, false);
    Led::set(0, 60, 0, 3000);
    setSt(slot, St::Home, "귀가 %d dBm, 외출 %lu분 → 문 열기", d.rssi, (unsigned long)(awaySec / 60));
  }
}

// 미감지 상태에서 다시 감지됨: 문은 열지 않는다. 외출 확인이 안 돼 못 연 것일 수 있으면 알려 준다
static void handleReseen(int slot, Rt& r, const Ble::Det& d, const Params& P) {
  uint32_t awaySec = awaySecAt(r, d.ms);
  bool couldBeReturn = hasPres(slot) && awaySec >= P.minAwaySec && d.rssi >= P.arriveRssi && P.autoEnabled;
  if (!couldBeReturn) {
    setSt(slot, St::Home, "다시 감지 (%d dBm, %lu분 만에)", d.rssi, (unsigned long)(awaySec / 60));
    return;
  }
  Chk c = check(slot);
  const char* why = c == Chk::Stale ? "SmartThings 위치를 조회하지 못해" : "SmartThings 위치가 '집'이라";
  setSt(slot, St::Home, "다시 감지 (%d dBm, %lu분 만에) — %s 열지 않음", d.rssi, (unsigned long)(awaySec / 60), why);
  if (P.notifyUnlock) {  // 기본은 끔 — 로그에만 남는다
    char m[200];
    snprintf(m, sizeof(m), "%s 다시 감지 — %s 문을 열지 않았습니다. 외출이었다면 SmartThings 앱으로 여세요.", devName(slot), why);
    Net::notify(m);
  }
}

void onDet(const Ble::Det& d) {
  if (d.slot >= MAX_DEVICES || !Store::devices()[d.slot].used) return;
  int slot = d.slot;
  Rt& r = R[slot];
  const Params& P = Store::params();

  if (r.lastSeenMs) {
    uint32_t gap = d.ms - r.lastSeenMs;
    if (gap < 60000) r.itvEma = r.itvEma == 0 ? gap : r.itvEma * 0.9f + gap * 0.1f;
    if (r.st == St::Home && gap > r.maxGapMs) r.maxGapMs = gap;
  }
  r.lastRssi = d.rssi;
  r.ema = r.ema <= -126 ? d.rssi : r.ema * 0.7f + d.rssi * 0.3f;
  r.advCount++;
  uint32_t s = d.ms / 1000;
  secAdvance(r, s);
  if (d.rssi > r.sec[s % 300]) r.sec[s % 300] = d.rssi;
  memcpy(r.lastAddr, d.addr, 6);

  switch (r.st) {
    case St::Unknown: setSt(slot, St::Home, "감지됨 (%d dBm)", d.rssi); break;
    case St::Unseen: handleReseen(slot, r, d, P); break;
    case St::Away: handleArrival(slot, r, d, P); break;
    case St::Home: break;
  }
  r.lastSeenMs = d.ms;
  // 재실 중 본 주소만 기록 (외출 중 약하게 잡힌 새 주소는 기록하지 않음)
  if (r.st == St::Home) rememberAddr(r, d.addr);
}

// 미감지 → 외출 확인
static void confirmAway(int slot, Rt& r, const Params& P, uint32_t now) {
  if (!hasPres(slot)) {
    // 위치 기기 없음: 현관으로 나가는 강한 신호가 있었을 때만 외출
    if (r.awayExact && r.lastExitPeak >= P.exitRssi)
      setSt(slot, St::Away, "외출 — 사라지기 직전 %d dBm (현관 통과로 판단)", r.lastExitPeak);
    return;
  }
  Net::Pres p = Net::pres(slot);
  if (p.val == 0 && p.awayRun >= PRES_CONFIRM_N && awayMatches(r, p, now)) {
    String at = p.awaySince ? Log::stamp(p.awaySince, 0) : String("시각 모름");
    setSt(slot, St::Away, "외출 — SmartThings 위치 '외출' 확인 (%s부터)", at.c_str());
    return;
  }
  if (!presStale(r, p, now)) return;
  if (P.presFallback) {
    if (r.awayExact && r.lastExitPeak >= P.exitRssi)
      setSt(slot, St::Away, "외출 — SmartThings 조회 안 됨, 사라지기 직전 %d dBm으로 판단", r.lastExitPeak);
    return;
  }
  if (!r.staleNotified) {
    r.staleNotified = true;
    Log::printf("[%s] SmartThings 위치를 조회하지 못해 외출로 바꾸지 않음 (귀가해도 문이 열리지 않음)", devName(slot));
    char m[200];
    snprintf(m, sizeof(m), "%s 외출 확인 불가 — SmartThings 위치를 조회하지 못해 귀가해도 문이 열리지 않습니다. 웹 UI에서 SmartThings 상태를 확인하세요.", devName(slot));
    Net::notify(m);
  }
}

void tick() {
  static uint32_t lastTick = 0;
  uint32_t now = millis();
  if (now - lastTick < 250) return;
  lastTick = now;

  const Params& P = Store::params();
  bool anyAway = false;
  Device* D = Store::devices();

  for (int slot = 0; slot < MAX_DEVICES; slot++) {
    if (!D[slot].used || !D[slot].enabled) continue;
    Rt& r = R[slot];

    if (r.st == St::Home && now - r.lastSeenMs > P.absentSec * 1000UL) {
      r.lastExitPeak = peak(r, r.lastSeenMs / 1000, P.exitWindowSec);
      r.awaySinceMs = r.lastSeenMs;
      r.awayExact = true;
      setSt(slot, St::Unseen, "미감지 %lu초 — 사라지기 직전 최대 %d dBm", (unsigned long)P.absentSec, r.lastExitPeak);
    } else if (r.st == St::Unknown && r.lastSeenMs == 0) {
      // 부팅(또는 등록·재활성화) 뒤 한 번도 안 보이면 외출 중일 가능성이 높다
      uint32_t wait = max<uint32_t>(P.minAwaySec, P.absentSec) * 1000UL;
      if (now - r.stSinceMs > wait) {
        r.awaySinceMs = r.stSinceMs;
        r.awayExact = false;
        r.lastExitPeak = -127;
        if (hasPres(slot)) setSt(slot, St::Unseen, "시작 후 계속 미감지 — SmartThings 위치로 외출 확인");
        else setSt(slot, St::Away, "시작 후 계속 미감지 → 외출로 간주");
      }
    }
    if (r.st == St::Unseen) confirmAway(slot, r, P, now);
    if (r.st == St::Away) anyAway = true;
    presPoll(slot, r, now);
  }
  Net::setAnyAway(anyAway);
}

void resetSlot(int slot) {
  if (slot < 0 || slot >= MAX_DEVICES) return;
  R[slot] = Rt();
  R[slot].stSinceMs = millis();
  presNextMs[slot] = 0;
  Net::presReset(slot);
}

bool forceState(int slot, bool away) {
  if (slot < 0 || slot >= MAX_DEVICES || !Store::devices()[slot].used) return false;
  if (away) {
    R[slot].awaySinceMs = millis();
    R[slot].awayExact = false;
    setSt(slot, St::Away, "수동으로 외출 설정 (귀가하면 문 열기)");
  } else {
    setSt(slot, St::Home, "수동으로 재실 설정");
  }
  return true;
}

const Rt& rt(int slot) { return R[slot]; }

void begin() {
  for (int i = 0; i < MAX_DEVICES; i++) resetSlot(i);
}

}  // namespace Presence

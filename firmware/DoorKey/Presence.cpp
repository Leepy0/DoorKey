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

static const char* devName(int slot) { return Store::devices()[slot].name; }

const char* stName(St s) {
  switch (s) {
    case St::Home: return "home";
    case St::Lost: return "lost";
    case St::Away: return "away";
    default: return "unknown";
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

  // 큐에 먼저 들어온 감지가 수동 전환보다 늦게 처리될 수 있으므로 음수는 0으로
  int32_t awayMs = (int32_t)(d.ms - r.awaySinceMs);
  uint32_t awaySec = awayMs > 0 ? (uint32_t)awayMs / 1000 : 0;
  if (awaySec < P.minAwaySec) {
    setSt(slot, St::Home, "짧은 외출(%lu초) — 자동 개방 안 함", (unsigned long)awaySec);
  } else if (P.requireNewAddr && inRecent(r, d.addr)) {
    setSt(slot, St::Home, "외출 전 BLE 주소 재등장 — 재생 방지로 개방 안 함");
  } else if (!P.autoEnabled) {
    setSt(slot, St::Home, "귀가 (자동 개방 꺼짐)");
  } else if (!inActiveHours(P)) {
    setSt(slot, St::Home, "귀가 (허용 시간대 아님)");
  } else if (lastUnlockReqMs && d.ms - lastUnlockReqMs < P.cooldownSec * 1000UL) {
    setSt(slot, St::Home, "귀가 (쿨다운 — 직전에 이미 열림)");
  } else {
    lastUnlockReqMs = d.ms;
    Net::requestUnlock(devName(slot), d.ms, false);
    Led::set(0, 60, 0, 3000);
    setSt(slot, St::Home, "귀가 %d dBm, 외출 %lu분 → 문 열기", d.rssi, (unsigned long)(awaySec / 60));
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
    case St::Lost: setSt(slot, St::Home, "재감지 (%d dBm)", d.rssi); break;
    case St::Away: handleArrival(slot, r, d, P); break;
    case St::Home: break;
  }
  r.lastSeenMs = d.ms;
  // 집에 있는 동안 본 주소만 기록 (외출 중 약하게 잡힌 새 주소는 기록하지 않음)
  if (r.st == St::Home) rememberAddr(r, d.addr);
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
      int8_t pk = peak(r, r.lastSeenMs / 1000, P.exitWindowSec);
      r.lastExitPeak = pk;
      if (pk >= P.exitRssi) {
        r.awaySinceMs = r.lastSeenMs;
        setSt(slot, St::Away, "외출 판정 (직전 최대 %d dBm)", pk);
      } else {
        setSt(slot, St::Lost, "신호 끊김, 직전 최대 %d < %d dBm → 집 안으로 판단", pk, P.exitRssi);
      }
    } else if (r.st == St::Unknown && r.lastSeenMs == 0) {
      // 부팅(또는 등록·재활성화) 뒤 한 번도 안 보이면 외출 중으로 본다
      uint32_t wait = max<uint32_t>(P.minAwaySec, P.absentSec) * 1000UL;
      if (now - r.stSinceMs > wait) {
        r.awaySinceMs = r.stSinceMs;
        setSt(slot, St::Away, "시작 후 계속 미감지 → 외출로 간주");
      }
    }
    if (r.st == St::Away) anyAway = true;
  }
  Net::setAnyAway(anyAway);
}

void resetSlot(int slot) {
  if (slot < 0 || slot >= MAX_DEVICES) return;
  R[slot] = Rt();
  R[slot].stSinceMs = millis();
}

bool forceState(int slot, bool away) {
  if (slot < 0 || slot >= MAX_DEVICES || !Store::devices()[slot].used) return false;
  if (away) {
    R[slot].awaySinceMs = millis();
    setSt(slot, St::Away, "수동: 외출로 설정");
  } else {
    setSt(slot, St::Home, "수동: 재실로 설정");
  }
  return true;
}

const Rt& rt(int slot) { return R[slot]; }

void begin() {
  for (int i = 0; i < MAX_DEVICES; i++) resetSlot(i);
}

}  // namespace Presence

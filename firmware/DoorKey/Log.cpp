#include "Log.h"
#include <time.h>
#include <stdarg.h>

namespace Log {

static const int CAP = 150;
static Entry* ring = nullptr;  // PSRAM에 할당
static int head = 0;    // 다음에 쓸 위치
static int count = 0;
static uint32_t nextId = 1;
static SemaphoreHandle_t mtx = nullptr;

void begin() {
  if (!mtx) mtx = xSemaphoreCreateMutex();
  if (!ring) {
    ring = (Entry*)heap_caps_calloc(CAP, sizeof(Entry), MALLOC_CAP_SPIRAM);
    if (!ring) ring = (Entry*)calloc(CAP, sizeof(Entry));
  }
}

void utf8Fix(char* s) {
  size_t n = strlen(s);
  size_t i = n;
  int cont = 0;
  while (i > 0 && ((uint8_t)s[i - 1] & 0xC0) == 0x80) {
    i--;
    cont++;
  }
  if (i == 0) {
    if (cont) s[0] = 0;
    return;
  }
  uint8_t lead = (uint8_t)s[i - 1];
  int need = (lead & 0x80) == 0 ? 0 : (lead & 0xE0) == 0xC0 ? 1 : (lead & 0xF0) == 0xE0 ? 2 : (lead & 0xF8) == 0xF0 ? 3 : 0;
  if (cont < need) s[i - 1] = 0;
}

static uint32_t nowEpoch() {
  time_t t = time(nullptr);
  return t > 1700000000 ? (uint32_t)t : 0;
}

String stamp(uint32_t epoch, uint32_t ms) {
  char buf[24];
  if (epoch) {
    time_t t = epoch;
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(buf, sizeof(buf), "%m-%d %H:%M:%S", &tmv);
  } else {
    snprintf(buf, sizeof(buf), "+%lu.%lus", (unsigned long)(ms / 1000), (unsigned long)((ms % 1000) / 100));
  }
  return String(buf);
}

void printf(const char* fmt, ...) {
  Entry e;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(e.msg, sizeof(e.msg), fmt, ap);
  va_end(ap);
  utf8Fix(e.msg);
  e.epoch = nowEpoch();
  e.ms = millis();

  if (!ring) {
    Serial.println(e.msg);
    return;
  }
  if (mtx) xSemaphoreTake(mtx, portMAX_DELAY);
  e.id = nextId++;
  ring[head] = e;
  head = (head + 1) % CAP;
  if (count < CAP) count++;
  if (mtx) xSemaphoreGive(mtx);

  Serial.printf("[%s] %s\n", stamp(e.epoch, e.ms).c_str(), e.msg);
}

int copySince(uint32_t since, Entry* out, int max) {
  int n = 0;
  if (!ring) return 0;
  if (mtx) xSemaphoreTake(mtx, portMAX_DELAY);
  // 오래된 것부터 순회
  int start = (head - count + CAP) % CAP;
  for (int i = 0; i < count && n < max; i++) {
    const Entry& e = ring[(start + i) % CAP];
    if (e.id > since) out[n++] = e;
  }
  if (mtx) xSemaphoreGive(mtx);
  return n;
}

uint32_t lastId() {
  return nextId - 1;
}

}  // namespace Log

#pragma once
#include <Arduino.h>

// 여러 태스크에서 호출 가능한 링버퍼 로그 (웹 UI 표시 + 시리얼 출력)
namespace Log {

struct Entry {
  uint32_t id;
  uint32_t epoch;  // 0이면 시간 미동기
  uint32_t ms;
  char msg[160];
};

void begin();
void printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// since 보다 큰 id의 로그를 최대 max개 복사. 반환: 복사 개수
int copySince(uint32_t since, Entry* out, int max);
uint32_t lastId();

// 버퍼 끝에서 잘린 UTF-8 멀티바이트 문자를 제거
void utf8Fix(char* s);

// 사람이 읽는 시각 문자열 (MM-DD HH:MM:SS 또는 +123.4s)
String stamp(uint32_t epoch, uint32_t ms);

}  // namespace Log

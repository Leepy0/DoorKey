#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

// 판정 파라미터 (웹 UI > 설정)
struct Params {
  bool autoEnabled = true;       // 자동 개방 전체 스위치
  int8_t arriveRssi = -85;       // 귀가 판정 최소 RSSI
  uint8_t confirmCount = 1;      // 귀가 판정에 필요한 감지 횟수 (5초 이내)
  int8_t exitRssi = -80;         // 외출 판정: 사라지기 직전 최대 RSSI가 이 값 이상이어야 "현관으로 나감"
  uint16_t exitWindowSec = 180;  // 위 최대 RSSI를 보는 구간 (마지막 감지 이전 N초, 최대 300)
  uint16_t absentSec = 90;       // 이 시간 동안 안 보이면 부재
  uint16_t minAwaySec = 300;     // 최소 외출 시간 (짧은 외출은 자동 개방 안 함)
  bool requireNewAddr = true;    // 재생 공격 방지: 귀가 시 외출 전과 다른 BLE 주소 요구
  uint16_t cooldownSec = 30;     // 문 열기 요청 간 최소 간격
  uint8_t activeFrom = 0;        // 자동 개방 허용 시간대 (시, 0~23)
  uint8_t activeTo = 24;         // (시, 1~24). 0~24 이면 항상
  bool keepWarm = true;          // 외출 중 SmartThings TLS 연결 미리 유지
  bool notifyUnlock = true;      // 문 열 때 ntfy 알림
};

struct Device {
  bool used = false;
  bool enabled = true;
  char name[40] = {0};  // UTF-8, 한글 약 12자
  uint8_t irk[16] = {0};  // 빅엔디언
  char idAddr[18] = {0};  // 페어링 때 받은 폰의 신원 주소 (수동 추가면 빈 값)
  char presId[40] = {0};  // SmartThings 폰 위치(재실) 기기 ID. 비우면 위치 확인 안 함
};

struct StConf {
  String clientId, clientSecret, redirect;
  String deviceId, component, capability, command, args;
};

struct StTokens {
  String access, refresh;
  uint32_t expiresAt = 0;  // epoch. 0 = 모름
};

namespace Store {

void begin();

// ---- 파라미터 / 기기 (loop 태스크 전용) ----
Params& params();
void saveParams();
void paramsToJson(JsonObject o);
bool paramsFromJson(JsonObjectConst o, String& err);

Device* devices();  // MAX_DEVICES 크기 배열
int addDevice(const char* name, const uint8_t irkBE[16], const char* idAddr = "");  // 반환: 슬롯, 실패 -1. 같은 IRK면 갱신
bool updateDevice(int slot, const char* name, bool enabled);
bool setPresId(int slot, const char* id);  // 반환: 바뀌었으면 true
bool deleteDevice(int slot);
void saveDevices();

// ---- 여러 태스크에서 접근 (내부 잠금) ----
StConf getSt();
void setSt(const StConf& c);
StTokens getTokens();
void setTokens(const StTokens& t);
void clearTokens();

String wifiSsid();
String wifiPass();
void setWifi(const String& ssid, const String& pass);

String adminPass();
void setAdminPass(const String& p);

String ntfyUrl();
void setNtfyUrl(const String& u);

// 백업/복원 (토큰 제외)
void exportJson(JsonDocument& doc);
bool importJson(JsonDocument& doc, String& err);

void factoryReset();

}  // namespace Store

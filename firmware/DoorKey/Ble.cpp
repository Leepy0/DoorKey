#include "Ble.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "config.h"
#include "Irk.h"
#include "Log.h"
#include "Presence.h"
#include "Store.h"
#include <atomic>

namespace Ble {

// ---------------------------------------------------------------- IRK 테이블 / 캐시

struct IrkEnt {
  uint8_t slot;
  uint8_t irk[16];
};
static IrkEnt irkTab[MAX_DEVICES];
static int irkN = 0;
static std::atomic<uint32_t> irkGen{1};
static SemaphoreHandle_t irkMtx = nullptr;

// 주소 → 해석 결과 캐시 (BLE 호스트 태스크 전용)
struct CacheEnt {
  uint8_t addr[6];
  int8_t slot;
  uint32_t gen;
};
static const int CACHE_N = 128;
static CacheEnt cache[CACHE_N];
static int cacheNext = 0;

static QueueHandle_t detQ = nullptr;

static std::atomic<uint32_t> stAdv{0}, stRpa{0}, stHit{0}, stDrop{0};
static std::atomic<uint32_t> appMs{0};
static std::atomic<int8_t> appRssi{0}, appSlot{-1};
static NimBLEUUID appUuid(APP_BEACON_UUID);

void rebuildIrkTable() {
  xSemaphoreTake(irkMtx, portMAX_DELAY);
  irkN = 0;
  Device* d = Store::devices();
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (!d[i].used || !d[i].enabled) continue;
    irkTab[irkN].slot = i;
    memcpy(irkTab[irkN].irk, d[i].irk, 16);
    irkN++;
  }
  irkGen++;  // 캐시 무효화
  xSemaphoreGive(irkMtx);
}

static int lookup(const uint8_t* a) {
  uint32_t gen = irkGen;
  for (int i = 0; i < CACHE_N; i++) {
    if (cache[i].gen == gen && memcmp(cache[i].addr, a, 6) == 0) {
      stHit++;
      return cache[i].slot;
    }
  }
  int slot = -1;
  if (xSemaphoreTake(irkMtx, pdMS_TO_TICKS(5)) != pdTRUE) return -1;
  for (int j = 0; j < irkN; j++) {
    if (Irk::resolve(a, irkTab[j].irk)) {
      slot = irkTab[j].slot;
      break;
    }
  }
  gen = irkGen;
  xSemaphoreGive(irkMtx);

  CacheEnt& c = cache[cacheNext];
  memcpy(c.addr, a, 6);
  c.slot = (int8_t)slot;
  c.gen = gen;
  cacheNext = (cacheNext + 1) % CACHE_N;
  return slot;
}

// ---------------------------------------------------------------- 스캔

class ScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    stAdv++;
    const NimBLEAddress& a = d->getAddress();
    const uint8_t* v = a.getVal();
    int slot = -1;
    if (a.getType() == BLE_ADDR_RANDOM && Irk::isRpa(v)) {
      stRpa++;
      slot = lookup(v);
    }
    // 갤럭시 앱 비컨 진단 (해석 성공 여부 확인용)
    if (d->isAdvertisingService(appUuid)) {
      appMs = millis();
      appRssi = d->getRSSI();
      appSlot = (int8_t)slot;
    }
    if (slot < 0) return;

    Det e;
    e.slot = (uint8_t)slot;
    e.rssi = d->getRSSI();
    memcpy(e.addr, v, 6);
    e.ms = millis();
    if (xQueueSend(detQ, &e, 0) != pdTRUE) stDrop++;
  }
};
static ScanCB scanCb;
static uint32_t lastScanStartMs = 0;

static void startScan() {
  NimBLEScan* s = NimBLEDevice::getScan();
  s->setScanCallbacks(&scanCb, true);  // 중복 광고도 모두 받는다
  s->setActiveScan(false);
  s->setInterval(BLE_SCAN_ITVL_MS);
  s->setWindow(BLE_SCAN_WIN_MS);
  s->setMaxResults(0);  // 결과 저장 안 함
  lastScanStartMs = millis();
  if (!s->start(0, false, true)) Log::printf("BLE 스캔 시작 실패");
}

bool popDet(Det& d) {
  return detQ && xQueueReceive(detQ, &d, 0) == pdTRUE;
}

Stats stats() {
  Stats s;
  s.advTotal = stAdv;
  s.rpaTotal = stRpa;
  s.cacheHit = stHit;
  s.queueDrop = stDrop;
  s.scanning = NimBLEDevice::getScan()->isScanning();
  s.appBeaconMs = appMs;
  s.appBeaconRssi = appRssi;
  s.appBeaconSlot = appSlot;
  return s;
}

// ---------------------------------------------------------------- 등록(페어링)

// 표준 키보드 리포트 맵. iOS/Android 블루투스 설정 목록에 뜨게 하려고 HID로 광고한다.
static const uint8_t reportMap[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
  0xC0
};

static NimBLEServer* server = nullptr;
static NimBLEHIDDevice* hid = nullptr;
static EnrollInfo info;

// 콜백(호스트 태스크) → loop 태스크 전달용
static portMUX_TYPE cbMux = portMUX_INITIALIZER_UNLOCKED;
static std::atomic<bool> cbConnected{false}, cbAuthDone{false}, cbAuthFail{false}, cbDisconnected{false};
static std::atomic<uint16_t> cbConnHandle{BLE_HS_CONN_HANDLE_NONE};
static ble_addr_t cbPeerId;
static uint32_t authMs = 0;
static uint32_t cleanupAt = 0;  // 0 이 아니면 이 시각에 정리

class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo& ci) override {
    cbConnHandle = ci.getConnHandle();
    cbConnected = true;
    NimBLEDevice::startSecurity(ci.getConnHandle());  // 폰에 페어링 요청
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    cbConnHandle = BLE_HS_CONN_HANDLE_NONE;
    cbDisconnected = true;
  }
  void onAuthenticationComplete(NimBLEConnInfo& ci) override {
    if (!ci.isEncrypted() || !ci.isBonded()) {
      cbAuthFail = true;
      return;
    }
    portENTER_CRITICAL(&cbMux);
    cbPeerId = *ci.getIdAddress().getBase();
    portEXIT_CRITICAL(&cbMux);
    cbAuthDone = true;
  }
};
static ServerCB serverCb;

static void setEnrollMsg(EnrollState st, const char* msg) {
  info.st = st;
  strlcpy(info.msg, msg, sizeof(info.msg));
  Log::utf8Fix(info.msg);
}

static void stopEnrollRadio() {
  NimBLEDevice::getAdvertising()->stop();
  if (cbConnHandle != BLE_HS_CONN_HANDLE_NONE) server->disconnect(cbConnHandle);
  cleanupAt = millis() + 1500;  // 연결 해제 후 본딩 삭제·스캔 재개
}

bool startEnroll(const char* name, String& err) {
  if (info.st == EnrollState::Waiting || info.st == EnrollState::Connected || cleanupAt) {
    err = "이미 등록 진행 중";
    return false;
  }
  int freeSlots = 0;
  for (int i = 0; i < MAX_DEVICES; i++)
    if (!Store::devices()[i].used) freeSlots++;
  if (!freeSlots) {
    err = "기기는 최대 " + String(MAX_DEVICES) + "대";
    return false;
  }

  NimBLEDevice::getScan()->stop();
  NimBLEDevice::deleteAllBonds();

  cbConnected = false;
  cbAuthDone = false;
  cbAuthFail = false;
  cbDisconnected = false;
  info = EnrollInfo();
  strlcpy(info.name, (name && *name) ? name : "기기", sizeof(info.name));
  Log::utf8Fix(info.name);
  info.startedMs = millis();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->reset();
  adv->setAppearance(0x03C1);  // HID 키보드
  adv->addServiceUUID(hid->getHidService()->getUUID());
  adv->setName(BLE_ENROLL_NAME);
  if (!adv->start()) {
    setEnrollMsg(EnrollState::Failed, "광고 시작 실패");
    startScan();
    return false;
  }
  setEnrollMsg(EnrollState::Waiting, "폰 블루투스 설정에서 '" BLE_ENROLL_NAME "' 을 눌러 페어링하세요");
  Log::printf("등록 모드 시작: %s", info.name);
  return true;
}

void cancelEnroll() {
  if (info.st == EnrollState::Waiting || info.st == EnrollState::Connected) {
    setEnrollMsg(EnrollState::Failed, "취소됨");
    stopEnrollRadio();
    Log::printf("등록 취소");
  }
}

EnrollInfo enrollInfo() { return info; }

static void enrollLoop() {
  uint32_t now = millis();

  if (cleanupAt && (int32_t)(now - cleanupAt) >= 0) {
    cleanupAt = 0;
    // ESP 쪽 본딩은 IRK만 뽑으면 필요 없다
    NimBLEDevice::deleteAllBonds();
    startScan();
    return;
  }

  if (info.st != EnrollState::Waiting && info.st != EnrollState::Connected) return;

  if (cbConnected && info.st == EnrollState::Waiting) {
    cbConnected = false;
    setEnrollMsg(EnrollState::Connected, "연결됨 — 페어링 진행 중 (폰에서 확인을 누르세요)");
  }
  if (cbDisconnected && info.st == EnrollState::Connected && !cbAuthDone) {
    cbDisconnected = false;
    setEnrollMsg(EnrollState::Waiting, "연결이 끊겼습니다. 다시 시도하세요");
    NimBLEDevice::getAdvertising()->start();
  }
  if (cbAuthFail) {
    cbAuthFail = false;
    setEnrollMsg(EnrollState::Failed, "페어링 실패 (암호화/본딩 안 됨). 폰에서 기기 삭제 후 다시 시도");
    Log::printf("등록 실패: 페어링 실패");
    stopEnrollRadio();
    return;
  }

  if (cbAuthDone) {
    if (!authMs) authMs = now;
    ble_addr_t peer;
    portENTER_CRITICAL(&cbMux);
    peer = cbPeerId;
    portEXIT_CRITICAL(&cbMux);

    ble_store_key_sec key = {};
    key.peer_addr = peer;
    ble_store_value_sec val = {};
    int rc = ble_store_read_peer_sec(&key, &val);
    if (rc == 0 && val.irk_present) {
      uint8_t irkBE[16];
      Irk::reverse16(val.irk, irkBE);  // SMP는 리틀엔디언으로 전달
      char idStr[18];
      Irk::addrToStr(peer.val, idStr);
      int slot = Store::addDevice(info.name, irkBE, idStr);
      cbAuthDone = false;
      authMs = 0;
      if (slot < 0) {
        setEnrollMsg(EnrollState::Failed, "저장 실패 (기기 수 초과)");
      } else {
        info.slot = slot;
        Presence::resetSlot(slot);  // 새로 시작 (등록 직후 곧바로 "귀가"로 오판하지 않도록)
        rebuildIrkTable();
        char hex[33];
        Irk::toHex(irkBE, hex);
        char m[160];
        snprintf(m, sizeof(m), "등록 완료 (기기 주소 %s). 폰 블루투스 목록에서 DoorKey를 삭제해도 됩니다", idStr);
        setEnrollMsg(EnrollState::Done, m);
        Log::printf("등록 완료: %s (슬롯 %d, 기기 주소 %s, IRK %.8s…)", info.name, slot, idStr, hex);
      }
      stopEnrollRadio();
      return;
    }
    if (now - authMs > 4000) {
      cbAuthDone = false;
      authMs = 0;
      setEnrollMsg(EnrollState::Failed, "IRK를 받지 못했습니다 (폰이 신원 키를 주지 않음)");
      Log::printf("등록 실패: IRK 없음 (rc=%d)", rc);
      stopEnrollRadio();
      return;
    }
  }

  if (now - info.startedMs > BLE_ENROLL_SEC * 1000UL) {
    setEnrollMsg(EnrollState::Failed, "시간 초과");
    Log::printf("등록 시간 초과");
    stopEnrollRadio();
  }
}

// ---------------------------------------------------------------- 공통

void begin() {
  irkMtx = xSemaphoreCreateMutex();
  detQ = xQueueCreate(64, sizeof(Det));
  memset(cache, 0, sizeof(cache));

  NimBLEDevice::init(BLE_ENROLL_NAME);
  NimBLEDevice::setSecurityAuth(true, false, true);  // 본딩, MITM 없음, SC
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  // 폰(개시자)에게 LTK + 신원키(IRK) 배포를 요청
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::deleteAllBonds();

  server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCb, false);
  server->advertiseOnDisconnect(false);

  hid = new NimBLEHIDDevice(server);
  hid->setManufacturer("DoorKey");
  hid->setPnp(0x02, 0xe502, 0xa111, 0x0210);
  hid->setHidInfo(0x00, 0x01);
  hid->setReportMap((uint8_t*)reportMap, sizeof(reportMap));
  hid->getInputReport(1);
  hid->setBatteryLevel(100);
  server->start();

  rebuildIrkTable();
  startScan();
  Log::printf("BLE 시작 (등록 기기 %d대)", irkN);
}

void loop() {
  enrollLoop();

  bool enrolling = info.st == EnrollState::Waiting || info.st == EnrollState::Connected || cleanupAt;
  if (!enrolling && !NimBLEDevice::getScan()->isScanning() && millis() - lastScanStartMs > 3000) {
    Log::printf("BLE 스캔이 멈춰 있어 재시작");
    startScan();
  }
}

}  // namespace Ble

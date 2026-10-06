#pragma once
#include <stdint.h>
#include <stddef.h>

// BLE Resolvable Private Address(RPA) 해석
//
// 바이트 순서 규칙
// - addr: NimBLE ble_addr_t.val 과 같은 리틀엔디언 (addr[5]가 최상위 바이트)
// - irk : 빅엔디언(사람이 읽는 16진수 표기 순서). SMP로 받은 값(LE)은 reverse16()으로 뒤집어 저장한다.
namespace Irk {

// RPA 여부: 최상위 2비트가 01
inline bool isRpa(const uint8_t addrLE[6]) { return (addrLE[5] & 0xC0) == 0x40; }

// ah(irk, prand) == hash 이면 true  (Core Spec Vol 3 Part H 2.2.2)
bool resolve(const uint8_t addrLE[6], const uint8_t irkBE[16]);

void reverse16(const uint8_t in[16], uint8_t out[16]);

// 32자 16진수 <-> 16바이트. 공백/콜론/대시는 무시
bool fromHex(const char* s, uint8_t out[16]);
void toHex(const uint8_t in[16], char out[33]);

// 주소를 "AA:BB:CC:DD:EE:FF" (표기 순서, 최상위 바이트 먼저)로
void addrToStr(const uint8_t addrLE[6], char out[18]);

}  // namespace Irk

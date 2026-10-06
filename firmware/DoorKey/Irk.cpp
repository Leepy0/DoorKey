#include "Irk.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#ifdef IRK_HOST_TEST
#include <openssl/aes.h>
static void aes128_ecb(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
  AES_KEY k;
  AES_set_encrypt_key(key, 128, &k);
  AES_encrypt(in, out, &k);
}
#else
#include "mbedtls/aes.h"
static void aes128_ecb(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, key, 128);
  mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, in, out);
  mbedtls_aes_free(&ctx);
}
#endif

namespace Irk {

bool resolve(const uint8_t addrLE[6], const uint8_t irkBE[16]) {
  // 평문 r' = 104비트 0 패딩 || prand(24비트), 빅엔디언 블록
  uint8_t pt[16] = {0};
  pt[13] = addrLE[5];
  pt[14] = addrLE[4];
  pt[15] = addrLE[3];

  uint8_t ct[16];
  aes128_ecb(irkBE, pt, ct);

  // 해시 = 결과의 하위 24비트 → 주소 하위 3바이트와 비교
  return ct[15] == addrLE[0] && ct[14] == addrLE[1] && ct[13] == addrLE[2];
}

void reverse16(const uint8_t in[16], uint8_t out[16]) {
  uint8_t tmp[16];
  for (int i = 0; i < 16; i++) tmp[i] = in[15 - i];
  memcpy(out, tmp, 16);
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  c = (char)tolower((unsigned char)c);
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool fromHex(const char* s, uint8_t out[16]) {
  int n = 0;
  int hi = -1;
  for (; *s; s++) {
    if (*s == ' ' || *s == ':' || *s == '-') continue;
    int v = hexVal(*s);
    if (v < 0) return false;
    if (hi < 0) {
      hi = v;
    } else {
      if (n >= 16) return false;
      out[n++] = (uint8_t)((hi << 4) | v);
      hi = -1;
    }
  }
  return n == 16 && hi < 0;
}

void toHex(const uint8_t in[16], char out[33]) {
  for (int i = 0; i < 16; i++) snprintf(out + i * 2, 3, "%02x", in[i]);
  out[32] = 0;
}

void addrToStr(const uint8_t a[6], char out[18]) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", a[5], a[4], a[3], a[2], a[1], a[0]);
}

}  // namespace Irk

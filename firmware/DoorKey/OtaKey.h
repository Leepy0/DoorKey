#pragma once
// 펌웨어 업데이트 서명 확인용 공개키 (ECDSA P-256).
// 짝이 되는 비밀키는 GitHub Actions secret OTA_SIGNING_KEY에만 있다.
// firmware/ota_pub.pem과 같은 내용이어야 한다.
static const char OTA_PUBKEY_PEM[] =
"-----BEGIN PUBLIC KEY-----\n"
"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEKiX5wB9ENgprGVkPkkIQCJN+ocVF\n"
"thqCIuI+Sf9mDKEHirM+yh5xlENGuLd5MwjF3FFq25ti4CHFmDclE3kdIA==\n"
"-----END PUBLIC KEY-----\n";

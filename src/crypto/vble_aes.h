/**
 * Minimal AES-128 CTR-mode implementation for VictronBLE.
 *
 * Trimmed (CTR only, AES-128 only) and symbol-prefixed adaptation of
 * kokke/tiny-AES-c (public domain / Unlicense), verified against the test
 * vectors in NIST SP 800-38A. Bundled so the library has no external crypto
 * dependency and builds identically on ESP32, nRF52 and any other target.
 *
 * Counter increment matches mbedTLS mbedtls_aes_crypt_ctr (increments the
 * 128-bit counter from the least-significant byte), so output is byte-identical
 * to the previous ESP32 mbedTLS-based decryption.
 */
#ifndef VBLE_AES_H_
#define VBLE_AES_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VBLE_AES_BLOCKLEN 16   // AES block length in bytes (128-bit)
#define VBLE_AES_KEYLEN   16   // AES-128 key length in bytes
#define VBLE_AES_KEYEXPSIZE 176

struct vble_aes_ctx {
    uint8_t RoundKey[VBLE_AES_KEYEXPSIZE];
    uint8_t Iv[VBLE_AES_BLOCKLEN];
};

// Initialise context with a 16-byte key and 16-byte IV (counter).
void vble_aes_init_ctx_iv(struct vble_aes_ctx* ctx,
                          const uint8_t* key, const uint8_t* iv);

// CTR-mode keystream XOR. Symmetric: same call encrypts and decrypts.
// Operates in place on `buf` for `length` bytes (length need not be a
// multiple of the block size).
void vble_aes_ctr_xcrypt(struct vble_aes_ctx* ctx, uint8_t* buf, size_t length);

#ifdef __cplusplus
}
#endif

#endif // VBLE_AES_H_

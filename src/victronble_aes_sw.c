/**
 * victronble — bundled software AES-128-CTR backend.
 *
 * Weak symbol: an alternative backend (PSA Crypto, mbedTLS, hardware) defines
 * victronble_aes_ctr_default strong and the linker drops this file's code —
 * and with it the bundled AES tables — from the final image.
 *
 * Copyright (c) 2025-2026 Scott Penrose
 * License: MIT
 */

#include "victronble.h"
#include "crypto/vble_aes.h"

#include <string.h>

#if defined(_MSC_VER)
#define VICTRONBLE_WEAK
#else
#define VICTRONBLE_WEAK __attribute__((weak))
#endif

VICTRONBLE_WEAK
int victronble_aes_ctr_default(const uint8_t key[16], const uint8_t iv[16],
                               const uint8_t *in, uint8_t *out,
                               size_t len, void *user)
{
    (void)user;

    struct vble_aes_ctx ctx;

    vble_aes_init_ctx_iv(&ctx, key, iv);
    if (out != in) {
        memcpy(out, in, len);
    }
    vble_aes_ctr_xcrypt(&ctx, out, len);
    return 0;
}

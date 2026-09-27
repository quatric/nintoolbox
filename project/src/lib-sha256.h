// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_SHA256_H
#define LIB_SHA256_H

#include "lib-nintendo.h"

#define SHA256_BLOCK_SIZE 64
#define SHA256_DIGEST_SIZE 32

typedef struct sha256_ctx_t
{
	u32 state[8];
	u64 count;
	u8 buffer[SHA256_BLOCK_SIZE];
} sha256_ctx_t;

void sha256_init (sha256_ctx_t *ctx);
void sha256_update (sha256_ctx_t *ctx, const void *data, size_t len);
void sha256_final (sha256_ctx_t *ctx, u8 digest[SHA256_DIGEST_SIZE]);
void sha256_calc (const void *data, size_t len, u8 digest[SHA256_DIGEST_SIZE]);

#endif // LIB_SHA256_H

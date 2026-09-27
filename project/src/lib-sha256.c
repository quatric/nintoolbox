// SPDX-License-Identifier: GPL-2.0+
#include "lib-sha256.h"
#include <string.h>

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SIGMA0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define SIGMA1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static const u32 K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void sha256_transform (sha256_ctx_t *ctx, const u8 data[64])
{
	u32 w[64];
	for (int i = 0; i < 16; i++)
		w[i] = ((u32)data[i * 4] << 24) | ((u32)data[i * 4 + 1] << 16) |
		       ((u32)data[i * 4 + 2] << 8) | (u32)data[i * 4 + 3];

	for (int i = 16; i < 64; i++)
		w[i] = SIG1(w[i - 2]) + w[i - 7] + SIG0(w[i - 15]) + w[i - 16];

	u32 a = ctx->state[0];
	u32 b = ctx->state[1];
	u32 c = ctx->state[2];
	u32 d = ctx->state[3];
	u32 e = ctx->state[4];
	u32 f = ctx->state[5];
	u32 g = ctx->state[6];
	u32 h = ctx->state[7];

	for (int i = 0; i < 64; i++)
	{
		u32 t1 = h + SIGMA1(e) + CH(e, f, g) + K[i] + w[i];
		u32 t2 = SIGMA0(a) + MAJ(a, b, c);
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}

	ctx->state[0] += a;
	ctx->state[1] += b;
	ctx->state[2] += c;
	ctx->state[3] += d;
	ctx->state[4] += e;
	ctx->state[5] += f;
	ctx->state[6] += g;
	ctx->state[7] += h;
}

void sha256_init (sha256_ctx_t *ctx)
{
	ctx->state[0] = 0x6a09e667;
	ctx->state[1] = 0xbb67ae85;
	ctx->state[2] = 0x3c6ef372;
	ctx->state[3] = 0xa54ff53a;
	ctx->state[4] = 0x510e527f;
	ctx->state[5] = 0x9b05688c;
	ctx->state[6] = 0x1f83d9ab;
	ctx->state[7] = 0x5be0cd19;
	ctx->count = 0;
}

void sha256_update (sha256_ctx_t *ctx, const void *data, size_t len)
{
	const u8 *p = (const u8 *)data;
	size_t buf_idx = (size_t)(ctx->count & 0x3F);
	ctx->count += len;

	if (buf_idx > 0)
	{
		size_t needed = 64 - buf_idx;
		if (len < needed)
		{
			memcpy (ctx->buffer + buf_idx, p, len);
			return;
		}
		memcpy (ctx->buffer + buf_idx, p, needed);
		sha256_transform (ctx, ctx->buffer);
		p += needed;
		len -= needed;
	}

	while (len >= 64)
	{
		sha256_transform (ctx, p);
		p += 64;
		len -= 64;
	}

	if (len > 0)
		memcpy (ctx->buffer, p, len);
}

void sha256_final (sha256_ctx_t *ctx, u8 digest[SHA256_DIGEST_SIZE])
{
	u64 total_bits = ctx->count * 8;
	size_t buf_idx = (size_t)(ctx->count & 0x3F);

	ctx->buffer[buf_idx++] = 0x80;
	if (buf_idx > 56)
	{
		memset (ctx->buffer + buf_idx, 0, 64 - buf_idx);
		sha256_transform (ctx, ctx->buffer);
		buf_idx = 0;
	}
	memset (ctx->buffer + buf_idx, 0, 56 - buf_idx);

	for (int i = 7; i >= 0; i--)
	{
		ctx->buffer[56 + (7 - i)] = (u8)(total_bits >> (i * 8));
	}
	sha256_transform (ctx, ctx->buffer);

	for (int i = 0; i < 8; i++)
	{
		digest[i * 4] = (u8)(ctx->state[i] >> 24);
		digest[i * 4 + 1] = (u8)(ctx->state[i] >> 16);
		digest[i * 4 + 2] = (u8)(ctx->state[i] >> 8);
		digest[i * 4 + 3] = (u8)(ctx->state[i]);
	}
}

void sha256_calc (const void *data, size_t len, u8 digest[SHA256_DIGEST_SIZE])
{
	sha256_ctx_t ctx;
	sha256_init (&ctx);
	sha256_update (&ctx, data, len);
	sha256_final (&ctx, digest);
}

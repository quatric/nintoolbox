// Minimal AES-128 (FIPS-197) implementation. Public-domain algorithm,
// written from scratch for this project -- no external crypto library.

#include "lib-aes.h"
#include <string.h>
#include <stdbool.h>

#define NB 4 // block size in 32-bit words (always 4 for AES)
#define NK 4 // key length in 32-bit words (4 = AES-128)
#define NR 10 // number of rounds (10 = AES-128)

static const uint8_t sbox[256] = { 0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67,
	0x2b, 0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2,
	0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5,
	0xf1, 0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80,
	0xe2, 0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6,
	0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe,
	0x39, 0x4a, 0x4c, 0x58, 0xcf, 0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02,
	0x7f, 0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda,
	0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e,
	0x3d, 0x64, 0x5d, 0x19, 0x73, 0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8,
	0x14, 0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac,
	0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4,
	0xea, 0x65, 0x7a, 0xae, 0x08, 0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74,
	0x1f, 0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57,
	0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87,
	0xe9, 0xce, 0x55, 0x28, 0xdf, 0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d,
	0x0f, 0xb0, 0x54, 0xbb, 0x16 };

static uint8_t inv_sbox[256];
static bool inv_sbox_ready = false;

static void build_inv_sbox (void)
{
	if (inv_sbox_ready)
		return;
	for (int i = 0; i < 256; i++)
		inv_sbox[sbox[i]] = (uint8_t)i;
	inv_sbox_ready = true;
}

static const uint8_t rcon[11]
	= { 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36 };

static uint8_t xtime (uint8_t x)
{
	return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0x00));
}

static uint8_t gmul (uint8_t a, uint8_t b)
{
	uint8_t p = 0;
	for (int i = 0; i < 8; i++)
	{
		if (b & 1)
			p ^= a;
		a = xtime (a);
		b >>= 1;
	}
	return p;
}

void AES128_Init (aes128_ctx_t *ctx, const uint8_t key[16])
{
	uint8_t *rk = ctx->round_key;
	memcpy (rk, key, 16);

	uint8_t temp[4];
	for (uint32_t i = NK; i < NB * (NR + 1); i++)
	{
		memcpy (temp, rk + (i - 1) * 4, 4);
		if (i % NK == 0)
		{
			uint8_t t = temp[0];
			temp[0] = sbox[temp[1]] ^ rcon[i / NK];
			temp[1] = sbox[temp[2]];
			temp[2] = sbox[temp[3]];
			temp[3] = sbox[t];
		}
		for (int j = 0; j < 4; j++)
			rk[i * 4 + j] = rk[(i - NK) * 4 + j] ^ temp[j];
	}
}

static void add_round_key (uint8_t state[16], const uint8_t *rk)
{
	for (int i = 0; i < 16; i++)
		state[i] ^= rk[i];
}

static void sub_bytes (uint8_t state[16])
{
	for (int i = 0; i < 16; i++)
		state[i] = sbox[state[i]];
}

static void inv_sub_bytes (uint8_t state[16])
{
	for (int i = 0; i < 16; i++)
		state[i] = inv_sbox[state[i]];
}

// state is column-major: state[col*4+row]
static void shift_rows (uint8_t s[16])
{
	uint8_t t;
	// row 1: shift left 1
	t = s[1];
	s[1] = s[5];
	s[5] = s[9];
	s[9] = s[13];
	s[13] = t;
	// row 2: shift left 2
	t = s[2];
	s[2] = s[10];
	s[10] = t;
	t = s[6];
	s[6] = s[14];
	s[14] = t;
	// row 3: shift left 3 (== shift right 1)
	t = s[15];
	s[15] = s[11];
	s[11] = s[7];
	s[7] = s[3];
	s[3] = t;
}

static void inv_shift_rows (uint8_t s[16])
{
	uint8_t t;
	// row 1: shift right 1
	t = s[13];
	s[13] = s[9];
	s[9] = s[5];
	s[5] = s[1];
	s[1] = t;
	// row 2: shift right 2
	t = s[2];
	s[2] = s[10];
	s[10] = t;
	t = s[6];
	s[6] = s[14];
	s[14] = t;
	// row 3: shift right 3 (== shift left 1)
	t = s[3];
	s[3] = s[7];
	s[7] = s[11];
	s[11] = s[15];
	s[15] = t;
}

static void mix_columns (uint8_t s[16])
{
	for (int c = 0; c < 4; c++)
	{
		uint8_t *p = s + c * 4;
		uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
		p[0] = (uint8_t)(gmul (a0, 2) ^ gmul (a1, 3) ^ a2 ^ a3);
		p[1] = (uint8_t)(a0 ^ gmul (a1, 2) ^ gmul (a2, 3) ^ a3);
		p[2] = (uint8_t)(a0 ^ a1 ^ gmul (a2, 2) ^ gmul (a3, 3));
		p[3] = (uint8_t)(gmul (a0, 3) ^ a1 ^ a2 ^ gmul (a3, 2));
	}
}

static void inv_mix_columns (uint8_t s[16])
{
	for (int c = 0; c < 4; c++)
	{
		uint8_t *p = s + c * 4;
		uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
		p[0] = (uint8_t)(gmul (a0, 14) ^ gmul (a1, 11) ^ gmul (a2, 13) ^ gmul (a3, 9));
		p[1] = (uint8_t)(gmul (a0, 9) ^ gmul (a1, 14) ^ gmul (a2, 11) ^ gmul (a3, 13));
		p[2] = (uint8_t)(gmul (a0, 13) ^ gmul (a1, 9) ^ gmul (a2, 14) ^ gmul (a3, 11));
		p[3] = (uint8_t)(gmul (a0, 11) ^ gmul (a1, 13) ^ gmul (a2, 9) ^ gmul (a3, 14));
	}
}

void AES128_EncryptBlock (const aes128_ctx_t *ctx, uint8_t block[16])
{
	const uint8_t *rk = ctx->round_key;
	add_round_key (block, rk);
	for (int round = 1; round < NR; round++)
	{
		sub_bytes (block);
		shift_rows (block);
		mix_columns (block);
		add_round_key (block, rk + round * 16);
	}
	sub_bytes (block);
	shift_rows (block);
	add_round_key (block, rk + NR * 16);
}

void AES128_DecryptBlock (const aes128_ctx_t *ctx, uint8_t block[16])
{
	build_inv_sbox ();
	const uint8_t *rk = ctx->round_key;
	add_round_key (block, rk + NR * 16);
	for (int round = NR - 1; round >= 1; round--)
	{
		inv_shift_rows (block);
		inv_sub_bytes (block);
		add_round_key (block, rk + round * 16);
		inv_mix_columns (block);
	}
	inv_shift_rows (block);
	inv_sub_bytes (block);
	add_round_key (block, rk);
}

void AES128_CBC_Encrypt (const uint8_t key[16], const uint8_t iv[16], uint8_t *data, size_t size)
{
	aes128_ctx_t ctx;
	AES128_Init (&ctx, key);
	uint8_t prev[16];
	memcpy (prev, iv, 16);
	for (size_t off = 0; off + 16 <= size; off += 16)
	{
		uint8_t *block = data + off;
		for (int i = 0; i < 16; i++)
			block[i] ^= prev[i];
		AES128_EncryptBlock (&ctx, block);
		memcpy (prev, block, 16);
	}
}

void AES128_CBC_Decrypt (const uint8_t key[16], const uint8_t iv[16], uint8_t *data, size_t size)
{
	aes128_ctx_t ctx;
	AES128_Init (&ctx, key);
	uint8_t prev[16], cipher[16];
	memcpy (prev, iv, 16);
	for (size_t off = 0; off + 16 <= size; off += 16)
	{
		uint8_t *block = data + off;
		memcpy (cipher, block, 16);
		AES128_DecryptBlock (&ctx, block);
		for (int i = 0; i < 16; i++)
			block[i] ^= prev[i];
		memcpy (prev, cipher, 16);
	}
}

void AES128_OFB_Crypt (const uint8_t key[16], const uint8_t iv[16], uint8_t *data, size_t size)
{
	aes128_ctx_t ctx;
	AES128_Init (&ctx, key);
	uint8_t stream[16];
	memcpy (stream, iv, 16);
	size_t off = 0;
	while (off < size)
	{
		AES128_EncryptBlock (&ctx, stream); // OFB: keystream = E(prev keystream)
		const size_t n = size - off < 16 ? size - off : 16;
		for (size_t i = 0; i < n; i++)
			data[off + i] ^= stream[i];
		off += n;
	}
}

void AES128_CTR_Crypt (const uint8_t key[16], const uint8_t iv[16], uint8_t *data, size_t size)
{
	aes128_ctx_t ctx;
	AES128_Init (&ctx, key);
	uint8_t counter[16];
	memcpy (counter, iv, 16);
	size_t off = 0;
	while (off < size)
	{
		uint8_t keystream[16];
		memcpy (keystream, counter, 16);
		AES128_EncryptBlock (&ctx, keystream);
		const size_t n = size - off < 16 ? size - off : 16;
		for (size_t i = 0; i < n; i++)
			data[off + i] ^= keystream[i];
		off += n;

		for (int c = 15; c >= 0; c--)
		{
			if (++counter[c] != 0)
				break;
		}
	}
}

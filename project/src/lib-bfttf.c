// SPDX-License-Identifier: GPL-2.0+
#include "lib-bfttf.h"
#include <string.h>

static inline u32 bfttf_unscramble_word (u32 w, u32 key)
{
	const u32 mask = 0xff00ff00;
	const u32 n = (w >> 16) | (w << 16);
	const u32 orig = ((n & mask) >> 8) | ((n << 8) & mask);
	return orig ^ key;
}

static inline u32 bfttf_scramble_word (u32 w, u32 key)
{
	const u32 mask = 0xff00ff00;
	u32 n = w ^ key;
	n = ((n & mask) >> 8) | ((n << 8) & mask);
	return (n >> 16) | (n << 16);
}

static inline u32 bfttf_derive_key (u32 w0)
{
	const u32 mask = 0xff00ff00;
	const u32 n = (w0 >> 16) | (w0 << 16);
	const u32 orig = ((n & mask) >> 8) | ((n << 8) & mask);
	return orig ^ BFTTF_SIGNATURE;
}

bool IsBFTTF (const u8 *data, uint size)
{
	if (!data || size < 12)
		return false;

	const u32 w0 = rd_le32 (data);
	const u32 key = bfttf_derive_key (w0);

	// Validate against known Nintendo SDK platform keys, or allow general valid key
	if (key != BFTTF_KEY_NX && key != BFTTF_KEY_CAFE && key != BFTTF_KEY_WIN && key == 0)
		return false;

	const u32 w1 = rd_le32 (data + 4);
	const u32 ttf_size = bfttf_unscramble_word (w1, key);
	if (ttf_size < 12 || ttf_size > 0x20000000)
		return false;

	const uint word_count = 2 + (ttf_size + 3) / 4;
	if ((u64)word_count * 4 > size)
		return false;

	// Validate TrueType / OpenType header magic from first decoded payload word
	const u32 w2 = rd_le32 (data + 8);
	const u32 tag = bfttf_unscramble_word (w2, key);
	if (tag != 0x00010000 && tag != 0x4f54544f && tag != 0x74727565 && tag != 0x74797031)
	{
		if (key != BFTTF_KEY_NX && key != BFTTF_KEY_CAFE && key != BFTTF_KEY_WIN)
			return false;
	}

	return true;
}

enumError DecodeBFTTF (u8 **out_ttf, uint *out_size, const u8 *data, uint size)
{
	if (!out_ttf || !out_size || !data || size < 12)
		return ERR_INVALID_DATA;

	const u32 w0 = rd_le32 (data);
	const u32 key = bfttf_derive_key (w0);
	const u32 w1 = rd_le32 (data + 4);
	const u32 ttf_size = bfttf_unscramble_word (w1, key);

	if (ttf_size < 12 || ttf_size > 0x20000000)
		return ERR_INVALID_DATA;

	const uint total_words = 2 + (ttf_size + 3) / 4;
	if ((u64)total_words * 4 > size)
		return ERR_INVALID_DATA;

	u8 *ttf = MALLOC (ttf_size);
	if (!ttf)
		return ERR_OUT_OF_MEMORY;

	uint dst_idx = 0;
	for (uint i = 2; i < total_words; i++)
	{
		const u32 raw_word = rd_le32 (data + i * 4);
		const u32 word = bfttf_unscramble_word (raw_word, key);
		for (int j = 3; j >= 0; j--)
		{
			if (dst_idx < ttf_size)
				ttf[dst_idx++] = (u8)((word >> (j * 8)) & 0xff);
		}
	}

	*out_ttf = ttf;
	*out_size = ttf_size;
	return ERR_OK;
}

enumError EncodeBFTTF (u8 **out_bfttf, uint *out_size, const u8 *ttf_data, uint ttf_size, u32 key)
{
	if (!out_bfttf || !out_size || !ttf_data || !ttf_size)
		return ERR_INVALID_DATA;

	if (!key)
		key = BFTTF_KEY_NX;

	const uint total_words = 2 + (ttf_size + 3) / 4;
	const uint bfttf_bytes = total_words * 4;

	u8 *bfttf = MALLOC (bfttf_bytes);
	if (!bfttf)
		return ERR_OUT_OF_MEMORY;

	wr_le32 (bfttf, bfttf_scramble_word (BFTTF_SIGNATURE, key));
	wr_le32 (bfttf + 4, bfttf_scramble_word (ttf_size, key));

	for (uint i = 2; i < total_words; i++)
	{
		u32 word = 0;
		for (int j = 0; j < 4; j++)
		{
			const uint idx = (i - 2) * 4 + (uint)j;
			if (idx < ttf_size)
				word |= (u32)ttf_data[idx] << (8 * (3 - j));
		}
		wr_le32 (bfttf + i * 4, bfttf_scramble_word (word, key));
	}

	*out_bfttf = bfttf;
	*out_size = bfttf_bytes;
	return ERR_OK;
}

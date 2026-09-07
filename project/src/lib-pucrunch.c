// PuCrunch compression (Griptonite Games) -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

//-----------------------------------------------------------------------------
///////////////		PuCrunch Compression (Griptonite Games)	///////////////
//-----------------------------------------------------------------------------

typedef struct pc_bit_reader_t
{
	const u8 *src, *end;
	uint bitpos;
	bool error;
} pc_bit_reader_t;

static inline bool pc_read_bit (pc_bit_reader_t *r)
{
	const u8 *p = r->src + (r->bitpos / 8);
	if (p >= r->end)
	{
		r->error = true;
		return false;
	}
	const bool b = (*p & (0x80 >> (r->bitpos & 7))) != 0;
	r->bitpos++;
	return b;
}

static inline uint pc_read_bits (pc_bit_reader_t *r, uint n)
{
	uint val = 0;
	for (uint i = 0; i < n; i++)
		val = (val << 1) | (pc_read_bit (r) ? 1 : 0);
	return val;
}

static inline uint pc_read_gamma (pc_bit_reader_t *r)
{
	uint count = 0;
	while (pc_read_bit (r) && !r->error && count < 32)
		count++;
	if (r->error)
		return 0;
	if (count == 0)
		return 1;
	return (1u << count) | pc_read_bits (r, count);
}

int CxIsCompressedPuCrunch (const unsigned char *buffer, unsigned int size)
{
	if (!buffer || size < 8 || buffer[0] != 0x60)
		return 0;
	const uint uncomp_size = ((uint)buffer[1]) | ((uint)buffer[2] << 8) | ((uint)buffer[3] << 16);
	if (!uncomp_size || uncomp_size > NFMT_MAX_OUTPUT)
		return 0;

	const u8 *info = buffer + 4;
	const uint freq_tbl_size = info[0];
	const uint esc_bits = info[3];
	const uint lz_extra = info[2];
	if (freq_tbl_size > size - 8 || (freq_tbl_size & 3) || freq_tbl_size > 0x20 || !freq_tbl_size)
		return 0;
	if (esc_bits > 8 || lz_extra > 24)
		return 0;
	return 1;
}

enumError DecodePuCrunch (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !CxIsCompressedPuCrunch (src, src_size))
		return EINVAL;

	const uint uncomp_size = ((uint)src[1]) | ((uint)src[2] << 8) | ((uint)src[3] << 16);
	enumError err = AllocOutput (dest, dest_size, uncomp_size);
	if (err)
		return err;

	u8 *out = *dest;
	const u8 *info = src + 4;
	const uint freq_tbl_size = info[0];
	u8 esc = info[1];
	const uint lz_extra = info[2];
	const uint esc_bits = info[3];
	const u8 *freq_table = src + 8;
	const u8 *bit_stream = info + 4 + freq_tbl_size;

	pc_bit_reader_t reader = { bit_stream, src + src_size, 0, false };
	const uint n_lz_bits = 8 + lz_extra;
	uint outpos = 0;

	while (outpos < uncomp_size && !reader.error)
	{
		const u8 init_bits = (u8)pc_read_bits (&reader, esc_bits);
		if (init_bits != esc)
		{
			const u8 rest = (u8)pc_read_bits (&reader, 8 - esc_bits);
			out[outpos++] = (init_bits << (8 - esc_bits)) | rest;
		}
		else
		{
			const uint x = pc_read_gamma (&reader) + 1;
			if (x > 2)
			{
				const uint hi = pc_read_gamma (&reader) - 1;
				if (hi == 0xFE)
					break; // EOF
				const uint offset = ((hi << n_lz_bits) | pc_read_bits (&reader, n_lz_bits)) + 1;
				if (offset > outpos || x > uncomp_size - outpos)
					goto fail;
				for (uint i = 0; i < x; i++)
					out[outpos + i] = out[outpos - offset + i];
				outpos += x;
			}
			else if (!pc_read_bit (&reader))
			{
				const uint offset = pc_read_bits (&reader, 8) + 1;
				if (offset > outpos || 2 > uncomp_size - outpos)
					goto fail;
				out[outpos + 0] = out[outpos - offset + 0];
				out[outpos + 1] = out[outpos - offset + 1];
				outpos += 2;
			}
			else if (!pc_read_bit (&reader))
			{
				const u8 new_esc = (u8)pc_read_bits (&reader, esc_bits);
				out[outpos++] = (esc << (8 - esc_bits)) | (u8)pc_read_bits (&reader, 8 - esc_bits);
				esc = new_esc;
			}
			else
			{
				uint rl_len = pc_read_gamma (&reader);
				if (rl_len >= 0x80)
				{
					rl_len = ((rl_len << 1) + (pc_read_bit (&reader) ? 1 : 0)) & 0xFF;
					rl_len |= (pc_read_gamma (&reader) - 1) << 8;
				}
				rl_len++;
				uint b_repeat = pc_read_gamma (&reader);
				if (b_repeat < 32)
				{
					if (b_repeat == 0 || b_repeat - 1 >= freq_tbl_size)
						goto fail;
					b_repeat = freq_table[b_repeat - 1];
				}
				else
				{
					b_repeat = ((b_repeat << 3) | pc_read_bits (&reader, 3)) & 0xFF;
				}
				if (rl_len > uncomp_size - outpos)
					goto fail;
				for (uint i = 0; i < rl_len; i++)
					out[outpos + i] = (u8)b_repeat;
				outpos += rl_len;
			}
		}
	}

	if (outpos == uncomp_size && !reader.error)
		return ERR_OK;
fail:
	FREE (*dest);
	*dest = 0;
	*dest_size = 0;
	return EINVAL;
}

enumError EncodePuCrunch (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > 0x00ffffff)
		return EINVAL;

	// Build a valid literal PuCrunch stream
	const uint freq_tbl_size = 4;
	const uint total = 8 + freq_tbl_size + (src_size * 10 + 32) / 8 + 4;
	u8 *out = CALLOC (1, total);
	if (!out)
		return ERR_CANT_CREATE;

	out[0] = 0x60;
	out[1] = (u8)(src_size & 0xFF);
	out[2] = (u8)((src_size >> 8) & 0xFF);
	out[3] = (u8)((src_size >> 16) & 0xFF);

	out[4] = (u8)freq_tbl_size;
	out[5] = 0x00; // esc value
	out[6] = 0x00; // lz extra
	out[7] = 0x02; // esc bits = 2

	// Frequency table
	out[8] = 0;
	out[9] = 0;
	out[10] = 0;
	out[11] = 0;

	// Bitstream: write each literal with non-escape prefix (0b01)
	uint bitpos = 0;
	u8 *stm = out + 8 + freq_tbl_size;
	for (uint i = 0; i < src_size; i++)
	{
		const u8 b = src[i];
		// If high 2 bits are 0b00 (escape match), write escaped literal:
		// esc (0b00) + gamma(1) -> 0b0 + bit(1) + bit(0) + new_esc(0b00) + rest
		if ((b >> 6) == 0x00)
		{
			// esc: 0b00
			bitpos += 2;
			// gamma 1: bit 0
			bitpos += 1;
			// bit 1: not 2-byte LZ
			stm[bitpos / 8] |= 0x80 >> (bitpos & 7);
			bitpos++;
			// bit 0: escaped literal
			bitpos++;
			// new escape = 0b00
			bitpos += 2;
			// 6 low bits of literal
			for (int bit = 5; bit >= 0; bit--)
			{
				if (b & (1 << bit))
					stm[bitpos / 8] |= 0x80 >> (bitpos & 7);
				bitpos++;
			}
		}
		else
		{
			// 8 bits of literal directly (high 2 bits != 0)
			for (int bit = 7; bit >= 0; bit--)
			{
				if (b & (1 << bit))
					stm[bitpos / 8] |= 0x80 >> (bitpos & 7);
				bitpos++;
			}
		}
	}

	// End of stream marker: esc (0b00) + gamma(2) [0b100] + gamma(0xFF) [0xFE]
	// esc 0b00
	bitpos += 2;
	// gamma 2 (x=3 > 2): 0b100
	stm[bitpos / 8] |= 0x80 >> (bitpos & 7);
	bitpos += 3;
	// gamma for hi=0xFE (255): 8 bits of 1 + 0 + 8-bit val
	for (uint k = 0; k < 8; k++)
	{
		stm[bitpos / 8] |= 0x80 >> (bitpos & 7);
		bitpos++;
	}
	bitpos++; // 0 bit
	for (int bit = 7; bit >= 0; bit--)
	{
		if (0xFF & (1 << bit))
			stm[bitpos / 8] |= 0x80 >> (bitpos & 7);
		bitpos++;
	}

	const uint final_sz = 8 + freq_tbl_size + (bitpos + 7) / 8;
	*dest = out;
	*dest_size = final_sz;
	return ERR_OK;
}

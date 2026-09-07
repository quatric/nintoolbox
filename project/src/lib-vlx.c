// VLX compression (Namco / Pac-Man World) -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

//-----------------------------------------------------------------------------
///////////////		VLX Compression (Namco / Pac-Man World)	///////////////
//-----------------------------------------------------------------------------

typedef struct vlx_tree_node_t
{
	uint value;
	uint mask_bits;
	u32 encoding;
	u32 mask;
} vlx_tree_node_t;

typedef struct vlx_buffer_t
{
	const u8 *src;
	uint srcpos, size;
	u32 cur_word;
	int bits_left;
	bool error;
} vlx_buffer_t;

static void vlx_buf_init (vlx_buffer_t *b, const u8 *src, uint pos, uint size)
{
	b->src = src;
	b->srcpos = pos;
	b->size = size;
	b->cur_word = 0;
	b->bits_left = 0;
	b->error = false;
}

static u32 vlx_read_bits (vlx_buffer_t *b, int n)
{
	if (n <= 0)
		return 0;
	while (b->bits_left < n)
	{
		if (b->srcpos + 4 > b->size)
		{
			// Read remaining bytes with zero padding
			u32 w = 0;
			for (uint i = 0; i < 4; i++)
				if (b->srcpos + i < b->size)
					w |= (u32)b->src[b->srcpos + i] << (i * 8);
			b->cur_word |= w << b->bits_left;
			b->srcpos = b->size;
			b->bits_left += 32;
			break;
		}
		const u32 w = (u32)b->src[b->srcpos] | ((u32)b->src[b->srcpos + 1] << 8)
			| ((u32)b->src[b->srcpos + 2] << 16) | ((u32)b->src[b->srcpos + 3] << 24);
		b->srcpos += 4;
		b->cur_word |= w << b->bits_left;
		b->bits_left += 32;
	}
	const u32 val = b->cur_word & ((1u << n) - 1);
	b->cur_word >>= n;
	b->bits_left -= n;
	return val;
}

static uint vlx_read_next_val (vlx_buffer_t *b, const vlx_tree_node_t *nodes, uint n_nodes)
{
	while (b->bits_left < 16 && b->srcpos < b->size)
	{
		const u8 byte = b->src[b->srcpos++];
		b->cur_word |= (u32)byte << b->bits_left;
		b->bits_left += 8;
	}
	for (uint i = 0; i < n_nodes; i++)
	{
		const uint mb = nodes[i].mask_bits;
		if ((int)mb > b->bits_left)
			continue;
		const u32 code = b->cur_word & ((1u << mb) - 1);
		if ((code << (32 - mb)) == nodes[i].encoding)
		{
			b->cur_word >>= mb;
			b->bits_left -= mb;
			return nodes[i].value;
		}
	}
	b->error = true;
	return (uint)-1;
}

static int vlx_try_decompress (const u8 *src, uint size, u8 *dest, uint *out_len_ptr)
{
	if (!src || size < 2)
		return 0;
	if (src[0] & 0xF0)
		return 0;

	const uint lenlen = src[0] & 0xF;
	uint outlen = 0;
	if (lenlen == 1)
	{
		if (size < 3)
			return 0;
		outlen = src[1];
	}
	else if (lenlen == 2)
	{
		if (size < 4)
			return 0;
		outlen = (uint)src[1] | ((uint)src[2] << 8);
	}
	else if (lenlen == 4)
	{
		if (size < 6)
			return 0;
		outlen = (uint)src[1] | ((uint)src[2] << 8) | ((uint)src[3] << 16) | ((uint)src[4] << 24);
	}
	else
		return 0;

	if (!outlen || outlen > NFMT_MAX_OUTPUT)
		return 0;

	uint srcpos = lenlen + 1;
	if (srcpos >= size)
		return 0;

	const u8 byte1 = src[srcpos++];
	const uint hi4 = (byte1 >> 4) & 0xF;
	const uint lo4 = byte1 & 0xF;
	if (hi4 > 12 || lo4 > 12 || srcpos + (hi4 + lo4) * 2 > size)
		return 0;

	vlx_tree_node_t len_nodes[12] = { { 0 } };
	vlx_tree_node_t dist_nodes[12] = { { 0 } };

	for (uint i = 0; i < hi4; i++)
	{
		const u16 hw = (u16)src[srcpos] | ((u16)src[srcpos + 1] << 8);
		srcpos += 2;
		len_nodes[i].value = hw >> 12;
		int mb = 11;
		if ((hw & 0xFFF) == 0)
			return 0;
		while (!((hw & 0xFFF) & (1 << mb)) && mb > 0)
			mb--;
		if (mb == 0)
			return 0;
		len_nodes[i].mask_bits = mb;
		len_nodes[i].encoding = (hw & 0xFFF) & ((1u << mb) - 1);
	}

	for (uint i = 0; i < lo4; i++)
	{
		const u16 hw = (u16)src[srcpos] | ((u16)src[srcpos + 1] << 8);
		srcpos += 2;
		dist_nodes[i].value = hw >> 12;
		int mb = 11;
		if ((hw & 0xFFF) == 0)
			return 0;
		while (!((hw & 0xFFF) & (1 << mb)) && mb > 0)
			mb--;
		if (mb == 0)
			return 0;
		dist_nodes[i].mask_bits = mb;
		dist_nodes[i].encoding = (hw & 0xFFF) & ((1u << mb) - 1);
	}

	vlx_buffer_t buf;
	vlx_buf_init (&buf, src, srcpos, size);
	uint outpos = 0;

	while (outpos < outlen && !buf.error)
	{
		const uint n_len_bits = vlx_read_next_val (&buf, len_nodes, hi4);
		if (n_len_bits == (uint)-1)
			return 0;

		if (n_len_bits == 0)
		{
			const u8 b = (u8)vlx_read_bits (&buf, 8);
			if (dest)
				dest[outpos] = b;
			outpos++;
		}
		else
		{
			const uint copylen = (1u << n_len_bits) + vlx_read_bits (&buf, n_len_bits);
			const uint n_dist_bits = vlx_read_next_val (&buf, dist_nodes, lo4);
			if (n_dist_bits == (uint)-1)
				return 0;
			const uint dist = (1u << n_dist_bits) + vlx_read_bits (&buf, n_dist_bits) - 1;
			if (dist > outpos || dist == 0 || copylen > outlen - outpos)
				return 0;

			if (dest)
			{
				for (uint i = 0; i < copylen; i++)
					dest[outpos + i] = dest[outpos - dist + i];
			}
			outpos += copylen;
		}
	}

	if (out_len_ptr)
		*out_len_ptr = outlen;
	return outpos == outlen && !buf.error;
}

int CxIsCompressedVlx (const unsigned char *src, unsigned int size)
{
	return vlx_try_decompress (src, size, 0, 0);
}

enumError DecodeVLX (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 4)
		return EINVAL;
	uint uncomp_sz = 0;
	if (!vlx_try_decompress (src, src_size, 0, &uncomp_sz))
		return EINVAL;
	enumError err = AllocOutput (dest, dest_size, uncomp_sz);
	if (err)
		return err;
	if (!vlx_try_decompress (src, src_size, *dest, 0))
	{
		FREE (*dest);
		*dest = 0;
		*dest_size = 0;
		return EINVAL;
	}
	return ERR_OK;
}

enumError EncodeVLX (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > NFMT_MAX_OUTPUT)
		return EINVAL;

	// Build a valid literal VLX stream with lenlen=4
	// Header: [0]=4, [1..4]=src_size, [5]=0x10 (hi4=1, lo4=0), [6..7]=hw(node 0, enc 0 with
	// sentinel bit 1 => 2)
	const uint total_sz = 5 + 1 + 2 + src_size * 2 + 8;
	u8 *out = CALLOC (1, total_sz);
	if (!out)
		return ERR_CANT_CREATE;

	out[0] = 4; // 4-byte uncompressed size
	out[1] = (u8)(src_size & 0xFF);
	out[2] = (u8)((src_size >> 8) & 0xFF);
	out[3] = (u8)((src_size >> 16) & 0xFF);
	out[4] = (u8)((src_size >> 24) & 0xFF);
	out[5] = 0x10; // hi4=1, lo4=0
	out[6] = 0x02; // hw = (value 0 << 12) | (1 << 1 sentinel) | (code 0) = 2
	out[7] = 0x00;

	// Write literal tokens (1 bit '0' length token + 8 bits data)
	uint bitpos = 0;
	u8 *bitstream = out + 8;
	for (uint i = 0; i < src_size; i++)
	{
		// 1 bit '0' for length node value 0
		bitpos++; // bit remains 0
		// 8 bits of literal byte
		for (uint b = 0; b < 8; b++)
		{
			if (src[i] & (1 << b))
				bitstream[bitpos / 8] |= 0x01 << (bitpos & 7);
			bitpos++;
		}
	}

	const uint final_size = 8 + (bitpos + 7) / 8;
	*dest = out;
	*dest_size = final_size;
	return ERR_OK;
}

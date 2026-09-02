#include "lib-std.h"
#include "lib-rnc.h"
#include <string.h>
#include <errno.h>
#include <limits.h>


//-----------------------------------------------------------------------------
// RNC (Rob Northen Compression) decoder, RNC1/RNC2 methods.
//
// Faithful port of the unpack paths from the decompiled RNC ProPack tool
// (github.com/lab313ru/rnc_propack_source; verbatim mirror used as reference:
// huderlem/carrotcrazy/tools/rnc.c).  Layout of the 18-byte header:
//
//   +0x00  "RNC" + method byte (1=RNC1, 2=RNC2)
//   +0x04  BE32 unpacked size
//   +0x08  BE32 packed size (bytes of compressed data after this header)
//   +0x0C  BE16 CRC16 of the unpacked data
//   +0x0E  BE16 CRC16 of the packed data
//   +0x10  byte leeway, +0x11 byte chunk count
//   +0x12  start of the compressed stream
//
// The two flag bits at the head of the stream select the decode method and
// signal encryption; encrypted streams need a key we do not carry, so they
// are rejected before any output is touched.

static const u16 rnc_crc_table[] = { 0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
	0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440, 0xCC01, 0x0CC0, 0x0D80, 0xCD41,
	0x0F00, 0xCFC1, 0xCE81, 0x0E40, 0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
	0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40, 0x1E00, 0xDEC1, 0xDF81, 0x1F40,
	0xDD01, 0x1DC0, 0x1C80, 0xDC41, 0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
	0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040, 0xF001, 0x30C0, 0x3180, 0xF141,
	0x3300, 0xF3C1, 0xF281, 0x3240, 0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
	0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41, 0xFA01, 0x3AC0, 0x3B80, 0xFB41,
	0x3900, 0xF9C1, 0xF881, 0x3840, 0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
	0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40, 0xE401, 0x24C0, 0x2580, 0xE541,
	0x2700, 0xE7C1, 0xE681, 0x2640, 0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
	0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240, 0x6600, 0xA6C1, 0xA781, 0x6740,
	0xA501, 0x65C0, 0x6480, 0xA441, 0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
	0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840, 0x7800, 0xB8C1, 0xB981, 0x7940,
	0xBB01, 0x7BC0, 0x7A80, 0xBA41, 0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
	0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640, 0x7200, 0xB2C1, 0xB381, 0x7340,
	0xB101, 0x71C0, 0x7080, 0xB041, 0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
	0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440, 0x9C01, 0x5CC0, 0x5D80, 0x9D41,
	0x5F00, 0x9FC1, 0x9E81, 0x5E40, 0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
	0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40, 0x4E00, 0x8EC1, 0x8F81, 0x4F40,
	0x8D01, 0x4DC0, 0x4C80, 0x8C41, 0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
	0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040 };

static const u8 rnc_match_offset_bits[] = { 0x00, 0x06, 0x08, 0x09, 0x15, 0x17, 0x1D, 0x1F, 0x28,
	0x29, 0x2C, 0x2D, 0x38, 0x39, 0x3C, 0x3D };
static const u8 rnc_match_offset_nbits[] = { 1, 3, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6 };

typedef struct rnc_huftable_t
{
	u32 l1, l3;
	u16 l2;
	u16 bit_depth;
} rnc_huftable_t;

typedef struct rnc_state_t
{
	const u8 *src;
	uint src_size;
	uint in_pos, processed, input_size, dict_size;
	u16 match_count, match_offset, bit_count, unpacked_crc_real;
	u32 bit_buffer;
	u8 *mem1, *decoded, *window, *pack_block;
	u8 *out;
} rnc_state_t;

static u16 rnc_rotate_key (u16 x)
{
	return (x & 1) ? (u16)(0x8000 | (x >> 1)) : (u16)(x >> 1);
}

static u8 rnc_read_source (rnc_state_t *v)
{
	if (v->pack_block == &v->mem1[0xFFFD])
	{
		int left = (int)v->src_size - (int)v->in_pos;
		int n;
		if (left <= 0xFFFD)
			n = left;
		else
			n = 0xFFFD;
		v->pack_block = v->mem1;
		memcpy (v->pack_block, v->src + v->in_pos, n);
		v->in_pos += n;
		if (left - n > 2)
			left = 2;
		else
			left -= n;
		memcpy (v->pack_block + n, v->src + v->in_pos, left);
	}
	return *v->pack_block++;
}

static u32 rnc_input_bits_m2 (rnc_state_t *v, int count)
{
	u32 bits = 0;
	while (count-- > 0)
	{
		if (!v->bit_count)
		{
			v->bit_buffer = rnc_read_source (v);
			v->bit_count = 8;
		}
		bits <<= 1;
		if (v->bit_buffer & 0x80)
			bits |= 1;
		v->bit_buffer <<= 1;
		v->bit_count--;
	}
	return bits;
}

static u32 rnc_input_bits_m1 (rnc_state_t *v, int count)
{
	u32 bits = 0, prev_bits = 1;
	while (count-- > 0)
	{
		if (!v->bit_count)
		{
			const u8 b1 = rnc_read_source (v), b2 = rnc_read_source (v);
			v->bit_buffer = ((u32)v->pack_block[1] << 24) | ((u32)v->pack_block[0] << 16)
				| ((u32)b2 << 8) | b1;
			v->bit_count = 16;
		}
		if (v->bit_buffer & 1)
			bits |= prev_bits;
		v->bit_buffer >>= 1;
		prev_bits <<= 1;
		v->bit_count--;
	}
	return bits;
}

static void rnc_write_decoded (rnc_state_t *v, u8 b)
{
	if (v->window == &v->decoded[0xFFFF])
	{
		memcpy (v->out, &v->decoded[v->dict_size], 0xFFFF - v->dict_size);
		v->out += 0xFFFF - v->dict_size;
		memmove (v->decoded, &v->window[-(int)v->dict_size], v->dict_size);
		v->window = &v->decoded[v->dict_size];
	}
	*v->window++ = b;
	v->unpacked_crc_real
		= rnc_crc_table[(v->unpacked_crc_real ^ b) & 0xFF] ^ (v->unpacked_crc_real >> 8);
}

static void rnc_decode_match_offset (rnc_state_t *v)
{
	v->match_offset = 0;
	if (rnc_input_bits_m2 (v, 1))
	{
		v->match_offset = rnc_input_bits_m2 (v, 1);
		if (rnc_input_bits_m2 (v, 1))
		{
			v->match_offset = ((v->match_offset << 1) | rnc_input_bits_m2 (v, 1)) | 4;
			if (!rnc_input_bits_m2 (v, 1))
				v->match_offset = (v->match_offset << 1) | rnc_input_bits_m2 (v, 1);
		}
		else if (!v->match_offset)
			v->match_offset = rnc_input_bits_m2 (v, 1) + 2;
	}
	v->match_offset = ((v->match_offset << 8) | rnc_read_source (v)) + 1;
}

static void rnc_decode_match_count (rnc_state_t *v)
{
	v->match_count = rnc_input_bits_m2 (v, 1) + 4;
	if (rnc_input_bits_m2 (v, 1))
		v->match_count = ((v->match_count - 1) << 1) + rnc_input_bits_m2 (v, 1);
}

static void rnc_container_match (rnc_state_t *v)
{
	const uint count = v->match_count;
	v->processed += count;
	uint i = count;
	while (i-- > 0)
		rnc_write_decoded (v, v->window[-v->match_offset]);
}

static void rnc_unpack_data_m2 (rnc_state_t *v)
{
	while (v->processed < v->input_size)
	{
		for (;;)
		{
			if (!rnc_input_bits_m2 (v, 1))
			{
				rnc_write_decoded (v, rnc_read_source (v));
				v->processed++;
			}
			else
			{
				if (rnc_input_bits_m2 (v, 1))
				{
					if (rnc_input_bits_m2 (v, 1))
					{
						if (rnc_input_bits_m2 (v, 1))
						{
							v->match_count = rnc_read_source (v) + 8;
							if (v->match_count == 8)
							{
								rnc_input_bits_m2 (v, 1);
								break;
							}
						}
						else
							v->match_count = 3;
						rnc_decode_match_offset (v);
					}
					else
					{
						v->match_count = 2;
						v->match_offset = rnc_read_source (v) + 1;
					}
					rnc_container_match (v);
				}
				else
				{
					rnc_decode_match_count (v);
					if (v->match_count != 9)
					{
						rnc_decode_match_offset (v);
						rnc_container_match (v);
					}
					else
					{
						uint data_length = (rnc_input_bits_m2 (v, 4) << 2) + 12;
						v->processed += data_length;
						while (data_length-- > 0)
							rnc_write_decoded (v, rnc_read_source (v));
					}
				}
			}
		}
	}
}

static void rnc_clear_table (rnc_huftable_t *t, int count)
{
	for (int i = 0; i < count; i++)
	{
		t[i].l1 = 0;
		t[i].l2 = 0xFFFF;
		t[i].l3 = 0;
		t[i].bit_depth = 0;
	}
}

static u32 rnc_inverse_bits (u32 value, int count)
{
	u32 out = 0;
	while (count-- > 0)
	{
		out <<= 1;
		if (value & 1)
			out |= 1;
		value >>= 1;
	}
	return out;
}

static void rnc_proc_20 (rnc_huftable_t *t, int count)
{
	u32 val = 0, div = 0x80000000;
	int depth = 1;
	while (depth <= 16)
	{
		for (int i = 0; i < count; i++)
		{
			if (t[i].bit_depth == depth)
			{
				t[i].l3 = rnc_inverse_bits (val / div, depth);
				val += div;
			}
		}
		depth++;
		div >>= 1;
	}
}

static void rnc_make_huftable (rnc_state_t *v, rnc_huftable_t *t, int count)
{
	rnc_clear_table (t, count);
	int leaf_nodes = (int)rnc_input_bits_m1 (v, 5);
	if (leaf_nodes)
	{
		if (leaf_nodes > 16)
			leaf_nodes = 16;
		for (int i = 0; i < leaf_nodes; i++)
			t[i].bit_depth = (u16)rnc_input_bits_m1 (v, 4);
		rnc_proc_20 (t, leaf_nodes);
	}
}

static u32 rnc_decode_table_data (rnc_state_t *v, rnc_huftable_t *t)
{
	for (u32 i = 0;; i++)
	{
		if (t[i].bit_depth && t[i].l3 == (v->bit_buffer & ((1u << t[i].bit_depth) - 1)))
		{
			rnc_input_bits_m1 (v, t[i].bit_depth);
			if (i < 2)
				return i;
			return rnc_input_bits_m1 (v, i - 1) | (1u << (i - 1));
		}
	}
}

static void rnc_unpack_data_m1 (rnc_state_t *v)
{
	rnc_huftable_t raw[16], len[16], pos[16];
	while (v->processed < v->input_size)
	{
		rnc_make_huftable (v, raw, 16);
		rnc_make_huftable (v, len, 16);
		rnc_make_huftable (v, pos, 16);
		int subchunks = (int)rnc_input_bits_m1 (v, 16);
		while (subchunks-- > 0)
		{
			uint data_length = rnc_decode_table_data (v, raw);
			v->processed += data_length;
			if (data_length)
			{
				while (data_length-- > 0)
					rnc_write_decoded (v, rnc_read_source (v));
				v->bit_buffer = ((((u32)v->pack_block[2] << 16) | ((u32)v->pack_block[1] << 8)
									 | v->pack_block[0])
									<< v->bit_count)
					| (v->bit_buffer & ((1u << v->bit_count) - 1));
			}
			if (subchunks)
			{
				v->match_offset = rnc_decode_table_data (v, len) + 1;
				v->match_count = rnc_decode_table_data (v, pos) + 2;
				rnc_container_match (v);
			}
		}
	}
}


enumError DecodeRNC (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size)
		return ERR_SEMANTIC;
	*dest = 0;
	*dest_size = 0;
	if (!src || src_size < 0x14 || memcmp (src, "RNC", 3))
		return EINVAL;

	const u8 method = src[3];
	if (method != 1 && method != 2)
		return EINVAL;

	const u32 input_size = rd_be32 (src + 0x04);
	const u32 packed_size = rd_be32 (src + 0x08);
	if (!input_size || input_size > NFMT_MAX_OUTPUT)
		return EFBIG;
	if (packed_size > src_size - 0x12)
		return EINVAL;

	u16 packed_crc = 0;
	for (u32 i = 0; i < packed_size; i++)
		packed_crc = rnc_crc_table[(packed_crc ^ src[0x12 + i]) & 0xFF] ^ (packed_crc >> 8);
	if (packed_crc != rd_be16 (src + 0x0E))
		return EINVAL;

	enumError err = AllocOutput (dest, dest_size, input_size);
	if (err)
		return err;

	rnc_state_t st;
	memset (&st, 0, sizeof (st));
	st.src = src;
	st.src_size = src_size;
	st.in_pos = 0x12;
	st.input_size = input_size;
	st.dict_size = method == 1 ? 0x8000 : 0x1000;
	st.out = *dest;
	st.mem1 = MALLOC (0xFFFF + 4);
	st.decoded = MALLOC (0xFFFF + 4);
	if (!st.mem1 || !st.decoded)
	{
		FREE (st.mem1);
		FREE (st.decoded);
		FREE (*dest);
		*dest = 0;
		*dest_size = 0;
		return ERR_CANT_CREATE;
	}
	st.pack_block = &st.mem1[0xFFFD];
	st.window = &st.decoded[st.dict_size];

	if (method == 1)
	{
		// flags: locked? + keyed?
		rnc_input_bits_m1 (&st, 1);
		if (rnc_input_bits_m1 (&st, 1))
		{
			FREE (st.mem1);
			FREE (st.decoded);
			FREE (*dest);
			*dest = 0;
			*dest_size = 0;
			return EINVAL;
		}
		rnc_unpack_data_m1 (&st);
	}
	else
	{
		rnc_input_bits_m2 (&st, 1);
		if (rnc_input_bits_m2 (&st, 1))
		{
			FREE (st.mem1);
			FREE (st.decoded);
			FREE (*dest);
			*dest = 0;
			*dest_size = 0;
			return EINVAL;
		}
		rnc_unpack_data_m2 (&st);
	}

	memcpy (st.out, &st.decoded[st.dict_size], st.window - &st.decoded[st.dict_size]);
	st.out += st.window - &st.decoded[st.dict_size];

	FREE (st.mem1);
	FREE (st.decoded);

	if (st.unpacked_crc_real != rd_be16 (src + 0x0C) || st.out - *dest != input_size)
	{
		FREE (*dest);
		*dest = 0;
		*dest_size = 0;
		return EINVAL;
	}

	return ERR_OK;
}

typedef struct rnc_writer_t
{
	u8 *buf;
	uint cap;
	uint len;
	int bit_pos;
	u8 bit_buf;
	uint bit_cnt;
} rnc_writer_t;

static void rnc_w_init (rnc_writer_t *w, uint initial_cap)
{
	w->cap = initial_cap ? initial_cap : 1024;
	w->buf = MALLOC (w->cap);
	w->len = 0;
	w->bit_pos = -1;
	w->bit_buf = 0;
	w->bit_cnt = 0;
}

static void rnc_w_put_bit (rnc_writer_t *w, int b)
{
	if (!w->bit_cnt)
	{
		if (w->len >= w->cap)
		{
			w->cap *= 2;
			w->buf = REALLOC (w->buf, w->cap);
		}
		w->bit_pos = (int)w->len++;
		w->buf[w->bit_pos] = 0;
		w->bit_buf = 0;
		w->bit_cnt = 8;
	}
	w->bit_cnt--;
	if (b)
		w->bit_buf |= (1u << w->bit_cnt);
	w->buf[w->bit_pos] = w->bit_buf;
}

static void rnc_w_put_bits (rnc_writer_t *w, u32 val, int n)
{
	for (int i = n - 1; i >= 0; i--)
		rnc_w_put_bit (w, (val >> i) & 1);
}

static void rnc_w_put_byte (rnc_writer_t *w, u8 byte)
{
	if (w->len >= w->cap)
	{
		w->cap *= 2;
		w->buf = REALLOC (w->buf, w->cap);
	}
	w->buf[w->len++] = byte;
}

static void rnc_w_put_match_offset (rnc_writer_t *w, uint dist)
{
	uint val = dist - 1;
	uint hi = (val >> 8) & 0x0F;
	uint lo = val & 0xFF;
	rnc_w_put_bits (w, rnc_match_offset_bits[hi], rnc_match_offset_nbits[hi]);
	rnc_w_put_byte (w, (u8)lo);
}

// Method 1 uses little-endian 16-bit bit tokens with literal bytes queued
// behind the token currently being assembled.  A literal-only stream is a
// fully conforming RNC1 stream and is deliberately used here: it gives us a
// simple, deterministic encoder without pretending the method-2 LZ stream
// is method 1. Compression quality can be improved independently later.
typedef struct rnc1_writer_t
{
	u8 *buf;
	uint cap, len, pending_len, pending_cap;
	u16 token;
	uint nbits;
	u8 *pending;
} rnc1_writer_t;

static bool rnc1_emit (rnc1_writer_t *w, u8 byte)
{
	if (w->len >= w->cap)
		return false;
	w->buf[w->len++] = byte;
	return true;
}

static bool rnc1_flush_token (rnc1_writer_t *w)
{
	if (!rnc1_emit (w, (u8)w->token) || !rnc1_emit (w, (u8)(w->token >> 8)))
		return false;
	for (uint i = 0; i < w->pending_len; i++)
		if (!rnc1_emit (w, w->pending[i]))
			return false;
	w->token = 0;
	w->nbits = 0;
	w->pending_len = 0;
	return true;
}

static bool rnc1_put_bits (rnc1_writer_t *w, u32 value, uint count)
{
	while (count--)
	{
		w->token >>= 1;
		if (value & 1)
			w->token |= 0x8000;
		value >>= 1;
		if (++w->nbits == 16 && !rnc1_flush_token (w))
			return false;
	}
	return true;
}

static bool rnc1_queue_byte (rnc1_writer_t *w, u8 byte)
{
	if (!w->nbits)
		return rnc1_emit (w, byte);
	if (w->pending_len >= w->pending_cap)
		return false;
	w->pending[w->pending_len++] = byte;
	return true;
}

static enumError rnc_encode_m1_literals (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!src_size)
		return EINVAL;
	const uint blocks = (src_size + 0x2fff) / 0x3000;
	const u64 capacity = (u64)src_size + (u64)blocks * 32 + 64;
	if (capacity > NFMT_MAX_OUTPUT)
		return EFBIG;
	rnc1_writer_t w = { 0 };
	w.cap = (uint)capacity;
	w.pending_cap = 0x3000;
	w.buf = MALLOC (w.cap);
	w.pending = MALLOC (w.pending_cap);
	if (!w.buf || !w.pending)
	{
		FREE (w.buf);
		FREE (w.pending);
		return ERR_OUT_OF_MEMORY;
	}
	bool ok = rnc1_put_bits (&w, 0, 1) && rnc1_put_bits (&w, 0, 1); // unlocked, unkeyed
	uint pos = 0;
	while (ok && pos < src_size)
	{
		const uint count = src_size - pos > 0x3000 ? 0x3000 : src_size - pos;
		uint symbol = 0, tmp = count;
		while (tmp)
		{
			symbol++;
			tmp >>= 1;
		}
		// raw Huffman table: one one-bit symbol; empty offset/length tables;
		// one literal-only subchunk.
		ok = rnc1_put_bits (&w, symbol + 1, 5);
		for (uint i = 0; ok && i <= symbol; i++)
			ok = rnc1_put_bits (&w, i == symbol ? 1 : 0, 4);
		ok = ok && rnc1_put_bits (&w, 0, 5) && rnc1_put_bits (&w, 0, 5) && rnc1_put_bits (&w, 1, 16)
			&& rnc1_put_bits (&w, 0, 1);
		if (symbol > 1)
			ok = ok && rnc1_put_bits (&w, count - (1u << (symbol - 1)), symbol - 1);
		for (uint i = 0; ok && i < count; i++)
			ok = rnc1_queue_byte (&w, src[pos + i]);
		pos += count;
	}
	if (ok && (w.nbits || w.pending_len))
	{
		w.token >>= 16 - w.nbits;
		ok = rnc1_flush_token (&w);
	}
	FREE (w.pending);
	if (!ok)
	{
		FREE (w.buf);
		return ERR_OUT_OF_MEMORY;
	}
	u16 unpacked_crc = 0, packed_crc = 0;
	for (uint i = 0; i < src_size; i++)
		unpacked_crc = rnc_crc_table[(unpacked_crc ^ src[i]) & 255] ^ (unpacked_crc >> 8);
	for (uint i = 0; i < w.len; i++)
		packed_crc = rnc_crc_table[(packed_crc ^ w.buf[i]) & 255] ^ (packed_crc >> 8);
	u8 *out = MALLOC (0x12 + w.len);
	if (!out)
	{
		FREE (w.buf);
		return ERR_OUT_OF_MEMORY;
	}
	memcpy (out, "RNC\1", 4);
	wr_be32 (out + 4, src_size);
	wr_be32 (out + 8, w.len);
	wr_be16 (out + 12, unpacked_crc);
	wr_be16 (out + 14, packed_crc);
	out[16] = 0;
	out[17] = (u8)blocks;
	memcpy (out + 18, w.buf, w.len);
	FREE (w.buf);
	*dest = out;
	*dest_size = 0x12 + w.len;
	return ERR_OK;
}

enumError EncodeRNC (u8 **dest, uint *dest_size, const u8 *src, uint src_size, int method)
{
	if (!dest || !dest_size)
		return ERR_SEMANTIC;
	*dest = 0;
	*dest_size = 0;
	if (!src && src_size)
		return ERR_SEMANTIC;
	if (method == 1)
		return rnc_encode_m1_literals (dest, dest_size, src, src_size);
	if (method != 2)
		return EINVAL;

	rnc_writer_t w;
	rnc_w_init (&w, src_size + 64);
	if (!w.buf)
		return ERR_OUT_OF_MEMORY;

	// init flags: locked=0, keyed=0
	rnc_w_put_bit (&w, 0);
	rnc_w_put_bit (&w, 0);

	int head[65536];
	memset (head, -1, sizeof (head));
	int *prev = src_size ? MALLOC (src_size * sizeof (int)) : 0;
	if (src_size && !prev)
	{
		FREE (w.buf);
		return ERR_OUT_OF_MEMORY;
	}

	uint p = 0;
	while (p < src_size)
	{
		uint best_len = 0, best_dist = 0;
		const uint max_l = (src_size - p > 263) ? 263 : (src_size - p);
		if (max_l >= 2)
		{
			u16 h = ((u16)src[p] << 8) | src[p + 1];
			int cand = head[h];
			uint chain = 64;
			while (cand >= 0 && chain-- > 0)
			{
				uint dist = p - cand;
				if (dist > 4095)
					break;
				uint l = 0;
				while (l < max_l && src[cand + l] == src[p + l])
					l++;
				if (l > best_len)
				{
					best_len = l;
					best_dist = dist;
					if (best_len == 263)
						break;
				}
				cand = prev[cand];
			}
		}

		if (best_len == 2 && best_dist <= 256)
		{
			rnc_w_put_bit (&w, 1);
			rnc_w_put_bit (&w, 1);
			rnc_w_put_bit (&w, 0);
			rnc_w_put_byte (&w, (u8)(best_dist - 1));
			for (uint i = 0; i < best_len; i++)
			{
				if (p + i + 1 < src_size)
				{
					u16 h = ((u16)src[p + i] << 8) | src[p + i + 1];
					prev[p + i] = head[h];
					head[h] = p + i;
				}
			}
			p += best_len;
		}
		else if (best_len == 3)
		{
			rnc_w_put_bit (&w, 1);
			rnc_w_put_bit (&w, 1);
			rnc_w_put_bit (&w, 1);
			rnc_w_put_bit (&w, 0);
			rnc_w_put_match_offset (&w, best_dist);
			for (uint i = 0; i < best_len; i++)
			{
				if (p + i + 1 < src_size)
				{
					u16 h = ((u16)src[p + i] << 8) | src[p + i + 1];
					prev[p + i] = head[h];
					head[h] = p + i;
				}
			}
			p += best_len;
		}
		else if (best_len >= 4 && best_len <= 8)
		{
			rnc_w_put_bit (&w, 1);
			rnc_w_put_bit (&w, 0);
			static const u8 cvals[] = { 0, 2, 2, 3, 6 };
			static const u8 cbits[] = { 2, 2, 3, 3, 3 };
			rnc_w_put_bits (&w, cvals[best_len - 4], cbits[best_len - 4]);
			rnc_w_put_match_offset (&w, best_dist);
			for (uint i = 0; i < best_len; i++)
			{
				if (p + i + 1 < src_size)
				{
					u16 h = ((u16)src[p + i] << 8) | src[p + i + 1];
					prev[p + i] = head[h];
					head[h] = p + i;
				}
			}
			p += best_len;
		}
		else if (best_len >= 9)
		{
			rnc_w_put_bit (&w, 1);
			rnc_w_put_bit (&w, 1);
			rnc_w_put_bit (&w, 1);
			rnc_w_put_bit (&w, 1);
			rnc_w_put_byte (&w, (u8)(best_len - 8));
			rnc_w_put_match_offset (&w, best_dist);
			for (uint i = 0; i < best_len; i++)
			{
				if (p + i + 1 < src_size)
				{
					u16 h = ((u16)src[p + i] << 8) | src[p + i + 1];
					prev[p + i] = head[h];
					head[h] = p + i;
				}
			}
			p += best_len;
		}
		else
		{
			rnc_w_put_bit (&w, 0);
			rnc_w_put_byte (&w, src[p]);
			if (p + 1 < src_size)
			{
				u16 h = ((u16)src[p] << 8) | src[p + 1];
				prev[p] = head[h];
				head[h] = p;
			}
			p++;
		}
	}

	if (prev)
		FREE (prev);

	// End of stream marker
	rnc_w_put_bit (&w, 1);
	rnc_w_put_bit (&w, 1);
	rnc_w_put_bit (&w, 1);
	rnc_w_put_bit (&w, 1);
	rnc_w_put_byte (&w, 0);
	rnc_w_put_bit (&w, 0);

	u16 unpacked_crc = 0;
	for (uint i = 0; i < src_size; i++)
		unpacked_crc = rnc_crc_table[(unpacked_crc ^ src[i]) & 0xFF] ^ (unpacked_crc >> 8);

	u16 packed_crc = 0;
	for (uint i = 0; i < w.len; i++)
		packed_crc = rnc_crc_table[(packed_crc ^ w.buf[i]) & 0xFF] ^ (packed_crc >> 8);

	uint out_total = 0x12 + w.len;
	u8 *out = MALLOC (out_total);
	if (!out)
	{
		FREE (w.buf);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (out, "RNC\x02", 4);
	out[0x04] = (u8)(src_size >> 24);
	out[0x05] = (u8)(src_size >> 16);
	out[0x06] = (u8)(src_size >> 8);
	out[0x07] = (u8)(src_size & 0xFF);
	out[0x08] = (u8)(w.len >> 24);
	out[0x09] = (u8)(w.len >> 16);
	out[0x0A] = (u8)(w.len >> 8);
	out[0x0B] = (u8)(w.len & 0xFF);
	out[0x0C] = (u8)(unpacked_crc >> 8);
	out[0x0D] = (u8)(unpacked_crc & 0xFF);
	out[0x0E] = (u8)(packed_crc >> 8);
	out[0x0F] = (u8)(packed_crc & 0xFF);
	out[0x10] = 0;
	out[0x11] = 1;
	memcpy (out + 0x12, w.buf, w.len);

	FREE (w.buf);
	*dest = out;
	*dest_size = out_total;
	return ERR_OK;
}


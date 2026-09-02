#include "lib-std.h"
#include "lib-ash0.h"
#include <string.h>
#include <errno.h>
#include <limits.h>

typedef struct ash_bits_t
{
	const u8 *src;
	uint size, pos, word, used;
} ash_bits_t;

static bool ash_feed (ash_bits_t *br)
{
	if (br->pos > br->size - 4)
		return false;
	br->word = rd_be32 (br->src + br->pos);
	br->pos += 4;
	br->used = 0;
	return true;
}

static bool ash_init (ash_bits_t *br, const u8 *src, uint size, uint pos)
{
	if (!br || pos > size)
		return false;
	br->src = src;
	br->size = size;
	br->pos = pos;
	br->word = br->used = 0;
	return ash_feed (br);
}

static bool ash_read (ash_bits_t *br, uint n, uint *value)
{
	if (!n || n > 24 || !value)
		return false;
	uint val = 0;
	while (n--)
	{
		val = val << 1 | br->word >> 31;
		if (++br->used == 32)
		{
			if (!ash_feed (br))
				return false;
		}
		else
			br->word <<= 1;
	}
	*value = val;
	return true;
}

static bool ash_tree (ash_bits_t *br, uint width, uint *left, uint *right, uint *root)
{
	const uint max = 1u << width, cap = 2 * max - 1;
	uint work[2 * 2048], work_used = 0, nodes = 0, next = max;
	for (;;)
	{
		uint bit, value;
		if (!ash_read (br, 1, &bit))
			return false;
		if (bit)
		{
			if (work_used + 2 > sizeof (work) / sizeof (*work) || next >= cap)
				return false;
			work[work_used++] = next | 0x80000000u;
			work[work_used++] = next | 0x40000000u;
			nodes += 2;
			next++;
			continue;
		}
		if (!ash_read (br, width, &value) || value >= max)
			return false;
		*root = value;
		while (nodes)
		{
			const uint node = work[--work_used], index = node & 0x3fffffffu;
			if (index >= cap)
				return false;
			nodes--;
			if (node & 0x80000000u)
			{
				right[index] = *root;
				*root = index;
			}
			else
			{
				left[index] = *root;
				break;
			}
		}
		if (!nodes)
			return true;
	}
}

static bool ash_symbol (
	ash_bits_t *br, uint root, uint max, const uint *left, const uint *right, uint *value)
{
	uint sym = root;
	while (sym >= max)
	{
		uint bit;
		if (sym >= 2 * max - 1 || !ash_read (br, 1, &bit))
			return false;
		sym = bit ? right[sym] : left[sym];
	}
	*value = sym;
	return true;
}

// ASH0's distance-tree bit width is a build-time choice baked into the
// encoder, not a field in the file header -- confirmed against
// NinjaCheetah/ASH0-tools (Decompressor/main.c), a from-scratch clean-room
// ASH0 codec whose CLI exposes it as a manual `-d` flag defaulting to 11
// ("These work for ASH0 files found in the System Menu and Animal Crossing:
// City Folk. ASH0 files found in My Pokémon Ranch require setting the
// distance tree bits to 15 instead.") -- there is no header bit to switch
// on. Since a real file gives no way to know up front, try the common case
// first and fall back to the one confirmed exception on failure, rather
// than guess a detection rule with no evidence behind it.
static enumError DecodeASH0Try (
	u8 **dest, uint *dest_size, const u8 *src, uint src_size, uint dist_bits)
{
	const uint out_size = rd_be32 (src + 4) & 0x00ffffff;
	const uint dist_start = rd_be32 (src + 8);
	enumError err = AllocOutput (dest, dest_size, out_size);
	if (err)
		return err;
	ash_bits_t syms, dists;
	const uint sym_max = 1u << 9, dist_max = 1u << dist_bits;
	uint *sl = CALLOC (2 * sym_max - 1, sizeof (*sl)), *sr = CALLOC (2 * sym_max - 1, sizeof (*sr));
	uint *dl = CALLOC (2 * dist_max - 1, sizeof (*dl)),
		 *dr = CALLOC (2 * dist_max - 1, sizeof (*dr));
	uint sym_root = 0, dist_root = 0;
	if (!sl || !sr || !dl || !dr || !ash_init (&syms, src, src_size, 0x0c)
		|| !ash_init (&dists, src, src_size, dist_start) || !ash_tree (&syms, 9, sl, sr, &sym_root)
		|| !ash_tree (&dists, dist_bits, dl, dr, &dist_root))
		goto invalid;
	for (uint pos = 0; pos < out_size;)
	{
		uint sym;
		if (!ash_symbol (&syms, sym_root, sym_max, sl, sr, &sym))
			goto invalid;
		if (sym < 0x100)
			(*dest)[pos++] = sym;
		else
		{
			uint distance;
			const uint len = sym - 0x100 + 3;
			if (!ash_symbol (&dists, dist_root, dist_max, dl, dr, &distance) || distance >= pos
				|| len > out_size - pos)
				goto invalid;
			for (uint n = 0; n < len; n++)
				(*dest)[pos + n] = (*dest)[pos - distance - 1 + n];
			pos += len;
		}
	}
	FREE (sl);
	FREE (sr);
	FREE (dl);
	FREE (dr);
	return ERR_OK;
invalid:
	FREE (sl);
	FREE (sr);
	FREE (dl);
	FREE (dr);
	FREE (*dest);
	*dest = 0;
	*dest_size = 0;
	return EINVAL;
}

enumError DecodeASH0 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!src || src_size < 0x10 || memcmp (src, "ASH0", 4))
		return EINVAL;
	const uint out_size = rd_be32 (src + 4) & 0x00ffffff;
	const uint dist_start = rd_be32 (src + 8);
	if (!out_size || out_size > NFMT_MAX_OUTPUT || dist_start > src_size - 4)
		return EINVAL;

	enumError err = DecodeASH0Try (dest, dest_size, src, src_size, 11);
	if (err)
		err = DecodeASH0Try (dest, dest_size, src, src_size, 15);
	return err;
}

typedef struct ash_writer_t
{
	u8 *data;
	uint size, bitpos;
} ash_writer_t;

static bool ash_write (ash_writer_t *bw, uint value, uint n)
{
	if (!n || n > 24 || bw->bitpos > bw->size * 8 - n)
		return false;
	while (n--)
	{
		if (value & (1u << n))
			bw->data[bw->bitpos / 8] |= 0x80 >> (bw->bitpos & 7);
		bw->bitpos++;
	}
	return true;
}

static bool ash_write_symbol_tree (ash_writer_t *bw, uint depth, uint value)
{
	if (depth == 9)
		return ash_write (bw, 0, 1) && ash_write (bw, value, 9);
	return ash_write (bw, 1, 1) && ash_write_symbol_tree (bw, depth + 1, value << 1)
		&& ash_write_symbol_tree (bw, depth + 1, value << 1 | 1);
}

enumError EncodeASH0 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	// A full 9-bit literal tree keeps this initial encoder simple and fully
	// interoperable. A future optimiser can replace it with a frequency tree
	// without changing the decoder or on-disk framing.
	if (!dest || !dest_size || !src || !src_size || src_size > 0x00ffffff)
		return EINVAL;
	const u64 sym_bits = 5631ull + 9ull * src_size;
	const u64 sym_size = (sym_bits + 7) / 8, dist_off = 12 + ((sym_size + 3) & ~3ull);
	const u64 total = dist_off + 4;
	if (total > NFMT_MAX_OUTPUT || total > UINT_MAX)
		return EFBIG;
	u8 *out = CALLOC (1, total);
	if (!out)
		return ERR_CANT_CREATE;
	memcpy (out, "ASH0", 4);
	wr_be32 (out + 4, src_size);
	wr_be32 (out + 8, dist_off);
	ash_writer_t bw = { out + 12, (uint)sym_size, 0 };
	if (!ash_write_symbol_tree (&bw, 0, 0))
		goto invalid_ash_encode;
	for (uint i = 0; i < src_size; i++)
		if (!ash_write (&bw, src[i], 9))
			goto invalid_ash_encode;
	bw.data = out + dist_off;
	bw.size = 4;
	bw.bitpos = 0;
	if (!ash_write (&bw, 0, 1) || !ash_write (&bw, 0, 11))
		goto invalid_ash_encode;
	*dest = out;
	*dest_size = total;
	return ERR_OK;
invalid_ash_encode:
	FREE (out);
	return EFBIG;
}


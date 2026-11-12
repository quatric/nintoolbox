// SPDX-License-Identifier: GPL-2.0+
// Rhythm Heaven Groove (Switch) sound, tempo map, texture and sprite files; see lib-rhgroove.h.

#include "lib-rhgroove.h"
#include "lib-bntx.h"
#include "lib-sbuf.h"
#include "bcn-decoder/bcn_wrapper.h"

static u32 rd32 (const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static u16 rd16 (const u8 *p)
{
	return (u16)(p[0] | p[1] << 8);
}

//-----------------------------------------------------------------------------
// CWV

#define CWV_FOOTER 0x100

// Delta for byte values 0..127; 128 is -32768 and 129..255 mirror 127..1 negated.
static const s16 cwv_delta[128] = { 0, 2, 4, 8, 13, 19, 27, 36, 47, 60, 75, 92, 112, 133, 157, 183,
	212, 244, 278, 315, 355, 398, 444, 493, 546, 601, 660, 722, 788, 857, 929, 1006, 1086, 1169,
	1257, 1348, 1443, 1542, 1645, 1753, 1864, 1980, 2099, 2223, 2352, 2484, 2621, 2763, 2909, 3060,
	3215, 3375, 3539, 3709, 3883, 4062, 4246, 4435, 4628, 4827, 5031, 5240, 5454, 5673, 5897, 6127,
	6362, 6602, 6848, 7099, 7356, 7618, 7885, 8158, 8437, 8722, 9012, 9307, 9609, 9916, 10229,
	10548, 10873, 11204, 11541, 11884, 12233, 12587, 12948, 13316, 13689, 14069, 14454, 14846,
	15245, 15649, 16061, 16478, 16902, 17332, 17769, 18213, 18663, 19119, 19583, 20052, 20529,
	21012, 21502, 21999, 22503, 23013, 23531, 24055, 24586, 25124, 25670, 26222, 26781, 27347,
	27921, 28501, 29089, 29684, 30286, 30896, 31512, 32136 };

static int cwv_step (u8 b)
{
	if (b < 128)
		return cwv_delta[b];
	if (b == 128)
		return -32768;
	return -cwv_delta[256 - b];
}

bool IsRHGrooveCwv (const u8 *d, size_t size)
{
	if (!d || size <= CWV_FOOTER)
		return false;
	const u8 *f = d + size - CWV_FOOTER;
	const u32 codec = rd32 (f), rate = rd32 (f + 4), frames = rd32 (f + 8);
	const u64 ls = rd32 (f + 0x10), le = ls + rd32 (f + 0x14);
	if ((codec != 2 && codec != 3) || rate < 4000 || rate > 192000 || !frames)
		return false;
	const uint ch = codec == 3 ? 2 : 1;
	return (u64)frames * ch <= size - CWV_FOOTER && ls <= le && le <= frames;
}

enumError DecodeRHGrooveCwv (const u8 *d, size_t size, s16 **pcm, uint *channels, uint *rate,
	uint *frames, uint *loop_start, uint *loop_end)
{
	*pcm = 0;
	if (!IsRHGrooveCwv (d, size))
		return ERR_INVALID_DATA;
	const u8 *f = d + size - CWV_FOOTER;
	const uint ch = rd32 (f) == 3 ? 2 : 1, n = rd32 (f + 8);
	s16 *out = MALLOC ((size_t)n * ch * sizeof (s16));
	if (!out)
		return ERR_OUT_OF_MEMORY;
	for (uint c = 0; c < ch; c++)
	{
		s16 acc = 0; // the game sums in a 16-bit register, so it wraps
		for (uint i = 0; i < n; i++)
		{
			const size_t k = (size_t)i * ch + c;
			acc = (s16)(acc + cwv_step (d[k]));
			out[k] = acc;
		}
	}
	*pcm = out;
	*channels = ch;
	*rate = rd32 (f + 4);
	*frames = n;
	*loop_start = rd32 (f + 0x10);
	*loop_end = *loop_start + rd32 (f + 0x14);
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// WMB

bool IsRHGrooveWmb (const u8 *d, size_t size)
{
	if (!d || size < 0x18 + 8 + 4 || (size - 0x18 - 4) % 8 || rd32 (d) || rd32 (d + size - 4))
		return false;
	const u32 rate = rd32 (d + 4);
	if (rate < 4000 || rate > 192000 || rd32 (d + 0x14))
		return false;
	u32 prev = 0;
	for (size_t o = 0x18; o + 4 < size; o += 8)
	{
		const u32 end = rd32 (d + o + 4);
		if (end < prev)
			return false;
		prev = end;
	}
	return true;
}

enumError ConvertRHGrooveWmbToYaml (const u8 *d, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	*text_size = 0;
	if (!IsRHGrooveWmb (d, size))
		return ERR_INVALID_DATA;
	const u32 rate = rd32 (d + 4);
	sbuf_t b = { 0 };
	sb_puts (&b, "# Rhythm Heaven Groove wave mark: tempo map of the .cwv of the same name\n");
	sb_printf (&b, "sample-rate: %u\nticks-per-beat: 480\n", rate);
	sb_printf (&b, "unknown-08: %d\nunknown-0c: %u\nunknown-10: %u\nsegments:\n", (s32)rd32 (d + 8),
		rd32 (d + 0x0c), rd32 (d + 0x10));
	u64 tick = 0;
	u32 start = 0;
	for (size_t o = 0x18; o + 4 < size && !b.fail; o += 8)
	{
		const u32 ticks = rd32 (d + o), end = rd32 (d + o + 4);
		sb_printf (&b,
			"  - {start-tick: %llu, ticks: %u, beats: %g, start-sample: %u, end-sample: %u",
			(unsigned long long)tick, ticks, ticks / 480.0, start, end);
		if (ticks && end > start)
			sb_printf (&b, ", bpm: %.3f", 60.0 * rate * ticks / 480.0 / (end - start));
		sb_puts (&b, "}\n");
		tick += ticks;
		start = end;
	}
	return sb_finish (&b, text, text_size);
}

//-----------------------------------------------------------------------------
// TEX

#define TEX_TRAILER 0x1000
#define TEX_MAX_DIM 16384

static uint tex_block_height (uint gob_rows)
{
	uint bh = 1;
	while (bh < 16 && bh * 2 <= gob_rows)
		bh *= 2;
	return bh;
}

static bool tex_format (uint fmt, uint *blk, uint *bpp)
{
	if (fmt == 101) // BC7
		*blk = 4, *bpp = 16;
	else if (fmt == 99) // RGBA8
		*blk = 1, *bpp = 4;
	else
		return false;
	return true;
}

bool IsRHGrooveTex (const u8 *d, size_t size)
{
	if (!d || size <= TEX_TRAILER)
		return false;
	const u8 *t = d + size - TEX_TRAILER;
	const uint fmt = rd16 (t), w = rd16 (t + 4), h = rd16 (t + 6), mips = rd16 (t + 8);
	uint blk, bpp;
	if (!tex_format (fmt, &blk, &bpp) || !w || !h || w > TEX_MAX_DIM || h > TEX_MAX_DIM || !mips
		|| mips > 16)
		return false;
	for (uint i = 10; i < 16; i++)
		if (t[i])
			return false;
	u64 total = 0;
	for (uint l = 0; l < mips; l++)
	{
		const uint lw = w >> l ? w >> l : 1, lh = h >> l ? h >> l : 1;
		const uint wb = (lw + blk - 1) / blk, hb = (lh + blk - 1) / blk;
		const uint gob_cols = (wb * bpp + 63) / 64, gob_rows = (hb + 7) / 8;
		const uint bh = tex_block_height (gob_rows);
		total += (u64)gob_cols * ((gob_rows + bh - 1) / bh) * bh * 512;
	}
	return total == size - TEX_TRAILER;
}

enumError DecodeRHGrooveTex (const u8 *d, size_t size, u8 **rgba, uint *width, uint *height)
{
	*rgba = 0;
	if (!IsRHGrooveTex (d, size))
		return ERR_INVALID_DATA;
	const u8 *t = d + size - TEX_TRAILER;
	const uint fmt = rd16 (t), w = rd16 (t + 4), h = rd16 (t + 6);
	uint blk, bpp;
	tex_format (fmt, &blk, &bpp);
	const uint wb = (w + blk - 1) / blk, hb = (h + blk - 1) / blk;
	uint bh_log2 = 0;
	for (uint bh = tex_block_height ((hb + 7) / 8); bh > 1; bh >>= 1)
		bh_log2++;
	u8 *linear = 0;
	uint linear_size = 0;
	enumError err = BntxDeswizzle (&linear, &linear_size, d, (uint)(size - TEX_TRAILER), w, h, blk,
		blk, bpp, 0, bh_log2, false);
	if (err)
		return ERR_INVALID_DATA;
	u8 *out = MALLOC ((size_t)w * h * 4);
	if (!out)
	{
		FREE (linear);
		return ERR_OUT_OF_MEMORY;
	}
	if (fmt == 99)
		memcpy (out, linear, (size_t)w * h * 4);
	else
	{
		const uint pw = wb * 4, ph = hb * 4;
		u8 *full = pw == w && ph == h ? out : MALLOC ((size_t)pw * ph * 4);
		if (!full || !szs_decode_bc7 (linear, pw, ph, full))
		{
			if (full != out)
				FREE (full);
			FREE (out);
			FREE (linear);
			return ERR_INVALID_DATA;
		}
		if (full != out)
		{
			for (uint y = 0; y < h; y++)
				memcpy (out + (size_t)y * w * 4, full + (size_t)y * pw * 4, (size_t)w * 4);
			FREE (full);
		}
	}
	FREE (linear);
	*rgba = out;
	*width = w;
	*height = h;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// SPR

#define SPR_COUNTS 14
#define SPR_SECTIONS 27
#define SPR_HEADER (0x38 + 8 * SPR_SECTIONS)

bool IsRHGrooveSpr (const u8 *d, size_t size)
{
	if (!d || size < SPR_HEADER || rd32 (d) > 1)
		return false;
	u64 prev = SPR_HEADER;
	for (uint i = 0; i < SPR_SECTIONS; i++)
	{
		const u64 o = (u64)rd32 (d + 0x38 + 8 * i) | (u64)rd32 (d + 0x3c + 8 * i) << 32;
		if (o < prev || o > size)
			return false;
		prev = o;
	}
	const u64 s0 = rd32 (d + 0x38), s1 = rd32 (d + 0x40);
	return s0 == SPR_HEADER && s1 - s0 == 16ull * rd32 (d + 4);
}

enumError ConvertRHGrooveSprToYaml (const u8 *d, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	*text_size = 0;
	if (!IsRHGrooveSpr (d, size))
		return ERR_INVALID_DATA;
	sbuf_t b = { 0 };
	sb_puts (&b, "# Rhythm Heaven Groove sprite set; rectangles index the .tex of the same name\n");
	sb_printf (&b, "flags: %u\ncounts: [", rd32 (d));
	for (uint i = 1; i < SPR_COUNTS; i++)
		sb_printf (&b, "%s%u", i > 1 ? ", " : "", rd32 (d + 4 * i));
	sb_puts (&b, "]\nsection-offsets: [");
	for (uint i = 0; i < SPR_SECTIONS; i++)
		sb_printf (&b, "%s0x%x", i ? ", " : "", rd32 (d + 0x38 + 8 * i));
	sb_puts (&b, "]\nrectangles:\n");
	const uint n = rd32 (d + 4), s0 = rd32 (d + 0x38);
	for (uint i = 0; i < n && !b.fail; i++)
	{
		const u8 *r = d + s0 + 16 * i;
		sb_printf (&b, "  - [%d, %d, %d, %d]  # %u: x, y, width, height\n", (s32)rd32 (r),
			(s32)rd32 (r + 4), (s32)rd32 (r + 8), (s32)rd32 (r + 12), i);
	}
	return sb_finish (&b, text, text_size);
}

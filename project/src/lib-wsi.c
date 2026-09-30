// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// ".wsi" blocked DSP-ADPCM streams; see lib-wsi.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-wsi.h"
#include "lib-std.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define WSI_BLOCK_HEAD 0x10
#define WSI_DSP_HEAD 0x60
#define WSI_MAX_CHANNELS 8

static u32 ws_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

// Walk the block list once: every set must have equal-sized, alternating
// channel blocks. Returns the channel count, or 0 if this is not a .wsi.
static uint wsi_probe (const u8 *d, size_t size, u32 *start, u32 *n_sets)
{
	if (!d || size < 0x20 + WSI_BLOCK_HEAD + WSI_DSP_HEAD)
		return 0;
	const u32 st = ws_be32 (d), ch = ws_be32 (d + 4);
	if (st < 8 || st > 0x1000 || ch < 1 || ch > WSI_MAX_CHANNELS)
		return 0;

	u64 off = st;
	u32 sets = 0;
	while (off + WSI_BLOCK_HEAD <= size)
	{
		const u32 bs = ws_be32 (d + off);
		if (bs < WSI_BLOCK_HEAD + (sets ? 0 : WSI_DSP_HEAD) || bs > 0x100000)
			break;
		if (off + (u64)bs * ch > size)
			break;
		for (u32 c = 0; c < ch; c++)
		{
			const u8 *b = d + off + (u64)bs * c;
			if (ws_be32 (b) != bs || ws_be32 (b + 8) != c + 1)
				return 0;
		}
		off += (u64)bs * ch;
		sets++;
	}
	if (sets < 2 || off != size)
		return 0;
	*start = st;
	*n_sets = sets;
	return ch;
}

bool IsWsi (const u8 *data, size_t size)
{
	u32 st, sets;
	return wsi_probe (data, size, &st, &sets) != 0;
}

enumError ScanWsi (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *d, size_t size)
{
	u32 start, sets;
	const uint ch = wsi_probe (d, size, &start, &sets);
	if (!entries || !n_entries || !ch)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	nintendo_sarc_entry_t *v = CALLOC (ch, sizeof (*v));
	if (!v)
		return ERR_OUT_OF_MEMORY;

	const u32 bs0 = ws_be32 (d + start);
	uint made = 0;
	for (uint c = 0; c < ch; c++)
	{
		// header from this channel's first block
		const u8 *h = d + start + (u64)bs0 * c + WSI_BLOCK_HEAD;

		u64 adpcm = 0;
		off_t off = start;
		for (u32 s = 0; s < sets; s++)
		{
			const u32 bs = ws_be32 (d + off);
			adpcm += bs - WSI_BLOCK_HEAD - (s ? 0 : WSI_DSP_HEAD);
			off += (off_t)bs * ch;
		}
		if (!adpcm || adpcm > 0x7fffffff - WSI_DSP_HEAD)
			continue;

		u8 *dsp = MALLOC (WSI_DSP_HEAD + adpcm);
		if (!dsp)
			continue;
		memcpy (dsp, h, WSI_DSP_HEAD);
		u64 used = 0;
		off = start;
		for (u32 s = 0; s < sets; s++)
		{
			const u32 bs = ws_be32 (d + off);
			const u8 *b = d + off + (u64)bs * c + WSI_BLOCK_HEAD + (s ? 0 : WSI_DSP_HEAD);
			const u32 n = bs - WSI_BLOCK_HEAD - (s ? 0 : WSI_DSP_HEAD);
			memcpy (dsp + WSI_DSP_HEAD + used, b, n);
			used += n;
			off += (off_t)bs * ch;
		}

		// The blocked header need not hold a valid start address / first
		// predictor byte; a plain .dsp decoder wants both.
		if (!ws_be32 (dsp + 0x18))
			dsp[0x1b] = 2;
		dsp[0x3e] = 0;
		dsp[0x3f] = dsp[WSI_DSP_HEAD];

		char name[32];
		snprintf (name, sizeof (name), ch == 1 ? "stream.dsp" : "ch%u.dsp", c);
		v[made].name = STRDUP (name);
		v[made].data = dsp;
		v[made].size = (uint)(WSI_DSP_HEAD + adpcm);
		made++;
	}
	if (!made)
	{
		FREE (v);
		return ERR_NOTHING_TO_DO;
	}
	*entries = v;
	*n_entries = made;
	return ERR_OK;
}

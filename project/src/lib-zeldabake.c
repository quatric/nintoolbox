// SPDX-License-Identifier: GPL-2.0+
// Switch 2 Zelda small data tables (BKLT, bbox, flnt) to YAML; see lib-zeldabake.h.

#include "lib-zeldabake.h"
#include "lib-sbuf.h"

static u32 zb32 (const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static float zbf (const u8 *p)
{
	const u32 v = zb32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static void zb_num (sbuf_t *b, float f)
{
	if (f != f)
		sb_puts (b, ".nan");
	else if (f == (float)(s32)f && f > -1e9f && f < 1e9f)
		sb_printf (b, "%d", (s32)f);
	else
		sb_printf (b, "%.9g", f);
}

// "[a, b, c]" of N floats starting at P
static void zb_vec (sbuf_t *b, const u8 *p, uint n)
{
	sb_puts (b, "[");
	for (uint i = 0; i < n; i++)
	{
		if (i)
			sb_puts (b, ", ");
		zb_num (b, zbf (p + 4 * i));
	}
	sb_puts (b, "]");
}

//-----------------------------------------------------------------------------

bool IsMcpk (const u8 *d, size_t size)
{
	return d && size >= 0x40 && !memcmp (d, "MCPK", 4) && !memcmp (d + 0x1c, "FMSH", 4)
		&& (u64)0x1c + zb32 (d + 12) + zb32 (d + 20) == size;
}

enumError ConvertMcpkToYaml (const u8 *d, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	*text_size = 0;
	if (!IsMcpk (d, size))
		return ERR_INVALID_DATA;
	sbuf_t b = { 0 };
	sb_puts (&b, "# Nintendo mesh codec package (MCPK); container only, payload not decoded\n");
	sb_printf (&b, "version: {micro: %u, minor: %u, major: %u}\n", d[4], d[5], (uint)(d[6] | d[7] << 8));
	sb_printf (&b, "flags: 0x%x\n", zb32 (d + 8));
	sb_printf (&b, "section_a: {offset: 0x1c, size: %u}\n", zb32 (d + 12));
	sb_printf (&b, "x: 0x%x\n", zb32 (d + 16));
	sb_printf (&b, "section_b: {offset: 0x%x, size: %u}\n", 0x1c + zb32 (d + 12), zb32 (d + 20));
	sb_printf (&b, "tag: 0x%x\n", zb32 (d + 24));
	sb_puts (&b, "fmsh:\n");
	sb_printf (&b, "  version: 0x%x\n", zb32 (d + 0x20));
	sb_printf (&b, "  work_mem_size: %u\n", zb32 (d + 0x24));
	sb_printf (&b, "  output_size_0: %u\n", zb32 (d + 0x28));
	sb_printf (&b, "  output_size_1: %u\n", zb32 (d + 0x2c));
	sb_printf (&b, "  field_14: 0x%x\n", zb32 (d + 0x30));
	return sb_finish (&b, text, text_size);
}

bool IsBklt (const u8 *d, size_t size)
{
	if (!d || size < 0x14 || memcmp (d, "BKLT", 4) || zb32 (d + 4) != 1)
		return false;
	const u64 need = 0x14 + (u64)zb32 (d + 8) * 28 + (u64)zb32 (d + 12) * 52 + (u64)zb32 (d + 16) * 60;
	return need == size;
}

enumError ConvertBkltToYaml (const u8 *d, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	*text_size = 0;
	if (!IsBklt (d, size))
		return ERR_INVALID_DATA;
	sbuf_t b = { 0 };
	const u32 n[3] = { zb32 (d + 8), zb32 (d + 12), zb32 (d + 16) };
	sb_puts (&b, "# Nintendo baked light list (BKLT version 1)\n");
	const u8 *p = d + 0x14;

	sb_printf (&b, "point_lights: # %u\n", n[0]);
	for (u32 i = 0; i < n[0]; i++, p += 28)
	{
		sb_puts (&b, "  - {position: ");
		zb_vec (&b, p, 3);
		sb_puts (&b, ", color: ");
		zb_vec (&b, p + 12, 3);
		sb_puts (&b, ", scale: ");
		zb_num (&b, zbf (p + 24));
		sb_puts (&b, "}\n");
	}
	sb_printf (&b, "spot_lights: # %u\n", n[1]);
	for (u32 i = 0; i < n[1]; i++, p += 52)
	{
		sb_puts (&b, "  - {position: ");
		zb_vec (&b, p, 3);
		sb_puts (&b, ", direction: ");
		zb_vec (&b, p + 12, 3);
		sb_puts (&b, ", color: ");
		zb_vec (&b, p + 24, 3);
		sb_puts (&b, ", falloff: ");
		zb_vec (&b, p + 36, 2);
		sb_puts (&b, ", intensity: ");
		zb_num (&b, zbf (p + 44));
		sb_puts (&b, ", scale: ");
		zb_num (&b, zbf (p + 48));
		sb_puts (&b, "}\n");
	}
	sb_printf (&b, "area_lights: # %u\n", n[2]);
	for (u32 i = 0; i < n[2]; i++, p += 60)
	{
		sb_puts (&b, "  - {position: ");
		zb_vec (&b, p, 3);
		sb_puts (&b, ", direction: ");
		zb_vec (&b, p + 12, 3);
		sb_puts (&b, ", up: ");
		zb_vec (&b, p + 24, 3);
		sb_puts (&b, ", color: ");
		zb_vec (&b, p + 36, 3);
		sb_puts (&b, ", size: ");
		zb_vec (&b, p + 48, 2);
		sb_puts (&b, ", intensity: ");
		zb_num (&b, zbf (p + 56));
		sb_puts (&b, "}\n");
	}
	return sb_finish (&b, text, text_size);
}

//-----------------------------------------------------------------------------

bool IsCullBoxes (const u8 *d, size_t size)
{
	return d && size >= 8 && zb32 (d) == 2 && (u64)zb32 (d + 4) * 44 + 8 == size;
}

enumError ConvertCullBoxesToYaml (const u8 *d, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	*text_size = 0;
	if (!IsCullBoxes (d, size))
		return ERR_INVALID_DATA;
	sbuf_t b = { 0 };
	const u32 n = zb32 (d + 4);
	sb_printf (&b, "# Nintendo culling boxes (version 2)\nboxes: # %u\n", n);
	for (u32 i = 0; i < n; i++)
	{
		const u8 *p = d + 8 + 44 * (size_t)i;
		sb_printf (&b, "  - {id: 0x%08x, flags: 0x%08x, center: ", zb32 (p), zb32 (p + 4));
		zb_vec (&b, p + 8, 3);
		if (zb32 (p + 20) | zb32 (p + 24) | zb32 (p + 28))
		{
			sb_puts (&b, ", reserved: ");
			zb_vec (&b, p + 20, 3);
		}
		sb_puts (&b, ", size: ");
		zb_vec (&b, p + 32, 3);
		sb_puts (&b, "}\n");
	}
	return sb_finish (&b, text, text_size);
}

//-----------------------------------------------------------------------------

static bool flnt_impostor (const u8 *d, size_t size)
{
	if (size < 16 || zb32 (d) != 2 || zb32 (d + 4) != 0)
		return false;
	const u64 n = zb32 (d + 8), r = zb32 (d + 12);
	return n && n < 100000 && 16 + n * 272 + r * 80 == size;
}

static bool flnt_tree (const u8 *d, size_t size)
{
	return size == 1168 && d[0] <= 32 && d[1] <= 64 && d[2] == 0 && d[3] == 1;
}

bool IsFoliageTable (const u8 *d, size_t size)
{
	return d && (flnt_impostor (d, size) || flnt_tree (d, size));
}

static void flnt_words (sbuf_t *b, const u8 *p, uint n)
{
	sb_puts (b, "[");
	for (uint i = 0; i < n; i++)
		sb_printf (b, "%s0x%02x", i ? ", " : "", p[i]);
	sb_puts (b, "]");
}

enumError ConvertFoliageTableToYaml (const u8 *d, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	*text_size = 0;
	sbuf_t b = { 0 };
	if (flnt_impostor (d, size))
	{
		const u32 n = zb32 (d + 8), r = zb32 (d + 12);
		const u8 *names = d + 16, *rng = names + 256 * (size_t)n, *rec = rng + 16 * (size_t)n;
		sb_printf (&b, "# Nintendo foliage impostor info (FLNT version 2)\nentries: # %u\n", n);
		for (u32 i = 0; i < n; i++)
		{
			const uint len = names[256 * (size_t)i];
			sb_puts (&b, "  - name: ");
			sb_quote (&b, (ccp)names + 256 * (size_t)i + 1, len < 255 ? len : 255);
			sb_printf (&b, "\n    first: %u\n    count: %u\n    kind: %u\n", zb32 (rng + 16 * (size_t)i),
				zb32 (rng + 16 * (size_t)i + 4), zb32 (rng + 16 * (size_t)i + 8));
		}
		sb_printf (&b, "frames: # %u\n", r);
		for (u32 i = 0; i < r; i++)
		{
			sb_puts (&b, "  - ");
			zb_vec (&b, rec + 80 * (size_t)i, 20);
			sb_puts (&b, "\n");
		}
	}
	else if (flnt_tree (d, size))
	{
		sb_puts (&b, "# Nintendo foliage node tree (FLNT, semantics of the node fields not decoded)\n");
		sb_printf (&b, "nodes: %u\nrecords: %u\nvalues: ", d[0], d[1]);
		zb_vec (&b, d + 4, 2);
		sb_puts (&b, "\nnode_list: # flags, packed bytes\n");
		for (uint i = 0; i < d[0]; i++)
		{
			const u8 *p = d + 12 + 12 * i;
			sb_printf (&b, "  - {flags: 0x%08x, packed: ", zb32 (p));
			flnt_words (&b, p + 4, 8);
			sb_puts (&b, "}\n");
		}
		sb_puts (&b, "record_list: # index, packed bytes\n");
		for (uint i = 0; i < d[1]; i++)
		{
			const u8 *p = d + 0x190 + 12 * i;
			sb_printf (&b, "  - {index: %u, packed: ", p[0]);
			flnt_words (&b, p + 4, 8);
			sb_puts (&b, "}\n");
		}
	}
	else
		return ERR_INVALID_DATA;
	return sb_finish (&b, text, text_size);
}

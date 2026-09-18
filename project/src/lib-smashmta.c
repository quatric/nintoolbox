// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 material animation (.mta, magic "MTA4").
//
// Reference: KillzXGaming/Smash-Forge, "Smash Forge/Filetypes/Animation/MTA.cs"
// (MIT licensed; clean-room C port of the binary layout).
//
// Big-endian. Header: "MTA4", u32 unknown, frameCount, startFrame,
// endFrame, frameRate, s32 matCount, s32 matOffset, s32 visCount,
// s32 visOffset. matOffset/visOffset point at arrays of absolute entry
// offsets. A material entry holds a name offset, two hashes, a property
// table (each property: name + per-frame float vectors) and an optional
// PAT0 texture-pattern block; a visibility entry holds per-frame
// show/hide keyframes.

#include "lib-smashmta.h"
#include "lib-std.h"
#include <string.h>

static float smashmta_rd_f32 (const u8 *p)
{
	u32 v = rd_be32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static bool smashmta_str_ok (const u8 *data, size_t size, u32 off)
{
	return off < size && memchr (data + off, 0, size - off) != 0;
}

static bool smashmta_pat_ok (const u8 *data, size_t size, u32 pat_off)
{
	if ((size_t)pat_off + 4 > size)
		return false;
	const u32 pat_data = rd_be32 (data + pat_off);
	if (!pat_data)
		return true;
	if ((size_t)pat_data + 20 > size)
		return false;
	const u32 kcount = rd_be32 (data + pat_data + 4);
	const u32 koff = rd_be32 (data + pat_data + 8);
	return (size_t)koff + (size_t)kcount * 8 <= size;
}

static bool smashmta_prop_ok (const u8 *data, size_t size, u32 prop_off)
{
	if ((size_t)prop_off + 20 > size)
		return false;
	const u32 name_off = rd_be32 (data + prop_off);
	const u32 vcount = rd_be32 (data + prop_off + 8);
	const u32 fcount = rd_be32 (data + prop_off + 12);
	const u32 data_off = rd_be32 (data + prop_off + 18);
	if (!smashmta_str_ok (data, size, name_off))
		return false;
	if (vcount > 1000 || fcount > 100000)
		return false;
	return (size_t)data_off + (size_t)fcount * vcount * 4 <= size;
}

static bool smashmta_mat_ok (const u8 *data, size_t size, u32 mat_off)
{
	if ((size_t)mat_off + 32 > size)
		return false;
	const u32 name_off = rd_be32 (data + mat_off);
	const u32 pcount = rd_be32 (data + mat_off + 8);
	const u32 ppos = rd_be32 (data + mat_off + 12);
	const u32 has_pat = data[mat_off + 16];
	const u32 pat_off = rd_be32 (data + mat_off + 20);
	const u32 name2_off = rd_be32 (data + mat_off + 24);
	if (!smashmta_str_ok (data, size, name_off))
		return false;
	if (name2_off && !smashmta_str_ok (data, size, name2_off))
		return false;
	if (pcount > 10000 || (size_t)ppos + (size_t)pcount * 4 > size)
		return false;
	for (u32 i = 0; i < pcount; i++)
	{
		const u32 prop_off = rd_be32 (data + ppos + (size_t)i * 4);
		if (!smashmta_prop_ok (data, size, prop_off))
			return false;
	}
	if (has_pat && !smashmta_pat_ok (data, size, pat_off))
		return false;
	return true;
}

static bool smashmta_vis_ok (const u8 *data, size_t size, u32 vis_off)
{
	if ((size_t)vis_off + 12 > size)
		return false;
	const u32 name_off = rd_be32 (data + vis_off);
	const u32 data_off = rd_be32 (data + vis_off + 8);
	if (!smashmta_str_ok (data, size, name_off))
		return false;
	if ((size_t)data_off + 12 > size)
		return false;
	const u32 kcount = (u16)rd_be16 (data + data_off + 8);
	const u32 koff = rd_be32 (data + data_off + 10);
	return (size_t)koff + (size_t)kcount * 4 <= size;
}

static bool smashmta_layout_ok (const u8 *data, size_t size,
	u32 *mat_count, u32 *mat_off, u32 *vis_count, u32 *vis_off)
{
	if (!data || size < 44 || memcmp (data, "MTA4", 4))
		return false;
	*mat_count = rd_be32 (data + 24);
	*mat_off = rd_be32 (data + 28);
	*vis_count = rd_be32 (data + 32);
	*vis_off = rd_be32 (data + 36);
	if (*mat_count > 100000 || *vis_count > 100000)
		return false;
	if (*mat_count && ((size_t)*mat_off + (size_t)*mat_count * 4 > size))
		return false;
	if (*vis_count && ((size_t)*vis_off + (size_t)*vis_count * 4 > size))
		return false;
	for (u32 i = 0; i < *mat_count; i++)
		if (!smashmta_mat_ok (data, size, rd_be32 (data + *mat_off + (size_t)i * 4)))
			return false;
	for (u32 i = 0; i < *vis_count; i++)
		if (!smashmta_vis_ok (data, size, rd_be32 (data + *vis_off + (size_t)i * 4)))
			return false;
	return true;
}

bool IsMTA (const u8 *data, size_t size)
{
	u32 mc, mo, vc, vo;
	return smashmta_layout_ok (data, size, &mc, &mo, &vc, &vo);
}

static void smashmta_dump_pat (FILE *out, const u8 *data, u32 pat_off)
{
	const u32 pat_data = rd_be32 (data + pat_off);
	if (!pat_data)
	{
		fprintf (out, "PAT0\nempty\n###\n");
		return;
	}
	const s32 def_tex = (s32)rd_be32 (data + pat_data);
	const u32 kcount = rd_be32 (data + pat_data + 4);
	const u32 koff = rd_be32 (data + pat_data + 8);
	const s32 fcount = (s32)rd_be32 (data + pat_data + 12);
	const s32 unk = (s32)rd_be32 (data + pat_data + 16);
	fprintf (out, "PAT0\nDefault TexId,0x%08x\nKeyframe Count,%u\nPAT0_Unknown,%d\n",
		def_tex, kcount, unk);
	for (u32 i = 0; i < kcount; i++)
	{
		const u8 *k = data + koff + (size_t)i * 8;
		fprintf (out, "frameNum,%d,texId,0x%08x\n",
			(s32)rd_be32 (k + 4), rd_be32 (k));
	}
	fprintf (out, "Frame Count,%d\n###\n", fcount);
	(void)unk;
}

enumError DecodeMTA_Text (FILE *out, const u8 *data, size_t size)
{
	u32 mat_count, mat_off, vis_count, vis_off;
	if (!out || !smashmta_layout_ok (data, size, &mat_count, &mat_off, &vis_count, &vis_off))
		return ERR_INVALID_DATA;

	// Layout mirrors MTA.Decompile() so the text stays close to the
	// reference tool's own exchange format.
	fprintf (out, "Header\nHeader_Unknown,%u\nFrame Count,%u\nFrame Rate,%u\n",
		rd_be32 (data + 4), rd_be32 (data + 8), rd_be32 (data + 20));

	for (u32 i = 0; i < mat_count; i++)
	{
		const u32 mo = rd_be32 (data + mat_off + (size_t)i * 4);
		const char *name = (const char *)(data + rd_be32 (data + mo));
		const s32 hash = (s32)rd_be32 (data + mo + 4);
		const u32 pcount = rd_be32 (data + mo + 8);
		const u32 ppos = rd_be32 (data + mo + 12);
		const u32 has_pat = data[mo + 16];
		const u32 pat_off = rd_be32 (data + mo + 20);
		const u32 name2_off = rd_be32 (data + mo + 24);
		const s32 hash2 = (s32)rd_be32 (data + mo + 28);
		fprintf (out, "--------------------------------------\nMaterial\n%s\n"
			"Material Hash,%08X\nHas PAT0,%s\n",
			name, hash, has_pat ? "true" : "false");
		if (name2_off)
			fprintf (out, "Second Material,%s\nSecond Material Hash,%08X\n",
				(const char *)(data + name2_off), hash2);
		fprintf (out, "###\n");
		for (u32 p = 0; p < pcount; p++)
		{
			const u32 po = rd_be32 (data + ppos + (size_t)p * 4);
			const char *pname = (const char *)(data + rd_be32 (data + po));
			const s32 unk = (s32)rd_be32 (data + po + 4);
			const u32 vcount = rd_be32 (data + po + 8);
			const u32 fcount = rd_be32 (data + po + 12);
			const u16 unk2 = rd_be16 (data + po + 16);
			const u16 atype = rd_be16 (data + po + 18);
			const u32 doff = rd_be32 (data + po + 20);
			fprintf (out, "Material Property\n%s\nMatProp_Unk1,%d\n"
				"MatProp_Unk2,%u\nAnimation Type,%u\n",
				pname, unk, unk2, atype);
			for (u32 f = 0; f < fcount; f++)
			{
				const u8 *row = data + doff + ((size_t)f * vcount) * 4;
				for (u32 v = 0; v < vcount; v++)
					fprintf (out, v + 1 < vcount ? "%.7g," : "%.7g\n",
						smashmta_rd_f32 (row + (size_t)v * 4));
			}
			fprintf (out, "###\n");
		}
		if (has_pat)
			smashmta_dump_pat (out, data, pat_off);
	}

	for (u32 i = 0; i < vis_count; i++)
	{
		const u32 vo = rd_be32 (data + vis_off + (size_t)i * 4);
		const char *name = (const char *)(data + rd_be32 (data + vo));
		const u32 doff = rd_be32 (data + vo + 8);
		const s32 fcount = (s32)rd_be32 (data + doff);
		const u32 unk1 = rd_be16 (data + doff + 4);
		const u32 kcount = (u16)rd_be16 (data + doff + 6);
		const u32 koff = rd_be32 (data + doff + 8);
		fprintf (out, "--------------------------------------\nVIS0\n%s\n"
			"Frame Count,%d\nKeyframe Count,%u\nIs Constant,%s\n",
			name, fcount, kcount, unk1 ? "true" : "false");
		for (u32 k = 0; k < kcount; k++)
		{
			const u8 *kr = data + koff + (size_t)k * 4;
			fprintf (out, "Frame,%d,State,%u,Unknown,%u\n",
				(s16)rd_be16 (kr), kr[2], kr[3]);
		}
	}
	fprintf (out, "\n");
	return ERR_OK;
}

// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 interface files: Lumen UI layouts (.lm) and
// texture-atlas tables (texlist).
//
// Reference: KillzXGaming/Smash-Forge, "Smash Forge/Filetypes/UI/LM.cs"
// and "Smash Forge/Filetypes/UI/Texlist.cs" (MIT licensed;
// clean-room C ports of the binary layouts).
//
// Lumen: 16-u32 header (magic + unknowns + filesize), endianness
// selected by the second word (0x10000000 means little-endian), then a
// tag stream of { u32 type; u32 size-in-words; payload } walked until
// the End tag (0xFF00). Only the tag framing is interpreted here; tag
// payloads are reported by offset/size.
// Texlist: little-endian "TLST", counts and table offsets, then atlas
// flags and texture entries { nameOff x2, uv rect floats, w/h/atlas
// shorts } plus the name blob.

#include "lib-smashtui.h"
#include "lib-std.h"
#include "lib-archive-util.h"
#include <string.h>

static u32 smashtui_rd32 (const u8 *p, bool be)
{
	return be ? rd_be32 (p) : rd_le32 (p);
}

// ---------------------------------------------------------------------------
// Lumen (.lm)

static const char *smashtui_tag_name (u32 tag)
{
	switch (tag)
	{
		case 0x0000:
			return "Invalid";
		case 0x000A:
			return "Fonts";
		case 0xF001:
			return "Symbols";
		case 0xF002:
			return "Colors";
		case 0xF003:
			return "Transforms";
		case 0xF004:
			return "Bounds";
		case 0xF005:
			return "ActionScript";
		case 0xFF05:
			return "ActionScript2";
		case 0xF007:
			return "TextureAtlases";
		case 0xF008:
			return "UnkF008";
		case 0xF009:
			return "UnkF009";
		case 0xF00A:
			return "UnkF00A";
		case 0xF00B:
			return "UnkF00B";
		case 0xF00C:
			return "Properties";
		case 0xF00D:
			return "Defines";
		case 0xF022:
			return "Shape";
		case 0xF024:
			return "Graphic";
		case 0xF037:
			return "ColorMatrix";
		case 0xF103:
			return "Positions";
		case 0x0025:
			return "DynamicText";
		case 0x0027:
			return "DefineSprite";
		case 0x002B:
			return "FrameLabel";
		case 0x0001:
			return "ShowFrame";
		case 0xF105:
			return "Keyframe";
		case 0x0004:
			return "PlaceObject";
		case 0x0005:
			return "RemoveObject";
		case 0x000C:
			return "DoAction";
		case 0xFF00:
			return "End";
		default:
			return 0;
	}
}

// Walk the tag stream; with `out` != 0 print each tag. Returns the
// detected endianness when out==0 and `be` != 0.
static bool smashtui_lm_walk (const u8 *data, size_t size, FILE *out, bool *be)
{
	if (!data || size < 64 || (size & 3))
		return false;
	// The reference reader defaults to big-endian and switches to
	// little-endian when the second header word reads 0x10000000.
	bool le = rd_be32 (data + 4) == 0x10000000u;
	if (be)
		*be = !le;

	size_t pos = 64;
	uint ntags = 0;
	for (;;)
	{
		if (pos + 8 > size || ntags > 1000000)
			return false;
		const u32 tag = smashtui_rd32 (data + pos, !le);
		const u32 words = smashtui_rd32 (data + pos + 4, !le);
		if (tag == 0x0000)
			return false;
		if ((u64)words * 4 + pos + 8 > size)
			return false;
		if (out)
		{
			const char *tname = smashtui_tag_name (tag);
			fprintf (out, "tag%u | 0x%04x %-14s | offset 0x%zx | %u bytes\n", ntags, tag,
				tname ? tname : "Unknown", pos, words * 4);
		}
		pos += 8 + (size_t)words * 4;
		ntags++;
		if (tag == 0xFF00)
			return pos == size;
	}
}

bool IsSmashLM (const u8 *data, size_t size, ccp name)
{
	// Lumen files carry no documented magic, so content sniffing alone
	// is unsafe: require the .lm extension plus a clean tag walk.
	if (!name || !is_ext_match (name, ".lm"))
		return false;
	return smashtui_lm_walk (data, size, 0, 0);
}

enumError DecodeSmashLM_Text (FILE *out, const u8 *data, size_t size)
{
	bool be;
	if (!out || !smashtui_lm_walk (data, size, 0, &be))
		return ERR_INVALID_DATA;
	fprintf (out,
		"#LM\n# Super Smash Bros. 4 Lumen UI layout\n\n"
		"endian = %s\nmagic = 0x%08x\nfilesize_field = %u\n\n[tags]\n",
		be ? "big" : "little", smashtui_rd32 (data, be), smashtui_rd32 (data + 28, be));
	if (!smashtui_lm_walk (data, size, out, 0))
		return ERR_INVALID_DATA;
	return ERR_OK;
}

// ---------------------------------------------------------------------------
// Texlist
//
// Entry (0x20 bytes, kept contiguous here): the reference reader seeks
// into the string table for the name and then keeps reading the
// remaining fields from there, which only works by accident; the
// reference Rebuild() shows the true contiguous layout used below:
// nameOff x2, 4 uv floats, w/h/atlas shorts, pad short.

static bool smashtui_texlist_ok (const u8 *data, size_t size, u32 *n_atlas, u32 *n_tex)
{
	if (!data || size < 16 || memcmp (data, "TLST", 4))
		return false;
	const u32 na = rd_le16 (data + 6), nt = rd_le16 (data + 8);
	const u32 foff = rd_le16 (data + 10), eoff = rd_le16 (data + 12);
	const u32 soff = rd_le16 (data + 14);
	if (na > 10000 || nt > 100000)
		return false;
	if (foff != 0x10 || (size_t)eoff != 0x10 + (size_t)na * 4)
		return false;
	if ((size_t)soff != 0x10 + (size_t)na * 4 + (size_t)nt * 0x20)
		return false;
	if (soff > size)
		return false;

	for (u32 i = 0; i < nt; i++)
	{
		const u8 *e = data + eoff + (size_t)i * 0x20;
		if (e + 0x20 > data + size)
			return false;
		const u32 n1 = rd_le32 (e), n2 = rd_le32 (e + 4);
		if (n1 != n2 || (size_t)soff + n1 >= size)
			return false;
		if (!memchr (data + soff + n1, 0, size - soff - n1))
			return false;
		const u16 atlas = rd_le16 (e + 0x1c);
		if (atlas != 0xFFFF && atlas >= na)
			return false;
	}
	*n_atlas = na;
	*n_tex = nt;
	return true;
}

bool IsSmashTexlist (const u8 *data, size_t size)
{
	u32 na, nt;
	return smashtui_texlist_ok (data, size, &na, &nt);
}

static float smashtui_f32le (const u8 *p)
{
	u32 v = rd_le32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

enumError DecodeSmashTexlist_Text (FILE *out, const u8 *data, size_t size)
{
	u32 na, nt;
	if (!out || !smashtui_texlist_ok (data, size, &na, &nt))
		return ERR_INVALID_DATA;

	const u32 eoff = rd_le16 (data + 12);
	const u32 soff = rd_le16 (data + 14);
	fprintf (out,
		"#TEXLIST\n# Super Smash Bros. 4 UI texture-atlas table\n\n"
		"atlases = %u\ntextures = %u\n\n[atlases]\n",
		na, nt);
	for (u32 i = 0; i < na; i++)
		fprintf (out, "atlas%u flags = 0x%08x%s\n", i, rd_le32 (data + 0x10 + (size_t)i * 4),
			rd_le32 (data + 0x10 + (size_t)i * 4) & 0x01000000u ? " (dynamic)" : "");
	fprintf (out, "\n[textures]\n# idx | name | uv_tl uv_br | w h | atlas\n");
	for (u32 i = 0; i < nt; i++)
	{
		const u8 *e = data + eoff + (size_t)i * 0x20;
		fprintf (out, "%u | %s | %.6g %.6g %.6g %.6g | %d %d | %d\n", i,
			(const char *)(data + soff + rd_le32 (e)), smashtui_f32le (e + 8),
			smashtui_f32le (e + 12), smashtui_f32le (e + 16), smashtui_f32le (e + 20),
			(s16)rd_le16 (e + 0x18), (s16)rd_le16 (e + 0x1a), (s16)rd_le16 (e + 0x1c));
	}
	return ERR_OK;
}

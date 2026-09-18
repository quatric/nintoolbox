// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 object-motion animation (.omo, magic "OMO ").
//
// Reference: KillzXGaming/Smash-Forge, "Smash Forge/Filetypes/Animation/OMO.cs"
// (MIT licensed; clean-room C port of the binary layout).
//
// Big-endian throughout. Header (after the 4-byte magic):
//   u16 verHi, u16 verLow, s32 flags, u16 unk1, u16 boneCount,
//   u16 frameCount, u16 frameSize, s32 nodeOffset, s32 interOffset,
//   s32 keyOffset.
// boneCount nodes at nodeOffset, each { s32 flags; u32 hash;
// s32 interOffset (relative to the header interOffset); s32 keyOffset }.
// Interpolation min/max floats live at header.interOffset +
// node.interOffset, selected by the node flag word; frame key tables
// (frameCount rows of frameSize bytes = u16 keys) live at keyOffset.

#include "lib-smashomo.h"
#include "lib-std.h"
#include <string.h>

static float smashomo_rd_f32 (const u8 *p)
{
	u32 v = rd_be32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

#define OMO_HAS_POS 0x01000000u
#define OMO_HAS_ROT 0x02000000u
#define OMO_HAS_SCA 0x04000000u
#define OMO_POS_INTER 0x00080000u
#define OMO_POS_CONST 0x00200000u
#define OMO_ROT_INTER 0x00005000u
#define OMO_ROT_FCONST 0x00006000u
#define OMO_ROT_CONST 0x00007000u
#define OMO_ROT_FRAME 0x0000A000u
#define OMO_SCA_CONST 0x00000200u
#define OMO_SCA_CONST2 0x00000300u
#define OMO_SCA_INTER 0x00000080u

static bool smashomo_has (u32 flags, u32 f)
{
	if (f == OMO_HAS_POS || f == OMO_HAS_ROT || f == OMO_HAS_SCA)
		return ((flags & 0xFF000000u) & f) == f;
	if (f == OMO_POS_CONST || f == OMO_POS_INTER)
		return ((flags & 0x00FF0000u) & f) == f;
	if (f == OMO_ROT_INTER || f == OMO_ROT_FRAME || f == OMO_ROT_FCONST || f == OMO_ROT_CONST)
		return (flags & 0x0000F000u) == f;
	if (f == OMO_SCA_CONST || f == OMO_SCA_CONST2)
		return (flags & 0x00000F00u) == f;
	return (flags & 0x000000F0u) == f;
}

// Bytes of interpolation payload selected by a node flag word, or
// (size_t)-1 when the flag combination is not recognised.
static size_t smashomo_interp_size (u32 flags)
{
	size_t n = 0;
	if (smashomo_has (flags, OMO_HAS_POS))
	{
		if (smashomo_has (flags, OMO_POS_CONST))
			n += 12;
		else if (smashomo_has (flags, OMO_POS_INTER))
			n += 24;
		else
			return (size_t)-1;
	}
	if (smashomo_has (flags, OMO_HAS_ROT))
	{
		if (smashomo_has (flags, OMO_ROT_CONST))
			n += 12;
		else if (smashomo_has (flags, OMO_ROT_FCONST))
			n += 16;
		else if (smashomo_has (flags, OMO_ROT_INTER))
			n += 24;
		else if (!smashomo_has (flags, OMO_ROT_FRAME))
			return (size_t)-1;
	}
	if (smashomo_has (flags, OMO_HAS_SCA))
	{
		if (smashomo_has (flags, OMO_SCA_CONST) || smashomo_has (flags, OMO_SCA_CONST2))
			n += 12;
		else if (smashomo_has (flags, OMO_SCA_INTER))
			n += 24;
		else
			return (size_t)-1;
	}
	return n;
}

static bool smashomo_layout_ok (const u8 *data, size_t size,
	u32 *bone_count, u32 *frame_count, u32 *frame_size,
	u32 *node_off, u32 *inter_off, u32 *key_off)
{
	if (!data || size < 32 || memcmp (data, "OMO ", 4))
		return false;
	const u8 *h = data + 4;
	*bone_count = rd_be16 (h + 10);
	*frame_count = rd_be16 (h + 12);
	*frame_size = rd_be16 (h + 14);
	*node_off = rd_be32 (h + 16);
	*inter_off = rd_be32 (h + 20);
	*key_off = rd_be32 (h + 24);
	if (*bone_count > 100000 || *frame_count > 100000 || *frame_size > 100000)
		return false;
	if ((size_t)*node_off + (size_t)*bone_count * 16 > size)
		return false;
	if ((u64)*frame_count * *frame_size + *key_off > size)
		return false;
	if (*frame_size & 1)
		return false;

	for (u32 i = 0; i < *bone_count; i++)
	{
		const u8 *n = data + *node_off + (size_t)i * 16;
		const u32 flags = rd_be32 (n);
		const u32 rel = rd_be32 (n + 8);
		const size_t isz = smashomo_interp_size (flags);
		if (isz == (size_t)-1)
			return false;
		if ((size_t)*inter_off + rel + isz > size)
			return false;
	}
	return true;
}

bool IsOMO (const u8 *data, size_t size)
{
	u32 bc, fc, fsz, no, io, ko;
	return smashomo_layout_ok (data, size, &bc, &fc, &fsz, &no, &io, &ko);
}

static const char *smashomo_pos_name (u32 flags)
{
	if (!smashomo_has (flags, OMO_HAS_POS))
		return "none";
	if (smashomo_has (flags, OMO_POS_CONST))
		return "const";
	if (smashomo_has (flags, OMO_POS_INTER))
		return "interp";
	return "unknown";
}

static const char *smashomo_rot_name (u32 flags)
{
	if (!smashomo_has (flags, OMO_HAS_ROT))
		return "none";
	if (smashomo_has (flags, OMO_ROT_CONST))
		return "const";
	if (smashomo_has (flags, OMO_ROT_FCONST))
		return "full-const";
	if (smashomo_has (flags, OMO_ROT_INTER))
		return "interp";
	if (smashomo_has (flags, OMO_ROT_FRAME))
		return "frame";
	return "unknown";
}

static const char *smashomo_sca_name (u32 flags)
{
	if (!smashomo_has (flags, OMO_HAS_SCA))
		return "none";
	if (smashomo_has (flags, OMO_SCA_CONST) || smashomo_has (flags, OMO_SCA_CONST2))
		return "const";
	if (smashomo_has (flags, OMO_SCA_INTER))
		return "interp";
	return "unknown";
}

enumError DecodeOMO_Text (FILE *out, const u8 *data, size_t size)
{
	u32 bone_count, frame_count, frame_size, node_off, inter_off, key_off;
	if (!out || !smashomo_layout_ok (data, size,
		&bone_count, &frame_count, &frame_size, &node_off, &inter_off, &key_off))
		return ERR_INVALID_DATA;

	const u8 *h = data + 4;
	fprintf (out, "#OMO\n"
		"# Super Smash Bros. 4 object-motion animation\n\n"
		"version = %u.%u\n"
		"flags = 0x%08x\n"
		"unk1 = %u\n"
		"bones = %u\n"
		"frames = %u\n"
		"frame_size = %u\n\n",
		rd_be16 (h), rd_be16 (h + 2), rd_be32 (h + 4), rd_be16 (h + 8),
		bone_count, frame_count, frame_size);

	fprintf (out, "[nodes]\n"
		"# idx | flags | hash | pos | rot | sca | key_offset | min/max floats\n");
	for (u32 i = 0; i < bone_count; i++)
	{
		const u8 *n = data + node_off + (size_t)i * 16;
		const u32 flags = rd_be32 (n);
		const u32 hash = rd_be32 (n + 4);
		const u32 rel = rd_be32 (n + 8);
		const s32 koff = (s32)rd_be32 (n + 12);
		const u8 *ip = data + inter_off + rel;
		fprintf (out, "%u | 0x%08x | 0x%08x | %s | %s | %s | %d |",
			i, flags, hash,
			smashomo_pos_name (flags), smashomo_rot_name (flags),
			smashomo_sca_name (flags), koff);
		const size_t isz = smashomo_interp_size (flags);
		for (size_t k = 0; k < isz; k += 4)
			fprintf (out, " %.6g", smashomo_rd_f32 (ip + k));
		fprintf (out, "\n");
	}

	fprintf (out, "\n[frames]\n"
		"# frame | u16 keys (hex)\n");
	for (u32 f = 0; f < frame_count; f++)
	{
		const u8 *row = data + key_off + (size_t)f * frame_size;
		fprintf (out, "%u |", f);
		for (u32 k = 0; k < frame_size; k += 2)
			fprintf (out, " %04x", rd_be16 (row + k));
		fprintf (out, "\n");
	}
	return ERR_OK;
}

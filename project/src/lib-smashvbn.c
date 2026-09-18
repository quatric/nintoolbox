// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 skeleton family: VBN boneset, SB swing bones,
// JTB joint table and MOI model index.
//
// Reference: KillzXGaming/Smash-Forge, "Smash Forge/Filetypes/Models/"
// (VBN.cs incl. class SB, JTB.cs, MOI.cs). C# implementation is MIT
// licensed; this is a clean-room C port of the binary layouts.
//
// VBN: magic "VBN " (big-endian) or " NBV" (little-endian), then
//   s16 unk_1, s16 unk_2, u32 totalBoneCount, u32 boneCountPerType[4],
//   then totalBoneCount records of { char name[64]; u32 boneType;
//   s32 parentIndex (0x0FFFFFFF = root); u32 boneId; },
//   then totalBoneCount transform blocks of 9 floats
//   (pos xyz, rotation xyz, scale xyz).
// SB: magic " BWS" (little-endian), u16 ver (5), u16 (1), u32 count,
//   then count 140-byte entries (hash, params, 6 swing-range floats,
//   8 bone hashes, 10 unknown floats, factor, 3 ints).
// JTB: u16 size1, u16 size2, then size1+size2 s16 joint indices
//   (big-endian; little-endian if the first count exceeds 255).
// MOI: big-endian { u32 entryCount, otherCount, start (=0x30),
//   0x20, 8, 0x30, otherStart }, then entryCount 8-u32 records and
//   otherCount 2-u32 records, each leading with a name-table offset.

#include "lib-smashvbn.h"
#include "lib-std.h"
#include <string.h>

static float smashvbn_rd_f32 (const u8 *p, bool be)
{
	u32 v = be ? rd_be32 (p) : rd_le32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static const char *smashvbn_bone_type_name (u32 t)
{
	switch (t)
	{
		case 0: return "Normal";
		case 1: return "Follow";
		case 2: return "Helper";
		case 3: return "Swing";
		default: return "Unknown";
	}
}

// ---------------------------------------------------------------------------
// VBN

bool IsVBN (const u8 *data, size_t size)
{
	if (!data || size < 28)
		return false;
	bool be;
	if (!memcmp (data, "VBN ", 4))
		be = true;
	else if (!memcmp (data, " NBV", 4))
		be = false;
	else
		return false;

	const u8 *h = data + 4;
	u32 total = be ? rd_be32 (h + 4) : rd_le32 (h + 4);
	if (total == 0 || total > 100000)
		return false;
	const size_t need = 28 + (size_t)total * (64 + 12) + (size_t)total * 36;
	if (need != size)
		return false;

	// every name field must be NUL-terminated inside its 64 bytes and
	// every parent index must be root (0x0FFFFFFF) or a valid bone
	for (u32 i = 0; i < total; i++)
	{
		const u8 *rec = data + 28 + (size_t)i * 76;
		if (!memchr (rec, 0, 64))
			return false;
		s32 pi = (s32)(be ? rd_be32 (rec + 68) : rd_le32 (rec + 68));
		if (pi != 0x0FFFFFFF && (pi < 0 || (u32)pi >= total))
			return false;
	}
	return true;
}

enumError DecodeVBN_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsVBN (data, size))
		return ERR_INVALID_DATA;

	const bool be = !memcmp (data, "VBN ", 4);
	const u8 *h = data + 4;
	const s16 unk1 = (s16)(be ? rd_be16 (h) : rd_le16 (h));
	const s16 unk2 = (s16)(be ? rd_be16 (h + 2) : rd_le16 (h + 2));
	const u32 total = be ? rd_be32 (h + 4) : rd_le32 (h + 4);

	fprintf (out, "#VBN\n"
		"# Super Smash Bros. 4 boneset (Namco Visual Bones)\n\n"
		"endian = %s\n"
		"unk_1 = %d\n"
		"unk_2 = %d\n"
		"total_bones = %u\n"
		"count_normal = %u\n"
		"count_follow = %u\n"
		"count_helper = %u\n"
		"count_swing = %u\n\n",
		be ? "big" : "little",
		unk1, unk2, total,
		be ? rd_be32 (h + 8) : rd_le32 (h + 8),
		be ? rd_be32 (h + 12) : rd_le32 (h + 12),
		be ? rd_be32 (h + 16) : rd_le32 (h + 16),
		be ? rd_be32 (h + 20) : rd_le32 (h + 20));

	fprintf (out, "[bones]\n"
		"# idx | name | type | parent_idx | id | pos xyz | rot xyz | scale xyz\n");
	const u8 *xforms = data + 28 + (size_t)total * 76;
	for (u32 i = 0; i < total; i++)
	{
		const u8 *rec = data + 28 + (size_t)i * 76;
		char name[65];
		memcpy (name, rec, 64);
		name[64] = 0;
		const u32 btype = be ? rd_be32 (rec + 64) : rd_le32 (rec + 64);
		const s32 pi = (s32)(be ? rd_be32 (rec + 68) : rd_le32 (rec + 68));
		const u32 id = be ? rd_be32 (rec + 72) : rd_le32 (rec + 72);
		const u8 *t = xforms + (size_t)i * 36;
		char parent[16];
		if (pi == 0x0FFFFFFF)
			snprintf (parent, sizeof (parent), "root");
		else
			snprintf (parent, sizeof (parent), "%d", pi);
		fprintf (out, "%u | %s | %s(%u) | %s | 0x%08x | "
			"%.6g %.6g %.6g | %.6g %.6g %.6g | %.6g %.6g %.6g\n",
			i, name, smashvbn_bone_type_name (btype), btype, parent, id,
			smashvbn_rd_f32 (t, be), smashvbn_rd_f32 (t + 4, be),
			smashvbn_rd_f32 (t + 8, be),
			smashvbn_rd_f32 (t + 12, be), smashvbn_rd_f32 (t + 16, be),
			smashvbn_rd_f32 (t + 20, be),
			smashvbn_rd_f32 (t + 24, be), smashvbn_rd_f32 (t + 28, be),
			smashvbn_rd_f32 (t + 32, be));
	}
	return ERR_OK;
}

// ---------------------------------------------------------------------------
// SB (swing bones)

#define SMASH_SB_ENTRY_SIZE 140

bool IsSmashSB (const u8 *data, size_t size)
{
	if (!data || size < 12 || memcmp (data, " BWS", 4))
		return false;
	const u32 count = rd_le32 (data + 8);
	if (count > 100000)
		return false;
	return size == 12 + (size_t)count * SMASH_SB_ENTRY_SIZE;
}

enumError DecodeSmashSB_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsSmashSB (data, size))
		return ERR_INVALID_DATA;

	const u32 count = rd_le32 (data + 8);
	fprintf (out, "#SB\n"
		"# Super Smash Bros. 4 swing (cloth/hair-physics) bones\n\n"
		"version = %u.%u\n"
		"count = %u\n\n",
		rd_le16 (data + 4), rd_le16 (data + 6), count);

	fprintf (out, "[entries]\n"
		"# idx | hash | rx_range | ry_range | rz_range | chained_hashes[8] | factor\n");
	for (u32 i = 0; i < count; i++)
	{
		const u8 *e = data + 12 + (size_t)i * SMASH_SB_ENTRY_SIZE;
		const u32 hash = rd_le32 (e);
		fprintf (out, "%u | 0x%08x | %.6g..%.6g | %.6g..%.6g | %.6g..%.6g |",
			i, hash,
			smashvbn_rd_f32 (e + 28, false), smashvbn_rd_f32 (e + 32, false),
			smashvbn_rd_f32 (e + 36, false), smashvbn_rd_f32 (e + 40, false),
			smashvbn_rd_f32 (e + 44, false), smashvbn_rd_f32 (e + 48, false));
		for (int j = 0; j < 8; j++)
			fprintf (out, " 0x%08x", rd_le32 (e + 52 + j * 4));
		fprintf (out, " | %.6g\n", smashvbn_rd_f32 (e + 124, false));
	}
	return ERR_OK;
}

// ---------------------------------------------------------------------------
// JTB (joint table)

bool IsSmashJTB (const u8 *data, size_t size)
{
	if (!data || size < 4 || size > 0x40000 || (size & 1))
		return false;
	const u32 s1be = rd_be16 (data), s2be = rd_be16 (data + 2);
	if ((size_t)4 + ((size_t)s1be + s2be) * 2 == size)
	{
		// mirror the reference reader: counts above 255 mean little-endian
		if (s1be > 255)
		{
			const u32 s1le = rd_le16 (data), s2le = rd_le16 (data + 2);
			return (size_t)4 + ((size_t)s1le + s2le) * 2 == size;
		}
		return true;
	}
	const u32 s1le = rd_le16 (data), s2le = rd_le16 (data + 2);
	return (size_t)4 + ((size_t)s1le + s2le) * 2 == size;
}

enumError DecodeSmashJTB_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsSmashJTB (data, size))
		return ERR_INVALID_DATA;

	bool be = true;
	u32 s1 = rd_be16 (data), s2 = rd_be16 (data + 2);
	if ((size_t)4 + ((size_t)s1 + s2) * 2 != size || s1 > 255)
	{
		be = false;
		s1 = rd_le16 (data);
		s2 = rd_le16 (data + 2);
	}
	fprintf (out, "#JTB\n"
		"# Super Smash Bros. 4 joint-index table (VBN bone-index remap)\n\n"
		"endian = %s\n"
		"table1_size = %u\n"
		"table2_size = %u\n\n",
		be ? "big" : "little", s1, s2);

	const u8 *p = data + 4;
	fprintf (out, "[table1]\n");
	for (u32 i = 0; i < s1; i++, p += 2)
		fprintf (out, "%u = %d\n", i, (s16)(be ? rd_be16 (p) : rd_le16 (p)));
	fprintf (out, "\n[table2]\n");
	for (u32 i = 0; i < s2; i++, p += 2)
		fprintf (out, "%u = %d\n", i, (s16)(be ? rd_be16 (p) : rd_le16 (p)));
	return ERR_OK;
}

// ---------------------------------------------------------------------------
// MOI (model index)

bool IsSmashMOI (const u8 *data, size_t size)
{
	if (!data || size < 28)
		return false;
	const u32 n_ent = rd_be32 (data), n_other = rd_be32 (data + 4);
	const u32 start = rd_be32 (data + 8);
	const u32 u1 = rd_be32 (data + 12), u2 = rd_be32 (data + 16);
	const u32 u3 = rd_be32 (data + 20), ostart = rd_be32 (data + 24);
	if (start != 0x30 || u1 != 0x20 || u2 != 8 || u3 != 0x30)
		return false;
	if (n_ent > 100000 || n_other > 100000)
		return false;
	if ((size_t)start + (size_t)n_ent * 32 > size || (size_t)ostart + (size_t)n_other * 8 > size)
		return false;

	for (u32 i = 0; i < n_ent; i++)
	{
		const u32 noff = rd_be32 (data + start + (size_t)i * 32);
		if (noff >= size || !memchr (data + noff, 0, size - noff))
			return false;
	}
	for (u32 i = 0; i < n_other; i++)
	{
		const u32 noff = rd_be32 (data + ostart + (size_t)i * 8);
		if (noff >= size || !memchr (data + noff, 0, size - noff))
			return false;
	}
	return true;
}

enumError DecodeSmashMOI_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsSmashMOI (data, size))
		return ERR_INVALID_DATA;

	const u32 n_ent = rd_be32 (data), n_other = rd_be32 (data + 4);
	fprintf (out, "#MOI\n"
		"# Super Smash Bros. 4 model index\n\n"
		"entries = %u\n"
		"other_entries = %u\n\n",
		n_ent, n_other);

	const u32 start = rd_be32 (data + 8);
	fprintf (out, "[entries]\n"
		"# idx | name | values[1..7]\n");
	for (u32 i = 0; i < n_ent; i++)
	{
		const u8 *e = data + start + (size_t)i * 32;
		fprintf (out, "%u | %s |", i, (const char *)(data + rd_be32 (e)));
		for (int j = 1; j < 8; j++)
			fprintf (out, " %d", (s32)rd_be32 (e + j * 4));
		fprintf (out, "\n");
	}
	const u32 ostart = rd_be32 (data + 24);
	fprintf (out, "\n[other_entries]\n"
		"# idx | name | value1\n");
	for (u32 i = 0; i < n_other; i++)
	{
		const u8 *e = data + ostart + (size_t)i * 8;
		fprintf (out, "%u | %s | %d\n",
			i, (const char *)(data + rd_be32 (e)), (s32)rd_be32 (e + 4));
	}
	return ERR_OK;
}

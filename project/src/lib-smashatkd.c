// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 attack/subaction frame data (.bin, magic "ATKD").
//
// Reference: KillzXGaming/Smash-Forge, "Smash Forge/Filetypes/ATKD.cs"
// (MIT licensed; clean-room C port of the binary layout).
//
// Big-endian: "ATKD", s32 entryCount, u32 commonSubactions,
// u32 uniqueSubactions, then entries of { u16 subaction, u16 padding,
// u16 startFrame, u16 lastFrame, float xmin, xmax, ymin, ymax }.

#include "lib-smashatkd.h"
#include "lib-std.h"
#include <string.h>

static float smashatkd_f32 (const u8 *p)
{
	u32 v = rd_be32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static bool smashatkd_ok (const u8 *data, size_t size, u32 *count)
{
	if (!data || size < 16 || memcmp (data, "ATKD", 4))
		return false;
	const u32 n = rd_be32 (data + 4);
	if (n > 1000000)
		return false;
	if (16 + (size_t)n * 24 != size)
		return false;
	*count = n;
	return true;
}

bool IsSmashATKD (const u8 *data, size_t size)
{
	u32 n;
	return smashatkd_ok (data, size, &n);
}

enumError DecodeSmashATKD_Text (FILE *out, const u8 *data, size_t size)
{
	u32 n;
	if (!out || !smashatkd_ok (data, size, &n))
		return ERR_INVALID_DATA;

	fprintf (out,
		"#ATKD\n# Super Smash Bros. 4 attack/subaction frame rects\n\n"
		"entries = %u\ncommon_subactions = %u\nunique_subactions = %u\n\n"
		"[entries]\n# idx | subaction | frames | x_min x_max y_min y_max\n",
		n, rd_be32 (data + 8), rd_be32 (data + 12));
	for (u32 i = 0; i < n; i++)
	{
		const u8 *e = data + 16 + (size_t)i * 24;
		fprintf (out, "%u | %u | %u..%u | %.6g %.6g %.6g %.6g\n", i, rd_be16 (e), rd_be16 (e + 4),
			rd_be16 (e + 6), smashatkd_f32 (e + 8), smashatkd_f32 (e + 12), smashatkd_f32 (e + 16),
			smashatkd_f32 (e + 20));
	}
	return ERR_OK;
}

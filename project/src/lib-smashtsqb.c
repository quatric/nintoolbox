// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 sound sequence archive (.sqb, magic "SQB\0").
//
// Reference: KillzXGaming/Smash-Forge, "Smash Forge/Filetypes/Sounds/SQB.cs"
// (MIT licensed; clean-room C port of the binary layout).
//
// Little-endian: "SQB\0", s16 unk1, s16 unk2, s32 sequenceCount,
// s32 sequenceDataOffset, then sequenceCount s32 offsets (relative to
// 0x10+sequenceDataOffset; -1 = empty). Each sequence: 4 s16 header
// words (unk, eventCount, unk, unk) then events of { u32 hash;
// s16 type, frame, unk1..unk4 } (16 bytes each).

#include "lib-smashtsqb.h"
#include "lib-std.h"
#include <string.h>

#define SMASHTSQB_EV_SIZE 16

static bool smashtsqb_ok (const u8 *data, size_t size, u32 *n_seq)
{
	if (!data || size < 16 || memcmp (data, "SQB\0", 4))
		return false;
	const u32 n = rd_le32 (data + 8);
	const u32 data_off = rd_le32 (data + 12);
	if (n > 100000 || 16 + (size_t)n * 4 > size)
		return false;
	for (u32 i = 0; i < n; i++)
	{
		const s32 off = (s32)rd_le32 (data + 16 + (size_t)i * 4);
		if (off == -1)
			continue;
		if (off < 0)
			return false;
		const size_t base = 16 + data_off + (size_t)off;
		if (base + 8 > size)
			return false;
		const u32 nev = rd_le16 (data + base + 2);
		if (nev > 1000000)
			return false;
		if (base + 8 + (size_t)nev * SMASHTSQB_EV_SIZE > size)
			return false;
	}
	*n_seq = n;
	return true;
}

bool IsSmashSQB (const u8 *data, size_t size)
{
	u32 n;
	return smashtsqb_ok (data, size, &n);
}

enumError DecodeSmashSQB_Text (FILE *out, const u8 *data, size_t size)
{
	u32 n;
	if (!out || !smashtsqb_ok (data, size, &n))
		return ERR_INVALID_DATA;

	const u32 data_off = rd_le32 (data + 12);
	fprintf (out,
		"#SQB\n# Super Smash Bros. 4 sound sequences\n\n"
		"unk1 = %d\nunk2 = %d\nsequences = %u\n",
		(s16)rd_le16 (data + 4), (s16)rd_le16 (data + 6), n);

	for (u32 i = 0; i < n; i++)
	{
		const s32 off = (s32)rd_le32 (data + 16 + (size_t)i * 4);
		if (off == -1)
		{
			fprintf (out, "\n[sequence %u: empty]\n", i);
			continue;
		}
		const u8 *sq = data + 16 + data_off + (size_t)off;
		const u32 nev = rd_le16 (sq + 2);
		fprintf (out,
			"\n[sequence %u]\nunk = %d %d %d\nevents = %u\n"
			"# idx | hash | type | frame | unk1..unk4\n",
			i, (s16)rd_le16 (sq), (s16)rd_le16 (sq + 4), (s16)rd_le16 (sq + 6), nev);
		for (u32 e = 0; e < nev; e++)
		{
			const u8 *ev = sq + 8 + (size_t)e * SMASHTSQB_EV_SIZE;
			fprintf (out, "%u | 0x%08x | %d | %d | %d %d %d %d\n", e, rd_le32 (ev),
				(s16)rd_le16 (ev + 4), (s16)rd_le16 (ev + 6), (s16)rd_le16 (ev + 8),
				(s16)rd_le16 (ev + 10), (s16)rd_le16 (ev + 12), (s16)rd_le16 (ev + 14));
		}
	}
	return ERR_OK;
}

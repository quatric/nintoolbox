// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 moveset files: ACMD scripts (game.bin,
// effect.bin, sound.bin, expression.bin) and motion.mtable.
//
// Reference: Sammi-Husky/Sm4sh-Tools, SALT/Scripting/AnimCMD/
// (ACMDFile.cs, ACMDScript.cs, MTable.cs), used by
// KillzXGaming/Smash-Forge (MovesetManager.cs, ACMDScript.cs).
// MIT licensed; clean-room C port of the container layout.

#include "lib-smashacmd.h"
#include "lib-std.h"
#include "lib-archive-util.h"
#include <string.h>

// Script terminator word appended to every command stream.
#define SMASH_ACMD_END 0x5766F889u

static u32 smashacmd_rd32 (const u8 *p, bool be)
{
	return be ? rd_be32 (p) : rd_le32 (p);
}

// Header: "ACMD", s32 version (=2), s32 actionCount, s32 commandCount,
// then actionCount table entries of { u32 animCRC, s32 absoluteOffset }.
// Byte 0x04 == 0x02 selects little-endian (3DS), 0x00 big-endian (Wii U).
static bool smashacmd_ok (const u8 *data, size_t size, u32 *actions, bool *be)
{
	if (!data || size < 16 || memcmp (data, "ACMD", 4))
		return false;
	bool le;
	if (data[4] == 0x02)
		le = true;
	else if (data[4] == 0x00)
		le = false;
	else
		return false;
	const u32 ver = smashacmd_rd32 (data + 4, !le);
	const u32 n = smashacmd_rd32 (data + 8, !le);
	const u32 body = 16 + (size_t)n * 8;
	if (ver != 2 || !n || n > 100000 || body > size)
		return false;

	// script offsets must tile the region from the end of the table
	// to the end of the file in order, and every script region must
	// end in the terminator word
	u32 prev = (u32)body;
	for (u32 i = 0; i < n; i++)
	{
		const u32 off = smashacmd_rd32 (data + 16 + (size_t)i * 8 + 4, !le);
		if (off < body || off > size || (off & 3) || off < prev)
			return false;
		const u32 next
			= i + 1 < n ? smashacmd_rd32 (data + 16 + (size_t)(i + 1) * 8 + 4, !le) : (u32)size;
		if (next < off || next > size)
			return false;
		// an empty region is a script slot aliasing the next script's
		// offset; otherwise the region must end in the terminator word
		if (next > off && smashacmd_rd32 (data + next - 4, !le) != SMASH_ACMD_END)
			return false;
		prev = off;
	}
	*actions = n;
	*be = !le;
	return true;
}

bool IsSmashACMD (const u8 *data, size_t size)
{
	u32 n;
	bool be;
	return smashacmd_ok (data, size, &n, &be);
}

enumError DecodeSmashACMD_Text (FILE *out, const u8 *data, size_t size)
{
	u32 n;
	bool be;
	if (!out || !smashacmd_ok (data, size, &n, &be))
		return ERR_INVALID_DATA;

	fprintf (out,
		"#ACMD\n# Super Smash Bros. 4 moveset script file\n\n"
		"endian = %s\nversion = %u\nactions = %u\ntotal_commands = %u\n\n"
		"[scripts]\n# idx | anim_crc | offset | words\n",
		be ? "big" : "little", smashacmd_rd32 (data + 4, be), n, smashacmd_rd32 (data + 12, be));

	const size_t body = 16 + (size_t)n * 8;
	for (u32 i = 0; i < n; i++)
	{
		const u32 crc = smashacmd_rd32 (data + 16 + (size_t)i * 8, be);
		const u32 off = smashacmd_rd32 (data + 16 + (size_t)i * 8 + 4, be);
		const u32 next
			= i + 1 < n ? smashacmd_rd32 (data + 16 + (size_t)(i + 1) * 8 + 4, be) : (u32)size;
		const u32 words = next >= off ? (next - off) / 4 : 0;
		(void)body;
		fprintf (out, "%u | 0x%08x | 0x%x | %u\n", i, crc, off, words);
	}
	return ERR_OK;
}

// ---------------------------------------------------------------------------
// motion.mtable: flat array of anim-name CRC32s in animation order, no
// magic, no header, no count (count = size/4). The file endianness is
// inherited from the sibling ACMD files, so both interpretations are
// reported and the caller picks the matching one.

bool IsSmashMTable (const u8 *data, size_t size, ccp name)
{
	if (!data || !name || size < 4 || size > 0x100000 || (size & 3))
		return false;
	ccp leaf = leaf_name (name);
	if (strcasecmp (leaf, "motion.mtable") && !is_ext_match (name, ".mtable"))
		return false;
	return true;
}

enumError DecodeSmashMTable_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !data || size < 4 || size > 0x100000 || (size & 3))
		return ERR_INVALID_DATA;
	const u32 n = (u32)size / 4;
	fprintf (out,
		"#MTABLE\n# Super Smash Bros. 4 motion table (anim CRC per slot)\n"
		"# endianness is inherited from the sibling ACMD files: compare\n"
		"# the crc_be column against the ACMD script table to pick a side\n\n"
		"slots = %u\n\n[slots]\n# idx | crc_be | crc_le\n",
		n);
	for (u32 i = 0; i < n; i++)
		fprintf (out, "%u | 0x%08x | 0x%08x\n", i, rd_be32 (data + (size_t)i * 4),
			rd_le32 (data + (size_t)i * 4));
	return ERR_OK;
}

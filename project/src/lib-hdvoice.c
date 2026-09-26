// SPDX-License-Identifier: GPL-2.0+
// Tenchu: Shadow Assassins voice-line manifest (".hd") -- see
// lib-hdvoice.h for exactly what is and is not understood about it.

#include "lib-hdvoice.h"
#include "dclib-debug.h"
#include "dclib-basics.h"
#include <string.h>
#include <errno.h>

//-----------------------------------------------------------------------------

#define HDV_SLOT_SIZE 8
#define HDV_PARAM_SIZE 44
#define HDV_MAX_SLOTS (1u << 20) // sanity cap

// Walks the slot table starting right after the 8-byte header. On success,
// returns 1 and fills *n_real with the number of non-sentinel slots and
// *table2_start with the byte offset where the parameter table must begin;
// the caller still has to check that the remaining bytes exactly match
// n_real * HDV_PARAM_SIZE. Returns 0 if the slot table doesn't parse as
// "sequential index, or -1" all the way through.
static int walk_slots (
	const u8 *data, size_t size, u32 total_slots, u32 *n_real, size_t *table2_start)
{
	if (total_slots > HDV_MAX_SLOTS)
		return 0;

	size_t pos = 8;
	const size_t need = (size_t)total_slots * HDV_SLOT_SIZE;
	if (pos + need > size)
		return 0;

	s32 last_seq = -1;
	u32 real = 0;
	for (u32 i = 0; i < total_slots; i++, pos += HDV_SLOT_SIZE)
	{
		const s32 seq = (s32)be32 (data + pos + 4);
		if (seq == last_seq + 1)
		{
			last_seq = seq;
			real++;
		}
		else if (seq != -1)
			return 0;
	}

	*n_real = real;
	*table2_start = pos;
	return 1;
}

int IsHdVoice (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < 8 || file_size < 8)
		return 0;

	const u32 total_slots = be32 (data);
	u32 n_real;
	size_t table2_start;
	if (!walk_slots (data, size, total_slots, &n_real, &table2_start))
		return 0;

	const size_t remain = file_size - table2_start;
	return remain == (size_t)n_real * HDV_PARAM_SIZE;
}

//-----------------------------------------------------------------------------

enumError DecodeHdVoice_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !IsHdVoice (data, size, file_size))
		return EINVAL;

	const u32 total_slots = be32 (data);
	const u32 group = be32 (data + 4);

	u32 n_real;
	size_t table2_start;
	walk_slots (data, size, total_slots, &n_real, &table2_start);

	fprintf (f, "# Tenchu: Shadow Assassins voice-line manifest (.hd)\n");
	fprintf (f, "file_size = %zu\n", file_size);
	fprintf (f, "total_slots = %u\n", total_slots);
	fprintf (f, "group (meaning unconfirmed) = %u\n", group);
	fprintf (f, "n_real = %u\n", n_real);
	fprintf (f, "n_pad = %u\n", total_slots - n_real);

	fprintf (f, "\n# Slot table (category, variant, param-index or -1 = unrecorded)\n");
	size_t pos = 8;
	for (u32 i = 0; i < total_slots; i++, pos += HDV_SLOT_SIZE)
	{
		const u16 category = (u16)(be32 (data + pos) >> 16);
		const u16 variant = (u16)be32 (data + pos);
		const s32 index = (s32)be32 (data + pos + 4);
		if (index == -1)
			fprintf (f, "  [%u] category=%u variant=%u (unrecorded)\n", i, category, variant);
		else
			fprintf (
				f, "  [%u] category=%u variant=%u -> param[%d]\n", i, category, variant, index);
	}

	fprintf (f, "\n# Parameter table (one per recorded slot)\n");
	for (u32 i = 0; i < n_real; i++)
	{
		const u8 *p = data + table2_start + (size_t)i * HDV_PARAM_SIZE;
		const u32 self_index = be32 (p + 0);
		const u32 volume = be32 (p + 4);
		const u32 pitch_bits = be32 (p + 12);
		float pitch;
		memcpy (&pitch, &pitch_bits, 4);
		const u32 group2 = be32 (p + 20);
		const u32 priority = be32 (p + 24);
		const u32 unknown7 = be32 (p + 28);
		const u32 unknown8 = be32 (p + 32);
		const u32 flag9 = be32 (p + 36);
		const u32 flag10 = be32 (p + 40);

		fprintf (f,
			"  [%u] self=%u volume=%u pitch=%.3f group2=%u priority=%u"
			" unk7=%u unk8=%u flag9=%u flag10=%u\n",
			i, self_index, volume, pitch, group2, priority, unknown7, unknown8, flag9, flag10);
	}

	return ERR_OK;
}

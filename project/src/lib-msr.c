#include "lib-std.h"
#include "lib-msr.h"
#include <string.h>
#include <errno.h>

//-----------------------------------------------------------------------------
///////////////	   Metroid: Samus Returns (3DS) archive		///////////////
//-----------------------------------------------------------------------------

// Layout ported from aluigi's public metroid_sr_3ds.bms, all little-endian:
//
//   u32 info_size, u32 data_size, u32 files
//   entry[files]: u32 crc, u32 offset, u32 end_offset
//
// There is no magic and no name table, so detection is purely structural.
// Every constraint the format implies is enforced, and all of them must hold
// at once: the declared info section must be exactly the size of the header
// plus the table, info+data must account for the whole file, the members
// must start where the table ends, run in non-decreasing order, and stay
// in-bounds.  A file that satisfies all of that while not being this format
// is vanishingly unlikely, but callers should still run this scanner last.
enumError ScanMetroidSR (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || size < 24)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u32 info_size = rd_le32 (data);
	const u32 data_size = rd_le32 (data + 4);
	const u32 files = rd_le32 (data + 8);

	if (!files || files > 0x100000)
		return EINVAL;
	if ((u64)12 + (u64)files * 12 != info_size)
		return EINVAL;
	if ((u64)info_size + data_size != size)
		return EINVAL;

	u32 prev = info_size;
	for (u32 i = 0; i < files; i++)
	{
		const u8 *e = data + 12 + (u64)i * 12;
		const u32 off = rd_le32 (e + 4);
		const u32 end = rd_le32 (e + 8);
		if (off < info_size || end < off || end > size || off < prev)
			return EINVAL;
		prev = end;
	}
	if (rd_le32 (data + 12 + 4) != info_size)
		return EINVAL; // members must begin immediately after the table

	nintendo_sarc_entry_t *out = CALLOC (files, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	for (u32 i = 0; i < files; i++)
	{
		const u8 *e = data + 12 + (u64)i * 12;
		const u32 off = rd_le32 (e + 4);
		const u32 end = rd_le32 (e + 8);
		char name[32];
		snprintf (name, sizeof (name), "%05u.bin", i);
		if (!OwnedEntryAdd (out, i, name, data + off, end - off))
		{
			ResetOwnedEntries (out, i);
			return ERR_CANT_CREATE;
		}
	}
	*entries = out;
	*n_entries = files;
	return ERR_OK;
}

// ============================================================

#include "lib-std.h"
#include "lib-bg4.h"
#include <zlib.h>
#include <string.h>
#include <errno.h>

//-----------------------------------------------------------------------------
///////////////	    BG4 (Mario & Luigi: Paper Jam, 3DS)		///////////////
//-----------------------------------------------------------------------------

// Layout ported from aluigi's public QuickBMS script (mario_luigi_paper.bms),
// all little-endian:
//
//   'BG4\0', u16 dummy, u16 files, u32 data_off, u32 dummy   <- 16-byte header
//   entry[files]: u32 offset, u32 size, u32 crc, u16 name_off (14 bytes each)
//   names: NUL-terminated strings, name_off is relative to the end of the
//          entry table
//
// offset==0 marks an unused slot.  Bit 31 of offset marks a BLZ ("backward
// LZSS", the DS/3DS ARM-binary compression) member; the flag is masked off
// and the payload decompressed with this tool's existing DecodeBLZ.
enumError ScanBG4 (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || size < 16 || memcmp (data, "BG4\0", 4))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const uint files = rd_le16 (data + 6);
	if (!files)
		return EINVAL;
	const u64 tab = 16;
	const u64 names_off = tab + (u64)files * 14;
	if (names_off > size)
		return EINVAL;

	nintendo_sarc_entry_t *out = CALLOC (files, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint n = 0;
	for (uint i = 0; i < files; i++)
	{
		const u8 *e = data + tab + i * 14;
		u32 off = rd_le32 (e);
		const u32 fsize = rd_le32 (e + 4);
		const uint noff = rd_le16 (e + 12);
		if (!off)
			continue;
		const bool compressed = (off & 0x80000000u) != 0;
		off &= 0x7fffffffu;
		if ((u64)off + fsize > size)
			continue;

		char name[256];
		const u64 nabs = names_off + noff;
		if (nabs >= size)
			continue;
		uint len = 0;
		while (nabs + len < size && data[nabs + len] && len < sizeof (name) - 1)
			len++;
		memcpy (name, data + nabs, len);
		name[len] = 0;
		if (!OwnedNameOk (name))
			snprintf (name, sizeof (name), "%04u.bin", i);

		bool ok;
		if (compressed)
		{
			u8 *dec = 0;
			uint dec_size = 0;
			if (DecodeBLZ (&dec, &dec_size, data + off, fsize) != ERR_OK || !dec)
			{
				// Keep the raw member rather than losing it entirely.
				ok = OwnedEntryAdd (out, n, name, data + off, fsize);
			}
			else
			{
				ok = OwnedEntryAdd (out, n, name, dec, dec_size);
				FREE (dec);
			}
		}
		else
			ok = OwnedEntryAdd (out, n, name, data + off, fsize);

		if (!ok)
		{
			ResetOwnedEntries (out, n);
			return ERR_CANT_CREATE;
		}
		n++;
	}

	if (!n)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

enumError CreateBG4 (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xFFFF)
		return EINVAL;

	const uint tab_size = n_entries * 14;
	uint names_size = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = entries[i].name ? entries[i].name : "";
		names_size += (uint)strlen (name) + 1;
	}
	names_size = (names_size + 3) & ~3u;

	const uint header_and_meta = 16 + tab_size + names_size;
	const uint data_start = (header_and_meta + 15) & ~15u;

	u64 total_size = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		total_size += entries[i].size;
		total_size = (total_size + 15) & ~15u;
	}

	if (total_size > 0x7fffffff)
		return EFBIG;

	u8 *out = CALLOC (1, (size_t)total_size);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, "BG4\0", 4);
	wr_le16 (out + 4, 0);
	wr_le16 (out + 6, (u16)n_entries);
	wr_le32 (out + 8, (u32)data_start);
	wr_le32 (out + 12, 0);

	const uint names_base = 16 + tab_size;
	uint cur_name_off = 0;
	u32 cur_data_off = (u32)data_start;

	for (uint i = 0; i < n_entries; i++)
	{
		u8 *e = out + 16 + i * 14;
		ccp name = entries[i].name ? entries[i].name : "";
		const uint name_len = (uint)strlen (name);

		wr_le32 (e, cur_data_off);
		wr_le32 (e + 4, entries[i].size);

		u32 crc = 0;
		if (entries[i].data && entries[i].size)
			crc = (u32)crc32 (0, entries[i].data, entries[i].size);
		wr_le32 (e + 8, crc);
		wr_le16 (e + 12, (u16)cur_name_off);

		memcpy (out + names_base + cur_name_off, name, name_len + 1);
		cur_name_off += name_len + 1;

		if (entries[i].data && entries[i].size)
			memcpy (out + cur_data_off, entries[i].data, entries[i].size);

		cur_data_off += entries[i].size;
		cur_data_off = (cur_data_off + 15) & ~15u;
	}

	*dest = out;
	*dest_size = (uint)total_size;
	return ERR_OK;
}


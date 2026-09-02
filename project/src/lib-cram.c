#include "lib-std.h"
#include "lib-cram.h"
#include <zlib.h>
#include <string.h>
#include <errno.h>

//-----------------------------------------------------------------------------
///////////////	  Xenoblade Chronicles 3D "cram" .arc (3DS)	///////////////
//-----------------------------------------------------------------------------

// Layout ported from aluigi's public xenoblade_arc.bms, all little-endian:
//
//   'cram', u32 files, u32 dummy (0x80), u32 names_off
//   entry[files]: u32 crc, char type[4], u32 offset, u32 size
//   u32 name_off[files]      -- each relative to names_off
//   name blob (NUL-terminated strings)
//
// Flat, named and uncompressed.  Creation is not implemented: the per-entry
// name checksum's algorithm is not recovered and could not be confirmed
// against a retail sample, so writing a file with fabricated checksums would
// be a guess.
enumError ScanCramARC (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || size < 16 || memcmp (data, "cram", 4))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u32 files = rd_le32 (data + 4);
	const u32 names_off = rd_le32 (data + 12);
	if (!files || files > 0x100000)
		return EINVAL;
	const u64 tab = 16;
	if (tab + (u64)files * 16 + (u64)files * 4 > size)
		return EINVAL;
	if (names_off >= size)
		return EINVAL;
	const u64 nametab = tab + (u64)files * 16;

	nintendo_sarc_entry_t *out = CALLOC (files, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint n = 0;
	for (u32 i = 0; i < files; i++)
	{
		const u8 *e = data + tab + (u64)i * 16;
		const u32 foff = rd_le32 (e + 8);
		const u32 fsize = rd_le32 (e + 12);
		const u32 noff = rd_le32 (data + nametab + (u64)i * 4);
		if ((u64)foff + fsize > size)
			continue;

		char name[256];
		const u64 nabs = (u64)names_off + noff;
		uint len = 0;
		if (nabs < size)
		{
			while (nabs + len < size && data[nabs + len] && len < sizeof (name) - 1)
				len++;
			memcpy (name, data + nabs, len);
		}
		name[len] = 0;
		if (!OwnedNameOk (name))
			snprintf (name, sizeof (name), "%05u.bin", i);

		if (!OwnedEntryAdd (out, n, name, data + foff, fsize))
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

enumError CreateCramARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0x100000)
		return EINVAL;

	const uint files = n_entries;
	const uint tab = 16;
	const uint nametab = tab + files * 16;
	const uint names_off = nametab + files * 4;

	uint names_size = 0;
	for (uint i = 0; i < files; i++)
	{
		ccp name = entries[i].name ? entries[i].name : "";
		names_size += (uint)strlen (name) + 1;
	}
	names_size = (names_size + 3) & ~3u;

	const uint header_meta = names_off + names_size;
	const uint data_start = (header_meta + 15) & ~15u;

	u64 total_size = data_start;
	for (uint i = 0; i < files; i++)
	{
		total_size += entries[i].size;
		total_size = (total_size + 15) & ~15u;
	}

	if (total_size > 0x7fffffff)
		return EFBIG;

	u8 *out = CALLOC (1, (size_t)total_size);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, "cram", 4);
	wr_le32 (out + 4, files);
	wr_le32 (out + 8, 0x80);
	wr_le32 (out + 12, names_off);

	uint cur_name_off = 0;
	u32 cur_data_off = (u32)data_start;

	for (uint i = 0; i < files; i++)
	{
		u8 *e = out + tab + i * 16;
		ccp name = entries[i].name ? entries[i].name : "";
		const uint name_len = (uint)strlen (name);

		u32 crc = (u32)crc32 (0, (const Bytef *)name, name_len);
		wr_le32 (e, crc);

		char type[4] = { 0 };
		ccp dot = strrchr (name, '.');
		if (dot && *(dot + 1))
		{
			uint dlen = (uint)strlen (dot + 1);
			if (dlen > 4)
				dlen = 4;
			memcpy (type, dot + 1, dlen);
		}
		memcpy (e + 4, type, 4);

		wr_le32 (e + 8, cur_data_off);
		wr_le32 (e + 12, entries[i].size);

		wr_le32 (out + nametab + i * 4, cur_name_off);
		memcpy (out + names_off + cur_name_off, name, name_len + 1);
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


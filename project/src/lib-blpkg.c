// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Barking Lizards Technologies "pkg\0" archive (see lib-blpkg.h).
//-----------------------------------------------------------------------------

#include "lib-std.h"
#include "lib-blpkg.h"
#include <string.h>
#include <zlib.h>

#define BLPKG_HEADER_SIZE 16
#define BLPKG_STATS_SIZE 32
#define BLPKG_ENTRY_NAME_SIZE 32
#define BLPKG_ENTRY_FIELDS_SIZE 20
#define BLPKG_ENTRY_SIZE (BLPKG_ENTRY_NAME_SIZE + BLPKG_ENTRY_FIELDS_SIZE)
#define BLPKG_MAX_ENTRIES 0x10000
#define BLPKG_MAX_OUTPUT NFMT_MAX_OUTPUT

bool IsBLPKG (const u8 *data, uint size)
{
	if (!data || size < BLPKG_HEADER_SIZE + BLPKG_STATS_SIZE || memcmp (data, "pkg\0", 4))
		return false;

	const u32 count = rd_be32 (data + 8);
	if (!count || count > BLPKG_MAX_ENTRIES)
		return false;

	const u64 table_end = (u64)BLPKG_HEADER_SIZE + BLPKG_STATS_SIZE + (u64)count * BLPKG_ENTRY_SIZE;
	return table_end <= size;
}

// Inflate a full gzip member into a heap buffer of the caller-expected size.
// Returns false and frees nothing of the caller's on failure.
static bool blpkg_gzip_inflate (const u8 *src, uint src_size, u32 expect_size, u8 *out)
{
	z_stream strm;
	memset (&strm, 0, sizeof (strm));
	strm.next_in = (Bytef *)src;
	strm.avail_in = src_size;
	strm.next_out = out;
	strm.avail_out = expect_size;

	if (inflateInit2 (&strm, 15 + 32) != Z_OK) // 32: auto-detect gzip/zlib header
		return false;

	const int ret = inflate (&strm, Z_FINISH);
	const uint produced = expect_size - strm.avail_out;
	inflateEnd (&strm);

	return ret == Z_STREAM_END && produced == expect_size;
}

enumError ScanBLPKG (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries)
		return ERR_SEMANTIC;
	*entries = 0;
	*n_entries = 0;
	if (!IsBLPKG (data, size))
		return ERR_NOTHING_TO_DO;

	const u32 count = rd_be32 (data + 8);
	nintendo_sarc_entry_t *out = CALLOC (count, sizeof (*out));
	if (!out)
		return ERR_OUT_OF_MEMORY;

	uint n = 0;
	u64 pos = BLPKG_HEADER_SIZE + BLPKG_STATS_SIZE;
	for (u32 i = 0; i < count; i++)
	{
		if (pos + BLPKG_ENTRY_SIZE > size)
		{
			ResetOwnedEntries (out, n);
			return ERR_INVALID_DATA;
		}

		char name[BLPKG_ENTRY_NAME_SIZE + 1];
		memcpy (name, data + pos, BLPKG_ENTRY_NAME_SIZE);
		name[BLPKG_ENTRY_NAME_SIZE] = 0;
		// Guard against a non-terminated 32-byte name run.
		bool has_nul = false;
		for (uint k = 0; k < BLPKG_ENTRY_NAME_SIZE; k++)
			if (!name[k])
			{
				has_nul = true;
				break;
			}
		pos += BLPKG_ENTRY_NAME_SIZE;

		const u32 idx_type = rd_be32 (data + pos);
		const u32 dsize = rd_be32 (data + pos + 4);
		const u32 csize = rd_be32 (data + pos + 8);
		const u32 foff = rd_be32 (data + pos + 12);
		pos += BLPKG_ENTRY_FIELDS_SIZE;

		const u32 type = idx_type & 0xffff;
		if (type > 1 || dsize > BLPKG_MAX_OUTPUT || csize > BLPKG_MAX_OUTPUT)
		{
			ResetOwnedEntries (out, n);
			return ERR_INVALID_DATA;
		}
		if ((u64)foff + csize > size)
		{
			ResetOwnedEntries (out, n);
			return ERR_INVALID_DATA;
		}

		ccp use_name = name;
		char safe_name[32];
		if (!has_nul || !*name || !OwnedNameOk (name))
		{
			snprintf (safe_name, sizeof (safe_name), "%04u.bin", i);
			use_name = safe_name;
		}

		bool ok;
		if (type == 0)
		{
			// Stored: raw bytes, compressed_size == decompressed_size.
			if (csize != dsize)
			{
				ResetOwnedEntries (out, n);
				return ERR_INVALID_DATA;
			}
			ok = OwnedEntryAdd (out, n, use_name, data + foff, csize);
		}
		else
		{
			u8 *dec = dsize ? MALLOC (dsize) : MALLOC (1);
			if (!dec)
			{
				ResetOwnedEntries (out, n);
				return ERR_OUT_OF_MEMORY;
			}
			if (!blpkg_gzip_inflate (data + foff, csize, dsize, dec))
			{
				FREE (dec);
				ResetOwnedEntries (out, n);
				return ERR_INVALID_DATA;
			}
			ok = OwnedEntryAdd (out, n, use_name, dec, dsize);
			FREE (dec);
		}
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
		return ERR_NOTHING_TO_DO;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

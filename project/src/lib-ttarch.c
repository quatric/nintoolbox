// SPDX-License-Identifier: GPL-2.0+
// Telltale Tool legacy "ttarch" archive (.ttarch). See lib-ttarch.h for the
// full layout and what was confirmed against real files vs. reused from a
// third-party reference implementation.

#include <zlib.h>
#include "lib-std.h"
#include "lib-ttarch.h"
#include "lib-archive-util.h"
#include "lib-nintendo.h"
#include <string.h>

// Single-shot raw-deflate (no zlib/gzip header) decompression of exactly
// one independently-compressed block into a caller-sized buffer. Used both
// for the (possibly chunked) file-table header and for each 64KiB data
// chunk -- both are raw deflate streams in every sample seen.
static enumError ttarch_inflate_raw (const u8 *src, uint src_size, u8 *dest, uint dest_cap, uint *out_size)
{
	z_stream zs;
	memset (&zs, 0, sizeof (zs));
	if (inflateInit2 (&zs, -15) != Z_OK)
		return ERR_INVALID_DATA;

	zs.next_in = (Bytef *)src;
	zs.avail_in = src_size;
	zs.next_out = (Bytef *)dest;
	zs.avail_out = dest_cap;

	int ret = inflate (&zs, Z_FINISH);
	const uint produced = dest_cap - zs.avail_out;
	inflateEnd (&zs);

	if (ret != Z_STREAM_END)
		return ERR_INVALID_DATA;
	if (out_size)
		*out_size = produced;
	return ERR_OK;
}

// Reads a little-endian u32 at *pos (bounds-checked against 'limit') and
// advances *pos by 4. Returns false (and leaves *pos unchanged) on overrun.
static bool tt_read_u32 (const u8 *base, size_t limit, size_t *pos, u32 *out)
{
	if (*pos + 4 > limit)
		return false;
	*out = rd_le32 (base + *pos);
	*pos += 4;
	return true;
}

enumError ScanTtarch (ttarch_t *tt, const u8 *data, size_t size)
{
	if (!tt || !data)
		return EINVAL;
	memset (tt, 0, sizeof (*tt));

	size_t p = 0;
	u32 version, encryption, unused_field, files_mode;
	if (!tt_read_u32 (data, size, &p, &version))
		return ERR_INVALID_DATA;
	// Only versions 8 and 9 were seen on the disc this was reverse-engineered
	// against; earlier versions have a materially different header layout
	// (fewer fields -- see lib-ttarch.h) that was never exercised or tested.
	if (version < 8 || version > 9)
		return ERR_INVALID_DATA;

	if (!tt_read_u32 (data, size, &p, &encryption)
		|| !tt_read_u32 (data, size, &p, &unused_field)
		|| !tt_read_u32 (data, size, &p, &files_mode))
		return ERR_INVALID_DATA;

	u32 chunk_count;
	if (!tt_read_u32 (data, size, &p, &chunk_count))
		return ERR_INVALID_DATA;
	if (chunk_count > 10000000 || p + (u64)chunk_count * 4 > size)
		return ERR_INVALID_DATA;
	const u32 *block_size = (const u32 *)(data + p);
	p += (size_t)chunk_count * 4;

	u32 file_data_size;
	if (!tt_read_u32 (data, size, &p, &file_data_size))
		return ERR_INVALID_DATA;
	// Sanity check confirmed exact (not just approximate) against real
	// files: the declared total equals the sum of the per-chunk sizes.
	u64 block_sum = 0;
	for (u32 i = 0; i < chunk_count; i++)
		block_sum += rd_le32 ((const u8 *)(block_size + i));
	if (chunk_count && block_sum != file_data_size)
		return ERR_INVALID_DATA;

	u32 priority, priority2, xmode1, xmode2;
	if (!tt_read_u32 (data, size, &p, &priority)
		|| !tt_read_u32 (data, size, &p, &priority2)
		|| !tt_read_u32 (data, size, &p, &xmode1)
		|| !tt_read_u32 (data, size, &p, &xmode2))
		return ERR_INVALID_DATA;

	u32 chunk_size_kb;
	if (!tt_read_u32 (data, size, &p, &chunk_size_kb))
		return ERR_INVALID_DATA;
	const u32 chunk_size = chunk_size_kb ? chunk_size_kb * 1024 : 65536;

	if (p + 1 > size)
		return ERR_INVALID_DATA;
	p += 1; // unknown_byte, version >= 8

	if (files_mode >= 1)
	{
		u32 crc32_unused;
		if (!tt_read_u32 (data, size, &p, &crc32_unused))
			return ERR_INVALID_DATA;
	}

	u32 header_size;
	if (!tt_read_u32 (data, size, &p, &header_size))
		return ERR_INVALID_DATA;
	if (!header_size && !tt_read_u32 (data, size, &p, &header_size))
		return ERR_INVALID_DATA;
	if (!header_size || header_size > 0x40000000)
		return ERR_INVALID_DATA;

	u8 *table = 0;
	if (files_mode >= 2)
	{
		u32 compressed_header_size;
		if (!tt_read_u32 (data, size, &p, &compressed_header_size))
			return ERR_INVALID_DATA;
		if (p + compressed_header_size > size)
			return ERR_INVALID_DATA;

		table = MALLOC (header_size);
		if (!table)
			return ERR_CANT_CREATE;
		uint produced = 0;
		enumError err = ttarch_inflate_raw (data + p, compressed_header_size, table, header_size, &produced);
		p += compressed_header_size;
		if (err || produced != header_size)
		{
			FREE (table);
			return ERR_INVALID_DATA;
		}
	}
	else
	{
		if (p + header_size > size)
			return ERR_INVALID_DATA;
		table = MALLOC (header_size);
		if (!table)
			return ERR_CANT_CREATE;
		memcpy (table, data + p, header_size);
		p += header_size;
	}

	const u32 files_offset = (u32)p;

	// Parse the decompressed/stored file table.
	size_t q = 0;
	u32 dir_count;
	if (!tt_read_u32 (table, header_size, &q, &dir_count) || dir_count > 1000000)
		goto invalid;
	for (u32 i = 0; i < dir_count; i++)
	{
		u32 name_len;
		if (!tt_read_u32 (table, header_size, &q, &name_len) || q + name_len > header_size)
			goto invalid;
		q += name_len;
	}

	u32 file_count;
	if (!tt_read_u32 (table, header_size, &q, &file_count) || !file_count || file_count > 10000000)
		goto invalid;

	ttarch_entry_t *entries = CALLOC (file_count, sizeof (ttarch_entry_t));
	if (!entries)
	{
		FREE (table);
		return ERR_CANT_CREATE;
	}

	for (u32 i = 0; i < file_count; i++)
	{
		u32 name_len, zero_unused, off, fsize;
		if (!tt_read_u32 (table, header_size, &q, &name_len) || q + name_len > header_size)
		{
			FREE (entries);
			goto invalid;
		}
		const u8 *name = table + q;
		q += name_len;

		if (!tt_read_u32 (table, header_size, &q, &zero_unused)
			|| !tt_read_u32 (table, header_size, &q, &off)
			|| !tt_read_u32 (table, header_size, &q, &fsize))
		{
			FREE (entries);
			goto invalid;
		}

		const size_t n = name_len < sizeof (entries[i].name) - 1 ? name_len : sizeof (entries[i].name) - 1;
		memcpy (entries[i].name, name, n);
		entries[i].name[n] = 0;
		for (char *cp = entries[i].name; *cp; cp++)
			if (*cp == '\\')
				*cp = '/';
		entries[i].offset = off;
		entries[i].size = fsize;
	}

	FREE (table);

	tt->raw = data;
	tt->raw_size = size;
	tt->version = version;
	tt->encryption = encryption;
	tt->files_mode = files_mode;
	tt->chunk_size = files_mode >= 2 ? chunk_size : 0;
	tt->chunk_count = chunk_count;
	tt->block_size = block_size;
	tt->files_offset = files_offset;
	tt->n_entries = file_count;
	tt->entries = entries;
	return ERR_OK;

invalid:
	FREE (table);
	return ERR_INVALID_DATA;
}

void ResetTtarch (ttarch_t *tt)
{
	if (!tt)
		return;
	if (tt->entries)
		FREE (tt->entries);
	memset (tt, 0, sizeof (*tt));
}

enumError ReadTtarchEntry (const ttarch_t *tt, const ttarch_entry_t *e, u8 **out_data, uint *out_size)
{
	if (!tt || !e || !out_data)
		return EINVAL;
	*out_data = 0;
	if (out_size)
		*out_size = 0;

	if (tt->files_mode <= 1)
	{
		// Stored directly: files_offset + entry.offset, uncompressed.
		if ((u64)tt->files_offset + e->offset + e->size > tt->raw_size)
			return ERR_INVALID_DATA;
		u8 *buf = MALLOC (e->size ? e->size : 1);
		if (!buf)
			return ERR_CANT_CREATE;
		memcpy (buf, tt->raw + tt->files_offset + e->offset, e->size);
		*out_data = buf;
		if (out_size)
			*out_size = e->size;
		return ERR_OK;
	}

	if (tt->files_mode != 2 || !tt->chunk_size)
		return ERR_INVALID_DATA; // unsupported/unverified mode

	// Chunked, independently raw-deflate-compressed data region: each
	// chunk decompresses to exactly tt->chunk_size bytes (confirmed against
	// every chunk checked, including mid-stream ones picked at random --
	// there is no cross-chunk dictionary/back-reference dependency).
	const u32 start_chunk = e->offset / tt->chunk_size;
	const u32 last_byte = e->size ? e->offset + e->size - 1 : e->offset;
	const u32 end_chunk = last_byte / tt->chunk_size;
	if (end_chunk >= tt->chunk_count)
		return ERR_INVALID_DATA;

	// Compressed-chunk offsets are only known cumulatively.
	u64 cursor = tt->files_offset;
	for (u32 i = 0; i < start_chunk; i++)
		cursor += rd_le32 ((const u8 *)(tt->block_size + i));

	u8 *buf = MALLOC (e->size ? e->size : 1);
	if (!buf)
		return ERR_CANT_CREATE;

	u8 *chunk = MALLOC (tt->chunk_size);
	if (!chunk)
	{
		FREE (buf);
		return ERR_CANT_CREATE;
	}

	enumError err = ERR_OK;
	for (u32 c = start_chunk; c <= end_chunk && !err; c++)
	{
		const u32 comp_size = rd_le32 ((const u8 *)(tt->block_size + c));
		if (cursor + comp_size > tt->raw_size)
		{
			err = ERR_INVALID_DATA;
			break;
		}
		uint produced = 0;
		err = ttarch_inflate_raw (tt->raw + cursor, comp_size, chunk, tt->chunk_size, &produced);
		cursor += comp_size;
		if (err)
			break;

		const u64 chunk_logical_start = (u64)c * tt->chunk_size;
		const u64 want_start = chunk_logical_start > e->offset ? chunk_logical_start : e->offset;
		const u64 want_end_excl = e->offset + (u64)e->size < chunk_logical_start + produced
			? e->offset + (u64)e->size : chunk_logical_start + produced;
		if (want_end_excl > want_start)
			memcpy (buf + (want_start - e->offset), chunk + (want_start - chunk_logical_start),
				want_end_excl - want_start);
	}

	FREE (chunk);
	if (err)
	{
		FREE (buf);
		return err;
	}

	*out_data = buf;
	if (out_size)
		*out_size = e->size;
	return ERR_OK;
}

enumError ExtractTtarch (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".ttarch"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	ttarch_t tt;
	err = ScanTtarch (&tt, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	if (tt.encryption != 0 || (tt.files_mode > 2))
	{
		// Encrypted (Blowfish key not available/verified for any game) or
		// an unrecognised files_mode: refuse rather than guess.
		ResetTtarch (&tt);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT TTARCH:%s (%u files, mode %u) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			arg, tt.n_entries, tt.files_mode, dest);

	for (uint i = 0; i < tt.n_entries; i++)
	{
		const ttarch_entry_t *e = tt.entries + i;

		char rel[300];
		if (e->name[0] && OwnedNameOk (e->name))
			snprintf (rel, sizeof (rel), "%s", e->name);
		else
			snprintf (rel, sizeof (rel), "%06u.bin", i);

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, rel);

		if (testmode)
			continue;

		u8 *fdata = 0;
		uint fsize = 0;
		if (ReadTtarchEntry (&tt, e, &fdata, &fsize))
			continue; // corrupt entry; skip rather than abort the whole archive

		CreatePath (out_path, false);
		SaveFile (out_path, 0, 0, fdata, fsize, 0);
		FREE (fdata);
	}

	ResetTtarch (&tt);
	FREE (raw);
	return ERR_OK;
}

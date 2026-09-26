// SPDX-License-Identifier: GPL-2.0+
// Split out of lib-nintendo-archives.c -- one archive format per file.
#include "lib-nintendo-archives.h"
#include "lib-nintendo.h"
#include "lib-image.h"
#include "lib-camelot.h"
#include "lib-yay0.h"
#include "lib-flim.h"
#include "lib-szs.h"
#include "lib-std.h"
#include "lib-zstd.h"
#include "lib-archive-util.h"
#include <zlib.h>
#include <stdlib.h>
#include <string.h>

// ----------------------------------------------------------------------------
// Pokemon Stadium (N64) PERS-SZP asset container
//
// A 24-byte header wrapping an ordinary Yay0 stream, big-endian:
//
//   0x00  "PERS-SZP"
//   0x08  u32 header size (0x18 in every instance on the retail cart)
//   0x0c  u32 decompressed size
//   0x10  u32 decompressed size again
//   0x14  u32 zero
//   0x18  Yay0 stream
//
// Established against Pokemon Stadium (USA, Rev 1), which carries 410 of
// them. The sibling "FRAGMENT" signature in the same ROM is a different
// thing entirely -- a MIPS code overlay with a 0x20-byte header, not an
// asset -- so it is deliberately not claimed here.
// ----------------------------------------------------------------------------
// Animal Crossing: Pocket Camp .zdat
//
// A flat container: a 16-bit header naming three offsets and an entry count,
// then one 16-byte record per file (name length, size, uncompressed size, and
// a word that is always zero), then the names back to back, then the payloads
// back to back. Verified against 31 files pulled from the game's own CDN --
// single-entry up to 45 entries -- where the arithmetic closes exactly: the
// name table ends where the data begins, and the last payload ends at EOF.
//
// Every payload is a Unity asset bundle XORed with one repeated byte. The key
// is not a secret held elsewhere; it falls out of the bundle's own "UnityFS"
// signature, and the check below is what confirms it: a bundle records its own
// total length, and across all 168 payloads seen that length matched the entry
// size exactly once the key was applied. A wrong key does not survive that.
#define ZDAT_ENT_SIZE 16

static enumError zdat_read_header (
	const u8 *data, size_t size, uint *ent_off, uint *name_off, uint *data_off, uint *count)
{
	if (size < 0x30 || memcmp (data, "ZDAT", 4))
		return ERR_NOTHING_TO_DO;

	*ent_off = rd_le16 (data + 0x06);
	*name_off = rd_le16 (data + 0x0a);
	*data_off = rd_le16 (data + 0x0e);
	*count = rd_le16 (data + 0x12);

	// The name table begins immediately after the entry array, so its offset
	// is fixed by the count rather than being free: 0x30 only for a
	// single-entry container, further out for the rest (45 entries push it to
	// 0x2f0). Requiring 0x30 here would reject every archive holding more
	// than one file.
	if (*ent_off != 0x20 || !*count)
		return ERR_INVALID_DATA;
	if (*name_off != *ent_off + (u64)*count * ZDAT_ENT_SIZE)
		return ERR_INVALID_DATA;
	if (*data_off > size)
		return ERR_INVALID_DATA;
	return ERR_OK;
}

// Recover the repeated byte from the known signature, then require the bundle
// to agree about its own size. Returns 0 when this is not a bundle at all and
// stores the repeated byte in *key_out (0 for non-bundles). The buffer is
// unmasked in place.
static int zdat_unmask (u8 *p, uint size, u8 *key_out)
{
	*key_out = 0;
	static const char sig[] = "UnityFS";
	const uint siglen = sizeof (sig);
	if (size < 0x20)
		return 0;

	const u8 key = p[0] ^ (u8)sig[0];
	for (uint i = 1; i < siglen; i++)
		if ((p[i] ^ key) != (u8)sig[i])
			return 0;

	// UnityFS: signature, u32 format, two NUL-terminated version strings,
	// then the bundle's own total size as a big-endian u64. Inspect masked
	// bytes first: failed probes must leave the payload and key unchanged.
	uint off = 8 + 4;
	for (int i = 0; i < 2; i++)
	{
		while (off < size && (p[off] ^ key))
			off++;
		if (off >= size)
			return 0;
		off++;
	}
	if (size - off < 8)
		return 0;

	u64 total = 0;
	for (int i = 0; i < 8; i++)
		total = total << 8 | (p[off + i] ^ key);
	if (total != size)
		return 0;

	for (uint i = 0; i < size; i++)
		p[i] ^= key;
	*key_out = key;
	return 1;
}

enumError ExtractZDATArchive (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	uint ent_off, name_off, data_off, count;
	enumError err = zdat_read_header (raw, raw_size, &ent_off, &name_off, &data_off, &count);
	if (err)
	{
		FREE (raw);
		return err;
	}

	// Walk the table once before writing anything: a container whose entries
	// do not add up to the file is not one of these, and half an extraction
	// is worse than none.
	u64 name_pos = name_off, data_pos = data_off;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = raw + ent_off + (size_t)i * ZDAT_ENT_SIZE;
		const u32 nlen = rd_le32 (e);
		const u32 size = rd_le32 (e + 4);
		if (!nlen || nlen > 4096 || name_pos + nlen > data_off)
		{
			FREE (raw);
			return ERR_INVALID_DATA;
		}
		if (data_pos + size > raw_size)
		{
			FREE (raw);
			return ERR_INVALID_DATA;
		}
		name_pos += nlen;
		data_pos += size;
	}
	if (name_pos != data_off || data_pos != raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ZDAT:%s (%u file%s) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, count == 1 ? "" : "s", dest);

	name_pos = name_off;
	data_pos = data_off;
	u8 *keys = MALLOC (count ? count : 1);
	if (!keys)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint written = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = raw + ent_off + (size_t)i * ZDAT_ENT_SIZE;
		const u32 nlen = rd_le32 (e);
		const u32 size = rd_le32 (e + 4);

		char name[PATH_MAX];
		const uint keep = nlen < sizeof (name) - 1 ? nlen : sizeof (name) - 1;
		memcpy (name, raw + name_pos, keep);
		name[keep] = 0;
		name_pos += nlen;

		// Stored names may carry a directory part of their own.
		for (char *q = name; *q; q++)
			if (*q == '\\' || (*q == '.' && q[1] == '.'))
				*q = '_';

		u8 *payload = MALLOC (size ? size : 1);
		if (!payload)
		{
			FREE (keys);
			FREE (raw);
			return ERR_OUT_OF_MEMORY;
		}
		memcpy (payload, raw + data_pos, size);
		data_pos += size;

		const int unmasked = zdat_unmask (payload, size, &keys[i]);

		char out[PATH_MAX];
		snprintf (out, sizeof (out), "%s/%s", dest, name);
		if (!testmode)
		{
			CreatePath (out, false);
			if (!SaveFile (out, 0, 0, payload, size, 0))
				written++;
		}
		else
			written++;

		if (verbose > 0)
			fprintf (stdlog, "  %-40s %8u bytes%s\n", name, size,
				unmasked ? "" : "  (not a Unity bundle, stored as found)");

		FREE (payload);
	}

	// Remember each member's XOR key so CREATE can restore the original
	// bytes (the key is not recoverable from the unmasked file on disk).
	if (!testmode && written == count)
	{
		char cache[PATH_MAX];
		snprintf (cache, sizeof (cache), "%s/%s", dest, ZDAT_CACHE_FILE);
		if (verbose > 0)
			fprintf (stdlog, "  writing %s\n", cache);
		FILE *f = fopen (cache, "w");
		if (f)
		{
			fprintf (f, "# Animal Crossing: Pocket Camp ZDAT extraction cache\n");
			fprintf (f, "# <name>[TAB]<xor-key>\\n -- fed back by CREATE for byte-exact repacks\n");
			name_pos = name_off;
			for (uint i = 0; i < count; i++)
			{
				const u8 *e = raw + ent_off + (size_t)i * ZDAT_ENT_SIZE;
				const u32 nlen = rd_le32 (e);
				char name[PATH_MAX];
				const uint keep = nlen < sizeof (name) - 1 ? nlen : sizeof (name) - 1;
				memcpy (name, raw + name_pos, keep);
				name[keep] = 0;
				name_pos += nlen;
				for (char *q = name; *q; q++)
					if (*q == '\\' || (*q == '.' && q[1] == '.'))
						*q = '_';
				fprintf (f, "%s\t%u\n", name, keys[i]);
			}
			fclose (f);
		}
	}
	FREE (keys);

	FREE (raw);
	return written ? ERR_OK : ERR_INVALID_DATA;
}

// Rebuild a ZDAT archive. The layout mirrors ExtractZDATArchive's expectations
// exactly (header words, entry records, names back to back, payloads), and
// mask_keys restore the original payload bytes that the extractor unmasked.
enumError CreateZDATArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries,
	uint n_entries, const u8 *mask_keys)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xffff)
		return EINVAL;

	// name_off = 0x20 + count*16, data_off = name_off + names, payloads then
	// follow. Every offset word is u16 LE, so a container that pushes the data
	// region past 0xffff is unrepresentable in this format.
	u64 name_off = 0x20 + (u64)n_entries * ZDAT_ENT_SIZE;
	u64 data_off = name_off;
	u64 cur = data_off;
	if (name_off > 0xffff)
		return EFBIG;
	for (uint i = 0; i < n_entries; i++)
	{
		const ccp name = entries[i].name ? entries[i].name : "";
		const uint nlen = (uint)strlen (name);
		if (!nlen || nlen > 4096 || (entries[i].size && !entries[i].data))
			return EINVAL;
		cur += (u64)nlen + entries[i].size;
		if (data_off + nlen > 0xffff || cur > NFMT_MAX_OUTPUT)
			return EFBIG;
		data_off += nlen;
	}
	// The loop above advanced data_off to its final value.
	const u64 final_data_off = data_off;

	u8 *out = CALLOC (1, (size_t)cur);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, "ZDAT", 4);
	wr_le16 (out + 0x06, 0x20);
	wr_le16 (out + 0x0a, (u16)name_off);
	wr_le16 (out + 0x0e, (u16)final_data_off);
	wr_le16 (out + 0x12, n_entries);

	u64 name_pos = name_off;
	uint data_pos = (uint)final_data_off;
	for (uint i = 0; i < n_entries; i++)
	{
		const ccp name = entries[i].name ? entries[i].name : "";
		const uint nlen = (uint)strlen (name);
		u8 *e = out + 0x20 + (size_t)i * ZDAT_ENT_SIZE;
		wr_le32 (e, nlen);
		wr_le32 (e + 4, entries[i].size);
		wr_le32 (e + 8, entries[i].size);
		wr_le32 (e + 12, 0);
		memcpy (out + name_pos, name, nlen);
		name_pos += nlen;

		const uint size = entries[i].size;
		if (size)
			memcpy (out + data_pos, entries[i].data, size);
		const u8 key = mask_keys ? mask_keys[i] : 0;
		if (key)
			for (uint j = 0; j < size; j++)
				out[data_pos + j] ^= key;
		data_pos += size;
	}

	*dest = out;
	*dest_size = (uint)cur;
	return ERR_OK;
}

// Animal Crossing: Pocket Camp ZDAT. EXTRACT writes .zdat-cache.txt next to
// the members so a CREATE can restore the original bytes: the XOR key is not
// recoverable from the unmasked files on disk, and the archive's own entry
// order (which is not alphabetical) is remembered the same way.
enumError read_zdat_cache (ccp source, ccp **names_out, u8 **keys_out, uint *n_out)
{
	*names_out = 0;
	*keys_out = 0;
	*n_out = 0;

	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/%s", source, ZDAT_CACHE_FILE);
	FILE *f = fopen (path, "r");
	if (!f)
		return ERR_NOTHING_TO_DO;

	ccp *names = 0;
	u8 *keys = 0;
	uint n = 0, cap = 0;
	char line[PATH_MAX];
	while (fgets (line, sizeof (line), f))
	{
		char *p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '#' || !*p)
			continue;
		// <name>[TAB]<key>
		char *tab = strchr (p, '\t');
		if (tab)
			*tab = 0;
		char *end = p + strlen (p);
		while (end > p && (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' ' || end[-1] == '\t'))
			*--end = 0;
		if (!*p)
			continue;
		unsigned key = 0;
		if (!tab || !*p || sscanf (tab + 1, "%u", &key) != 1 || key > 255)
			continue;
		if (n == cap)
		{
			cap = cap ? cap * 2 : 16;
			names = REALLOC (names, cap * sizeof (*names));
			keys = REALLOC (keys, cap);
		}
		names[n] = STRDUP (p);
		keys[n] = (u8)key;
		n++;
	}
	fclose (f);

	if (!n)
	{
		FREE (names);
		FREE (keys);
		return ERR_NOTHING_TO_DO;
	}
	*names_out = names;
	*keys_out = keys;
	*n_out = n;
	return ERR_OK;
}

// Anonymous entries (never written into the directory) mean the container
// cannot be reproduced byte for byte; they are still zipped up in sorted
// order so nothing is silently dropped.
enumError create_zdat_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;

	ccp *cache_names = 0;
	u8 *cache_keys = 0;
	uint n_cache = 0;
	if (!err)
	{
		enumError c_err = read_zdat_cache (source, &cache_names, &cache_keys, &n_cache);
		if (c_err != ERR_NOTHING_TO_DO && c_err != ERR_OK)
			err = c_err;
	}

	u8 *data = 0;
	uint size = 0;
	if (!err)
	{
		// Reorder the (sorted) collected entries to match the original
		// archive: every cached name first, in cache order, then any
		// uncached member in sorted order with an all-zero mask.
		nintendo_sarc_entry_t *ordered = CALLOC (list.used, sizeof (*ordered));
		u8 *keys = CALLOC (list.used, 1);
		bool *done = CALLOC (list.used, 1);
		if (!ordered || !keys || !done)
			err = ERR_OUT_OF_MEMORY;
		uint out_count = 0;
		if (!err)
		{
			for (uint c = 0; c < n_cache; c++)
				for (uint i = 0; i < list.used; i++)
				{
					if (done[i] || strcmp (list.entry[i].name, cache_names[c]))
						continue;
					done[i] = true;
					ordered[out_count] = list.entry[i];
					keys[out_count] = cache_keys[c];
					list.entry[i].name = 0;
					list.entry[i].data = 0;
					out_count++;
					break;
				}
			for (uint i = 0; i < list.used; i++)
			{
				if (done[i])
					continue;
				ordered[out_count] = list.entry[i];
				keys[out_count] = 0;
				list.entry[i].name = 0;
				list.entry[i].data = 0;
				out_count++;
			}
			err = CreateZDATArchive (&data, &size, ordered, out_count, keys);
		}
		for (uint i = 0; i < out_count; i++)
		{
			FREE ((void *)ordered[i].name);
			FREE ((void *)ordered[i].data);
		}
		FREE (ordered);
		FREE (keys);
		FREE (done);
	}
	for (uint i = 0; i < n_cache; i++)
		FREE ((void *)cache_names[i]);
	FREE (cache_names);
	FREE (cache_keys);

	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

// ----------------------------------------------------------------------------
// Mario Party 3DS compressed archive ("RZPK").
//
// Port of MPLibrary/MPLibrary/3DS/ZDAT.cs (KillzXGaming/MPLibrary, "A Mario
// Party Library"): LE header "RZPK" + u32 version + u32 num_files + u32
// data_offset + u32 data_size, entries at 0x20 as 44-byte records
// (char name[0x20] NUL-padded + u32 decompressed_size + u32 size + u32 offset
// relative to data_offset), each member a zlib stream. The C# reader seeks
// per-entry to 32 + i*44 and to data_offset + offset; the layout below
// mirrors that exactly.
#define RZPK_ENT_SIZE 44
#define RZPK_HDR_SIZE 0x20

bool IsRZPK (const u8 *data, size_t size)
{
	if (!data || size < RZPK_HDR_SIZE)
		return false;
	if (memcmp (data, "RZPK", 4))
		return false;
	const u32 num = rd_le32 (data + 8);
	const u32 data_off = rd_le32 (data + 12);
	if (!num || num > 10000)
		return false;
	if (data_off < RZPK_HDR_SIZE || (u64)data_off > size)
		return false;
	if ((u64)RZPK_HDR_SIZE + (u64)num * RZPK_ENT_SIZE > size)
		return false;
	return true;
}

enumError ScanRZPK (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || !IsRZPK (data, size))
		return ERR_INVALID_DATA;

	const u32 num = rd_le32 (data + 8);
	const u32 data_off = rd_le32 (data + 12);

	*entries = 0;
	*n_entries = 0;

	nintendo_sarc_entry_t *out = CALLOC (num ? num : 1, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint out_cnt = 0;
	for (uint i = 0; i < num; i++)
	{
		const u8 *e = data + RZPK_HDR_SIZE + (size_t)i * RZPK_ENT_SIZE;
		char name[0x21];
		memcpy (name, e, 0x20);
		name[0x20] = 0;
		// MPLibrary reads the name with ReadString(0x20, true): NUL-trimmed.
		name[strnlen (name, 0x20)] = 0;
		const u32 decomp_size = rd_le32 (e + 0x20);
		const u32 comp_size = rd_le32 (e + 0x24);
		const u32 rel_off = rd_le32 (e + 0x28);
		if ((u64)data_off + rel_off + comp_size > size)
			continue;

		u8 *decomp = 0;
		uint decomp_len = 0;
		if (DecodeZlibGrow (&decomp, &decomp_len, data + data_off + rel_off, comp_size) != ERR_OK
			|| !decomp)
			continue;
		// Trust the inflate output but keep the header's size as a sanity
		// check in verbose mode rather than truncating real data.
		if (decomp_size && decomp_len != decomp_size && verbose > 1)
			fprintf (stdlog, "RZPK: %s inflated to %u bytes, header says %u\n",
				name[0] ? name : "unnamed", decomp_len, decomp_size);

		char entry_name[PATH_MAX];
		if (name[0])
			snprintf (entry_name, sizeof (entry_name), "%s", name);
		else
			snprintf (entry_name, sizeof (entry_name), "file_%05u.bin", i);
		for (char *q = entry_name; *q; q++)
			if (*q == '\\' || (*q == '.' && q[1] == '.'))
				*q = '_';

		if (!OwnedEntryAdd (out, out_cnt++, entry_name, decomp, decomp_len))
		{
			FREE (decomp);
			continue;
		}
		FREE (decomp);
	}

	if (!out_cnt)
	{
		FREE (out);
		return ERR_NOTHING_TO_DO;
	}

	*entries = out;
	*n_entries = out_cnt;
	return ERR_OK;
}

enumError ExtractRZPKArchive (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX || !IsRZPK (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	enumError err = ScanRZPK (&entries, &n_entries, raw, (uint)raw_size);
	FREE (raw);
	if (err || !n_entries)
	{
		if (!err)
			err = ERR_NOTHING_TO_DO;
		return err;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT RZPK:%s (%u file%s) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_entries, n_entries == 1 ? "" : "s", dest);

	uint written = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		char out[PATH_MAX];
		snprintf (out, sizeof (out), "%s/%s", dest, entries[i].name);
		char *slash = strrchr (out, '/');
		if (slash)
		{
			*slash = 0;
			CreatePath (out, true);
			*slash = '/';
		}
		if (!testmode)
		{
			if (!SaveFile (out, 0, 0, entries[i].data, entries[i].size, 0))
				written++;
		}
		else
			written++;
		if (verbose > 0)
			fprintf (stdlog, "  %-40s %8u bytes\n", entries[i].name, entries[i].size);
	}

	ResetOwnedEntries (entries, n_entries);
	FREE (entries);

	(void)depth;
	if (!written)
		return ERR_INVALID_DATA;
	return ERR_OK;
}

// Rebuild an RZPK archive: entries in directory order, each member
// zlib-compressed (level 9). Names are truncated to 0x1F chars + NUL to fit
// the 0x20-byte field.
enumError CreateRZPKArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 10000)
		return EINVAL;

	u8 **comp_data = CALLOC (n_entries, sizeof (*comp_data));
	u32 *comp_size = CALLOC (n_entries, sizeof (*comp_size));
	u8 (*names)[0x20] = CALLOC (n_entries, sizeof (*names));
	if (!comp_data || !comp_size || !names)
	{
		FREE (comp_data);
		FREE (comp_size);
		FREE (names);
		return ERR_OUT_OF_MEMORY;
	}

	u64 payload_total = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		const ccp base = entries[i].name ? entries[i].name : "";
		ccp slash = strrchr (base, '/');
		ccp leaf = slash ? slash + 1 : base;
		snprintf ((char *)names[i], 0x20, "%s", leaf);

		uLongf bound = compressBound (entries[i].size ? entries[i].size : 1);
		comp_data[i] = MALLOC (bound ? (size_t)bound : 1);
		if (!comp_data[i])
		{
			for (uint k = 0; k < i; k++)
				FREE (comp_data[k]);
			FREE (comp_data);
			FREE (comp_size);
			FREE (names);
			return ERR_OUT_OF_MEMORY;
		}
		uLongf out_len = bound;
		const int zerr
			= compress2 (comp_data[i], &out_len, entries[i].data ? entries[i].data : (const u8 *)"",
				entries[i].size, Z_BEST_COMPRESSION);
		if (zerr != Z_OK)
		{
			for (uint k = 0; k <= i; k++)
				FREE (comp_data[k]);
			FREE (comp_data);
			FREE (comp_size);
			FREE (names);
			return ERR_CANT_CREATE;
		}
		comp_size[i] = (u32)out_len;
		payload_total += out_len;
		if (payload_total > NFMT_MAX_OUTPUT)
		{
			for (uint k = 0; k < n_entries; k++)
				FREE (comp_data[k]);
			FREE (comp_data);
			FREE (comp_size);
			FREE (names);
			return EFBIG;
		}
	}

	const u32 data_off = RZPK_HDR_SIZE + n_entries * RZPK_ENT_SIZE;
	const u64 total = (u64)data_off + payload_total;
	if (total > NFMT_MAX_OUTPUT)
	{
		for (uint i = 0; i < n_entries; i++)
			FREE (comp_data[i]);
		FREE (comp_data);
		FREE (comp_size);
		FREE (names);
		return EFBIG;
	}

	u8 *out = CALLOC (1, (size_t)total);
	if (!out)
	{
		for (uint i = 0; i < n_entries; i++)
			FREE (comp_data[i]);
		FREE (comp_data);
		FREE (comp_size);
		FREE (names);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (out, "RZPK", 4);
	wr_le32 (out + 4, 1); // version (1 is observed)
	wr_le32 (out + 8, n_entries);
	wr_le32 (out + 12, data_off);
	wr_le32 (out + 16, (u32)payload_total);

	u32 cur = data_off;
	for (uint i = 0; i < n_entries; i++)
	{
		u8 *e = out + RZPK_HDR_SIZE + (size_t)i * RZPK_ENT_SIZE;
		memcpy (e, names[i], 0x20);
		wr_le32 (e + 0x20, entries[i].size);
		wr_le32 (e + 0x24, comp_size[i]);
		wr_le32 (e + 0x28, cur - data_off);
		memcpy (out + cur, comp_data[i], comp_size[i]);
		cur += comp_size[i];
		FREE (comp_data[i]);
	}
	FREE (comp_data);
	FREE (comp_size);
	FREE (names);

	*dest = out;
	*dest_size = (uint)total;
	return ERR_OK;
}

enumError create_rzpk_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;

	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateRZPKArchive (&data, &size, list.entry, list.used);

	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

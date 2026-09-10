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
// Bandai Namco NUS3AUDIO audio archive (.nus3audio / NUS3)
//
// Used by Super Smash Bros. Ultimate (and Namco's other NUS3 middleware
// titles) to bundle per-track audio streams. Little-endian throughout:
//
//   0x00  "NUS3"
//   0x04  u32 body size
//   0x08  chunks, each an 8-byte tag (4 characters, NUL-padded) followed by
//         a u32 size and that many payload bytes:
//           AUDIINDX  u32 track count
//           TNID      u32 track id per track
//           NMOF      u32 offset into TNNM per track
//           ADOF      u32 offset, u32 size into PACK per track
//           TNNM      name table: u8 length, name bytes, NUL terminator
//           PACK      the concatenated track payloads
//
// Track payloads are whole audio files. IDSP and Opus are the two that turn
// up in practice, so name members by their own magic and fall back to .bin.
// ----------------------------------------------------------------------------
enumError ExtractNUS3AudioArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".nus3audio") && !is_ext_match (arg, ".nus3bank")
		&& !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (raw_size < 16 || memcmp (raw, "NUS3", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u8 *nmof = 0, *adof = 0, *tnnm = 0;
	uint nmof_size = 0, adof_size = 0, tnnm_size = 0;
	const u8 *pack = 0;
	uint pack_size = 0;
	u32 n_tracks = 0;

	for (size_t pos = 8; pos + 12 <= raw_size;)
	{
		const u8 *tag = raw + pos;
		const u32 csize = rd_le32 (raw + pos + 8);
		const size_t payload = pos + 12;
		if (csize > raw_size - payload)
			break;

		if (!memcmp (tag, "AUDIINDX", 8) && csize >= 4)
			n_tracks = rd_le32 (raw + payload);
		else if (!memcmp (tag, "NMOF", 4))
			nmof = raw + payload, nmof_size = csize;
		else if (!memcmp (tag, "ADOF", 4))
			adof = raw + payload, adof_size = csize;
		else if (!memcmp (tag, "TNNM", 4))
			tnnm = raw + payload, tnnm_size = csize;
		else if (!memcmp (tag, "PACK", 4))
			pack = raw + payload, pack_size = csize;

		pos = payload + csize;
	}

	if (!n_tracks || n_tracks > 100000 || !adof || !pack || adof_size < (u64)n_tracks * 8)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NUS3AUDIO:%s (%u tracks) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_tracks, dest);

	for (uint i = 0; i < n_tracks; i++)
	{
		const u32 off = rd_le32 (adof + i * 8);
		const u32 size = rd_le32 (adof + i * 8 + 4);
		if (off > pack_size || size > pack_size - off)
			continue;
		const u8 *data = pack + off;

		// Names are optional: a track without one is keyed by its index.
		char name[PATH_MAX];
		name[0] = 0;
		if (tnnm && nmof && nmof_size >= (u64)(i + 1) * 4)
		{
			const u32 noff = rd_le32 (nmof + i * 4);
			if (noff < tnnm_size)
			{
				const uint nlen = tnnm[noff];
				if (nlen && noff + 1 + nlen <= tnnm_size)
				{
					memcpy (name, tnnm + noff + 1, nlen);
					name[nlen] = 0;
				}
			}
		}
		// Track names come straight out of the file, so keep them to a
		// single plain filename rather than letting one escape the
		// destination directory.
		bool name_ok = name[0] != 0;
		for (ccp c = name; name_ok && *c; c++)
			if (*c == '/' || *c == '\\' || (u8)*c < 0x20)
				name_ok = false;
		if (name_ok && (!strcmp (name, ".") || !strcmp (name, "..")))
			name_ok = false;
		if (!name_ok)
			snprintf (name, sizeof (name), "track_%04u", i);

		ccp ext = ".bin";
		if (size >= 4)
		{
			if (!memcmp (data, "IDSP", 4))
				ext = ".idsp";
			else if (!memcmp (data, "OPUS", 4) || !memcmp (data, "OpusHead", 4))
				ext = ".lopus";
			else if (!memcmp (data, "BNSF", 4))
				ext = ".bnsf";
			else if (!memcmp (data, "RIFF", 4))
				ext = ".wav";
		}

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s%s", dest, name, ext);
		if (!testmode && size)
			SaveFile (out_path, 0, 0, data, size, 0);
	}

	FREE (raw);
	return ERR_OK;
}


enumError CreateNUS3AudioArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	// Strip directory and extension from each entry to get the track name
	char (*names)[PATH_MAX] = CALLOC (n_entries, sizeof (*names));
	if (!names)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	u32 tnnm_size = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp leaf = leaf_name (sorted[i].name);
		snprintf (names[i], sizeof (names[i]), "%s", leaf);
		char *dot = strrchr (names[i], '.');
		if (dot)
			*dot = 0;
		tnnm_size += 1 + (u32)strlen (names[i]) + 1; // 1 byte len + chars + NUL
	}

	const u32 audiindx_len = 4;
	const u32 tnid_len = n_entries * 4;
	const u32 nmof_len = n_entries * 4;
	const u32 adof_len = n_entries * 8;
	const u32 tnnm_chunk_len = tnnm_size;

	u32 pack_len = 0;
	for (uint i = 0; i < n_entries; i++)
		pack_len += sorted[i].size;

	const u32 body_size = (8 + 4 + audiindx_len) + (8 + 4 + tnid_len) + (8 + 4 + nmof_len)
		+ (8 + 4 + adof_len) + (8 + 4 + tnnm_chunk_len) + (8 + 4 + pack_len);

	const u32 total_size = 8 + body_size;
	u8 *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (names);
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (out, "NUS3", 4);
	wr_le32 (out + 4, body_size);

	u32 pos = 8;

	// 1. AUDIINDX
	memcpy (out + pos, "AUDIINDX", 8);
	wr_le32 (out + pos + 8, audiindx_len);
	wr_le32 (out + pos + 12, n_entries);
	pos += 12 + audiindx_len;

	// 2. TNID
	memcpy (out + pos, "TNID\0\0\0\0", 8);
	wr_le32 (out + pos + 8, tnid_len);
	for (uint i = 0; i < n_entries; i++)
		wr_le32 (out + pos + 12 + i * 4, 100 + i);
	pos += 12 + tnid_len;

	// 3. NMOF
	memcpy (out + pos, "NMOF\0\0\0\0", 8);
	wr_le32 (out + pos + 8, nmof_len);
	u32 cur_tnnm_off = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		wr_le32 (out + pos + 12 + i * 4, cur_tnnm_off);
		cur_tnnm_off += 1 + (u32)strlen (names[i]) + 1;
	}
	pos += 12 + nmof_len;

	// 4. ADOF
	memcpy (out + pos, "ADOF\0\0\0\0", 8);
	wr_le32 (out + pos + 8, adof_len);
	u32 cur_pack_off = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		wr_le32 (out + pos + 12 + i * 8, cur_pack_off);
		wr_le32 (out + pos + 12 + i * 8 + 4, sorted[i].size);
		cur_pack_off += sorted[i].size;
	}
	pos += 12 + adof_len;

	// 5. TNNM
	memcpy (out + pos, "TNNM\0\0\0\0", 8);
	wr_le32 (out + pos + 8, tnnm_chunk_len);
	u32 tnnm_payload = pos + 12;
	for (uint i = 0; i < n_entries; i++)
	{
		const u8 nlen = (u8)strlen (names[i]);
		out[tnnm_payload++] = nlen;
		memcpy (out + tnnm_payload, names[i], nlen);
		tnnm_payload += nlen;
		out[tnnm_payload++] = 0; // NUL terminator
	}
	pos += 12 + tnnm_chunk_len;

	// 6. PACK
	memcpy (out + pos, "PACK\0\0\0\0", 8);
	wr_le32 (out + pos + 8, pack_len);
	u32 pack_payload = pos + 12;
	for (uint i = 0; i < n_entries; i++)
	{
		if (sorted[i].data && sorted[i].size)
			memcpy (out + pack_payload, sorted[i].data, sorted[i].size);
		pack_payload += sorted[i].size;
	}

	FREE (names);
	FREE (sorted);

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}

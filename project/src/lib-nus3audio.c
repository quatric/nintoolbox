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
//   0x08  chunks. The first, AUDIINDX, has an 8-byte tag; every later chunk
//         has a 4-byte tag. Each is followed by a u32 size and that many
//         payload bytes:
//           AUDIINDX  u32 track count
//           TNID      u32 track id per track
//           NMOF      u32 offset into TNNM per track
//           ADOF      u32 offset, u32 size per track -- offsets are absolute
//                     from the start of the file, and point into PACK
//           TNNM      name table: NUL-terminated strings
//           JUNK      alignment padding
//           PACK      the concatenated track payloads
//
// Track payloads are whole audio files. IDSP and Opus are the two that turn
// up in practice, so name members by their own magic and fall back to .bin.
// ----------------------------------------------------------------------------

typedef struct nus3_t
{
	const u8 *nmof, *adof, *tnnm;
	uint nmof_size, adof_size, tnnm_size;
	u32 n_tracks;
	bool has_pack;
} nus3_t;

static bool nus3_parse (const u8 *raw, size_t size, nus3_t *n)
{
	memset (n, 0, sizeof (*n));
	if (size < 16 || memcmp (raw, "NUS3", 4))
		return false;

	for (size_t pos = 8; pos + 8 <= size;)
	{
		const uint tag_len = pos + 12 <= size && !memcmp (raw + pos, "AUDIINDX", 8) ? 8 : 4;
		const u8 *tag = raw + pos;
		const u32 csize = rd_le32 (raw + pos + tag_len);
		const size_t payload = pos + tag_len + 4;
		if (csize > size - payload)
			break;

		if (tag_len == 8 && csize >= 4)
			n->n_tracks = rd_le32 (raw + payload);
		else if (!memcmp (tag, "NMOF", 4))
			n->nmof = raw + payload, n->nmof_size = csize;
		else if (!memcmp (tag, "ADOF", 4))
			n->adof = raw + payload, n->adof_size = csize;
		else if (!memcmp (tag, "TNNM", 4))
			n->tnnm = raw + payload, n->tnnm_size = csize;
		else if (!memcmp (tag, "PACK", 4))
			n->has_pack = true;

		pos = payload + csize;
	}

	return n->n_tracks && n->n_tracks <= 100000 && n->adof && n->has_pack
		&& n->adof_size >= (u64)n->n_tracks * 8;
}

// Track name as the extractor gives it: the TNNM string NMOF points at, or a
// plain index-based one when it is missing or not a usable filename.
static void nus3_track_name (const nus3_t *n, uint i, char *name, size_t name_size)
{
	name[0] = 0;
	if (n->tnnm && n->nmof && n->nmof_size >= (u64)(i + 1) * 4)
	{
		const u32 noff = rd_le32 (n->nmof + i * 4);
		if (noff < n->tnnm_size)
		{
			const size_t max = n->tnnm_size - noff;
			size_t len = 0;
			while (len < max && n->tnnm[noff + len])
				len++;
			if (len < max && len < name_size)
			{
				memcpy (name, n->tnnm + noff, len);
				name[len] = 0;
			}
		}
	}

	// Track names come straight out of the file, so keep them to a single
	// plain filename rather than letting one escape the destination
	// directory.
	bool name_ok = name[0] != 0;
	for (ccp c = name; name_ok && *c; c++)
		if (*c == '/' || *c == '\\' || (u8)*c < 0x20)
			name_ok = false;
	if (name_ok && (!strcmp (name, ".") || !strcmp (name, "..")))
		name_ok = false;
	if (!name_ok)
		snprintf (name, name_size, "track_%04u", i);
}

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

	nus3_t n;
	if (!nus3_parse (raw, raw_size, &n))
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}
	const u32 n_tracks = n.n_tracks;

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NUS3AUDIO:%s (%u tracks) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_tracks, dest);

	for (uint i = 0; i < n_tracks; i++)
	{
		const u32 off = rd_le32 (n.adof + i * 8);
		const u32 size = rd_le32 (n.adof + i * 8 + 4);
		if (off > raw_size || size > raw_size - off)
			continue;
		const u8 *data = raw + off;

		char name[PATH_MAX];
		nus3_track_name (&n, i, name, sizeof (name));

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
		tnnm_size += (u32)strlen (names[i]) + 1; // chars + NUL
	}

	const u32 audiindx_len = 4;
	const u32 tnid_len = n_entries * 4;
	const u32 nmof_len = n_entries * 4;
	const u32 adof_len = n_entries * 8;

	u32 pack_len = 0;
	for (uint i = 0; i < n_entries; i++)
		pack_len += sorted[i].size;

	// AUDIINDX carries an 8-byte tag, every other chunk a 4-byte one.
	const u32 head_size = (12 + audiindx_len) + (8 + tnid_len) + (8 + nmof_len)
		+ (8 + adof_len) + (8 + tnnm_size);
	const u32 pack_payload_off = 8 + head_size + 8;
	const u32 body_size = head_size + 8 + pack_len;
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

	memcpy (out + pos, "AUDIINDX", 8);
	wr_le32 (out + pos + 8, audiindx_len);
	wr_le32 (out + pos + 12, n_entries);
	pos += 12 + audiindx_len;

	memcpy (out + pos, "TNID", 4);
	wr_le32 (out + pos + 4, tnid_len);
	for (uint i = 0; i < n_entries; i++)
		wr_le32 (out + pos + 8 + i * 4, 100 + i);
	pos += 8 + tnid_len;

	memcpy (out + pos, "NMOF", 4);
	wr_le32 (out + pos + 4, nmof_len);
	u32 cur_tnnm_off = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		wr_le32 (out + pos + 8 + i * 4, cur_tnnm_off);
		cur_tnnm_off += (u32)strlen (names[i]) + 1;
	}
	pos += 8 + nmof_len;

	// ADOF offsets are absolute file offsets into PACK.
	memcpy (out + pos, "ADOF", 4);
	wr_le32 (out + pos + 4, adof_len);
	u32 cur_pack_off = pack_payload_off;
	for (uint i = 0; i < n_entries; i++)
	{
		wr_le32 (out + pos + 8 + i * 8, cur_pack_off);
		wr_le32 (out + pos + 8 + i * 8 + 4, sorted[i].size);
		cur_pack_off += sorted[i].size;
	}
	pos += 8 + adof_len;

	memcpy (out + pos, "TNNM", 4);
	wr_le32 (out + pos + 4, tnnm_size);
	u32 tnnm_payload = pos + 8;
	for (uint i = 0; i < n_entries; i++)
	{
		const size_t nlen = strlen (names[i]);
		memcpy (out + tnnm_payload, names[i], nlen);
		tnnm_payload += (u32)nlen + 1; // NUL terminator (buffer is zeroed)
	}
	pos += 8 + tnnm_size;

	memcpy (out + pos, "PACK", 4);
	wr_le32 (out + pos + 4, pack_len);
	u32 pack_payload = pos + 8;
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

enumError create_nus3audio_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;

	u8 *data = 0;
	uint size = 0;

	if (!err)
	{
		// Byte-exact round trip: when the destination already holds a NUS3
		// whose tracks still match the tree (every member carries the name
		// its TNNM entry extracts to, with an unchanged size), reproduce the
		// original file and only overwrite each PACK payload with the current
		// member bytes. Everything else -- header, TNID ids, chunk order,
		// inter-chunk slack and trailing data -- is copied as-is, unlike a
		// first-time build which has to synthesise all of it.
		u8 *raw = 0;
		size_t raw_size = 0;
		if (!LoadFileAlloc (dest, 0, 0, &raw, &raw_size, 0, 0, 0, false) && raw_size >= 16
			&& raw_size <= UINT_MAX && !memcmp (raw, "NUS3", 4))
		{
			nus3_t n;
			bool reusable = nus3_parse (raw, raw_size, &n);
			const u32 n_tracks = n.n_tracks;
			uint *match = reusable ? CALLOC (n_tracks, sizeof (*match)) : 0;
			if (reusable && !match)
				reusable = false;
			if (reusable)
			{
				bool *used_member = CALLOC (list.used, sizeof (*used_member));
				if (!used_member)
					reusable = false;
				else
				{
					uint n_matched = 0;
					for (uint i = 0; i < n_tracks; i++)
					{
						// Skip tracks the extractor would have skipped, and
						// name every other one exactly as it does, so the
						// tree entries can be matched back 1:1.
						const u32 off = rd_le32 (n.adof + i * 8);
						const u32 tsize = rd_le32 (n.adof + i * 8 + 4);
						match[i] = UINT_MAX;
						if (off > raw_size || tsize > raw_size - off)
							continue;
						char name[PATH_MAX];
						nus3_track_name (&n, i, name, sizeof (name));

						for (uint k = 0; k < list.used; k++)
						{
							if (used_member[k])
								continue;
							nintendo_sarc_entry_t *e = &list.entry[k];
							ccp leaf = strrchr (e->name, '/');
							leaf = leaf ? leaf + 1 : e->name;
							char base[PATH_MAX];
							snprintf (base, sizeof (base), "%s", leaf);
							char *dot = strrchr (base, '.');
							if (dot)
								*dot = 0;
							if (strcmp (base, name) || e->size != tsize)
								continue;
							used_member[k] = true;
							match[i] = k;
							n_matched++;
							break;
						}
					}
					FREE (used_member);
					if (n_matched != n_tracks)
						reusable = false;
				}
			}

			if (reusable)
			{
				u8 *out = MALLOC (raw_size);
				if (!out)
					err = ERR_CANT_CREATE;
				else
				{
					memcpy (out, raw, raw_size);
					for (uint i = 0; i < n_tracks; i++)
					{
						const u32 off = rd_le32 (n.adof + i * 8);
						const u32 tsize = rd_le32 (n.adof + i * 8 + 4);
						memcpy (out + off, list.entry[match[i]].data, tsize);
					}
					data = out;
					size = (uint)raw_size;
				}
			}
		}
		FREE (raw);
	}

	if (!err && !data)
		err = CreateNUS3AudioArchive (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, dest);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

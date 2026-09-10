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
// 18. Nintendo Binary Audio Resource Archive (.bars / BARS)
// ----------------------------------------------------------------------------
enumError ExtractBARSArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".bars") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 16 || memcmp (raw, "BARS", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// BARS Header:
	// 0x00: Magic ("BARS", 4 bytes)
	// 0x04: u32 file_size
	// 0x08: u16 BOM (0xFEFF for big-endian, 0xFFFE for little-endian)
	// 0x0A: u16 version
	// 0x0C: u32 asset_count
	const u16 bom = rd_be16 (raw + 8);
	const bool big = (bom == 0xFEFF);

	const u32 asset_count = big ? rd_be32 (raw + 12) : rd_le32 (raw + 12);
	if (!asset_count || asset_count > 100000)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BARS:%s (%u assets, %s-endian) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, asset_count,
			big ? "big" : "little", dest);

	const uint hash_table_offset = 16;
	const uint offset_pairs_table = hash_table_offset + asset_count * 4;

	if (offset_pairs_table + asset_count * 8 > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	for (uint i = 0; i < asset_count; i++)
	{
		const uint pair_pos = offset_pairs_table + i * 8;
		const u32 amta_offset = big ? rd_be32 (raw + pair_pos) : rd_le32 (raw + pair_pos);
		const u32 audio_offset = big ? rd_be32 (raw + pair_pos + 4) : rd_le32 (raw + pair_pos + 4);

		char name[PATH_MAX];
		bool name_found = false;

		// Check AMTA metadata chunk for filename
		if (amta_offset + 0x30 <= raw_size && !memcmp (raw + amta_offset, "AMTA", 4))
		{
			const u32 name_rel_ptr
				= big ? rd_be32 (raw + amta_offset + 0x24) : rd_le32 (raw + amta_offset + 0x24);
			const uint name_abs = amta_offset + 0x24 + name_rel_ptr;
			if (name_abs < raw_size)
			{
				const char *str = (const char *)(raw + name_abs);
				size_t slen = strnlen (str, sizeof (name) - 1);
				if (slen > 0)
				{
					memcpy (name, str, slen);
					name[slen] = 0;
					name_found = true;
				}
			}
		}

		if (!name_found)
			snprintf (name, sizeof (name), "audio_%04u", i);

		// If audio_offset is valid, extract the audio asset (BWAV / BFWAV / etc.)
		if (audio_offset != 0xFFFFFFFF && audio_offset < raw_size)
		{
			// Determine audio asset size: BWAV / BFWAV header contains size at offset 8 or 12
			u32 audio_sz = 0;
			ccp ext = ".bwav";
			if (audio_offset + 16 <= raw_size)
			{
				if (!memcmp (raw + audio_offset, "BWAV", 4))
				{
					ext = ".bwav";
					audio_sz
						= big ? rd_be32 (raw + audio_offset + 8) : rd_le32 (raw + audio_offset + 8);
				}
				else if (!memcmp (raw + audio_offset, "FWAV", 4))
				{
					ext = ".bfwav";
					audio_sz
						= big ? rd_be32 (raw + audio_offset + 8) : rd_le32 (raw + audio_offset + 8);
				}
			}

			if (audio_sz == 0 || audio_offset + audio_sz > raw_size)
			{
				// Bound by raw_size or next asset/offset
				audio_sz = (u32)(raw_size - audio_offset);
			}

			char out_path[PATH_MAX];
			// Avoid double extension if name already has one
			if (strchr (name, '.'))
				snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);
			else
				snprintf (out_path, sizeof (out_path), "%s/%s%s", dest, name, ext);

			char *slash = strrchr (out_path, '/');
			if (slash)
			{
				*slash = 0;
				CreatePath (out_path, true);
				*slash = '/';
			}

			if (!testmode && audio_sz > 0)
				SaveFile (out_path, 0, 0, raw + audio_offset, audio_sz, 0);
		}

		// Also extract AMTA metadata chunk alongside if available
		if (amta_offset < raw_size && amta_offset + 12 <= raw_size
			&& !memcmp (raw + amta_offset, "AMTA", 4))
		{
			u32 amta_sz = big ? rd_be32 (raw + amta_offset + 8) : rd_le32 (raw + amta_offset + 8);
			if (amta_sz == 0 || amta_offset + amta_sz > raw_size)
				amta_sz = (u32)(raw_size - amta_offset);

			char out_amta[PATH_MAX];
			char clean_name[PATH_MAX];
			snprintf (clean_name, sizeof (clean_name), "%s", name);
			char *dot = strrchr (clean_name, '.');
			if (dot)
				*dot = 0;
			snprintf (out_amta, sizeof (out_amta), "%s/%s.amta", dest, clean_name);

			char *slash = strrchr (out_amta, '/');
			if (slash)
			{
				*slash = 0;
				CreatePath (out_amta, true);
				*slash = '/';
			}

			if (!testmode && amta_sz > 0)
				SaveFile (out_amta, 0, 0, raw + amta_offset, amta_sz, 0);
		}
	}

	FREE (raw);
	return ERR_OK;
}

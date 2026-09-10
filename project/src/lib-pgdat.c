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
// 15. PlatinumGames DAT Archive (.dat / DAT\0)
// ----------------------------------------------------------------------------
enumError ExtractPGDATArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".dat") && !is_ext_match (arg, ".pkz") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 24 || memcmp (raw, "DAT\0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 file_count = rd_le32 (raw + 4);
	const u32 offset_file_offset_tbl = rd_le32 (raw + 8);
	const u32 offset_file_ext_tbl = rd_le32 (raw + 12);
	const u32 offset_file_name_tbl = rd_le32 (raw + 16);
	const u32 offset_file_size_tbl = rd_le32 (raw + 20);

	if (!file_count || file_count > 100000 || offset_file_offset_tbl >= raw_size
		|| offset_file_size_tbl >= raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PG-DAT:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, file_count, dest);

	u32 str_size = 0;
	if (offset_file_name_tbl + 4 <= raw_size)
		str_size = rd_le32 (raw + offset_file_name_tbl);

	for (uint i = 0; i < file_count; i++)
	{
		if (offset_file_offset_tbl + (i + 1) * 4 > raw_size
			|| offset_file_size_tbl + (i + 1) * 4 > raw_size)
			break;

		const u32 offset = rd_le32 (raw + offset_file_offset_tbl + i * 4);
		u32 size = rd_le32 (raw + offset_file_size_tbl + i * 4);

		char name[PATH_MAX];
		if (str_size > 0 && offset_file_name_tbl + 4 + (i + 1) * str_size <= raw_size)
		{
			const char *s = (const char *)(raw + offset_file_name_tbl + 4 + i * str_size);
			size_t slen = strnlen (s, sizeof (name) - 1);
			memcpy (name, s, slen);
			name[slen] = 0;
		}
		else
		{
			char ext[8] = "bin";
			if (offset_file_ext_tbl > 0 && offset_file_ext_tbl + (i + 1) * 4 <= raw_size)
			{
				const char *e = (const char *)(raw + offset_file_ext_tbl + i * 4);
				uint elen = 0;
				while (elen < 4 && e[elen] && (isalnum ((unsigned char)e[elen]) || e[elen] == '_'))
					elen++;
				if (elen > 0)
				{
					memcpy (ext, e, elen);
					ext[elen] = 0;
				}
			}
			snprintf (name, sizeof (name), "file_%04u.%s", i, ext);
		}

		if (offset >= raw_size)
			continue;
		if (offset + size > raw_size)
			size = (u32)(raw_size - offset);

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

		char *slash = strrchr (out_path, '/');
		if (slash)
		{
			*slash = 0;
			CreatePath (out_path, true);
			*slash = '/';
		}

		if (!testmode && size > 0)
			SaveFile (out_path, 0, 0, raw + offset, size, 0);
	}

	FREE (raw);
	return ERR_OK;
}

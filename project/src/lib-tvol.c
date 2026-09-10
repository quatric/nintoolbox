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


// Extract Koei Tecmo / Gust Texture Volume Archive (.tvol)
enumError ExtractTVOLArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".tvol"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 12)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 num_textures = rd_le32 (raw);
	if (num_textures == 0 || num_textures > 10000 || 4 + num_textures * 8 > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	for (uint i = 0; i < num_textures; i++)
	{
		const u32 offset = rd_le32 (raw + 4 + i * 8);
		const u32 size = rd_le32 (raw + 4 + i * 8 + 4);

		if (size == 0 || offset >= raw_size)
			continue;

		const u32 to_write = offset + size <= raw_size ? size : (u32)(raw_size - offset);

		// Name is stored at offset as null-terminated string (up to 48 bytes)
		char name[64] = "";
		if (offset + 48 <= raw_size)
		{
			const char *nptr = (const char *)(raw + offset);
			size_t nlen = strnlen (nptr, 47);
			if (nlen > 0)
			{
				memcpy (name, nptr, nlen);
				name[nlen] = 0;
			}
		}

		char out_file[PATH_MAX];
		if (*name)
			snprintf (out_file, sizeof (out_file), "%s/%s.bin", dest, name);
		else
			snprintf (out_file, sizeof (out_file), "%s/tex_%04u.bin", dest, i);

		if (!testmode && to_write > 0)
			SaveFile (out_file, 0, 0, raw + offset, to_write, 0);
	}

	FREE (raw);
	return ERR_OK;
}

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
// 16. PlatinumGames WT Archive (.wta / WTA )
// ----------------------------------------------------------------------------
enumError ExtractWTAArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".wta") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 32 || memcmp (raw, "WTA ", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// WTA Header:
	// 0x00: Magic ("WTA\x20" or "WTB\x00")
	// 0x04: Version (usually 1)
	// 0x08: Number of textures / files
	// 0x0C: Offset to data offset table (or texture info table)
	// 0x10: Offset to data size table
	// 0x14: Unk table / flags table
	// 0x18: Unk2 table / idx table
	// 0x1C: Texture info table
	const u32 ver_be = rd_be32 (raw + 4);
	const u32 ver_le = rd_le32 (raw + 4);
	const bool big = (ver_be > 0 && ver_be <= 0xFFFF && ver_le > 0xFFFF);

	const u32 file_count = big ? rd_be32 (raw + 8) : rd_le32 (raw + 8);
	const u32 offset_pos_table = big ? rd_be32 (raw + 12) : rd_le32 (raw + 12);
	const u32 offset_size_table = big ? rd_be32 (raw + 16) : rd_le32 (raw + 16);

	if (!file_count || file_count > 100000)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT WTA:%s (%u files, %s-endian) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, file_count,
			big ? "big" : "little", dest);

	for (uint i = 0; i < file_count; i++)
	{
		u32 cur_data_offset = 0;
		u32 comp_sz = 0;
		u32 uncomp_sz = 0;

		if (offset_pos_table >= 32 && offset_pos_table + (i + 1) * 4 <= raw_size
			&& offset_size_table >= 32 && offset_size_table + (i + 1) * 4 <= raw_size)
		{
			cur_data_offset = big ? rd_be32 (raw + offset_pos_table + i * 4)
								  : rd_le32 (raw + offset_pos_table + i * 4);
			comp_sz = big ? rd_be32 (raw + offset_size_table + i * 4)
						  : rd_le32 (raw + offset_size_table + i * 4);
			uncomp_sz = comp_sz;
		}
		else
		{
			// Fallback: 32-byte descriptor table starting at offset 32
			const uint entry_pos = 32 + i * 32;
			if (entry_pos + 32 > raw_size)
				break;
			uncomp_sz = big ? rd_be32 (raw + entry_pos + 12) : rd_le32 (raw + entry_pos + 12);
			comp_sz = big ? rd_be32 (raw + entry_pos + 16) : rd_le32 (raw + entry_pos + 16);
			cur_data_offset = 32 + file_count * 32;
		}

		char name[PATH_MAX];
		snprintf (name, sizeof (name), "texture_%04u.bin", i);

		if (cur_data_offset < raw_size && comp_sz > 0)
		{
			if (cur_data_offset + comp_sz > raw_size)
				comp_sz = (u32)(raw_size - cur_data_offset);

			char out_path[PATH_MAX];
			snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

			char *slash = strrchr (out_path, '/');
			if (slash)
			{
				*slash = 0;
				CreatePath (out_path, true);
				*slash = '/';
			}

			if (!testmode)
			{
				if (comp_sz != uncomp_sz && comp_sz >= 2 && raw[cur_data_offset] == 0x78)
				{
					u8 *decomp = 0;
					uint decomp_sz = 0;
					err = DecodeZlibGrow (&decomp, &decomp_sz, raw + cur_data_offset, comp_sz);
					if (!err && decomp)
					{
						SaveFile (out_path, 0, 0, decomp, decomp_sz, 0);
						FREE (decomp);
					}
					else
					{
						SaveFile (out_path, 0, 0, raw + cur_data_offset, comp_sz, 0);
					}
				}
				else
				{
					SaveFile (out_path, 0, 0, raw + cur_data_offset, comp_sz, 0);
				}
			}
		}
	}

	FREE (raw);
	return ERR_OK;
}

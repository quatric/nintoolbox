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


// Extract Next Level Games Texture To Go (.txtg / 6PK0)
enumError ExtractTXTGArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".txtg") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 0x50 || memcmp (raw + 4, "6PK0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// TXTG Header:
	// 0x00: u16 HeaderSize (usually 0x50)
	// 0x02: u16 Version (usually 0x11)
	// 0x04: 4 bytes Magic "6PK0"
	// 0x08: u16 Width
	// 0x0A: u16 Height
	// 0x0C: u16 Depth
	// 0x0E: u8 MipCount
	// 0x0F: u8 Unknown1
	// 0x10: u8 Unknown2
	// 0x11: u8 Padding
	// 0x12: u8 FormatFlag
	// 0x13: u32 FormatSetting (or padding)
	// 0x44: u16 Format
	const u16 header_size = rd_le16 (raw);
	const u16 width = rd_le16 (raw + 8);
	const u16 height = rd_le16 (raw + 10);
	const u16 depth_cnt = rd_le16 (raw + 12);
	const u8 mip_count = raw[14];
	const u16 fmt = rd_le16 (raw + 0x44);

	const uint total_surfaces = (depth_cnt ? depth_cnt : 1) * (mip_count ? mip_count : 1);
	if (total_surfaces == 0 || total_surfaces > 1000)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT TXTG:%s (%ux%u, fmt 0x%04x, %u surfaces) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, width, height, fmt,
			total_surfaces, dest);

	// Surface headers follow header_size
	// First table: total_surfaces * 4 bytes (u16 ArrayLevel, u8 MipLevel, u8 unk)
	// Second table: total_surfaces * 8 bytes (u32 Size, u32 unk)
	const uint table1_off = header_size;
	const uint table2_off = table1_off + total_surfaces * 4;
	const uint data_start = table2_off + total_surfaces * 8;

	if (data_start > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	u32 cur_data_off = data_start;
	for (uint i = 0; i < total_surfaces; i++)
	{
		const u16 array_lvl = rd_le16 (raw + table1_off + i * 4);
		const u8 mip_lvl = raw[table1_off + i * 4 + 2];
		const u32 surf_sz = rd_le32 (raw + table2_off + i * 8);

		if (cur_data_off >= raw_size)
			break;

		const u32 to_write
			= cur_data_off + surf_sz <= raw_size ? surf_sz : (u32)(raw_size - cur_data_off);

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/surface_a%u_m%u.bin", dest, array_lvl, mip_lvl);

		if (!testmode && to_write > 0)
			SaveFile (out_path, 0, 0, raw + cur_data_off, to_write, 0);

		cur_data_off = (cur_data_off + surf_sz + 15) & ~15;
	}

	FREE (raw);
	return ERR_OK;
}

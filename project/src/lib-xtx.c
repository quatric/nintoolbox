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


// Extract Nintendo Switch XTX Texture Container (.xtx / DFvN)
enumError ExtractXTXArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".xtx") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 16 || memcmp (raw, "DFvN", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// XTX Header (16 bytes):
	// 0x00: "DFvN"
	// 0x04: u32 HeaderSize
	// 0x08: u32 MajorVersion
	// 0x0C: u32 MinorVersion
	const u32 header_size = rd_le32 (raw + 4);
	if (header_size < 16 || header_size >= raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	uint block_pos = header_size;
	uint image_idx = 0;

	// Loop through blocks (HBvN)
	while (block_pos + 32 <= raw_size)
	{
		if (memcmp (raw + block_pos, "HBvN", 4))
			break;

		const u32 block_size = rd_le32 (raw + block_pos + 4);
		const u64 data_size = rd_le64 (raw + block_pos + 8);
		const s64 data_offset = (s64)rd_le64 (raw + block_pos + 16);
		const u32 block_type = rd_le32 (raw + block_pos + 24);

		// BlockType 3 is Texture Data block
		if (block_type == 3 && data_size > 0)
		{
			const s64 abs_payload = (s64)block_pos + data_offset;
			if (abs_payload >= 0 && (size_t)abs_payload + data_size <= raw_size)
			{
				char out_file[PATH_MAX];
				snprintf (out_file, sizeof (out_file), "%s/texture_%04u.bin", dest, image_idx++);

				if (!testmode)
					SaveFile (out_file, 0, 0, raw + abs_payload, (uint)data_size, 0);
			}
		}

		if (block_size == 0)
			break;
		block_pos += block_size;
	}

	FREE (raw);
	return ERR_OK;
}

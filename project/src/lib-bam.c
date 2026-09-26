// SPDX-License-Identifier: GPL-2.0+
// See lib-bam.h for the layout and what is/isn't recovered.
#include "lib-bam.h"
#include "lib-archive-util.h"
#include <string.h>

#define BAM_MAX_CELLS 1000000

enumError ExtractBamArchive (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (raw_size < 10 || rd_le16 (raw) != 2 || rd_le16 (raw + 2) != 22)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 width = rd_le16 (raw + 4);
	const u32 height = rd_le16 (raw + 6);
	const u64 grid_end = 0x08 + (u64)width * height;
	if (!width || !height || (u64)width * height > BAM_MAX_CELLS || grid_end + 2 > raw_size
		|| raw[grid_end] != 0xff || raw[grid_end + 1] != 0xff)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BAM:%s (%ux%u grid) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, width, height, dest);

	enumError err = ERR_OK;
	if (!testmode)
	{
		char grid_path[PATH_MAX], tail_path[PATH_MAX];
		snprintf (grid_path, sizeof (grid_path), "%s/grid.txt", dest);
		snprintf (tail_path, sizeof (tail_path), "%s/trailing.bin", dest);

		size_t cap = (size_t)(width + 1) * height * 4 + 64, len = 0;
		char *text = MALLOC (cap);
		len += (size_t)snprintf (
			text + len, cap - len, "# %u x %u bubble grid, one cell per byte\n", width, height);
		const u8 *grid = raw + 0x08;
		for (u32 y = 0; y < height; y++)
		{
			for (u32 x = 0; x < width; x++)
				len += (size_t)snprintf (
					text + len, cap - len, "%s%u", x ? " " : "", grid[(u64)y * width + x]);
			len += (size_t)snprintf (text + len, cap - len, "\n");
		}
		if (SaveFile (grid_path, 0, 0, (const u8 *)text, (uint)len, 0))
			err = ERR_CANT_CREATE;
		FREE (text);

		const u64 tail_start = grid_end + 2;
		if (tail_start < raw_size
			&& SaveFile (tail_path, 0, 0, raw + tail_start, (uint)(raw_size - tail_start), 0))
			err = ERR_CANT_CREATE;
	}

	FREE (raw);
	(void)depth;
	return err;
}

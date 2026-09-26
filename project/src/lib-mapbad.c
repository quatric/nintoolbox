// SPDX-License-Identifier: GPL-2.0+
// See lib-mapbad.h for the layout and what is/isn't recovered.
#include "lib-mapbad.h"
#include "lib-archive-util.h"
#include <string.h>

#define MAPBAD_REC_SIZE 32
#define MAPBAD_MAX_RECORDS 1000000

static float rd_be_float (const u8 *p)
{
	const u32 v = rd_be32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

enumError ExtractMapBadArchive (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (raw_size < 12 || memcmp (raw, "BAD", 3))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 area_count = rd_be32 (raw + 4);
	const u32 link_count = rd_be32 (raw + 8);
	const u64 areas_end = 0x0c + (u64)area_count * MAPBAD_REC_SIZE;
	const u64 links_end = areas_end + (u64)link_count * MAPBAD_REC_SIZE;
	if (area_count > MAPBAD_MAX_RECORDS || link_count > MAPBAD_MAX_RECORDS || links_end > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// The one invariant this layout was recovered from: every area record's
	// 3rd word is the sentinel 0xffffffff. Refuse to touch the file unless
	// every one of them holds -- this is the only thing standing between
	// "recognized format" and "misread some other engine table."
	for (u32 i = 0; i < area_count; i++)
		if (rd_be32 (raw + 0x0c + (u64)i * MAPBAD_REC_SIZE + 8) != 0xffffffff)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BAD:%s (%u areas, %u links) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, area_count, link_count, dest);

	enumError err = ERR_OK;
	if (!testmode)
	{
		char areas_path[PATH_MAX], links_path[PATH_MAX], geo_path[PATH_MAX];
		snprintf (areas_path, sizeof (areas_path), "%s/areas.txt", dest);
		snprintf (links_path, sizeof (links_path), "%s/links.txt", dest);
		snprintf (geo_path, sizeof (geo_path), "%s/geometry.bin", dest);

		size_t cap = (size_t)(area_count + link_count) * 128 + 128, len = 0;
		char *text = MALLOC (cap);
		len += (size_t)snprintf (text + len, cap - len, "# idx\ta\tb\tscale_x\tscale_y\tscale_z\n");
		for (u32 i = 0; i < area_count; i++)
		{
			const u8 *rec = raw + 0x0c + (u64)i * MAPBAD_REC_SIZE;
			len += (size_t)snprintf (text + len, cap - len, "%u\t%u\t%u\t%g\t%g\t%g\n", i,
				rd_be32 (rec), rd_be32 (rec + 4), rd_be_float (rec + 0x14),
				rd_be_float (rec + 0x18), rd_be_float (rec + 0x1c));
		}
		if (SaveFile (areas_path, 0, 0, (const u8 *)text, (uint)len, 0))
			err = ERR_CANT_CREATE;

		len = 0;
		len += (size_t)snprintf (
			text + len, cap - len, "# idx\traw 8 big-endian u32 words (hex)\n");
		for (u32 i = 0; i < link_count; i++)
		{
			const u8 *rec = raw + areas_end + (u64)i * MAPBAD_REC_SIZE;
			len += (size_t)snprintf (text + len, cap - len, "%u\t", i);
			for (int w = 0; w < 8; w++)
				len += (size_t)snprintf (text + len, cap - len, "%08x ", rd_be32 (rec + w * 4));
			len += (size_t)snprintf (text + len, cap - len, "\n");
		}
		if (link_count && SaveFile (links_path, 0, 0, (const u8 *)text, (uint)len, 0))
			err = ERR_CANT_CREATE;
		FREE (text);

		if (links_end < raw_size
			&& SaveFile (geo_path, 0, 0, raw + links_end, (uint)(raw_size - links_end), 0))
			err = ERR_CANT_CREATE;
	}

	FREE (raw);
	(void)depth;
	return err;
}

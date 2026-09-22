// SPDX-License-Identifier: GPL-2.0+
// See lib-agr.h for the layout and what is/isn't recovered.
#include "lib-agr.h"
#include "lib-archive-util.h"
#include <string.h>

#define AGR_MAX_CHUNKS 100000

enumError ExtractAGRArchive (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (raw_size < 12 || memcmp (raw, "ANIM", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Gate on the one invariant this layout was recovered from: walking
	// self-sized "ANIM" chunks (8+chunk_size bytes each) from the start of
	// the file must land EXACTLY on EOF, with every chunk actually
	// starting with the magic. A garbled or unrelated file is extremely
	// unlikely to satisfy this by chance.
	size_t *off = MALLOC (AGR_MAX_CHUNKS * sizeof (*off));
	if (!off)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}

	uint n = 0;
	size_t pos = 0;
	while (pos + 12 <= raw_size && n < AGR_MAX_CHUNKS)
	{
		if (memcmp (raw + pos, "ANIM", 4))
		{
			FREE (off);
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		const u32 chunk_size = rd_be32 (raw + pos + 8);
		if (!chunk_size || pos + 8 + (u64) chunk_size > raw_size)
		{
			FREE (off);
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		off[n++] = pos;
		pos += 8 + (size_t)chunk_size;
	}
	if (!n || pos != raw_size)
	{
		FREE (off);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT AGR:%s (%u clip(s)) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n, dest);

	enumError err = ERR_OK;
	if (!testmode)
	{
		for (uint i = 0; i < n; i++)
		{
			const size_t start = off[i];
			const size_t end = i + 1 < n ? off[i + 1] : raw_size;

			char out_path[PATH_MAX];
			snprintf (out_path, sizeof (out_path), "%s/clip%04u.anim", dest, i);
			if (SaveFile (out_path, 0, 0, raw + start, (uint) (end - start), 0))
				err = ERR_CANT_CREATE;
		}
	}

	FREE (off);
	FREE (raw);
	(void) depth;
	return err;
}

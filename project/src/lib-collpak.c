// SPDX-License-Identifier: GPL-2.0+
// See lib-collpak.h for the layout and what is/isn't recovered.
#include "lib-collpak.h"
#include "lib-archive-util.h"
#include <string.h>

#define COLLPAK_REC_SIZE 12
#define COLLPAK_MAX_ENTRIES 100000

static bool is_col_magic (const u8 *p)
{
	return !memcmp (p, "COLL", 4) || !memcmp (p, "COL2", 4) || !memcmp (p, "COL3", 4)
		|| !memcmp (p, "COL4", 4);
}

enumError ExtractCollPakArchive (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (raw_size < 12)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 declared_size = rd_be32 (raw);
	const u32 count = rd_be32 (raw + 4); // includes this header record itself
	if (declared_size != raw_size || !count || count > COLLPAK_MAX_ENTRIES
		|| (u64) count * COLLPAK_REC_SIZE > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Gate on the same "contiguous, ascending, chained" invariant the
	// IDE.dir/img decoder uses, plus every entry's data actually starting
	// with a known GTA-family COL FourCC -- together, essentially
	// impossible for an unrelated file to satisfy by chance.
	const uint n = count - 1;
	u32 expect_off = 0;
	for (uint i = 0; i < n; i++)
	{
		const u8 *rec = raw + (i + 1) * COLLPAK_REC_SIZE;
		const u32 off = rd_be32 (rec);
		const u32 size = rd_be32 (rec + 4);
		if (i == 0)
			expect_off = off; // first entry sets the (16-byte-aligned) base
		if (off != expect_off || !size || (u64) off + size > raw_size)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		if (!is_col_magic (raw + off))
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		expect_off += size;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT COLL-PAK:%s (%u member(s)) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);

	enumError err = ERR_OK;
	if (!testmode)
	{
		for (uint i = 0; i < n; i++)
		{
			const u8 *rec = raw + (i + 1) * COLLPAK_REC_SIZE;
			const u32 off = rd_be32 (rec);
			const u32 size = rd_be32 (rec + 4);
			const u32 hash = rd_be32 (rec + 8);

			char out_path[PATH_MAX];
			snprintf (out_path, sizeof (out_path), "%s/col%04u_%08x.col", dest, i, hash);
			if (SaveFile (out_path, 0, 0, raw + off, size, 0))
				err = ERR_CANT_CREATE;
		}
	}

	FREE (raw);
	(void) depth;
	return err;
}

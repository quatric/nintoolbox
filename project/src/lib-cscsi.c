// SPDX-License-Identifier: GPL-2.0+
// See lib-cscsi.h for the layout and what is/isn't recovered.
#include "lib-cscsi.h"
#include "lib-archive-util.h"
#include <string.h>

#define CSCSI_NAME_OFF 0xac
#define CSCSI_NAME_MAX 256

enumError ExtractCSCsiArchive (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (raw_size < CSCSI_NAME_OFF + 2 || raw[0] != 0x30 || raw[1] != 0x33 || raw[2] != 0x43
		|| raw[3] != 0x0b)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Gate on the one invariant this layout was recovered from: the file
	// records its own total size at 0x24.
	if (rd_be32 (raw + 0x24) != raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const size_t name_scan_end = raw_size < CSCSI_NAME_OFF + CSCSI_NAME_MAX ? raw_size
									      : CSCSI_NAME_OFF + CSCSI_NAME_MAX;
	const u8 *nul = memchr (raw + CSCSI_NAME_OFF, 0, name_scan_end - CSCSI_NAME_OFF);
	bool name_ok = nul != 0 && nul > raw + CSCSI_NAME_OFF;
	if (name_ok)
		for (const u8 *p = raw + CSCSI_NAME_OFF; p < nul && name_ok; p++)
			if (*p < 0x20 || *p > 0x7e)
				name_ok = false;

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CSI:%s -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	// Only the source name is understood well enough to pull out; the
	// keyframe/event track data itself is left in place (this decoder
	// does not know where it begins, so it does not claim to carve it).
	enumError err = ERR_OK;
	if (!testmode && name_ok)
	{
		char out[PATH_MAX];
		snprintf (out, sizeof (out), "%s/source_name.txt", dest);
		const size_t len = (size_t)(nul - (raw + CSCSI_NAME_OFF));
		if (SaveFile (out, 0, 0, raw + CSCSI_NAME_OFF, (uint)len, 0))
			err = ERR_CANT_CREATE;
	}

	FREE (raw);
	(void)depth;
	return err;
}

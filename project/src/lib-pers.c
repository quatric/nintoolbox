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


///////////////////////////////////////////////////////////////////////////////

enumError ExtractPERSFile (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (raw_size < 0x20 || memcmp (raw, "PERS-SZP", 8))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 hdr_size = rd_be32 (raw + 8);
	const u32 declared = rd_be32 (raw + 0x0c);
	if (hdr_size < 0x18 || hdr_size >= raw_size || !declared)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}
	if (memcmp (raw + hdr_size, "Yay0", 4))
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	u8 *decoded = 0;
	uint decoded_size = 0;
	const enumError derr
		= DecodeYay0 (&decoded, &decoded_size, raw + hdr_size, (uint)(raw_size - hdr_size));
	if (derr || !decoded)
	{
		FREE (decoded);
		FREE (raw);
		return ERR_INVALID_DATA;
	}
	// The header states the payload size independently of the Yay0 stream's
	// own, so a mismatch means this was not really one of these.
	if (decoded_size != declared)
	{
		FREE (decoded);
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	ccp stem = strrchr (arg, '/');
	stem = stem ? stem + 1 : arg;

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PERS:%s (%u bytes) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, decoded_size, dest);

	char out[PATH_MAX];
	snprintf (out, sizeof (out), "%s/%s.bin", dest, stem);
	if (!testmode)
		SaveFile (out, 0, 0, decoded, decoded_size, 0);

	FREE (decoded);
	FREE (raw);
	return ERR_OK;
}

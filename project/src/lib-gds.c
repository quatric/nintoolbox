// SPDX-License-Identifier: GPL-2.0+
// See lib-gds.h for the layout and what is/isn't recovered.
#include "lib-gds.h"
#include "lib-archive-util.h"
#include <ctype.h>
#include <string.h>

#define GDS_HDR_SIZE 0x50
#define GDS_MAX_ENTRIES 1000000

enumError ExtractGDSArchive (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	static const u8 zero16[16] = { 0 };
	if (raw_size < GDS_HDR_SIZE || memcmp (raw, "GDS", 3) || memcmp (raw + 4, zero16, 16))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 count = rd_be32 (raw + 0x28);
	if (count > GDS_MAX_ENTRIES || (u64)GDS_HDR_SIZE + (u64)count * 8 > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT GDS:%s (version %u, %u names) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, raw[3], count, dest);

	enumError err = ERR_OK;
	if (!testmode)
	{
		// Names sidecar: index + the 1-4 char identifier, byte-reversed
		// back into reading order and trimmed of NUL padding.
		char names_path[PATH_MAX];
		snprintf (names_path, sizeof (names_path), "%s/names.txt", dest);
		size_t cap = count * 24 + 64, len = 0;
		char *text = MALLOC (cap);
		for (u32 i = 0; i < count; i++)
		{
			const u8 *rec = raw + GDS_HDR_SIZE + (u64)i * 8;
			char rev[5] = { (char)rec[3], (char)rec[2], (char)rec[1], (char)rec[0], 0 };
			for (int k = 0; k < 4; k++)
				if (!isprint ((unsigned char)rev[k]))
					rev[k] = 0;
			const u32 idx = rd_be32 (rec + 4);
			len += (size_t)snprintf (text + len, cap - len, "%u\t%s\n", idx, rev);
		}
		if (SaveFile (names_path, 0, 0, (const u8 *)text, (uint)len, 0))
			err = ERR_CANT_CREATE;
		FREE (text);

		// The rest of the file (its resource tree) is not decoded here;
		// carve it out so a later pass can re-scan it for nested magics.
		const u64 tail_off = GDS_HDR_SIZE + (u64)count * 8;
		if (tail_off < raw_size)
		{
			char payload_path[PATH_MAX];
			snprintf (payload_path, sizeof (payload_path), "%s/payload.bin", dest);
			if (SaveFile (payload_path, 0, 0, raw + tail_off, (uint)(raw_size - tail_off), 0))
				err = ERR_CANT_CREATE;
		}
	}

	FREE (raw);
	(void)depth;
	return err;
}

// SPDX-License-Identifier: GPL-2.0+
// See lib-cspak.h for the layout.
#include "lib-cspak.h"
#include "lib-archive-util.h"
#include <string.h>

#define CSPAK_REC_SIZE 28
#define CSPAK_NAME_SIZE 20
#define CSPAK_MAX_ENTRIES 100000

enumError ExtractCSPakArchive (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (raw_size < 16 || memcmp (raw, "KAP.", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 count = rd_be32 (raw + 8);
	const u32 tbl_off = rd_be32 (raw + 12);
	if (!count || count > CSPAK_MAX_ENTRIES
		|| (u64)tbl_off + (u64)count * CSPAK_REC_SIZE > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Validate before committing to this format: every data_offset must be
	// strictly increasing, in bounds, and past the table -- the one
	// invariant that let the record layout be recovered in the first place.
	u32 prev_off = tbl_off + count * CSPAK_REC_SIZE - 1;
	for (u32 i = 0; i < count; i++)
	{
		const u32 off = rd_be32 (raw + tbl_off + (u64)i * CSPAK_REC_SIZE);
		if (off <= prev_off || off >= raw_size)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		prev_off = off;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PAK:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);

	uint written = 0;
	for (u32 i = 0; i < count; i++)
	{
		const u8 *rec = raw + tbl_off + (u64)i * CSPAK_REC_SIZE;
		const u32 off = rd_be32 (rec);
		const u32 next = i + 1 < count ? rd_be32 (rec + CSPAK_REC_SIZE) : (u32)raw_size;
		if (next <= off || next > raw_size)
			continue;

		// The entry's data begins with a redundant, NOT truncated copy of
		// its own relative path (the 20-byte table field is often too
		// short and truncated from the front); prefer that full label,
		// falling back to the table field only if the label looks wrong.
		char name[PATH_MAX];
		uint nlen = 0;
		const u8 *nul = memchr (raw + off, 0, next - off);
		if (nul && nul > raw + off && (size_t)(nul - (raw + off)) < sizeof (name))
		{
			const uint llen = (uint)(nul - (raw + off));
			bool printable = true;
			for (uint k = 0; k < llen && printable; k++)
				if (raw[off + k] < 0x20 || raw[off + k] > 0x7e)
					printable = false;
			if (printable)
			{
				for (uint k = 0; k < llen; k++)
					name[nlen++] = raw[off + k] == '\\' ? '/' : raw[off + k];
				name[nlen] = 0;
			}
		}
		if (!nlen)
		{
			char raw_name[CSPAK_NAME_SIZE + 1];
			memcpy (raw_name, rec + 8, CSPAK_NAME_SIZE);
			raw_name[CSPAK_NAME_SIZE] = 0;
			for (uint k = 0; k < CSPAK_NAME_SIZE && raw_name[k]; k++)
				name[nlen++] = raw_name[k] == '\\' ? '/' : raw_name[k];
			name[nlen] = 0;
		}
		if (!nlen)
			snprintf (name, sizeof (name), "%05u.bin", i);

		// Skip the redundant path label + 0xCD padding at the start of the
		// entry's own data to land on the real payload.
		u32 data_off = off;
		if (nul)
		{
			u32 p = (u32)(nul - raw) + 1;
			while (p < next && raw[p] == 0xcd)
				p++;
			if (p < next)
				data_off = p;
		}

		char out[PATH_MAX];
		snprintf (out, sizeof (out), "%s/%s", dest, name);
		char *slash = strrchr (out, '/');
		if (slash && slash != out + strlen (dest))
		{
			*slash = 0;
			CreatePath (out, true);
			*slash = '/';
		}

		if (!testmode)
		{
			if (!SaveFile (out, 0, 0, raw + data_off, next - data_off, 0))
				written++;
		}
		else
			written++;
		if (verbose > 0)
			fprintf (stdlog, "  %-40s %8u bytes\n", name, next - data_off);
	}

	FREE (raw);
	(void)depth;
	if (!written)
		return ERR_INVALID_DATA;
	return ERR_OK;
}

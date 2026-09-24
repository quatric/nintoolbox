// SPDX-License-Identifier: GPL-2.0+
// See lib-chx.h for the layout and what is/isn't recovered.
#include "lib-chx.h"
#include "lib-archive-util.h"
#include <string.h>

enumError ExtractCHXArchive (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (raw_size < 8 || memcmp (raw, "DRHC", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Collect every NUL-terminated printable-ASCII run of the file's
	// embedded animation names and behaviour-script strings, in order.
	ccp *strs = 0;
	uint n_strs = 0, cap = 0;
	size_t i = 4;
	while (i < raw_size)
	{
		if (raw[i] < 0x20 || raw[i] > 0x7e)
		{
			i++;
			continue;
		}
		const size_t start = i;
		while (i < raw_size && raw[i] >= 0x20 && raw[i] <= 0x7e)
			i++;
		const size_t len = i - start;
		if (i < raw_size && raw[i] == 0 && len >= 3)
		{
			if (n_strs == cap)
			{
				cap = cap ? cap * 2 : 32;
				ccp *new_strs = REALLOC (strs, cap * sizeof (*strs));
				if (!new_strs)
					break;
				strs = new_strs;
			}
			strs[n_strs++] = (ccp)(raw + start);
		}
	}

	if (!n_strs)
	{
		FREE (strs);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	char out[PATH_MAX];
	snprintf (out, sizeof (out), "%s/strings.txt", dest);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CHX:%s (%u strings) -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_strs, out);

	enumError err = ERR_OK;
	if (!testmode)
	{
		size_t total = 0;
		for (uint k = 0; k < n_strs; k++)
			total += strlen (strs[k]) + 1;
		if (total > NFMT_MAX_OUTPUT)
			err = ERR_FILE_TOO_BIG;
		else
		{
			char *text = MALLOC (total + 1);
			if (!text)
				err = ERR_OUT_OF_MEMORY;
			else
			{
				char *w = text;
				for (uint k = 0; k < n_strs; k++)
				{
					const size_t l = strlen (strs[k]);
					memcpy (w, strs[k], l);
					w += l;
					*w++ = '\n';
				}
				*w = 0;
				if (SaveFile (out, 0, 0, (const u8 *)text, (uint)(w - text), 0))
					err = ERR_CANT_CREATE;
				FREE (text);
			}
		}
	}

	FREE (strs);
	FREE (raw);
	(void)depth;
	return err;
}

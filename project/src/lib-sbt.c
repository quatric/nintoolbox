// SPDX-License-Identifier: GPL-2.0+
// See lib-sbt.h for the layout and what is/isn't recovered.
#include "lib-sbt.h"
#include "lib-archive-util.h"
#include <ctype.h>
#include <string.h>

enumError ExtractSBTArchive (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (raw_size < 12 || memcmp (raw, "sbtf", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// The cue-name pool sits after the "CTTS" (composition-time-to-sample)
	// sub-atom and reaches EOF; scanning only past it avoids false-positive
	// printable runs inside the preceding binary timing table.
	size_t ctts_end = 4;
	for (size_t k = 4; k + 4 <= raw_size; k++)
		if (!memcmp (raw + k, "CTTS", 4))
		{
			ctts_end = k + 4;
			break;
		}
	size_t i = ctts_end;

	ccp *names = 0;
	uint n_names = 0, cap = 0;
	while (i < raw_size)
	{
		// A run may open with a stray non-identifier byte spilled over from
		// a preceding binary field; only the identifier-shaped suffix of
		// the run is a real cue name.
		if (raw[i] != '_' && !isalnum ((unsigned char)raw[i]))
		{
			i++;
			continue;
		}
		const size_t start = i;
		while (i < raw_size && (raw[i] == '_' || isalnum ((unsigned char)raw[i])))
			i++;
		const size_t len = i - start;
		if (i < raw_size && raw[i] == 0 && len >= 2)
		{
			if (n_names == cap)
			{
				cap = cap ? cap * 2 : 16;
				names = REALLOC (names, cap * sizeof (*names));
			}
			names[n_names++] = (ccp)(raw + start);
			i++; // past the NUL
		}
		else
		{
			// Not immediately NUL-terminated (more non-identifier bytes
			// before the terminator): skip past this run and keep looking.
			while (i < raw_size && raw[i] != 0)
				i++;
			if (i < raw_size)
				i++;
		}
	}

	if (!n_names)
	{
		FREE (names);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	char out[PATH_MAX];
	snprintf (out, sizeof (out), "%s/cues.txt", dest);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SBT:%s (%u cue names) -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_names, out);

	enumError err = ERR_OK;
	if (!testmode)
	{
		size_t total = 0;
		for (uint k = 0; k < n_names; k++)
			total += strlen (names[k]) + 1;
		char *text = MALLOC (total + 1);
		char *w = text;
		for (uint k = 0; k < n_names; k++)
		{
			const size_t l = strlen (names[k]);
			memcpy (w, names[k], l);
			w += l;
			*w++ = '\n';
		}
		*w = 0;
		if (SaveFile (out, 0, 0, (const u8 *)text, (uint)(w - text), 0))
			err = ERR_CANT_CREATE;
		FREE (text);
	}

	FREE (names);
	FREE (raw);
	(void)depth;
	return err;
}

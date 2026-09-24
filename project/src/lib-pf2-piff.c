// SPDX-License-Identifier: GPL-2.0+
// Pony Friends 2 ".rbh" container -- see lib-pf2-piff.h for exactly what
// is and is not understood.

#include "lib-pf2-piff.h"
#include "lib-nintendo.h"
#include <string.h>

bool IsPF2Piff (const u8 *data, size_t size)
{
	if (!data || size < 8 || memcmp (data, "FFIP", 4))
		return false;
	const u32 body_size = rd_be32 (data + 4);
	return body_size == size - 8;
}

static const char *const known_tags[] = { "FHBR", "HHBR", "YDOB", "KCAP", 0 };

enumError DecodePF2Piff_Text (FILE *f, const u8 *data, size_t size)
{
	if (!IsPF2Piff (data, size))
		return ERR_INVALID_DATA;

	const u32 body_size = rd_be32 (data + 4);
	fprintf (f, "FFIP container: body_size=0x%x (%u), file_size=%zu\n",
		body_size, body_size, size);
	fprintf (f, "// internal layout beyond the 8-byte FFIP shell is not "
		"reliably decoded (see lib-pf2-piff.h); below is a diagnostic\n"
		"// scan for the other confirmed reversed-spelling tags only.\n");

	for (size_t off = 8; off + 4 <= size; off++)
	{
		for (int i = 0; known_tags[i]; i++)
			if (!memcmp (data + off, known_tags[i], 4))
			{
				u32 follow = off + 8 <= size ? rd_be32 (data + off + 4) : 0;
				fprintf (f, "  off=0x%08zx tag=%.4s next_u32_be=0x%08x\n",
					off, data + off, follow);
				break;
			}
	}
	return ERR_OK;
}

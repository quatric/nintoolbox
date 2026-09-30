// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "NIBM" streamed-audio wrapper; see lib-nibm.h.
//-----------------------------------------------------------------------------
#include "lib-nibm.h"
#include <string.h>

#define NIBM_SEARCH 4096 // the Ogg stream begins within the first few hundred bytes

// Walk Ogg pages from 'off'; true only if they tile the buffer exactly.
static bool ogg_tiles (const u8 *d, size_t size, size_t off)
{
	uint pages = 0;
	while (off + 27 <= size && !memcmp (d + off, "OggS", 4))
	{
		const uint nseg = d[off + 26];
		if (off + 27 + nseg > size)
			return false;
		size_t payload = 0;
		for (uint i = 0; i < nseg; i++)
			payload += d[off + 27 + i];
		if (off + 27 + nseg + payload > size)
			return false;
		off += 27 + nseg + payload;
		pages++;
	}
	return pages >= 3 && off == size; // identification, comment, setup + audio
}

bool IsNibmOgg (const u8 *data, size_t size, size_t *ogg_off)
{
	if (!data || size < 64 || memcmp (data, "NIBM", 4))
		return false;
	const size_t lim = size < NIBM_SEARCH ? size : NIBM_SEARCH;
	for (size_t i = 8; i + 4 <= lim; i++)
		if (!memcmp (data + i, "OggS", 4))
		{
			if (!ogg_tiles (data, size, i))
				return false;
			*ogg_off = i;
			return true;
		}
	return false;
}

// SPDX-License-Identifier: GPL-2.0+
#include "lib-donkeykonga.h"
#include <string.h>
#include <zlib.h>

//-----------------------------------------------------------------------------
// ".tpl.dkz" "DKZF" zlib texture wrapper

#define DKZF_HEADER_SIZE 8
#define DKZF_MAX_DECOMPRESSED 0x10000000u // 256 MiB sanity cap

bool IsDKZF (const u8 *data, size_t size)
{
	if (!data || size < DKZF_HEADER_SIZE + 2)
		return false;
	if (memcmp (data, "DKZF", 4))
		return false;

	const u32 dsize = rd_be32 (data + 4);
	if (!dsize || dsize > DKZF_MAX_DECOMPRESSED)
		return false;

	// Standard raw zlib stream: first byte is always 0x78 for the
	// deflate/32K-window headers every real sample uses.
	return data[DKZF_HEADER_SIZE] == 0x78;
}

enumError DecodeDKZF (const u8 *data, size_t size, u8 **dest, uint *dest_size)
{
	if (dest)
		*dest = 0;
	if (dest_size)
		*dest_size = 0;
	if (!dest || !dest_size || !IsDKZF (data, size))
		return ERR_INVALID_DATA;

	const u32 dsize = rd_be32 (data + 4);
	u8 *out = MALLOC (dsize ? dsize : 1);
	if (!out)
		return ERR_OUT_OF_MEMORY;

	uLongf destLen = dsize;
	const int zerr = uncompress (out, &destLen, data + DKZF_HEADER_SIZE,
		(uLong)(size - DKZF_HEADER_SIZE));
	if (zerr != Z_OK || destLen != dsize)
	{
		FREE (out);
		return ERR_INVALID_DATA;
	}

	*dest = out;
	*dest_size = dsize;
	return ERR_OK;
}

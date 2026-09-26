// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Sumo Digital .stz container decoder; see lib-sumostz.h.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-sumostz.h"
#include <zlib.h>

#define STZ_PAYLOAD_OFFSET 0x48
#define STZ_SIZE_OFFSET 0x28

static u32 stz_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

bool IsSumoSTZ (const u8 *d, size_t size)
{
	if (!d || size < STZ_PAYLOAD_OFFSET + 2)
		return false;
	if (d[STZ_PAYLOAD_OFFSET] != 0x78 || d[STZ_PAYLOAD_OFFSET + 1] != 0xda)
		return false;

	// Word 0 always holds the payload offset (0x48 in every sample seen,
	// single- and multi-locale alike). The redundant-looking table at
	// 0x00/0x10/0x20 is only self-referential (all entries == 0x48) for a
	// collapsed single-locale build; real multi-locale .stz files (e.g.
	// Assets/Wii_error_popup_*.stz) carry genuine, differing per-locale
	// offsets/sizes in the rest of that table (and their +0x28 field
	// undershoots the real inflated size -- see DecodeSumoSTZ()), so only
	// word 0 is checked structurally, together with the zlib magic above;
	// DecodeSumoSTZ()'s own Z_STREAM_END check on the actual inflate is
	// what really confirms a match.
	const u32 self = stz_be32 (d);
	if (self != STZ_PAYLOAD_OFFSET)
		return false;

	const u32 decompressed_size = stz_be32 (d + STZ_SIZE_OFFSET);
	return decompressed_size && decompressed_size <= 0x10000000u;
}

enumError DecodeSumoSTZ (u8 **dest, uint *dest_size, const u8 *d, size_t size, char tag[4])
{
	if (dest)
		*dest = 0;
	if (dest_size)
		*dest_size = 0;
	if (tag)
		memset (tag, 0, 4);
	if (!IsSumoSTZ (d, size))
		return ERR_NOTHING_TO_DO;

	// The +0x28 field is a reliable size hint on single-locale .stz files,
	// but on multi-locale ones (Assets/Wii_error_popup_*.stz) it undershoots
	// the real inflated size by a wide margin -- so it is only used to seed
	// the output buffer; the buffer grows as needed and the real stop
	// condition is zlib reporting Z_STREAM_END, exactly like the Python
	// prototype's zlib.decompressobj() (which just consumes until done).
	u32 cap = stz_be32 (d + STZ_SIZE_OFFSET);
	if (!cap || cap > 0x10000000u)
		cap = 0x10000;

	u8 *out = MALLOC (cap);
	if (!out)
		return ERR_CANT_CREATE;

	z_stream zs;
	memset (&zs, 0, sizeof (zs));
	zs.next_in = (Bytef *)(d + STZ_PAYLOAD_OFFSET);
	zs.avail_in = (uInt)(size - STZ_PAYLOAD_OFFSET);
	if (inflateInit (&zs) != Z_OK)
	{
		FREE (out);
		return ERR_INVALID_DATA;
	}

	int zerr;
	u32 got = 0;
	do
	{
		if (got == cap)
		{
			if (cap >= 0x10000000u)
			{
				inflateEnd (&zs);
				FREE (out);
				return ERR_INVALID_DATA;
			}
			const u32 ncap = cap > 0x08000000u ? 0x10000000u : cap * 2;
			u8 *nout = REALLOC (out, ncap);
			if (!nout)
			{
				inflateEnd (&zs);
				FREE (out);
				return ERR_CANT_CREATE;
			}
			out = nout;
			cap = ncap;
		}
		zs.next_out = (Bytef *)(out + got);
		zs.avail_out = cap - got;
		zerr = inflate (&zs, Z_NO_FLUSH);
		got = cap - zs.avail_out;
	} while (zerr == Z_OK && zs.avail_in);
	inflateEnd (&zs);

	if (zerr != Z_STREAM_END || !got)
	{
		FREE (out);
		return ERR_INVALID_DATA;
	}
	const u32 raw_size = got;

	if (tag && got >= 4)
	{
		bool ascii = true;
		for (uint i = 0; i < 4 && ascii; i++)
			ascii = out[i] >= 0x20 && out[i] < 0x7f;
		if (ascii)
			memcpy (tag, out, 4);
	}

	*dest = out;
	*dest_size = raw_size;
	return ERR_OK;
}

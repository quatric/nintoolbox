// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "FEZC" file packages; see lib-fezc.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-fezc.h"
#include "lib-std.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <zlib.h>

#define FEZC_HEADER 0x28u
#define FEZC_RECORD 32u
#define FEZC_FLAG_ZLIB 0x102u
#define FEZC_MAX_NAME 0x400u

bool IsFezc (const u8 *data, size_t size)
{
	return data && size >= FEZC_HEADER && !memcmp (data, "FEZC", 4);
}

// Member streams are sync-flushed rather than finished, so inflate until the
// recorded uncompressed size is produced; a missing final block is expected.
static bool fezc_inflate (u8 *out, uint usize, const u8 *in, uint csize)
{
	z_stream zs;
	memset (&zs, 0, sizeof (zs));
	if (inflateInit (&zs) != Z_OK)
		return false;
	zs.next_in = (Bytef *)in;
	zs.avail_in = csize;
	zs.next_out = out;
	zs.avail_out = usize;
	const int r = inflate (&zs, Z_SYNC_FLUSH);
	const bool ok = zs.total_out == usize && (r == Z_OK || r == Z_BUF_ERROR || r == Z_STREAM_END);
	inflateEnd (&zs);
	return ok;
}

// Walks the name table once; returns the offset of the entry after the last
// name, or 0 if a length is out of range.
static uint fezc_next_name (const u8 *d, uint p, uint end, uint *len)
{
	if (p + 4 > end)
		return 0;
	*len = rd_le32 (d + p);
	if (!*len || *len > FEZC_MAX_NAME || *len > end - p - 4)
		return 0;
	return p + 4 + *len;
}

enumError ScanFezc (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *d, size_t size)
{
	if (!entries || !n_entries || !IsFezc (d, size))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const uint n = rd_le32 (d + 8), names_off = rd_le32 (d + 12), records_off = rd_le32 (d + 16),
			   data_off = rd_le32 (d + 20);
	if (!n || names_off != FEZC_HEADER || records_off < names_off || records_off > size
		|| n > (size - records_off) / FEZC_RECORD || data_off != records_off + FEZC_RECORD * n
		|| data_off > size)
		return EINVAL;

	// the name table must hold exactly n names and end where the records begin
	uint p = names_off, len = 0;
	for (uint i = 0; i < n; i++)
	{
		p = fezc_next_name (d, p, records_off, &len);
		if (!p)
			return EINVAL;
	}
	if (p != records_off)
		return EINVAL;

	nintendo_sarc_entry_t *v = CALLOC (n, sizeof (*v));
	if (!v)
		return ERR_OUT_OF_MEMORY;

	uint made = 0;
	p = names_off;
	for (uint i = 0; i < n; i++)
	{
		const uint name_at = p + 4;
		p = fezc_next_name (d, p, records_off, &len);

		const u8 *r = d + records_off + FEZC_RECORD * i;
		const uint flags = rd_le32 (r), off = rd_le32 (r + 4), csize = rd_le32 (r + 8),
				   usize = rd_le32 (r + 12);
		if (off < data_off || off > size || csize > size - off || !usize)
			continue;
		if (flags != FEZC_FLAG_ZLIB && flags != 0)
			continue;
		if (flags == 0 && csize != usize)
			continue;

		// the stored name is NUL-terminated inside its padded slot
		char name[FEZC_MAX_NAME + 1];
		uint nl = 0;
		while (nl < len && d[name_at + nl])
		{
			name[nl] = (char)d[name_at + nl];
			nl++;
		}
		name[nl] = 0;
		if (!nl || name[0] == '/' || strstr (name, ".."))
			continue;

		u8 *out = MALLOC (usize);
		if (!out)
			continue;
		bool ok = true;
		if (flags == 0)
			memcpy (out, d + off, usize);
		else
			ok = fezc_inflate (out, usize, d + off, csize);
		if (!ok)
		{
			FREE (out);
			continue;
		}

		v[made].name = STRDUP (name);
		v[made].data = out;
		v[made].size = usize;
		made++;
	}
	if (!made)
	{
		FREE (v);
		return ERR_NOTHING_TO_DO;
	}
	*entries = v;
	*n_entries = made;
	return ERR_OK;
}

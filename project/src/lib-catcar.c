// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Cat Daddy Games CDGaCube archive scanner; see lib-catcar.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-catcar.h"
#include <string.h>
#include <zlib.h>

#define CAR_MAX_ENTRIES 0x100000
#define CAR_ENTRY_SIZE 24
#define CAR_SECTOR 2048

static u32 car_rd32 (const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }

// Inflate a bare zlib stream that must end exactly at src_size. Returns an
// owned buffer or NULL when the bytes are not such a stream.
static u8 *car_inflate (const u8 *src, size_t src_size, size_t *out_size)
{
	if (!src || !src_size || src_size > 0x1fffffff)
		return 0;
	z_stream strm;
	memset (&strm, 0, sizeof (strm));
	if (inflateInit (&strm) != Z_OK)
		return 0;
	strm.next_in = (Bytef *)src;
	strm.avail_in = (uInt)src_size;
	size_t cap = src_size * 4 + 4096;
	if (cap > 0x20000000u)
		cap = 0x20000000u;
	u8 *out = MALLOC (cap);
	int ret = Z_OK;
	while (out)
	{
		strm.next_out = out + strm.total_out;
		strm.avail_out = (uInt)(cap - strm.total_out);
		ret = inflate (&strm, Z_NO_FLUSH);
		if (ret != Z_OK || strm.avail_out)
			break;
		if (cap >= 0x20000000u)
		{
			FREE (out);
			out = 0;
			break;
		}
		const size_t ncap = cap > 0x10000000u ? 0x20000000u : cap * 2;
		u8 *n = REALLOC (out, ncap);
		if (!n)
		{
			FREE (out);
			out = 0;
		}
		else
		{
			out = n;
			cap = ncap;
		}
	}
	const bool ok = out && ret == Z_STREAM_END && !strm.avail_in;
	*out_size = strm.total_out;
	inflateEnd (&strm);
	if (!ok)
	{
		FREE (out);
		return 0;
	}
	return out;
}

enumError ScanCatCar (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size)
{
	if (!entries || !n_entries || !data || size < 0x18 || memcmp (data, "CDGaCube", 8))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u32 count = car_rd32 (data + 0x0c);
	if (count < 1 || count > CAR_MAX_ENTRIES || car_rd32 (data + 0x10) != CAR_ENTRY_SIZE)
		return EINVAL;
	const u64 table_end = 0x14 + (u64)CAR_ENTRY_SIZE * count;
	if (table_end >= size)
		return EINVAL;
	const u8 *ent = data + 0x14;

	// Name table: one NUL-terminated path per entry, in entry order.
	ccp *names = CALLOC (count, sizeof (*names));
	nintendo_sarc_entry_t *out = CALLOC (count, sizeof (*out));
	if (!names || !out)
	{
		FREE (names);
		FREE (out);
		return ERR_CANT_CREATE;
	}
	const u8 *p = data + table_end, *end = data + size;
	for (uint i = 0; i < count; i++)
	{
		const u8 *z = memchr (p, 0, end - p);
		if (!z)
		{
			FREE (names);
			FREE (out);
			return EINVAL;
		}
		names[i] = (ccp)p;
		p = z + 1;
	}

	uint n = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = ent + (size_t)CAR_ENTRY_SIZE * i;
		if ((car_rd32 (e) & 0x30) != 0x20)
			continue; // directory or "." / ".." entry
		const u32 fsize = car_rd32 (e + 12);
		const u64 off = (u64)car_rd32 (e + 20) * CAR_SECTOR;
		if (off + fsize > size)
		{
			ResetOwnedEntries (out, n);
			FREE (names);
			return EINVAL;
		}
		char name[512];
		if (strlen (names[i]) >= sizeof (name) || !OwnedNameOk (names[i]))
			snprintf (name, sizeof (name), "%05u.bin", i);
		else
			strcpy (name, names[i]);

		const u8 *src = data + off;
		size_t osize = fsize;
		u8 *plain = 0;
		if (fsize > 2 && src[0] == 0x78)
			plain = car_inflate (src, fsize, &osize);
		const bool ok = OwnedEntryAdd (out, n, name, plain ? plain : src, plain ? (uint)osize : fsize);
		FREE (plain);
		if (!ok)
		{
			ResetOwnedEntries (out, n);
			FREE (names);
			return ERR_CANT_CREATE;
		}
		n++;
	}
	FREE (names);
	if (!n)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

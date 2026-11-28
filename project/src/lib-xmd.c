// SPDX-License-Identifier: GPL-2.0+
#include "lib-xmd.h"
#include "lib-xtd.h"
#include "lib-nintendo.h"
#include "lib-std.h"
#include "lib-archive-util.h"
#include <string.h>

// ----------------------------------------------------------------------------
// Genki "GTI Club: Supermini Festa!" model container (.mdl)
// ----------------------------------------------------------------------------
// Header (all fields big-endian, like the "RESOURCE:GX" block it wraps --
// unlike the little-endian "XTD\0" block):
//   +0x00  "XMD" + 0x01 (a 4-byte magic, not the ASCII "XMD\0" the '.mdl'
//          extension might suggest)
//   +0x04  4 ASCII digits + NUL, e.g. "001\0" (format version)
//   +0x08  u32  reserved, 0 in every sample seen
//   +0x0C  u32  block_count -- always 2 so far: geometry, then textures
//   +0x10  u32  geometry_offset -- always 0x20 (right after this header)
//          in every sample seen
//   +0x14  u32  geometry_end -- geometry_offset + <geometry block's own
//          declared size> + 4 pad bytes; also where the texture block
//          begins
//   +0x18  8 bytes reserved, 0
//   +0x20  geometry block: a "RESOURCE:GX" container, byte-identical to
//          the sibling ".r3d" file. Its own header repeats a size field
//          at relative +0x20 (see R3D_SIZE_FIELD_OFFS below) which is
//          what geometry_end above is derived from; the display-list /
//          vertex-array format inside it is not decoded here.
//   4 bytes zero padding
//   geometry_end  texture block: an "XTD\0" table, byte-identical to the
//          sibling ".unq.xtd" file -- see lib-xtd.h.
enum
{
	XMD_HEADER_SIZE = 0x20,
	R3D_SIZE_FIELD_OFFS = 0x20, // size field inside the embedded RESOURCE:GX block
	R3D_MIN_HEADER = 0x24,
};

bool IsXMD (const u8 *data, uint size)
{
	if (!data || size < XMD_HEADER_SIZE || memcmp (data, "XMD\x01", 4))
		return false;

	const u32 block_count = rd_be32 (data + 0x0c);
	const u32 geo_off = rd_be32 (data + 0x10);
	const u32 geo_end = rd_be32 (data + 0x14);
	if (block_count != 2 || geo_off != XMD_HEADER_SIZE)
		return false;
	if ((u64)geo_off + R3D_MIN_HEADER > size || memcmp (data + geo_off, "RESOURCE:GX", 11))
		return false;

	const u32 geo_size = rd_be32 (data + geo_off + R3D_SIZE_FIELD_OFFS);
	if ((u64)geo_off + geo_size + 4 != geo_end || (u64)geo_end > size)
		return false;

	return IsXTD (data + geo_end, size - geo_end);
}

static enumError extract_xmd_bank (const u8 *d, uint size, ccp arg, ccp dest);

// Generic "XMD\1" bank (the .mdl car banks, .gsf, .dat, .xmd ... of the game): the header is "XMD\1", "001\0", u32 flags and u32 count n, then n
// big-endian offsets in ascending order; member i runs up to the next offset (the last one to the end of the file). Whatever lies
// before the first member (the table plus the format specific header words) is kept as "xmd-header.bin".
bool IsXMDBank (const u8 *d, uint size)
{
	if (!d || size < 0x14 || memcmp (d, "XMD\x01", 4))
		return false;
	const u32 n = rd_be32 (d + 12);
	if (!n || n > 4096 || 16 + 4 * (u64)n > size)
		return false;
	u32 prev = 16 + 4 * n - 1;
	for (u32 i = 0; i < n; i++)
	{
		const u32 o = rd_be32 (d + 16 + 4 * i);
		if (o <= prev || o >= size)
			return false;
		prev = o;
	}
	return true;
}

static enumError extract_xmd_bank (const u8 *d, uint size, ccp arg, ccp dest)
{
	const u32 n = rd_be32 (d + 12), first = rd_be32 (d + 16);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT XMD-BANK:%s (%u members) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
	if (testmode)
		return ERR_OK;
	CreatePath (dest, true);
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/xmd-header.bin", dest);
	enumError err = SaveFile (path, 0, 0, d, first, 0);
	for (u32 i = 0; !err && i < n; i++)
	{
		const u32 o = rd_be32 (d + 16 + 4 * i), e = i + 1 < n ? rd_be32 (d + 20 + 4 * i) : size;
		ccp ext = !memcmp (d + o, "RESOURCE:GX", 11) ? "r3d" : !memcmp (d + o, "XTD\0", 4) ? "xtd" : "bin";
		snprintf (path, sizeof (path), "%s/%04u.%s", dest, i, ext);
		err = SaveFile (path, 0, 0, d + o, e - o, 0);
	}
	return err;
}

enumError ExtractXMDArchive (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size <= UINT_MAX && IsXMDBank (raw, (uint)raw_size))
	{
		char bdest[PATH_MAX];
		get_dest_dir (bdest, sizeof (bdest), arg, basedir);
		err = extract_xmd_bank (raw, (uint)raw_size, arg, bdest);
		FREE (raw);
		return err;
	}
	FREE (raw);
	if (!is_ext_match (arg, ".mdl"))
		return ERR_NOTHING_TO_DO;

	err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size > UINT_MAX || !IsXMD (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 geo_off = rd_be32 (raw + 0x10);
	const u32 geo_end = rd_be32 (raw + 0x14);
	const u32 geo_size = geo_end - geo_off - 4;

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	if (!testmode)
		CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT XMD:%s (geometry %u bytes, undecoded + textures) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, geo_size, dest);

	if (!testmode)
	{
		char geo_path[PATH_MAX];
		snprintf (geo_path, sizeof (geo_path), "%s/geometry.r3d", dest);
		err = SaveFile (geo_path, 0, 0, raw + geo_off, geo_size, 0);
	}

	if (!err)
		err = ExtractXTDBuffer (raw + geo_end, (uint)raw_size - geo_end, dest, false, arg);

	FREE (raw);
	return err;
}

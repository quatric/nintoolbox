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

enumError ExtractXMDArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".mdl"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
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

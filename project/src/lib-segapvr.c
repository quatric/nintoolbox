// SPDX-License-Identifier: GPL-2.0+
// Illvelo (Wii) Sega PVR texture header -- see lib-segapvr.h for exactly
// what is and is not decoded.

#include "lib-segapvr.h"
#include "lib-nintendo.h"
#include <string.h>

int IsSegaPVR (const u8 *data, size_t size)
{
	if (!data || size < 16)
		return 0;
	if (!memcmp (data, "XIBG", 4))
	{
		// GBIX header: tag(4) + size(4, BE) + 8-byte global index, then
		// the mandatory PVRT sub-header must follow immediately.
		if (size < 16 + 16)
			return 0;
		return !memcmp (data + 16, "TRVP", 4);
	}
	return !memcmp (data, "TRVP", 4) && size >= 16;
}

enumError DecodeSegaPVR_Text (FILE *f, const u8 *data, size_t size)
{
	if (!f || !IsSegaPVR (data, size))
		return EINVAL;

	fprintf (f, "# Illvelo (Wii) Sega PVR texture\n");
	fprintf (f,
		"# reverse-engineered header only: tags are byte-reversed ASCII\n"
		"# (\"XIBG\"/\"TRVP\" = reversed \"GBIX\"/\"PVRT\"), all numeric fields\n"
		"# are big-endian; the texel payload's pixel encoding could not be\n"
		"# confirmed (see lib-segapvr.h) and is reported by offset/size only\n");

	size_t off = 0;
	if (!memcmp (data, "XIBG", 4))
	{
		const u32 gbix_size = rd_be32 (data + 4);
		const u64 gidx = rd_be64 (data + 8);
		fprintf (f, "GBIX: size=0x%x global_index=0x%llx\n", gbix_size, (unsigned long long)gidx);
		off = 16;
	}

	const u32 pvrt_size = rd_be32 (data + off + 4);
	const u32 type = rd_be32 (data + off + 8);
	const u16 width = rd_be16 (data + off + 12);
	const u16 height = rd_be16 (data + off + 14);
	const u8 pixel_format = type & 0xff;
	const u8 mode = type >> 8 & 0xff;
	static const char *const pf_name[3] = { "ARGB1555", "RGB565", "ARGB4444" };

	const size_t texel_off = off + 16;
	const size_t texel_size = size > texel_off ? size - texel_off : 0;

	fprintf (f, "PVRT: size=0x%x type=0x%x (pixel_format=%u [%s], mode=0x%02x)\n", pvrt_size, type,
		pixel_format, pixel_format < 3 ? pf_name[pixel_format] : "unknown", mode);
	fprintf (f, "  width=%u height=%u texel_data: offset=0x%zx size=0x%zx\n", width, height,
		texel_off, texel_size);

	return ERR_OK;
}

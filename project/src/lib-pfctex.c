#include "lib-std.h"
#include "lib-pfctex.h"
#include "lib-lz10.h"
#include "lib-flim.h"
#include <string.h>
#include <errno.h>

// See lib-pfctex.h for the full layout writeup.

static bool scan_header (pfctex_info_t *info, const u8 *data, uint size)
{
	if (size < 0x18 || rd_le32 (data) != 1 || rd_le32 (data + 4) != 0x14 || data[0x14] != 0x11)
		return false;

	const uint tile_w = rd_le16 (data + 14);
	const uint tile_h = rd_le16 (data + 16);
	if (!tile_w || !tile_h || tile_w > 2048 || tile_h > 2048)
		return false;

	// LZ11's own 24-bit decompressed-size field (falls back to a 32-bit
	// field at +8 when that's 0 -- see DecodeLZ10LZ11()), used here only to
	// sanity-check that the base image actually fits before we bother
	// running the decompressor.
	u32 unpacked = (u32)data[0x15] | (u32)data[0x16] << 8 | (u32)data[0x17] << 16;
	uint hdr_extra = 4;
	if (!unpacked)
	{
		if (size < 0x1c)
			return false;
		unpacked = rd_le32 (data + 0x18);
		hdr_extra = 8;
	}
	(void)hdr_extra;

	const u64 need = (u64)tile_w * 8 * tile_h * 8 * 2;
	if (!need || need > unpacked)
		return false;

	if (info)
	{
		info->width = tile_w * 8;
		info->height = tile_h * 8;
	}
	return true;
}

bool IsPFCTex (const u8 *data, uint size)
{
	return data && scan_header (0, data, size);
}

enumError ScanPFCTex (pfctex_info_t *info, const u8 *data, uint size)
{
	if (!data || !scan_header (info, data, size))
		return EINVAL;
	return ERR_OK;
}

enumError DecodePFCTex_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, uint size)
{
	if (!dest || !width || !height)
		return EINVAL;

	pfctex_info_t info;
	enumError err = ScanPFCTex (&info, data, size);
	if (err)
		return err;

	u8 *unpacked = 0;
	uint unpacked_size = 0;
	err = DecodeLZ10LZ11 (&unpacked, &unpacked_size, data + 0x14, size - 0x14);
	if (err)
		return err;

	const uint w = info.width, h = info.height;
	if ((u64)w * h * 2 > unpacked_size)
	{
		FREE (unpacked);
		return EINVAL;
	}

	u8 *rgba = MALLOC (w * h * 4);
	if (!rgba)
	{
		FREE (unpacked);
		return ERR_CANT_CREATE;
	}

	const uint tw = (w + 7) & ~7u;
	for (uint y = 0; y < h; y++)
		for (uint x = 0; x < w; x++)
		{
			const uint pos = ((y / 8) * (tw / 8) + x / 8) * 64 + morton8 (x & 7, y & 7);
			const u16 val = rd_le16 (unpacked + pos * 2);
			u8 *d = rgba + 4 * (y * w + x);
			d[0] = (u8)((val >> 12) * 17);
			d[1] = (u8)((val >> 8 & 15) * 17);
			d[2] = (u8)((val >> 4 & 15) * 17);
			d[3] = (u8)((val & 15) * 17);
		}

	FREE (unpacked);
	*dest = rgba;
	*width = w;
	*height = h;
	return ERR_OK;
}

#include "lib-std.h"
#include "lib-pfctex.h"
#include "lib-lz10.h"
#include "lib-flim.h"
#include <string.h>
#include <errno.h>

// See lib-pfctex.h for the full layout writeup.

// Pixel format codes seen at header offset 0x12 (the header's last u16).
// Values confirmed by rendering samples of each and checking the result:
//   274 (0x112): RGBA8, 4 bytes/pixel, byte order A,B,G,R (same legacy CTR
//                byte order as BCLIM/CTPK's RGBA8 -- see lib-flim.c)
//   275 (0x113): RGB8, 3 bytes/pixel, byte order B,G,R, opaque
//   276 (0x114): RGBA4444, 2 bytes/pixel, nibble order R,G,B,A
enum
{
	PFCTEX_FMT_RGBA8 = 274,
	PFCTEX_FMT_RGB8 = 275,
	PFCTEX_FMT_RGBA4 = 276,
};

static bool scan_header (pfctex_info_t *info, const u8 *data, uint size)
{
	if (size < 0x18 || rd_le32 (data) != 1 || rd_le32 (data + 4) != 0x14 || data[0x14] != 0x11)
		return false;

	const uint tile_w = rd_le16 (data + 14);
	const uint tile_h = rd_le16 (data + 16);
	const uint fmt = rd_le16 (data + 18);
	if (!tile_w || !tile_h || tile_w > 2048 || tile_h > 2048)
		return false;
	if (fmt != PFCTEX_FMT_RGBA8 && fmt != PFCTEX_FMT_RGB8 && fmt != PFCTEX_FMT_RGBA4)
		return false;
	const uint bpp = fmt == PFCTEX_FMT_RGBA8 ? 4 : fmt == PFCTEX_FMT_RGB8 ? 3 : 2;

	// LZ11's own 24-bit decompressed-size field (falls back to a 32-bit
	// field at +8 when that's 0 -- see DecodeLZ10LZ11()), used here only to
	// sanity-check that the base image actually fits before we bother
	// running the decompressor.
	u32 unpacked = (u32)data[0x15] | (u32)data[0x16] << 8 | (u32)data[0x17] << 16;
	if (!unpacked)
	{
		if (size < 0x1c)
			return false;
		unpacked = rd_le32 (data + 0x18);
	}

	const u64 need = (u64)tile_w * 8 * tile_h * 8 * bpp;
	if (!need || need > unpacked)
		return false;

	if (info)
	{
		info->width = tile_w * 8;
		info->height = tile_h * 8;
		info->format = fmt;
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
	const uint bpp = info.format == PFCTEX_FMT_RGBA8 ? 4 : info.format == PFCTEX_FMT_RGB8 ? 3 : 2;
	if ((u64)w * h * bpp > unpacked_size)
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
			const u8 *p = unpacked + pos * bpp;
			u8 *d = rgba + 4 * (y * w + x);
			switch (info.format)
			{
				case PFCTEX_FMT_RGBA8: // A,B,G,R
					d[0] = p[3];
					d[1] = p[2];
					d[2] = p[1];
					d[3] = p[0];
					break;
				case PFCTEX_FMT_RGB8: // B,G,R, opaque
					d[0] = p[2];
					d[1] = p[1];
					d[2] = p[0];
					d[3] = 0xff;
					break;
				default: // RGBA4444
				{
					const u16 val = rd_le16 (p);
					d[0] = (u8)((val >> 12) * 17);
					d[1] = (u8)((val >> 8 & 15) * 17);
					d[2] = (u8)((val >> 4 & 15) * 17);
					d[3] = (u8)((val & 15) * 17);
					break;
				}
			}
		}

	FREE (unpacked);
	*dest = rgba;
	*width = w;
	*height = h;
	return ERR_OK;
}

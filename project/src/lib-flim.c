#include "lib-std.h"
#include "lib-flim.h"
#include "lib-gtx.h"
#include "lib-bntx.h"
#include <string.h>
#include <errno.h>

// ETC1/ETC1A4 4x4 block decoder. The bit layout (base colors, table
// selection, per-pixel modifier index) was verified pixel-for-pixel
// against the independent `texture2ddecoder` reference decoder (both
// individual and differential color modes, both flip orientations) using
// real BFLIM sample data before this was written -- see commit message.
// Byte layout within the 16-byte ETC1A4 block (alpha first, then color)
// matches the documented Ohana3DS convention. The alpha nibble-to-pixel
// order and the block-to-tile arrangement for images larger than one
// 8x8-pixel tile are NOT independently verified (no oracle covers those);
// they follow the same tiling convention already used and verified for
// this codebase's other BFLIM pixel formats.
static const int16_t etc1_mod_table[8][4] = { { -8, -2, 2, 8 }, { -17, -5, 5, 17 },
	{ -29, -9, 9, 29 }, { -42, -13, 13, 42 }, { -60, -18, 18, 60 }, { -80, -24, 24, 80 },
	{ -106, -33, 33, 106 }, { -183, -47, 47, 183 } };

static inline u8 etc1_clamp255 (int v)
{
	return v < 0 ? 0 : v > 255 ? 255 : (u8)v;
}

// data = 8 bytes ETC1 color block. out = 4x4 RGBA (row-major, 64 bytes).
static void decode_etc1_block (const u8 data[8], u8 *out)
{
	u64 v = 0;
	for (int i = 0; i < 8; i++)
		v = (v << 8) | data[7 - i];

	const int diffbit = (v >> 33) & 1;
	const int flipbit = (v >> 32) & 1;
	const int table1 = (v >> 37) & 7;
	const int table2 = (v >> 34) & 7;
	int r1, g1, b1, r2, g2, b2;

	if (diffbit)
	{
		const int r_base = (int)((v >> 59) & 0x1f);
		const int g_base = (int)((v >> 51) & 0x1f);
		const int b_base = (int)((v >> 43) & 0x1f);
		int dr = (int)((v >> 56) & 7);
		int dg = (int)((v >> 48) & 7);
		int db = (int)((v >> 40) & 7);
		if (dr & 4)
			dr -= 8;
		if (dg & 4)
			dg -= 8;
		if (db & 4)
			db -= 8;
		const int r2_5 = r_base + dr;
		const int g2_5 = g_base + dg;
		const int b2_5 = b_base + db;
		r1 = expand5 (r_base);
		g1 = expand5 (g_base);
		b1 = expand5 (b_base);
		r2 = expand5 (r2_5);
		g2 = expand5 (g2_5);
		b2 = expand5 (b2_5);
	}
	else
	{
		r1 = (int)(((v >> 60) & 0xf) * 17);
		r2 = (int)(((v >> 56) & 0xf) * 17);
		g1 = (int)(((v >> 52) & 0xf) * 17);
		g2 = (int)(((v >> 48) & 0xf) * 17);
		b1 = (int)(((v >> 44) & 0xf) * 17);
		b2 = (int)(((v >> 40) & 0xf) * 17);
	}

	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 4; x++)
		{
			const int sub = flipbit ? (y >= 2) : (x >= 2);
			const int table = sub ? table2 : table1;
			const int R = sub ? r2 : r1;
			const int G = sub ? g2 : g1;
			const int B = sub ? b2 : b1;
			const int bit_idx = (x * 4 + y);
			const int msb = (int)((v >> (bit_idx + 16)) & 1);
			const int lsb = (int)((v >> bit_idx) & 1);
			const int mod_idx = (msb << 1) | lsb;
			const int mod = etc1_mod_table[table][mod_idx];
			u8 *o = out + 4 * (y * 4 + x);
			o[0] = etc1_clamp255 (R + mod);
			o[1] = etc1_clamp255 (G + mod);
			o[2] = etc1_clamp255 (B + mod);
			o[3] = 255;
		}
}

// Decodes a plain ETC1 (BFLIM fmt 10, no alpha block -- opaque) tiled
// texture into RGBA8. Same block/tile arrangement as decode_etc1a4_tiled,
// just an 8-byte color-only block instead of 16 bytes.
enumError decode_etc1_tiled (u8 *rgba, const u8 *src, uint w, uint h, uint data_size)
{
	const uint tw = (w + 7) & ~7u, th = (h + 7) & ~7u;
	const uint bw = (tw + 3) / 4, bh = (th + 3) / 4;
	if ((u64)bw * bh * 8 > data_size)
		return EINVAL;
	for (uint by = 0; by < bh; by++)
		for (uint bx = 0; bx < bw; bx++)
		{
			const uint tile_idx = (by / 2) * (bw / 2) + bx / 2;
			const uint local = (bx & 1) | (by & 1) << 1;
			const uint block_idx = tile_idx * 4 + local;
			u8 px[64];
			decode_etc1_block (src + (u64)block_idx * 8, px);
			for (int ly = 0; ly < 4; ly++)
				for (int lx = 0; lx < 4; lx++)
				{
					const uint x = bx * 4 + lx, y = by * 4 + ly;
					if (x >= w || y >= h)
						continue;
					memcpy (rgba + 4 * (y * w + x), px + 4 * (ly * 4 + lx), 4);
				}
		}
	return ERR_OK;
}

// Decodes an ETC1A4 (BFLIM fmt 11) tiled texture into RGBA8. Block
// arrangement follows the same 8x8-tile Morton scheme as this file's other
// tiled BFLIM formats (morton8), applied at 4x4-block granularity.
enumError decode_etc1a4_tiled (u8 *rgba, const u8 *src, uint w, uint h, uint data_size)
{
	const uint tw = (w + 7) & ~7u, th = (h + 7) & ~7u;
	const uint bw = (tw + 3) / 4, bh = (th + 3) / 4; // blocks across/down (tile-padded)
	if ((u64)bw * bh * 16 > data_size)
		return EINVAL;
	for (uint by = 0; by < bh; by++)
		for (uint bx = 0; bx < bw; bx++)
		{
			const uint tile_idx = (by / 2) * (bw / 2) + bx / 2;
			const uint local = (bx & 1) | (by & 1) << 1;
			const uint block_idx = tile_idx * 4 + local;
			const u8 *block = src + (u64)block_idx * 16;
			u8 alpha4[8], color8[8];
			memcpy (alpha4, block, 8);
			memcpy (color8, block + 8, 8);
			u8 px[64];
			decode_etc1_block (color8, px);
			for (int ly = 0; ly < 4; ly++)
				for (int lx = 0; lx < 4; lx++)
				{
					const uint x = bx * 4 + lx, y = by * 4 + ly;
					if (x >= w || y >= h)
						continue;
					const int p = lx * 4 + ly; // same column-major numbering as color
					const u8 nib = (p & 1) ? (alpha4[p >> 1] >> 4) : (alpha4[p >> 1] & 0xF);
					u8 *d = rgba + 4 * (y * w + x);
					const u8 *s = px + 4 * (ly * 4 + lx);
					d[0] = s[0];
					d[1] = s[1];
					d[2] = s[2];
					d[3] = (u8)(nib * 17);
				}
		}
	return ERR_OK;
}

static void bc1_block_wrap (const u8 *b, u8 *out)
{
	decode_bc1_block (b, out, false);
}

// Decodes a BC1..BC5 (DXT-family) tiled BFLIM texture into RGBA8. Same
// 8x8-tile Morton scheme as the ETC1 decoders above; block_size is 8 bytes
// for BC1/BC4, 16 for BC2/BC3/BC5. Reuses the already-verified block
// decoders from lib-bntx.c (BC1..BC5 are a standard, platform-agnostic
// byte layout -- Switch BNTX and Wii U BFLIM both use the same block math,
// only the surrounding container/swizzle differs).
static enumError decode_bc_tiled (u8 *rgba, const u8 *src, uint w, uint h, uint data_size,
	uint block_size, void (*decode_block) (const u8 *, u8 *))
{
	const uint tw = (w + 7) & ~7u, th = (h + 7) & ~7u;
	const uint bw = (tw + 3) / 4, bh = (th + 3) / 4;
	if ((u64)bw * bh * block_size > data_size)
		return EINVAL;
	for (uint by = 0; by < bh; by++)
		for (uint bx = 0; bx < bw; bx++)
		{
			const uint tile_idx = (by / 2) * (bw / 2) + bx / 2;
			const uint local = (bx & 1) | (by & 1) << 1;
			const uint block_idx = tile_idx * 4 + local;
			u8 px[64];
			decode_block (src + (u64)block_idx * block_size, px);
			for (int ly = 0; ly < 4; ly++)
				for (int lx = 0; lx < 4; lx++)
				{
					const uint x = bx * 4 + lx, y = by * 4 + ly;
					if (x >= w || y >= h)
						continue;
					memcpy (rgba + 4 * (y * w + x), px + 4 * (ly * 4 + lx), 4);
				}
		}
	return ERR_OK;
}

enumError DecodeFLIM_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size)
{
	if (!dest || !width || !height || !src || src_size < 0x28)
		return EINVAL;
	const u8 *foot = src + src_size - 0x28;
	if ((memcmp (foot, "FLIM", 4) && memcmp (foot, "CLIM", 4))
		|| (foot[4] != 0xfe || foot[5] != 0xff) && (foot[4] != 0xff || foot[5] != 0xfe))
		return EINVAL;
	const bool be = foot[4] == 0xfe;
	u16 (*r16) (const u8 *) = be ? rd_be16 : rd_le16;
	u32 (*r32) (const u8 *) = be ? rd_be32 : rd_le32;
	if (r16 (foot + 6) != 0x14 || memcmp (foot + 0x14, "imag", 4) || r32 (foot + 0x18) != 0x10)
		return EINVAL;
	if (be)
	{
		const uint w = r16 (foot + 0x1c), h = r16 (foot + 0x1e);
		const uint bflim_fmt = foot[0x22];
		const uint flags = foot[0x23];
		const uint tile_mode = flags & 31;
		const uint swizzle = (uint)((flags >> 5) & 7) << 8;
		const uint data_size = r32 (src + src_size - 4);
		if (!w || !h || w > 16384 || h > 16384 || data_size > src_size - 0x28)
			return EINVAL;

		uint gx2_fmt = 0;
		switch (bflim_fmt)
		{
			case 0: gx2_fmt = 0x0001; break; // L8
			case 1: gx2_fmt = 0x0001; break; // A8
			case 2: gx2_fmt = 0x0002; break; // LA4
			case 3: gx2_fmt = 0x0007; break; // LA8
			case 4: gx2_fmt = 0x0007; break; // HILO8
			case 5: gx2_fmt = 0x0008; break; // RGB565
			case 6: gx2_fmt = 0x001a; break; // B8G8R8A8
			case 7: gx2_fmt = 0x000a; break; // RGBA5551
			case 8: gx2_fmt = 0x000b; break; // RGBA4
			case 9: gx2_fmt = 0x001a; break; // RGBA8
			case 10: gx2_fmt = 0x0031; break; // ETC1
			case 11: gx2_fmt = 0x0031; break; // ETC1_A4
			case 12: gx2_fmt = 0x0031; break; // BC1
			case 13: gx2_fmt = 0x0032; break; // BC2
			case 14: gx2_fmt = 0x0033; break; // BC3
			case 15: gx2_fmt = 0x0034; break; // BC4
			case 16: gx2_fmt = 0x0034; break; // BC4_A
			case 17: gx2_fmt = 0x0035; break; // BC5
			case 18: gx2_fmt = 0x0002; break; // L4
			case 19: gx2_fmt = 0x0002; break; // A4
			case 20: gx2_fmt = 0x041a; break; // RGBA8_SRGB
			case 21: gx2_fmt = 0x0431; break; // BC1_SRGB
			case 22: gx2_fmt = 0x0432; break; // BC2_SRGB
			case 23: gx2_fmt = 0x0433; break; // BC3_SRGB
			default: return EINVAL;
		}

		uint out_w = 0, out_h = 0;
		enumError err = DecodeGX2SurfaceSlice_RGBA (dest, &out_w, &out_h, 1, w, h, 1,
			gx2_fmt, 0, tile_mode, 0, swizzle, 0, 0, src, data_size);
		if (!err)
		{
			*width = out_w;
			*height = out_h;
			return ERR_OK;
		}
		return err;
	}

	const uint w = r16 (foot + 0x1c), h = r16 (foot + 0x1e);
	// Layout after the "imag" sub-header (all LE): u16 width, u16 height,
	// u16 count/orientation (unused here), u8 format, u8 flags (bit0-4 =
	// tile mode). The format byte sits at +0x22, not +0x20 -- +0x20 is the
	// count field, so reading it there was decoding every CTR BCLIM/BFLIM
	// as whatever format happens to be encoded in the low byte of "count"
	// (always 1 = A8) instead of the real pixel format written by the
	// encoder at +0x22.
	const uint fmt = foot[0x22], tile_mode = 1;
	const uint data_size = r32 (src + src_size - 4);

	if (fmt == 10 || fmt == 11) // ETC1 (fmt 10, opaque) / ETC1A4 (fmt 11): block-compressed
	{
		if (!w || !h || w > 16384 || h > 16384 || data_size > src_size - 0x28)
			return EINVAL;
		if ((u64)w * h > NFMT_MAX_OUTPUT / 4)
			return EINVAL;
		u8 *rgba = MALLOC (w * h * 4);
		if (!rgba)
			return ERR_CANT_CREATE;
		enumError err = fmt == 11 ? decode_etc1a4_tiled (rgba, src, w, h, data_size)
								  : decode_etc1_tiled (rgba, src, w, h, data_size);
		if (err)
		{
			FREE (rgba);
			return err;
		}
		*dest = rgba;
		*width = w;
		*height = h;
		return ERR_OK;
	}

	// BC1..BC5 (fmt 14=BC3, 15/16=BC4 [two IDs for the same format, per
	// Nintendo-File-Formats' documented BFLIM table], 17=BC5, and the
	// version-3.3.0.0 SRGB variants 21=BC1_SRGB/22=BC2_SRGB/23=BC3_SRGB --
	// SRGB only changes gamma interpretation, not the block bit layout, so
	// it decodes identically to the UNORM form here). fmt 12/13 are left as
	// the existing L4/A4 nibble path below: real Wii U BFLIM files using
	// those IDs would need BC1/BC2 instead per the same table, but no file
	// in any real corpus checked against this fork has been observed using
	// them, so that's flagged as an open question rather than guessed at.
	if (fmt == 14 || fmt == 15 || fmt == 16 || fmt == 17 || fmt == 21 || fmt == 22 || fmt == 23)
	{
		if (!w || !h || w > 16384 || h > 16384 || data_size > src_size - 0x28)
			return EINVAL;
		if ((u64)w * h > NFMT_MAX_OUTPUT / 4)
			return EINVAL;
		u8 *rgba = MALLOC (w * h * 4);
		if (!rgba)
			return ERR_CANT_CREATE;
		enumError err;
		switch (fmt)
		{
			case 14:
			case 23:
				err = decode_bc_tiled (rgba, src, w, h, data_size, 16, decode_bc3_block);
				break;
			case 15:
			case 16:
				err = decode_bc_tiled (rgba, src, w, h, data_size, 8, decode_bc4_block);
				break;
			case 17:
				err = decode_bc_tiled (rgba, src, w, h, data_size, 16, decode_bc5_block);
				break;
			case 22:
				err = decode_bc_tiled (rgba, src, w, h, data_size, 16, decode_bc2_block);
				break;
			default /*21*/:
				err = decode_bc_tiled (rgba, src, w, h, data_size, 8, bc1_block_wrap);
				break;
		}
		if (err)
		{
			FREE (rgba);
			return err;
		}
		*dest = rgba;
		*width = w;
		*height = h;
		return ERR_OK;
	}

	const bool nibble_fmt = fmt == 12 || fmt == 13; // L4 / A4: 4 bits/pixel
	uint bpp;
	switch (fmt)
	{
		case 0:
		case 1:
		case 2:
			bpp = 1;
			break;
		case 12:
		case 13:
			bpp = 0;
			break; // nibble_fmt: handled separately below
		case 3:
		case 5:
		case 7:
		case 8:
			bpp = 2;
			break;
		case 9:
		case 20:
			bpp = 4;
			break;
		default:
			return EINVAL;
	}
	if (!w || !h || w > 16384 || h > 16384 || data_size > src_size - 0x28)
		return EINVAL;
	const uint tw = (w + 7) & ~7u, th = (h + 7) & ~7u;
	const u64 need = nibble_fmt ? ((u64)(tile_mode ? tw : w) * (tile_mode ? th : h) + 1) / 2
								: (u64)(tile_mode ? tw : w) * (tile_mode ? th : h) * bpp;
	if (need > data_size || (u64)w * h > NFMT_MAX_OUTPUT / 4)
		return EINVAL;
	u8 *rgba = MALLOC (w * h * 4);
	if (!rgba)
		return ERR_CANT_CREATE;
	for (uint y = 0; y < h; y++)
		for (uint x = 0; x < w; x++)
		{
			uint pos;
			if (tile_mode)
				pos = ((y / 8) * (tw / 8) + x / 8) * 64 + morton8 (x & 7, y & 7);
			else
				pos = y * w + x;
			u8 *d = rgba + 4 * (y * w + x);
			if (nibble_fmt)
			{
				const u8 byte = src[pos >> 1];
				const u8 nib = (pos & 1) ? (byte >> 4) : (byte & 0xF);
				const u8 v = (u8)(nib * 17);
				d[0] = d[1] = d[2] = d[3] = v;
				continue;
			}
			const u8 *p = src + pos * bpp;
			if (fmt == 0 || fmt == 1)
				d[0] = d[1] = d[2] = d[3] = p[0];
			else if (fmt == 2) // LA4: low nibble = luminance, high nibble = alpha
			{
				d[0] = d[1] = d[2] = (u8)((p[0] & 0xF) * 17);
				d[3] = (u8)((p[0] >> 4) * 17);
			}
			else if (fmt == 3) // LA8: byte0 = luminance, byte1 = alpha
			{
				d[0] = d[1] = d[2] = p[0];
				d[3] = p[1];
			}
			else if (fmt == 5)
			{
				const u16 c = r16 (p);
				d[0] = expand5 (c >> 11);
				d[1] = (c >> 5 & 63) * 255 / 63;
				d[2] = expand5 (c);
				d[3] = 255;
			}
			else if (fmt == 7)
			{
				const u16 c = r16 (p);
				d[0] = expand5 (c >> 11);
				d[1] = expand5 (c >> 6);
				d[2] = expand5 (c >> 1);
				d[3] = c & 1 ? 255 : 0;
			}
			else if (fmt == 8)
			{
				const u16 c = r16 (p);
				d[0] = (c >> 12) * 17;
				d[1] = (c >> 8 & 15) * 17;
				d[2] = (c >> 4 & 15) * 17;
				d[3] = (c & 15) * 17;
			}
			else // CTR/GX2 RGBA8 byte storage is A,B,G,R.
			{
				d[0] = p[3];
				d[1] = p[2];
				d[2] = p[1];
				d[3] = p[0];
			}
		}
	*dest = rgba;
	*width = w;
	*height = h;
	return ERR_OK;
}

enumError EncodeFLIM_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, bool bclim)
{
	if (!dest || !dest_size || !rgba || !width || !height || width > 16384 || height > 16384)
		return EINVAL;
	const uint tw = (width + 7) & ~7u, th = (height + 7) & ~7u;
	const u64 pixels = (u64)tw * th;
	if (pixels > (NFMT_MAX_OUTPUT - 0x28) / 4)
		return EFBIG;
	const uint image_size = 4 * pixels, total = image_size + 0x28;
	u8 *out = CALLOC (1, total);
	if (!out)
		return ERR_CANT_CREATE;
	for (uint y = 0; y < height; y++)
		for (uint x = 0; x < width; x++)
		{
			const uint pos = ((y / 8) * (tw / 8) + x / 8) * 64 + morton8 (x & 7, y & 7);
			const u8 *s = rgba + 4 * (y * width + x);
			u8 *d = out + 4 * pos;
			d[0] = s[3];
			d[1] = s[2];
			d[2] = s[1];
			d[3] = s[0]; // A,B,G,R
		}
	u8 *foot = out + image_size;
	memcpy (foot, bclim ? "CLIM" : "FLIM", 4);
	foot[4] = 0xff;
	foot[5] = 0xfe; // little endian BOM
	wr_le16 (foot + 6, 0x14);
	wr_le32 (foot + 8, 0x00020002); // BFLIM v2.2, accepted by CTR readers
	wr_le32 (foot + 0x0c, total);
	wr_le16 (foot + 0x10, 1);
	memcpy (foot + 0x14, "imag", 4);
	wr_le32 (foot + 0x18, 0x10);
	wr_le16 (foot + 0x1c, width);
	wr_le16 (foot + 0x1e, height);
	wr_le16 (foot + 0x20, 1);
	foot[0x22] = 9; // RGBA8
	foot[0x23] = 1; // 8x8 Morton tiles
	wr_le32 (foot + 0x24, image_size);
	*dest = out;
	*dest_size = total;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////			NUTEXB (Switch texture wrapper)		///////////////
//-----------------------------------------------------------------------------

// NUTEXB layout, all fields little endian (struct verified field-by-field
// against Switch-Toolbox's NUTEXB.cs -- KillzXGaming/Switch-Toolbox,
// File_Formats/Texture/NUTEXB.cs -- which is the closest thing to a spec
// this format has; there's no equivalent to 3dbrew for it). Everything
// lives in a fixed 0x70-byte trailer at the end of the file:
//   size-0x70   1 byte padding, then a 3-byte tag (unused by any reader,
//               including this one), then a NUL-terminated texture name
//               (the trailing bytes of this 64-byte name field are padding)
//   size-0x30   u32 padding, u32 width, u32 height, u32 depth,
//               u16 NUTEXImageFormat, u16 padding, u32 unk,
//               u32 mip_count, u32 alignment, u32 array_count,
//               u32 image_size
//   size-8      1 byte padding
//   size-7      3-byte magic "XET"
//   size-4      u32 version
// image_size bytes of (Tegra block-linear swizzled) pixel data sit at
// offset 0 of the file; a per-array-slice table of mip_count u32 mip sizes
// (each slice's table padded to 0x40 bytes) follows at offset image_size.
// This decoder only reads array slice 0, mip 0 -- the same single-texture
// scope as DecodeBNTX_RGBA and DecodeFLIM_RGBA.
//
// The pixel formats NUTEXB actually carries (RGBA8/BGRA8/BC1-BC7) are the
// same ones lib-bntx.c's Tegra deswizzle and block decoders already handle,
// just spelled with different numeric format codes; rather than
// reimplementing that swizzle/block math a second time, this builds a
// one-texture synthetic bntx_t using BNTX's own format-word encoding and
// hands it to DecodeBNTX_RGBA. NUTEXB has no field equivalent to BNTX's
// block_height_log2/tile_mode, so those are derived from height using the
// same GOB-count heuristic this codebase's own EncodeBNTX_RGBA already
// applies for its RGBA8 encodes; that heuristic is unverified here against
// a real compressed (BC1-BC7) NUTEXB sample with non-power-of-two height
// (see the NUTEXB README row).

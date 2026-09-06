
/***************************************************************************
 *                         _______ _______ _______                         *
 *                        |  ___  |____   |  ___  |                        *
 *                        | |   |_|    / /| |   |_|                        *
 *                        | |_____    / / | |_____                         *
 *                        |_____  |  / /  |_____  |                        *
 *                         _    | | / /    _    | |                        *
 *                        | |___| |/ /____| |___| |                        *
 *                        |_______|_______|_______|                        *
 *                                                                         *
 *                            Wiimms SZS Tools                             *
 *                          https://szs.wiimm.de/                          *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This file is part of the SZS project.                                 *
 *   Visit https://szs.wiimm.de/ for project details and sources.          *
 *                                                                         *
 *   Copyright (c) 2011-2024 by Dirk Clemens <wiimm@wiimm.de>              *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   See file gpl-2.0.txt or http://www.gnu.org/licenses/gpl-2.0.txt       *
 *                                                                         *
 ***************************************************************************/

#include "lib-std.h"
#include <png.h>
#include "lib-nintendo.h"
#include "lib-image.h"
#include "lib-breff.h"
#include "lib-bzip2.h"
#include "dclib-utf8.h"
#include "lib-plt0.h"
#include "ajpg/ajpg.h"
#include "lib-bntx.h"
#include "lib-smdh.h"
#include "lib-gtx.h"
#include "lib-nitro.h"
#include "lib-nut.h"

#include "red-36.inc"
#include "blue-40.inc"
#include "cup-images.inc"

///////////////////////////////////////////////////////////////////////////////

#ifdef TEST
#define ENABLE_IMAGE_TYPE_LOG 0 // 0|1
#define ENABLE_EXPORT_TIMER 0 // 0|1|2
#else
#define ENABLE_IMAGE_TYPE_LOG 0
#define ENABLE_EXPORT_TIMER 0
#endif

//
///////////////////////////////////////////////////////////////////////////////
///////////////			transformation data		///////////////
///////////////////////////////////////////////////////////////////////////////
// [[transform_mode_t]]

typedef enum transform_mode_t
{
	TM_IDX_FILE, // file format (=byte index)
	TM_IDX_IMG, // image format (=byte index)
	TM_IDX_PAL, // palette format (=byte index)
	TM_IDX_PALETTE, // switch PALETTE
	TM_IDX_COLOR, // switch COLOR
	TM_IDX_ALPHA, // switch ALPHA

	TM_IDX_N, // number of modes
	TM_IDX_MASK = 7,

	TM_F_PAL = 0x10, // flag: Palette support

	TF_ON = 1,
	TF_OFF = 2,
} transform_mode_t;

///////////////////////////////////////////////////////////////////////////////
// [[transform_t]]

typedef struct transform_t
{
	char src[TM_IDX_N];
	char dest[TM_IDX_N];
} transform_t;

#define MAX_TRANSFORM 100

static uint n_transform = 0;
static transform_t transform[MAX_TRANSFORM];

//
///////////////////////////////////////////////////////////////////////////////
///////////////		    AssignIMG(), LoadIMG()		///////////////
///////////////////////////////////////////////////////////////////////////////

// Attaches a decoded, tightly packed width*height RGBA8 buffer to 'img'.
//
// The rest of the image pipeline (SavePNG() and friends) indexes img->data
// with the EXPAND8-rounded xwidth/xheight stride, not the raw dimensions, so
// anything whose size is not already a multiple of 8 has to be repacked into
// that stride here -- assigning xwidth=width directly trips a DASSERT in
// SavePNG(). 'rgba' must be dclib-allocated; ownership transfers to 'img'.
static void AssignDecodedRGBA (Image_t *img, // pointer to valid img
	u8 *rgba, // tightly packed width*height RGBA8
	uint width, uint height,
	const endian_func_t *endian, // endianness the source format used
	ccp fname // object name, assigned
)
{
	const uint xwidth = EXPAND8 (width), xheight = EXPAND8 (height);
	u8 *data = rgba;
	if (xwidth != width || xheight != height)
	{
		data = CALLOC (1, (size_t)xwidth * xheight * 4);
		for (uint y = 0; y < height; y++)
			memcpy (data + (size_t)y * xwidth * 4, rgba + (size_t)y * width * 4, width * 4);
		FREE (rgba);
	}

	img->data = data;
	img->data_alloced = true;
	img->data_size = xwidth * xheight * 4;
	img->width = width;
	img->xwidth = xwidth;
	img->height = height;
	img->xheight = xheight;
	img->iform = img->info_iform = IMG_X_RGB;
	img->info_fform = FF_UNKNOWN;
	img->info_n_image = 1;
	img->alpha_status = 0;
	img->endian = endian;
	img->path = fname;
	img->seq_num = ++image_seq_num;
}

// GVR is Sega's GameCube/Wii texture wrapper.  The texture body uses the GX
// tile layouts, but has its own tiny GCIX/GVRT header.  This decoder covers
// the non-paletted formats used by Super Monkey Ball: Banana Blitz.
//
// The layout was independently verified against the MIT-licensed GvrTool
// project (https://github.com/MaikelChan/GvrTool), whose format notes credit
// the earlier Puyo Tools research.
static inline u8 GVRScale (uint value, uint max)
{
	return (u8)( value * 255 / max );
}

static void GVRStore (u8 *rgba, uint width, uint height, uint x, uint y, u8 r, u8 g, u8 b, u8 a)
{
	if (x < width && y < height)
	{
		u8 *dest = rgba + ( (size_t)y * width + x ) * 4;
		dest[0] = r;
		dest[1] = g;
		dest[2] = b;
		dest[3] = a;
	}
}

static void GVRDecode565 (u16 pixel, u8 *rgba)
{
	rgba[0] = GVRScale (pixel >> 11, 31);
	rgba[1] = GVRScale (pixel >> 5 & 63, 63);
	rgba[2] = GVRScale (pixel & 31, 31);
	rgba[3] = 255;
}

static enumError DecodeGVR_RGBA (u8 **rgba_ptr, uint *width_ptr, uint *height_ptr, const u8 *data, uint data_size)
{
	*rgba_ptr = 0;
	*width_ptr = *height_ptr = 0;
	if (data_size < 0x20 || memcmp (data, "GCIX", 4) || memcmp (data + 0x10, "GVRT", 4))
		return ERR_INVALID_DATA;

	const uint flags = data[0x1a] & 0x0f;
	const uint format = data[0x1b];
	const uint width = be_func.rd16 (data + 0x1c);
	const uint height = be_func.rd16 (data + 0x1e);
	if (!width || !height || width > 16384 || height > 16384 || (size_t)width * height > UINT_MAX / 4)
		return ERR_INVALID_DATA;
	if (flags & 0x0b) // mipmaps or palettes require a separately selected level/palette
		return ERR_INVALID_DATA;

	u8 *rgba = MALLOC ((size_t)width * height * 4);
	const u8 *src = data + 0x20, *end = data + data_size;
	#define GVR_NEED(n) do { if ((size_t)(end-src) < (n)) goto invalid_gvr; } while(0)

	if (format == 0 || format == 1 || format == 2 || format == 3)
	{
		const uint tw = format == 0 ? 8 : format == 1 || format == 2 ? 8 : 4;
		const uint th = format == 0 ? 8 : format == 1 || format == 2 ? 4 : 4;
		for (uint by = 0; by < height; by += th)
			for (uint bx = 0; bx < width; bx += tw)
				for (uint y = 0; y < th; y++)
					for (uint x = 0; x < tw; x++)
					{
						u8 i, a = 255;
						if (format == 0)
						{
							GVR_NEED (1);
							i = x & 1 ? *src++ & 15 : *src >> 4;
						}
						else
						{
							GVR_NEED (format == 3 ? 2 : 1);
							i = *src++;
							if (format == 2) { a = GVRScale (i >> 4, 15); i = GVRScale (i & 15, 15); }
							else if (format == 3) { a = i; i = *src++; }
						}
						if (format == 0) i = GVRScale (i, 15);
						GVRStore (rgba, width, height, bx+x, by+y, i, i, i, a);
					}
	}
	else if (format == 4 || format == 5)
	{
		for (uint by = 0; by < height; by += 4)
			for (uint bx = 0; bx < width; bx += 4)
				for (uint y = 0; y < 4; y++)
					for (uint x = 0; x < 4; x++)
					{
						GVR_NEED (2);
						const u16 p = be_func.rd16 (src); src += 2;
						u8 out[4];
						if (format == 4)
							GVRDecode565 (p, out);
						else if (p & 0x8000)
						{
							out[0] = GVRScale (p >> 10 & 31, 31); out[1] = GVRScale (p >> 5 & 31, 31);
							out[2] = GVRScale (p & 31, 31); out[3] = 255;
						}
						else
						{
							out[0] = GVRScale (p >> 8 & 15, 15); out[1] = GVRScale (p >> 4 & 15, 15);
							out[2] = GVRScale (p & 15, 15); out[3] = GVRScale (p >> 12 & 7, 7);
						}
						GVRStore (rgba, width, height, bx+x, by+y, out[0], out[1], out[2], out[3]);
					}
	}
	else if (format == 6)
	{
		for (uint by = 0; by < height; by += 4)
			for (uint bx = 0; bx < width; bx += 4)
			{
				GVR_NEED (64);
				for (uint y = 0; y < 4; y++)
					for (uint x = 0; x < 4; x++)
					{
						const uint i = ( y * 4 + x ) * 2;
						GVRStore (rgba, width, height, bx+x, by+y, src[i+1], src[32+i], src[33+i], src[i]);
					}
				src += 64;
			}
	}
	else if (format == 0x0e)
	{
		for (uint by = 0; by < height; by += 8)
			for (uint bx = 0; bx < width; bx += 8)
				for (uint sy = 0; sy < 8; sy += 4)
					for (uint sx = 0; sx < 8; sx += 4)
					{
						GVR_NEED (8); u8 pal[4][4];
						GVRDecode565 (be_func.rd16(src), pal[0]); GVRDecode565 (be_func.rd16(src+2), pal[1]);
						const u16 p0 = be_func.rd16(src), p1 = be_func.rd16(src+2); src += 4;
						for (uint c = 0; c < 3; c++)
							if (p0 > p1) { pal[2][c]=(2*pal[0][c]+pal[1][c])/3; pal[3][c]=(pal[0][c]+2*pal[1][c])/3; }
							else { pal[2][c]=(pal[0][c]+pal[1][c])/2; pal[3][c]=0; }
						pal[2][3] = 255; pal[3][3] = p0 > p1 ? 255 : 0;
						for (uint y = 0; y < 4; y++) { const u8 row = *src++; for (uint x = 0; x < 4; x++) { const u8 *p = pal[row >> (6-2*x) & 3]; GVRStore(rgba,width,height,bx+sx+x,by+sy+y,p[0],p[1],p[2],p[3]); } }
					}
	}
	else
		goto invalid_gvr;

	#undef GVR_NEED
	*rgba_ptr = rgba; *width_ptr = width; *height_ptr = height;
	return ERR_OK;

invalid_gvr:
	#undef GVR_NEED
	FREE (rgba);
	return ERR_INVALID_DATA;
}

// Public wrapper around InitializeIMG+AssignDecodedRGBA+SavePNG+ResetIMG for
// callers outside this file that already have a decoded RGBA8 buffer (e.g.
// wszst.c writing FTEX textures found inside a Wii U BFRES as sibling PNGs
// during extraction) and just want it on disk, without building their own
// Image_t. 'rgba' must be dclib-allocated; ownership transfers in either case.
enumError SaveDecodedRGBAToPNG (u8 *rgba, uint width, uint height, const endian_func_t *endian,
	ccp path1, ccp path2, bool overwrite)
{
	Image_t img;
	InitializeIMG (&img);
	AssignDecodedRGBA (&img, rgba, width, height, endian, path2 ? path2 : path1);
	const enumError err = SavePNG (&img, false, 0, path1, path2, 0, overwrite, 0);
	ResetIMG (&img);
	return err;
}

// [[brfna-compress]] Archived-font (.brfna) TGLP sheets whose sheetFormat has
// bit 0x8000 set store each sheet as a 4-byte-BE-size-prefixed compressed
// chunk instead of raw GX pixel data. This is a proprietary, undocumented
// codec RE'd from nw4r_fontcvtr.exe (no written spec exists anywhere in the
// SDK) -- decompiled via Ghidra and cross-checked against real retail
// samples (round-tripped through the actual Nintendo tool under Wine to get
// byte-exact ground truth, then independently verified by rendering decoded
// output and confirming legible glyph shapes). See the brfna_archived_font_
// format memory for the full RE trail. Three of the four opcodes are
// implemented (the fourth, 0x80, is a delta/predictive table encoder never
// observed on real pixel-sheet data and is treated as unsupported).

// nibble 0x10: classic byte-oriented LZSS. One control byte selects 8
// following operations MSB-first: 0-bit = literal byte, 1-bit = a 2-byte
// (length,distance) back-reference into the growing output itself
// (overlapping copies allowed, same as any textbook LZ77 variant).
static uint DecodeBRFNA_LZSS (const u8 *src, uint src_size, uint target, u8 *dest, uint dest_size)
{
	uint out = 0, in = 0;
	while (out < target && in < src_size)
	{
		u8 ctrl = src[in++];
		for (uint bit = 0; bit < 8 && out < target; bit++, ctrl <<= 1)
		{
			if (ctrl & 0x80)
			{
				if (in + 1 >= src_size)
					return out;
				const u8 b0 = src[in], b1 = src[in + 1];
				in += 2;
				const uint len = (b0 >> 4) + 3;
				const uint dist = (b1 | (b0 & 0xF) << 8) + 1;
				if (dist > out)
					return out;
				uint base = out - dist;
				for (uint k = 0; k < len && out < target; k++, out++)
				{
					if (out >= dest_size)
						return out;
					dest[out] = dest[base + k];
				}
			}
			else
			{
				if (in >= src_size || out >= dest_size)
					return out;
				dest[out++] = src[in++];
			}
		}
	}
	return out;
}

// nibble 0x30: simple RLE. Each control byte is either a literal-run count
// (bit7 clear: copy count+1 raw bytes) or a repeat-run (bit7 set: repeat the
// one following byte (count&0x7F)+3 times).
static uint DecodeBRFNA_RLE (const u8 *src, uint src_size, uint target, u8 *dest, uint dest_size)
{
	uint out = 0, in = 0;
	while (out < target && in < src_size)
	{
		const u8 b = src[in];
		if (b & 0x80)
		{
			if (in + 1 >= src_size)
				return out;
			const u8 val = src[in + 1];
			const uint count = (b & 0x7F) + 3;
			in += 2;
			for (uint k = 0; k < count && out < target; k++, out++)
			{
				if (out >= dest_size)
					return out;
				dest[out] = val;
			}
		}
		else
		{
			const uint count = b + 1;
			in++;
			for (uint k = 0; k < count && out < target; k++, out++)
			{
				if (in >= src_size || out >= dest_size)
					return out;
				dest[out] = src[in++];
			}
		}
	}
	return out;
}

// nibble 0x20: a canonical-Huffman-style bit-walk whose code tree is embedded
// directly in the token's own bytes (word-stride array at the token start,
// each entry packing a 6-bit "keep scanning" skip-length plus two branch-
// decision flag bits) rather than a separately transmitted table. The 32-bit
// bit-accumulator's source position is itself computed relative to the same
// token bytes (tok[0]+1)*2), so the whole thing is self-contained per call.
// low_nibble selects the output packing: exactly 8 means one decoded byte
// written per finalize event; any other value means two finalize events are
// packed into one output byte (low nibble first, high nibble second).
static uint DecodeBRFNA_Huffman (
	const u8 *tok, uint tok_size, uint target, u8 *dest, uint dest_size, u8 low_nibble)
{
	if (tok_size < 2)
		return 0;
	u8 bl = tok[1];
	bool half_pending = false;
	u8 half_val = 0;
	uint out = 0, cx = 1;
	uint bitptr = ((uint)tok[0] + 1) * 2 & 0xFFFF;

	while (out < target)
	{
		if ((u64)bitptr + 4 > tok_size)
			return out;
		u32 word = tok[bitptr] | tok[bitptr + 1] << 8 | tok[bitptr + 2] << 16
			| (u32)tok[bitptr + 3] << 24;
		bitptr += 4;
		for (uint b = 0; b < 32 && out < target; b++)
		{
			const uint bit = word >> 31 & 1;
			word <<= 1;
			const bool finalize = bit ? (bl & 0x40) != 0 : (bl & 0x80) != 0;
			const uint idx = cx * 2 + bit;
			if (idx >= tok_size)
				return out;
			if (!finalize)
			{
				bl = tok[idx];
				cx += (bl & 0x3F) + 1;
				continue;
			}
			const u8 val = tok[idx];
			if (low_nibble == 8)
			{
				if (out >= dest_size)
					return out;
				dest[out++] = val;
			}
			else if (half_pending)
			{
				if (out >= dest_size)
					return out;
				dest[out++] = half_val | val << 4;
				half_pending = false;
			}
			else
			{
				half_val = val;
				half_pending = true;
			}
			if (out >= target)
				return out;
			bl = tok[1];
			cx = 1;
		}
	}
	return out;
}

// Outer per-sheet dispatch: reads a 4-byte token (LE32), the top 3 bytes are
// this call's output-byte target, the low nibble of the low byte selects the
// codec. Every real sample checked so far uses exactly one token per sheet
// (the token's own target already equals the full sheet size), so this
// deliberately doesn't implement the ping-pong multi-token chaining the real
// decoder supports for the general case -- if a real file ever needs more
// than one token per sheet this returns false (caller treats as unsupported)
// rather than silently emitting a partially-decoded sheet.
static bool DecompressBRFNASheet (const u8 *comp, uint comp_size, u8 *dest, uint dest_size)
{
	if (comp_size < 4)
		return false;
	const u32 word = comp[0] | comp[1] << 8 | comp[2] << 16 | (u32)comp[3] << 24;
	const uint target = word >> 8;
	const uint opcode = word & 0xF0;
	const uint low_nibble = word & 0xF;
	const u8 *payload = comp + 4;
	const uint payload_size = comp_size - 4;
	if (!target || target > dest_size)
		return false;

	uint produced;
	switch (opcode)
	{
		case 0x10:
			produced = DecodeBRFNA_LZSS (payload, payload_size, target, dest, dest_size);
			break;
		case 0x20:
			produced
				= DecodeBRFNA_Huffman (payload, payload_size, target, dest, dest_size, low_nibble);
			break;
		case 0x30:
			produced = DecodeBRFNA_RLE (payload, payload_size, target, dest, dest_size);
			break;
		default:
			return false; // 0x80 (delta table) or an escape/unknown opcode
	}
	return produced >= target;
}

enumError AssignIMG (Image_t *img, // pointer to valid img
	int init_img, // <0:none, =0:reset, >0:init
	const u8 *data, // source data
	uint data_size, // size of 'data'
	uint img_index, // index of sub image, 0:main, >0:mipmaps
	bool mipmaps, // true: assign mipmaps
	const endian_func_t *endian, // endian functions to read data
	ccp fname // object name, assigned
)
{
	DASSERT (img);
	DASSERT (data);
	noPRINT ("ASSIGN-IMG: idx=%d mm=%d siz=%x %s\n", img_index, mipmaps, data_size, fname);

	if (init_img > 0)
		InitializeIMG (img);
	else if (!init_img)
		ResetIMG (img);

	if (data_size >= 4 && !memcmp (data, "TXTR", 4))
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeDSB_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported DSB texture: %s\n", fname);
		AssignDecodedRGBA (img, rgba, width, height, &le_func, fname);
		return PatchListIMG (img);
	}

	if (data_size >= 0x20 && !memcmp (data, "GCIX", 4) && !memcmp (data + 0x10, "GVRT", 4))
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeGVR_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported GVR texture: %s\n", fname);
		AssignDecodedRGBA (img, rgba, width, height, &be_func, fname);
		return PatchListIMG (img);
	}

	if (data_size >= 4 && !memcmp (data, "BNTX", 4))
	{
		// Switch texture container: decode its first texture. Multi-texture
		// containers are listed by `wszst BNTX`.
		bntx_t bntx;
		if (ScanBNTX (&bntx, data, data_size))
			return ERROR0 (ERR_INVALID_IFORM, "Invalid BNTX container: %s\n", fname);
		u8 *rgba = 0;
		uint w = 0, h = 0;
		const enumError berr = DecodeBNTX_RGBA (&rgba, &w, &h, &bntx, 0);
		ResetBNTX (&bntx);
		if (berr)
			return berr;
		AssignDecodedRGBA (img, rgba, w, h, &le_func, fname);
		return PatchListIMG (img);
	}

	if (data_size >= SMDH_SIZE && !memcmp (data, "SMDH", 4))
	{
		// 3DS icon/title metadata: decode the large (48x48) icon, the one
		// actually shown as the application icon. The small (24x24) icon and
		// the 16 per-language titles are available via `wszst DUMP`/extract,
		// not through this single-image decode path.
		smdh_t smdh;
		if (ScanSMDH (&smdh, data, data_size))
			return ERROR0 (ERR_INVALID_IFORM, "Invalid SMDH file: %s\n", fname);
		u8 *rgba = 0;
		uint w = 0, h = 0;
		const enumError serr = DecodeSMDHIcon_RGBA (&rgba, &w, &h, &smdh, true);
		ResetSMDH (&smdh);
		if (serr)
			return serr;
		AssignDecodedRGBA (img, rgba, w, h, &le_func, fname);
		return PatchListIMG (img);
	}

	if (data_size >= 4 && !memcmp (data, "Gfx2", 4))
	{
		// Wii U GX2 texture container: decode its first texture. Multi-
		// texture containers (rare for standalone .gtx; common for .gsh
		// shader files, which have none) are not separately listed yet.
		gtx_t gtx;
		if (ScanGTX (&gtx, data, data_size))
			return ERROR0 (ERR_INVALID_IFORM, "Invalid GTX container: %s\n", fname);
		u8 *rgba = 0;
		uint w = 0, h = 0;
		const enumError gerr = DecodeGTX_RGBA (&rgba, &w, &h, &gtx, 0);
		ResetGTX (&gtx);
		if (gerr)
			return gerr;
		AssignDecodedRGBA (img, rgba, w, h, &le_func, fname);
		return PatchListIMG (img);
	}

	if (data_size >= 4 && !memcmp (data, "AJPG", 4))
	{
		// ODH / "AJPG": ActImagine's baseline-JPEG-derived still image format
		// (GBA, and the Wii Message Board's photo attachments).
		u8 *rgba = 0;
		int width = 0, height = 0;
		if (!AjpgDecodeRGBA (data, data_size, &rgba, &width, &height))
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported AJPG image: %s\n", fname);
		// AjpgDecodeRGBA allocates with plain malloc(); hand the pixels to a
		// dclib-allocated buffer so the rest of the image pipeline can FREE()
		// them like any other decoded image.
		u8 *owned = MALLOC ((size_t)width * height * 4);
		memcpy (owned, rgba, (size_t)width * height * 4);
		AjpgFree (rgba);
		AssignDecodedRGBA (img, owned, width, height, &be_func, fname);
		return PatchListIMG (img);
	}

	if (data_size >= 4 && (!memcmp (data, "BNR1", 4) || !memcmp (data, "BNR2", 4)))
	{
		u8 *rgba = 0;
		const enumError err = DecodeBNR_RGBA (&rgba, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid Wii banner: %s\n", fname);
		img->data = rgba;
		img->data_alloced = true;
		img->data_size = 96 * 32 * 4;
		img->width = img->xwidth = 96;
		img->height = img->xheight = 32;
		img->iform = img->info_iform = IMG_X_RGB;
		img->info_fform = FF_UNKNOWN;
		img->info_n_image = 1;
		img->alpha_status = 0;
		img->endian = &be_func;
		img->path = fname;
		img->seq_num = ++image_seq_num;
		return PatchListIMG (img);
	}

	if (data_size >= 4 && !memcmp (data, "RGCN", 4))
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeNCGR_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid NCGR graphics: %s\n", fname);

		// Nitro resources are normally distributed as matching foo.ncgr and
		// foo.nclr files.  Colour the sheet automatically when that companion is
		// available, but keep the indexed grayscale diagnostic view when it is not.
		const ccp dot = strrchr (fname, '.');
		if (dot && dot != fname)
		{
			const int plen = (int)(dot - fname);
			static const ccp nclr_exts[] = { ".nclr", ".NCLR", ".nclr.p", ".NCLR.P", ".nclr.P",
				".NCLR.p", ".NCLR.bin", ".nclr.bin", ".NCLR.P.bin", ".nclr.lz", ".NCLR.lz" };
			u8 *nclr_data = 0, *palette = 0;
			size_t nclr_size = 0;
			for (uint e = 0; e < sizeof (nclr_exts) / sizeof (*nclr_exts); e++)
			{
				char nclr_path[PATH_MAX];
				snprintf (nclr_path, sizeof (nclr_path), "%.*s%s", plen, fname, nclr_exts[e]);
				if (!LoadFileAlloc (nclr_path, 0, 0, &nclr_data, &nclr_size, 0, 2, 0, 0)
					&& nclr_size >= 4)
					break;
				FREE (nclr_data);
				nclr_data = 0;
				nclr_size = 0;
			}
			if (nclr_data && nclr_size >= 4)
			{
				if ((nclr_data[0] == 0x10 || nclr_data[0] == 0x11) && nclr_size >= 4)
				{
					u8 *dec = 0;
					uint dec_sz = 0;
					if (DecodeLZ10LZ11 (&dec, &dec_sz, nclr_data, (uint)nclr_size) == ERR_OK && dec)
					{
						FREE (nclr_data);
						nclr_data = dec;
						nclr_size = dec_sz;
					}
				}
				uint pal_w = 0, pal_h = 0;
				const enumError pal_err
					= DecodeNCLR_RGBA (&palette, &pal_w, &pal_h, nclr_data, (uint)nclr_size);
				if (!pal_err && palette)
				{
					const uint depth = data_size >= 0x20 ? le_func.rd32 (data + 0x10 + 0x0c) : 3;
					const bool is_4bpp = (depth == 3);
					const uint n_entries = pal_w / 8 * (pal_h / 8);
					for (uint i = 0; i < width * height; i++)
						if (rgba[4 * i + 3])
						{
							const uint index = is_4bpp ? rgba[4 * i] / 17 : rgba[4 * i];
							if (index < n_entries)
							{
								const u8 *p
									= palette + 4 * ((index / 16 * 8) * pal_w + index % 16 * 8);
								rgba[4 * i] = p[0];
								rgba[4 * i + 1] = p[1];
								rgba[4 * i + 2] = p[2];
							}
						}
				}
				FREE (palette);
				FREE (nclr_data);
			}
		}
		img->data = rgba;
		img->data_alloced = true;
		img->data_size = width * height * 4;
		img->width = img->xwidth = width;
		img->height = img->xheight = height;
		img->iform = img->info_iform = IMG_X_RGB;
		img->info_fform = FF_UNKNOWN;
		img->info_n_image = 1;
		img->alpha_status = 0;
		img->endian = &le_func;
		img->path = fname;
		img->seq_num = ++image_seq_num;
		return PatchListIMG (img);
	}

	if (data_size >= 4 && !memcmp (data, "RLCN", 4))
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeNCLR_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid NCLR palette: %s\n", fname);
		img->data = rgba;
		img->data_alloced = true;
		img->data_size = width * height * 4;
		img->width = img->xwidth = width;
		img->height = img->xheight = height;
		img->iform = img->info_iform = IMG_X_RGB;
		img->info_fform = FF_UNKNOWN;
		img->info_n_image = 1;
		img->alpha_status = 0;
		img->endian = &le_func;
		img->path = fname;
		img->seq_num = ++image_seq_num;
		return PatchListIMG (img);
	}

	const nfmt_info_t nfmt = DetectNintendoFormat (data, data_size, fname);
	if (nfmt.type == NFMT_BFLIM || nfmt.type == NFMT_BCLIM)
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeFLIM_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported %s texture: %s\n",
				GetNintendoFormatName (nfmt.type), fname);
		AssignDecodedRGBA (img, rgba, width, height, nfmt.big_endian ? &be_func : &le_func, fname);
		return PatchListIMG (img);
	}

	if (nfmt.type == NFMT_NUTEXB)
	{
		// Switch texture wrapper (Smash Ultimate etc.) -- see
		// DecodeNUTEXB_RGBA in lib-nintendo.c. Only array slice 0 / mip 0 is
		// decoded, matching the single-texture scope used above for BNTX.
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeNUTEXB_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported NUTEXB texture: %s\n", fname);
		AssignDecodedRGBA (img, rgba, width, height, &le_func, fname);
		return PatchListIMG (img);
	}

	if (nfmt.type == NFMT_CTPK)
	{
		nintendo_ctpk_t ctpk;
		enumError err = ScanCTPK (&ctpk, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported CTPK container: %s\n", fname);
		if (!ctpk.n_entries)
			return ERROR0 (ERR_INVALID_IFORM, "Empty CTPK container: %s\n", fname);
		nintendo_ctpk_entry_t entry;
		err = GetCTPKEntry (&ctpk, 0, &entry);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Failed reading CTPK texture: %s\n", fname);
		u8 *rgba = 0;
		uint width = 0, height = 0;
		err = DecodeCTPKTexture_RGBA (&rgba, &width, &height, &entry);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Failed decoding CTPK texture: %s\n", fname);
		const uint xwidth = EXPAND8 (width), xheight = EXPAND8 (height);
		u8 *padded = xwidth == width && xheight == height ? rgba : CALLOC (1, xwidth * xheight * 4);
		if (padded != rgba)
		{
			for (uint y = 0; y < height; y++)
				memcpy (padded + y * xwidth * 4, rgba + y * width * 4, width * 4);
			FREE (rgba);
		}
		img->data = padded;
		img->data_alloced = true;
		img->data_size = xwidth * xheight * 4;
		img->width = width;
		img->xwidth = xwidth;
		img->height = height;
		img->xheight = xheight;
		img->iform = img->info_iform = IMG_X_RGB;
		img->info_fform = FF_UNKNOWN;
		img->info_n_image = ctpk.n_entries;
		img->alpha_status = 0;
		img->endian = &le_func;
		img->path = fname;
		img->seq_num = ++image_seq_num;
		return PatchListIMG (img);
	}

	if (data_size >= 0x18 && !memcmp (data, "ctxb", 4))
	{
		// Grezzo 3DS Texture Container (.ctxb)
		const u32 chunk_count = rd_le32 (data + 8);
		const u32 chunk_offset = rd_le32 (data + 16);
		const u32 tex_data_offset = rd_le32 (data + 20);

		if (chunk_offset >= data_size || tex_data_offset >= data_size)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid CTXB container: %s\n", fname);

		// Read first chunk ("tex ") and its first texture
		uint cur_chunk_off = chunk_offset;
		bool found_tex = false;
		u8 *rgba = 0;
		uint width = 0, height = 0;

		for (uint c = 0; c < chunk_count && cur_chunk_off + 12 <= data_size; c++)
		{
			if (memcmp (data + cur_chunk_off, "tex ", 4))
				break;
			const u32 sec_size = rd_le32 (data + cur_chunk_off + 4);
			const u32 tex_count = rd_le32 (data + cur_chunk_off + 8);

			if (tex_count > 0 && cur_chunk_off + 12 + 36 <= data_size)
			{
				const u8 *tentry = data + cur_chunk_off + 12;
				const u32 img_size = rd_le32 (tentry);
				width = (uint)rd_le16 (tentry + 8);
				height = (uint)rd_le16 (tentry + 10);
				const u32 ctxb_fmt = rd_le32 (tentry + 12);
				const u32 data_rel_off = rd_le32 (tentry + 16);

				// Map CTXB texture format to CTR PICA format
				uint pica_fmt = 0;
				switch (ctxb_fmt)
				{
					case 0x14016756: pica_fmt = 8; break;  // A8
					case 0x0000675A: pica_fmt = 12; break; // ETC1
					case 0x0000675B: pica_fmt = 13; break; // ETC1A4
					case 0x67616757: pica_fmt = 10; break; // L4
					case 0x14016757: pica_fmt = 7; break;  // L8
					case 0x14016758: pica_fmt = 5; break;  // LA8
					case 0x83636754: pica_fmt = 3; break;  // RGB565
					case 0x80336752: pica_fmt = 4; break;  // RGBA4444
					case 0x80346752: pica_fmt = 2; break;  // RGBA5551
					case 0x14016752: pica_fmt = 0; break;  // RGBA8
					case 0x14016754: pica_fmt = 1; break;  // RGB8
					default: pica_fmt = 0; break;
				}

				const u32 tex_start = tex_data_offset + data_rel_off;
				if (tex_start < data_size)
				{
					const uint avail = data_size - tex_start;
					const uint use_size = img_size <= avail ? img_size : avail;
					enumError derr = DecodePicaTexture (&rgba, &width, &height,
						data + tex_start, width, height, pica_fmt, use_size);
					if (!derr && rgba)
					{
						found_tex = true;
						break;
					}
				}
			}
			cur_chunk_off += 12 + sec_size;
		}

		if (!found_tex || !rgba)
			return ERROR0 (ERR_INVALID_IFORM, "Failed decoding CTXB texture: %s\n", fname);

		const uint xwidth = EXPAND8 (width), xheight = EXPAND8 (height);
		u8 *padded = xwidth == width && xheight == height ? rgba : CALLOC (1, xwidth * xheight * 4);
		if (padded != rgba)
		{
			for (uint y = 0; y < height; y++)
				memcpy (padded + y * xwidth * 4, rgba + y * width * 4, width * 4);
			FREE (rgba);
		}
		img->data = padded;
		img->data_alloced = true;
		img->data_size = xwidth * xheight * 4;
		img->width = width;
		img->xwidth = xwidth;
		img->height = height;
		img->xheight = xheight;
		img->iform = img->info_iform = IMG_X_RGB;
		img->info_fform = FF_CTXB;
		img->info_n_image = 1;
		img->alpha_status = 0;
		img->endian = &le_func;
		img->path = fname;
		img->seq_num = ++image_seq_num;
		return PatchListIMG (img);
	}

	if (IsNUT (data, data_size))
	{
		nut_t nut;
		if (ScanNUT (&nut, data, data_size) == ERR_OK && nut.n_textures > 0)
		{
			uint tex_idx = img_index < nut.n_textures ? img_index : 0;
			u8 *rgba = 0;
			u32 width = 0, height = 0;
			if (DecodeNUTTextureToRGBA (&nut.textures[tex_idx], &rgba, &width, &height) && rgba && width && height)
			{
				const uint xwidth = EXPAND8 (width), xheight = EXPAND8 (height);
				u8 *padded = (xwidth == width && xheight == height) ? rgba : CALLOC (1, xwidth * xheight * 4);
				if (padded != rgba)
				{
					for (uint y = 0; y < height; y++)
						memcpy (padded + y * xwidth * 4, rgba + y * width * 4, width * 4);
					FREE (rgba);
				}
				img->data = padded;
				img->data_alloced = true;
				img->data_size = xwidth * xheight * 4;
				img->width = width;
				img->xwidth = xwidth;
				img->height = height;
				img->xheight = xheight;
				img->iform = img->info_iform = IMG_X_RGB;
				img->info_fform = FF_NUT;
				img->info_n_image = nut.n_textures;
				img->alpha_status = 0;
				img->endian = nut.is_big_endian ? &be_func : &le_func;
				img->path = fname;
				img->seq_num = ++image_seq_num;
				ResetNUT (&nut);
				return PatchListIMG (img);
			}
			ResetNUT (&nut);
		}
	}

	if (nfmt.type == NFMT_NSBTX || (data_size >= 4 && !memcmp (data, "BTX0", 4)))
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeNSBTX_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid NSBTX texture archive: %s\n", fname);
		AssignDecodedRGBA (img, rgba, width, height, &le_func, fname);
		return PatchListIMG (img);
	}

	if (nfmt.type == NFMT_NFTR || nfmt.type == NFMT_BNFR
		|| (data_size >= 4 && (!memcmp (data, "RTNF", 4) || !memcmp (data, "FNTR", 4)
			|| !memcmp (data, "RNFB", 4) || !memcmp (data, "BNFR", 4))))
	{
		u8 *atlas = 0;
		uint width = 0, height = 0;
		char *xml = 0;
		const enumError err = DecodeNFTR_Atlas (&atlas, &width, &height, &xml, data, data_size);
		if (xml) FREE (xml);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid Nitro font resource: %s\n", fname);
		AssignDecodedRGBA (img, atlas, width, height, &le_func, fname);
		return PatchListIMG (img);
	}

	if (data_size >= 4 && !memcmp (data, "5TX0", 4))
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = Decode5TX_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid 5TX image: %s\n", fname);
		AssignDecodedRGBA (img, rgba, width, height, &le_func, fname);
		return PatchListIMG (img);
	}

	if (nfmt.type == NFMT_BRFNT || nfmt.type == NFMT_BRFNA)
	{
		// TGLP sheets use the normal GX texture encodings.  Their sheet pointer
		// is file-relative, so no temporary TPL container is needed here.
		uint off = 0x10;
		const uint n_sections = data_size >= 0x10 ? be16 (data + 0x0e) : 0;
		const u8 *tglp = 0;
		uint tglp_size = 0;
		for (uint i = 0; i < n_sections && off <= data_size - 8; i++)
		{
			const uint sec_size = be32 (data + off + 4);
			if (sec_size < 8 || sec_size > data_size - off)
				break;
			if (!memcmp (data + off, "TGLP", 4))
			{
				tglp = data + off;
				tglp_size = sec_size;
				break;
			}
			off += sec_size;
		}
		if (!tglp || data_size < 0x20 || tglp > data + data_size - 0x20)
			return ERROR0 (ERR_INVALID_IFORM, "No valid TGLP sheet in %s: %s\n",
				GetNintendoFormatName (nfmt.type), fname);
		const uint sheet_size = be32 (tglp + 0x0c), declared_sheets = be16 (tglp + 0x10);
		// The low byte is the real GX format id (0-14). Bit 15 of this same word
		// (checked separately, not folded into the masked format id) marks a
		// per-sheet compressed encoding used by every real archived-font (.brfna)
		// sample found -- see [[brfna-compress]] above for the codec itself.
		const uint sheet_format = be16 (tglp + 0x12);
		const bool sheet_compressed = (sheet_format & 0x8000) != 0;
		const uint iform = sheet_format & 0xFF, width = be16 (tglp + 0x18),
				   height = be16 (tglp + 0x1a);
		const uint data_off = be32 (tglp + 0x1c);
		const ImageGeometry_t *geo = GetImageGeometry (iform);
		if (!geo || !sheet_size || !declared_sheets || !width || !height || data_off > data_size)
			return ERROR0 (ERR_INVALID_IFORM, "Unsupported or invalid TGLP texture in %s\n", fname);
		const u8 *tglp_end = tglp + tglp_size;
		const u8 *img_start = data + data_off;
		uint n_sheets;
		const u8 *sheet_src
			= 0; // uncompressed: raw sheet pointer. compressed: unused (walked below).
		uint sheet_src_size = 0; // compressed: this sheet's compressed chunk size.

		if (sheet_compressed)
		{
			// Each sheet is a separate 4-byte-BE-size-prefixed compressed chunk,
			// chained back-to-back starting at data_off -- walk them to find how
			// many are really present (real files, e.g. RVL_SDK wbf1.brfna,
			// declare a sheetCount the block doesn't have room for -- see the
			// brfna_archived_font_format memory) and locate img_index's chunk.
			const u8 *p = img_start;
			uint count = 0;
			while (p + 4 <= tglp_end && p + 4 <= data + data_size && count < declared_sheets)
			{
				const uint csize = be32 (p);
				if (!csize || (u64)(p + 4 - data) + csize > (u64)(tglp_end - data))
					break;
				if (count == img_index)
				{
					sheet_src = p + 4;
					sheet_src_size = csize;
				}
				p += 4 + csize;
				count++;
			}
			n_sheets = count;
			if (!n_sheets || img_index >= n_sheets || !sheet_src)
				return ERROR0 (
					ERR_INVALID_IFORM, "Unsupported or invalid TGLP texture in %s\n", fname);
		}
		else
		{
			// Real retail multi-sheet CJK .brfna samples (RVL_SDK fonts_chn/fonts_kor
			// wbf1/wbf2 pairs) declare a sheet count that this file's own TGLP block
			// doesn't have room for -- the declared count appears to describe a
			// glyph-placement scheme shared across a family, not a promise that every
			// sheet is physically embedded here. Rather than reject the whole font,
			// clamp to however many sheets actually fit in the space this block's own
			// size (not just the whole file's remaining bytes -- CWDH/CMAP follow
			// immediately after) makes available, and decode only those.
			const uint avail_sheets
				= tglp_end > img_start ? (uint)(tglp_end - img_start) / sheet_size : 0;
			n_sheets = avail_sheets < declared_sheets ? avail_sheets : declared_sheets;
			if (!n_sheets || img_index >= n_sheets
				|| (u64)sheet_size * n_sheets > data_size - data_off)
				return ERROR0 (
					ERR_INVALID_IFORM, "Unsupported or invalid TGLP texture in %s\n", fname);
			sheet_src = img_start + sheet_size * img_index;
		}

		img->width = width;
		img->height = height;
		img->xwidth = ALIGN32 (width, geo->block_width);
		img->xheight = ALIGN32 (height, geo->block_height);
		img->alpha_status = geo->has_alpha ? 0 : -1;
		img->data_size = img->xwidth * img->xheight * geo->bits_per_pixel / 8;
		if (img->data_size > sheet_size)
			return ERROR0 (ERR_INVALID_IFORM, "Truncated TGLP texture in %s\n", fname);

		if (sheet_compressed)
		{
			u8 *decoded = MALLOC (sheet_size);
			if (!decoded || !DecompressBRFNASheet (sheet_src, sheet_src_size, decoded, sheet_size))
			{
				FREE (decoded);
				return ERROR0 (
					ERR_INVALID_IFORM, "Unsupported or invalid TGLP texture in %s\n", fname);
			}
			img->data = decoded;
			img->data_alloced = true;
		}
		else
		{
			img->data = (u8 *)sheet_src;
			img->data_alloced = false;
		}
		img->info_size = sheet_size;
		img->iform = img->info_iform = iform;
		img->info_fform = FF_UNKNOWN;
		img->info_n_image = n_sheets;
		img->pal = 0;
		img->pal_size = 0;
		img->pal_alloced = false;
		img->n_pal = 0;
		img->pform = img->info_pform = PAL_INVALID;
		img->endian = &be_func;
		img->path = fname;
		img->seq_num = ++image_seq_num;
		if (mipmaps && ++img_index < n_sheets)
		{
			img->mipmap = MALLOC (sizeof (*img->mipmap));
			if (img->mipmap)
				AssignIMG (img->mipmap, true, data, data_size, img_index, mipmaps, endian, fname);
		}
		return PatchListIMG (img);
	}

	if (nfmt.type == NFMT_BCFNT)
	{
		// BCFNT (3DS, magic "CFNT") and BFFNT (Wii U, magic "FFNT") share the same
		// FINF/TGLP/CWDH/CMAP container layout. Endianness is given by the BOM at +4
		// (FFFE = little-endian / 3DS; FEFF = big-endian / Wii U). TGLP.sheetFormat
		// (at offset 0x12 inside the TGLP section) uses the CTR/Cafe GPU format table
		// -- a different numbering from Wii GX. See PLAN.md and the long comment above
		// extract_cfnt_manifest() in wszst.c for the full story.
		//
		// Format 0 (RGBA8): stored linearly (4 bytes/pixel, no tile swizzle) by this
		// fork's own encoder. Decoded by copying into an IMG_X_RGB slab.
		// Formats 3/5/7/9/10 (RGB565/IA8/I8/IA4/I4): translated to the nearest Wii GX
		// iform and run through the standard GX tile decoder. Results are pixel-perfect
		// for files written with linear pixel data; real retail BCFNT/BFFNT sheets that
		// use CTR Morton-order or Cafe micro-tile swizzle will decode with garbled tile
		// order (the bit-depth and channel layout are still correct).
		if (data_size < 0x14)
			return ERROR0 (ERR_INVALID_IFORM, "Truncated BCFNT/BFFNT header: %s\n", fname);
		const bool bcfnt_be = data[4] == 0xFE && data[5] == 0xFF;
#define BCF16(p) (bcfnt_be ? be16 (p) : le16 (p))
#define BCF32(p) (bcfnt_be ? be32 (p) : le32 (p))
		const uint bcfnt_hdr = BCF16 (data + 6);
		if (bcfnt_hdr < 0x14 || (size_t)bcfnt_hdr + 0x14 > data_size
			|| memcmp (data + bcfnt_hdr, "FINF", 4))
			return ERROR0 (ERR_INVALID_IFORM, "No valid FINF in BCFNT/BFFNT: %s\n", fname);
		const uint finf_len = BCF32 (data + bcfnt_hdr + 4);
		if ((finf_len < 0x1C) || bcfnt_hdr + finf_len > data_size)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid FINF in BCFNT/BFFNT: %s\n", fname);
		const uint ptr_glyph = BCF32 (data + bcfnt_hdr + 0x14);
		if (ptr_glyph < 8 || ptr_glyph - 8 + 0x20 > data_size
			|| memcmp (data + ptr_glyph - 8, "TGLP", 4))
			return ERR_NOTHING_TO_DO; // Outline, scalable, or glyph-only font without raster TGLP sheets
		const u8 *btglp = data + (ptr_glyph - 8);
		const uint bsheet_sz = BCF32 (btglp + 0x0C);
		const uint bsheet_cnt = BCF16 (btglp + 0x10);
		const uint bctr_fmt = BCF16 (btglp + 0x12) & 0xFF;
		const uint bwidth = BCF16 (btglp + 0x18);
		const uint bheight = BCF16 (btglp + 0x1A);
		const uint bdata_off = BCF32 (btglp + 0x1C);
		if (!bsheet_sz || !bsheet_cnt || !bwidth || !bheight || bdata_off >= data_size
			|| (uint64_t)bsheet_sz * bsheet_cnt > data_size - bdata_off)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid TGLP geometry in BCFNT/BFFNT: %s\n", fname);
		if (img_index >= bsheet_cnt)
			return ERROR0 (ERR_INVALID_IFORM, "Sheet index %u >= sheet count %u in %s\n", img_index,
				bsheet_cnt, fname);
		const u8 *bsheet = data + bdata_off + (size_t)bsheet_sz * img_index;

		if (bctr_fmt == 0)
		{
			// RGBA8 linear: the encoder stores 4 bytes/pixel row-major without any
			// hardware tile swizzle, so we copy directly into an xwidth-strided slab.
			if (bsheet_sz < bwidth * bheight * 4u)
				return ERROR0 (
					ERR_INVALID_IFORM, "Truncated RGBA8 sheet in BCFNT/BFFNT: %s\n", fname);
			const uint bxw = EXPAND8 (bwidth), bxh = EXPAND8 (bheight);
			u8 *padded = CALLOC (1, bxw * bxh * 4);
			if (!padded)
				return ERROR0 (ERR_OUT_OF_MEMORY, "Out of memory: BCFNT/BFFNT RGBA8: %s\n", fname);
			for (uint y = 0; y < bheight; y++)
				memcpy (padded + y * bxw * 4, bsheet + y * bwidth * 4, bwidth * 4);
			img->data = padded;
			img->data_alloced = true;
			img->data_size = bxw * bxh * 4;
			img->width = bwidth;
			img->xwidth = bxw;
			img->height = bheight;
			img->xheight = bxh;
			img->iform = img->info_iform = IMG_X_RGB;
			img->alpha_status = 0;
		}
		else
		{
			// Map CTR format id → Wii GX image_format_t. -1 = no equivalent.
			// CTR: 3=RGB565 5=IA8/LA8 7=I8/L8 9=IA4/LA4 10=I4/L4
			static const int8_t ctr_to_gx[14] = {
				/* 0 RGBA8    */ IMG_RGBA32, // linear; handled above, listed for completeness
				/* 1 RGB8     */ -1,
				/* 2 RGBA5551 */ -1, // RGBA5551 ≠ GX RGB5A3 (different alpha encoding)
				/* 3 RGB565   */ IMG_RGB565,
				/* 4 RGBA4444 */ -1,
				/* 5 IA8/LA8  */ IMG_IA8,
				/* 6 HL8      */ -1,
				/* 7 I8/L8    */ IMG_I8,
				/* 8 A8       */ -1,
				/* 9 IA4/LA4  */ IMG_IA4,
				/*10 I4/L4    */ IMG_I4,
				/*11 A4       */ -1,
				/*12 ETC1     */ -1,
				/*13 ETC1A4   */ -1,
			};
			const int gx_iform = (bctr_fmt < 14) ? ctr_to_gx[bctr_fmt] : -1;
			if (gx_iform < 0)
				return ERROR0 (ERR_INVALID_IFORM, "BCFNT/BFFNT: unsupported sheet format %u: %s\n",
					bctr_fmt, fname);
			const ImageGeometry_t *geo = GetImageGeometry ((image_format_t)gx_iform);
			if (!geo)
				return ERROR0 (ERR_INVALID_IFORM,
					"BCFNT/BFFNT: no geometry for GX iform %d (CTR %u): %s\n", gx_iform, bctr_fmt,
					fname);
			img->data = (u8 *)bsheet;
			img->data_alloced = false;
			img->data_size = bsheet_sz;
			img->width = bwidth;
			img->xwidth = ALIGN32 (bwidth, geo->block_width);
			img->height = bheight;
			img->xheight = ALIGN32 (bheight, geo->block_height);
			img->iform = img->info_iform = (image_format_t)gx_iform;
			img->alpha_status = geo->has_alpha ? 0 : -1;
		}
#undef BCF16
#undef BCF32
		img->pal = 0;
		img->pal_size = 0;
		img->pal_alloced = false;
		img->n_pal = 0;
		img->pform = img->info_pform = PAL_INVALID;
		img->info_fform = FF_UNKNOWN;
		img->info_n_image = bsheet_cnt;
		img->endian = bcfnt_be ? &be_func : &le_func;
		img->path = fname;
		img->seq_num = ++image_seq_num;
		if (mipmaps && ++img_index < bsheet_cnt)
		{
			img->mipmap = MALLOC (sizeof (*img->mipmap));
			if (img->mipmap)
				AssignIMG (img->mipmap, true, data, data_size, img_index, mipmaps, endian, fname);
		}
		return PatchListIMG (img);
	}

	// [[analyse-magic]]
	const file_format_t fform = GetByMagicFF (data, data_size, data_size);

	image_format_t iform = IMG_INVALID;
	palette_format_t pform = PAL_INVALID;
	uint width = 0, height = 0, n_pal = 0, psize = 0, n_img = 0;
	const u8 *idata = 0, *pdata = 0;
	bool calc_geo = false; // true: calculate geometry for mipmaps

	switch (fform)
	{
			// [[tpl-ex+]]
			// case FF_CUPICON: never defined by magic
		case FF_TPL:
		case FF_TPLX:
		{
			const tpl_header_t *tpl;
			const tpl_pal_header_t *tp;
			const tpl_img_header_t *ti;

			if (SetupPointerTPL (
					data, data_size, img_index, &tpl, 0, &tp, &ti, &pdata, &idata, &be_func))
			{
				iform = be32 (&ti->iform);
				width = be16 (&ti->width);
				height = be16 (&ti->height);
				n_img = be32 (&tpl->n_image);

				// [[tpl-ex+]]
				if (fform == FF_TPLX)
				{
					tpl_header_ex_t *tplx = (tpl_header_ex_t *)tpl;
					width = be32 (&tplx->ex_width);
					height = be32 (&tplx->ex_height);
				}

				if (tp)
				{
					pform = be32 (&tp->pform);
					n_pal = be16 (&tp->n_entry);
					psize = pdata < idata ? idata - pdata : data + data_size - pdata;
					noPRINT ("PALETTE: %u*%02x[%s], off = 0x%zx, size = 0x%x, n= %u\n", n_pal,
						pform, GetImageFormatName (pform, "?"), pdata - data, psize, n_pal);
				}
			}
		}
		break;

		case FF_BTI:
		{
			const bti_header_t *bti = (bti_header_t *)data;
			iform = bti->iform;
			idata = data + be32 (&bti->data_off);
			width = be16 (&bti->width);
			height = be16 (&bti->height);
			n_img = bti->n_image;
			calc_geo = true;

			const u32 pal_off = be32 (&bti->pal_off);
			if (pal_off && pal_off < data_size)
			{
				pform = be16 (&bti->pform);
				pdata = data + pal_off;
				psize = pdata < idata ? idata - pdata : data_size - pal_off;
			}
		}
		break;

		case FF_TEX:
		case FF_TEX_CT: // ??? [[CTCODE]] add ctcode info
		{
			const brsub_header_t *bh = (brsub_header_t *)data;
			const uint n_grp = GetSectionNumBRSUB (data, data_size, endian);
			const tex_info_t *ti = (tex_info_t *)(bh->grp_offset + n_grp);
			uint grp_off = endian->rd32 (&bh->grp_offset);

			if (grp_off < data_size)
			{
				iform = endian->rd32 (&ti->iform);
				width = endian->rd16 (&ti->width);
				height = endian->rd16 (&ti->height);
				n_img = endian->rd32 (&ti->n_image);
				calc_geo = true;
				idata = (u8 *)data + grp_off;
			}
		}
		break;

		case FF_BREFT_IMG:
			if (data_size > sizeof (breft_image_t))
			{
				const breft_image_t *bi = (breft_image_t *)data;
				iform = bi->iform;
				width = be16 (&bi->width);
				height = be16 (&bi->height);
				idata = (u8 *)data + sizeof (*bi);
				n_img = bi->n_mipmap + 1;
				calc_geo = true;

				// REFT keeps an indexed image's palette inline, immediately after
				// the complete image+mipmap payload. BrawlCrate's REFTImageHeader is
				// the authoritative 0x20-byte layout; the old placeholder fields hid
				// pform/colorCount/paletteSize and made every CI4/CI8 effect fail.
				const uint image_size = be32 (&bi->img_size);
				const uint palette_size = be32 (&bi->pal_size);
				const uint palette_count = be16 (&bi->n_pal);
				const size_t palette_off = sizeof (*bi) + (size_t)image_size;
				if (palette_count && bi->pform <= PAL_RGB5A3 && palette_size >= palette_count * 2
					&& palette_off <= data_size && palette_size <= data_size - palette_off)
				{
					pform = bi->pform;
					n_pal = palette_count;
					psize = palette_size;
					pdata = data + palette_off;
				}
			}
			break;

		case FF_PLT0:
			// PLT0 is palette-only – call dedicated loader and return directly.
			{
				enumError perr = LoadPLT0 (img, data, data_size);
				if (perr)
					return perr;
				img->info_fform = FF_PLT0;
				img->path = fname;
				img->seq_num = ++image_seq_num;
				return PatchListIMG (img);
			}

		default:
			return opt_ignore || fform == FF_UNKNOWN
				? ERR_WARNING
				: ERROR0 (ERR_INVALID_IFORM, "No (supported) image file [file type=%s]: %s\n",
					  GetNameFF (0, fform), fname);
	}

	const ImageGeometry_t *geo = GetImageGeometry (iform);
	if (geo && calc_geo)
	{
		noPRINT_IF (
			img_index, "BASE IMAGE: %3u*%-3u %6zu\n", width, height, data + data_size - idata);
		uint n;
		for (n = img_index; n > 0; n--)
		{
			uint img_size;
			CalcImageGeometry (iform, width, height, 0, 0, 0, 0, &img_size);
			idata += img_size;
			width /= 2;
			height /= 2;
			noPRINT ("NEXT IMAGE: %3u*%-3u %6zu %6u\n", width, height, data + data_size - idata,
				img_size);
		}
	}

	if (!idata && data_size < 0x40)
	{
		// small => only a fragment (e.g. header) => be silent
		return ERR_INVALID_IFORM;
	}

	uint delta = idata - data;
	if (!idata || delta >= data_size)
		return ERROR0 (ERR_INVALID_IFORM, "Invalid image format [file type=%s]: %s\n",
			GetNameFF (0, fform), fname);

	img->width = width;
	img->height = height;
	img->xwidth = geo ? ALIGN32 (width, geo->block_width) : EXPAND8 (width);
	img->xheight = geo ? ALIGN32 (height, geo->block_height) : EXPAND8 (height);
	img->alpha_status = geo && !geo->has_alpha ? -1 : 0;

	// img->container	= set by caller if needed/wanted!
	img->data = (u8 *)idata;
	img->info_size = data_size - delta;
	img->data_size = geo ? img->xwidth * img->xheight * geo->bits_per_pixel / 8 : img->info_size;
	img->data_alloced = false;
	img->iform = iform;
	img->info_iform = iform;
	img->info_fform = fform;
	img->info_n_image = n_img;

	img->pal = (u8 *)pdata;
	img->pal_size = psize;
	img->pal_alloced = false;
	img->n_pal = n_pal;
	img->pform = pform;
	img->info_pform = pform;

	img->endian = endian;
	img->path = fname;
	img->seq_num = ++image_seq_num;

	noPRINT ("-> %u*%u->%u*%u [%u=0x%x]\n", img->width, img->height, img->xwidth, img->xheight,
		img->data_size, img->data_size);

	if (mipmaps && ++img_index < n_img)
	{
		DASSERT (!img->mipmap);
		img->mipmap = MALLOC (sizeof (*img->mipmap));
		AssignIMG (img->mipmap, true, data, data_size, img_index, mipmaps, endian, fname);
	}

	return PatchListIMG (img);
}

///////////////////////////////////////////////////////////////////////////////

enumError LoadIMG (Image_t *img, // pointer to valid img
	bool init_img, // true: initialize 'img'
	ccp fname, // filename of source
	uint img_index, // index of sub image, 0:main, >0:mipmaps
	bool mipmaps, // true: load and assign mipmaps
	bool allow_subfile, // allow to extract szs sub files
	bool ignore_no_file // ignore if file does not exists
						// and return warning ERR_NOT_EXISTS
)
{
	DASSERT (img);
	DASSERT (fname);
	TRACE ("LoadIMG(%p,%d,%d) fname=%s\n", img, init_img, ignore_no_file, fname);

	GenericImgParam_t genpar;
	if (CheckGenericIMG (&genpar, fname, ignore_no_file))
		return CreateGenericIMG (&genpar, img, init_img);

	if (init_img)
		InitializeIMG (img);
	else
		ResetIMG (img);

	szs_extract_t eszs;
	if (allow_subfile)
	{
		enumError err = ExtractSZS (&eszs, true, fname, 0, ignore_no_file);
		if (err)
			return err;
	}
	else
		InitializeExtractSZS (&eszs);

	if (!eszs.data)
	{
		File_t F;
		enumError err = OpenFILE (&F, true, fname, ignore_no_file, false);
		if (err || !F.f)
			return err;

		if (ignore_no_file && !S_ISREG (F.st.st_mode))
		{
			ResetFile (&F, 0);
			return ERR_WARNING;
		}

		u8 buf[0x200];
		size_t read_stat = fread (buf, 1, sizeof (buf), F.f);
		// [[analyse-magic]]
		const file_format_t fform = GetByMagicFF (buf, read_stat, 0);
		if (fform == FF_PNG)
		{
			err = ReadPNG (img, mipmaps, &F, buf, read_stat);
			img->path = F.fname;
			F.fname = 0;
			ResetFile (&F, 0);
			return err;
		}

		// use 'eszs' data structure to hold dynamic data
		eszs.data_size = F.st.st_size;
		eszs.data = MALLOC (eszs.data_size);
		eszs.data_alloced = true;

		memcpy (eszs.data, buf, read_stat);
		if (read_stat < eszs.data_size)
		{
			const uint read_len = eszs.data_size - read_stat;
			read_stat = fread (eszs.data + read_stat, 1, read_len, F.f);
			if (read_stat != read_len)
			{
				ERROR1 (ERR_READ_FAILED, "Can't read file: %s\n", fname);
				ResetFile (&F, 0);
				return ERR_READ_FAILED;
			}
		}
		ResetFile (&F, 0);
		FreeString (eszs.fname);
		eszs.fname = STRDUP (fname);
	}

	const nfmt_info_t nfmt = DetectNintendoFormat (eszs.data, eszs.data_size, fname);
	if (nfmt.type == NFMT_FZIP)
	{
		u8 *decoded = 0;
		uint decoded_size = 0;
		enumError derr = DecodeFZIP (&decoded, &decoded_size, eszs.data, eszs.data_size);
		if (derr)
		{
			ResetExtractSZS (&eszs);
			return ERROR0 (ERR_INVALID_DATA, "Invalid FZIP stream: %s\n", fname);
		}
		if (eszs.data_alloced)
			FREE (eszs.data);
		eszs.data = decoded;
		eszs.data_size = decoded_size;
		eszs.data_alloced = true;
	}
	else if (nfmt.type == NFMT_STPL)
	{
		u8 *decoded = 0;
		uint decoded_size = 0;
		enumError derr = DecodeCamelot (&decoded, &decoded_size, eszs.data, eszs.data_size);
		if (derr)
		{
			ResetExtractSZS (&eszs);
			return ERROR0 (ERR_INVALID_DATA, "Invalid Camelot STPL stream: %s\n", fname);
		}
		if (eszs.data_alloced)
			FREE (eszs.data);
		eszs.data = decoded;
		eszs.data_size = decoded_size;
		eszs.data_alloced = true;
		eszs.endian = &be_func;
	}
	else if (nfmt.type == NFMT_LZ10 || nfmt.type == NFMT_LZ11)
	{
		// WarioWare: D.I.Y. Showcase / "WarioWare Snapped!" (DSiWare) stores
		// its Nitro graphics (NCGR/NCLR/NCER/NANR) LZ11-compressed, and every
		// decompressed resource is itself wrapped in a 4-byte size-prefix
		// record before the real RGCN/RLCN/RECN/RNAN magic -- see the
		// NITRO_SIZE_PREFIX comment in DetectNintendoFormat() (lib-nintendo.c)
		// for the verified byte layout. Decompress here, then peel off that
		// wrapper if present, so AssignIMG()'s magic checks (which look for
		// RGCN/RLCN literally at offset 0) see the real resource.
		u8 *decoded = 0;
		uint decoded_size = 0;
		enumError derr = DecodeLZ10LZ11 (&decoded, &decoded_size, eszs.data, eszs.data_size);
		if (derr)
		{
			ResetExtractSZS (&eszs);
			return ERROR0 (ERR_INVALID_DATA, "Invalid LZ%s stream: %s\n",
				nfmt.type == NFMT_LZ10 ? "10" : "11", fname);
		}
		if (eszs.data_alloced)
			FREE (eszs.data);
		eszs.data = decoded;
		eszs.data_size = decoded_size;
		eszs.data_alloced = true;

		const nfmt_info_t inner = DetectNintendoFormat (eszs.data, eszs.data_size, fname);
		if (inner.payload_offset && inner.payload_offset < eszs.data_size)
		{
			u8 *stripped = MALLOC (eszs.data_size - inner.payload_offset);
			memcpy (
				stripped, eszs.data + inner.payload_offset, eszs.data_size - inner.payload_offset);
			FREE (eszs.data);
			eszs.data = stripped;
			eszs.data_size -= inner.payload_offset;
		}
	}

	enumError err = AssignIMG (
		img, -1, eszs.data, eszs.data_size, img_index, mipmaps, eszs.endian, eszs.fname);
	if (!err)
	{
		if (eszs.data_alloced)
		{
			eszs.data_alloced = false;
			img->container = eszs.data;
		}
		img->path_alloced = true;
		eszs.fname = 0;
	}

	ResetExtractSZS (&eszs);
	return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			create generic images		///////////////
///////////////////////////////////////////////////////////////////////////////

enum generic_img_cmd_t
{
	VICMD_BLANK,
	VICMD_TEXT,
	VICMD_CUP_IMAGES,
	VICMD_CUP_ICON,
	VICMD_CUP_FILE,
};

//-----------------------------------------------------------------------------

enum generic_img_options_t
{
	VIOPT_FONT = 0x0001,
	VIOPT_SIZE = 0x0002,
	VIOPT_COLOR = 0x0004,
};

//-----------------------------------------------------------------------------

static const KeywordTab_t generic_img_key[] = {
	{ VICMD_BLANK, "BLANK", 0, VIOPT_SIZE | VIOPT_COLOR },
	{ VICMD_TEXT, "TEXT", 0, VIOPT_FONT },
	{ VICMD_CUP_IMAGES, "CUP-IMAGE", "CUPIMAGE", 0 },
	{ VICMD_CUP_ICON, "CUP-ICON", "CUPICON", 0 },
	{ VICMD_CUP_FILE, "CUP-FILE", "CUPFILE", 0 },
	{ 0, 0, 0, 0 },
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

bool CheckGenericIMG // returns TRUE if command syntax is ok
	(GenericImgParam_t *par, // parameter to setup
		ccp fname, // filename to check
		bool ignore_unknown // true: ignore unknown (sub-)commands
	)
{
	DASSERT (par);
	memset (par, 0, sizeof (*par));
	par->ignore_unknown = ignore_unknown;

	if (fname && *fname == ':')
	{
		ccp eq = strchr (fname, '=');
		if (eq)
		{
			par->cmd_name.ptr = fname + 1;
			par->cmd_name.len = eq - par->cmd_name.ptr;
			par->param = MemByString0 (eq + 1);
		}
		else
		{
			par->cmd_name = MemByString0 (fname + 1);
			par->param = NullMem;
		}

		if (par->cmd_name.len && !memchr (par->cmd_name.ptr, '/', par->cmd_name.len)
			&& !memchr (par->cmd_name.ptr, '.', par->cmd_name.len))
		{
			return true;
		}
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////////

static enumError CreateGenericTextIMG (GenericImgParam_t *par, // valid parameters
	Image_t *img // pointer to valid img
)
{
	DASSERT (par);
	DASSERT (img);

	static char search[] = "ÄÖÜàáâäèéíñóôúüßτ "; // NBSP (0xa0) at end of string
	static char replace[] = "AOUaaaaeeinoouusT ";
	static u32 codelist[sizeof (replace)] = { 0 };

	if (!codelist[0])
	{
		ccp src = search;
		u32 *dest = codelist;
		for (;;)
		{
			const u32 code = ScanUTF8AnsiChar (&src);
			if (!code)
				break;
			*dest++ = code;
		}
		// HexDump16(stdout,0,0,codelist,sizeof(codelist));
	}

	BZ2Manager_t *font = par->font ? par->font : &blue_40_bin_mgr;
	DecodeBZIP2Manager (font);

	u32 *offset_list = (u32 *)(font->data + be32 (font->data));
	ccp char_list = (ccp)font->data + 4;

	ccp src = par->param.ptr;
	ccp end = src + par->param.len;
	if (logging >= 2)
		fprintf (stdlog, "# GenericText: |%.*s|\n", par->param.len, par->param.ptr);

	while (src < end)
	{
		u32 code = ScanUTF8AnsiChar (&src);
		if (code <= ' ' || code == '_')
			code = ' ';
		else if (code >= 0x80)
		{
			for (u32 *p = codelist; *p; p++)
				if (*p == code)
				{
					PRINT0 (" REPLACE |#%u| -> %zu |%c|\n", code, p - codelist,
						(uchar)replace[p - codelist]);
					code = (uchar)replace[p - codelist];
					break;
				}
			if (code >= 0x100)
				continue;
		}

		ccp found = strchr (char_list, code);
		PRINT0 (" |%c| : %zd\n", code, found ? found - char_list : -1);
		if (!found)
			continue;

		const int chidx = found - char_list;
		const u32 off = ntohl (offset_list[chidx]);
		const u8 *data = font->data + off;
		const int size = ntohl (offset_list[chidx + 1]) - off;

		if (img->width)
		{
			Image_t img2;
			AssignIMG (&img2, true, data, size, 0, false, &be_func, ":TEXT");
			PatchIMG (img, img, &img2, PIM_INS_RIGHT);
			ResetIMG (&img2);
		}
		else
			AssignIMG (img, false, data, size, 0, false, &be_func, ":TEXT");
	}

	return ERR_OK;
};

///////////////////////////////////////////////////////////////////////////////

static enumError CreateGenericCupImagesIMG (GenericImgParam_t *par, // valid parameters
	Image_t *img // pointer to valid img
)
{
	DASSERT (par);
	DASSERT (img);

	enum
	{
		I_ARROWS,
		I_ORIG,
		I_SWAPPED,
		I_WIIMM,

		I_PIXEL = 0x10, // N(pixel) = ( ( VAL & I_M_PIXEL ) >> I_S_PIXEL ) * 8 + 8
		I_M_PIXEL = 0xf0,
		I_S_PIXEL = 4,
	};

	static const KeywordTab_t keytab[] = {
		{ I_ARROWS, "ARROWS", 0, 0 },
		{ I_ORIG, "ORIGINAL", 0, 0 },
		{ I_SWAPPED, "SWAPPED", 0, 0 },
		{ I_WIIMM, "WIIMM", 0, 0 },

		{ 0 * I_PIXEL, "8PIXELS", 0, I_M_PIXEL },
		{ 1 * I_PIXEL, "16PIXELS", 0, I_M_PIXEL },
		{ 2 * I_PIXEL, "24PIXELS", 0, I_M_PIXEL },
		{ 3 * I_PIXEL, "32PIXELS", 0, I_M_PIXEL },
		{ 4 * I_PIXEL, "40PIXELS", 0, I_M_PIXEL },
		{ 5 * I_PIXEL, "48PIXELS", 0, I_M_PIXEL },
		{ 6 * I_PIXEL, "56PIXELS", 0, I_M_PIXEL },
		{ 7 * I_PIXEL, "64PIXELS", 0, I_M_PIXEL },
		{ 8 * I_PIXEL, "72PIXELS", 0, I_M_PIXEL },
		{ 9 * I_PIXEL, "80PIXELS", 0, I_M_PIXEL },
		{ 10 * I_PIXEL, "88PIXELS", 0, I_M_PIXEL },
		{ 11 * I_PIXEL, "96PIXELS", 0, I_M_PIXEL },
		{ 12 * I_PIXEL, "104PIXELS", 0, I_M_PIXEL },
		{ 13 * I_PIXEL, "112PIXELS", 0, I_M_PIXEL },
		{ 14 * I_PIXEL, "120PIXELS", 0, I_M_PIXEL },
		{ 15 * I_PIXEL, "128PIXELS", 0, I_M_PIXEL },

		{ 0, 0, 0, 0 },
	};

	mem_t parlist = par->param;
	while (parlist.len)
	{
		ccp comma = memchr (parlist.ptr, ',', parlist.len);
		ccp plus = memchr (parlist.ptr, '+', parlist.len);
		if (!comma || plus && plus < comma)
			comma = plus;
		mem_t text = BeforeMem (parlist, comma);
		parlist = BehindMem (parlist, comma ? comma + 1 : 0);

		const KeywordTab_t *cmd = ScanKeywordEx (0, text.ptr, text.len, LOUP_UPPER, keytab);
		if (!cmd)
		{
			if (!par->ignore_unknown)
				ERROR0 (ERR_NOT_EXISTS, "Unknown image name: %.*s\n", text.len, text.ptr);
			return ERR_NOT_EXISTS;
		}

		if (logging >= 1)
			fprintf (stdlog, "# GenericCupImage: %s\n", cmd->name1);

		BZ2Manager_t *bz2 = 0;
		switch (cmd->id)
		{
			case I_ARROWS:
				bz2 = &cup_arrows_tpl_mgr;
				break;
			case I_ORIG:
				bz2 = &cup_orig_tpl_mgr;
				break;
			case I_SWAPPED:
				bz2 = &cup_swapped_tpl_mgr;
				break;
			case I_WIIMM:
				bz2 = &cup_wiimm_tpl_mgr;
				break;
		}

		if (bz2)
		{
			DecodeBZIP2Manager (bz2);
			if (img->width)
			{
				Image_t img2;
				AssignIMG (&img2, true, bz2->data, bz2->size, 0, false, &be_func, ":IMAGE");
				PatchIMG (img, img, &img2, PIM_INS_BOTTOM);
				ResetIMG (&img2);
			}
			else
				AssignIMG (img, false, bz2->data, bz2->size, 0, false, &be_func, ":BOTTOM");
		}
	}
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError CreateGenericCupIconIMG (GenericImgParam_t *par, // valid parameters
	Image_t *img // pointer to valid img
)
{
	DASSERT (par);
	DASSERT (img);

	Color_t col = { .val = 0 };
	GenericImgParam_t mypar = *par;
	mem_t parlist = mypar.param;
	enumError err = ERR_OK;

	while (parlist.len)
	{
		ccp pipe = memchr (parlist.ptr, '|', parlist.len);
		ccp nl = memchr (parlist.ptr, '\n', parlist.len);
		if (!pipe || nl && nl < pipe)
			pipe = nl;
		mem_t text = BeforeMem (parlist, pipe);
		parlist = BehindMem (parlist, pipe ? pipe + 1 : 0);
		PRINT0 ("%d+%d : %.*s\n", text.len, parlist.len, text.len, text.ptr);

		if (text.len > 2 && *text.ptr == ':' && !memcmp (text.ptr + text.len - 2, "px", 2))
		{
			par->force_width = str2ul (text.ptr + 1, 0, 10);
			PRINT0 (">>>>> width %u\n", par->force_width);
			continue;
		}

		if (!memcmp (text.ptr, ":test", 5))
		{
			par->test_mode = true;
			continue;
		}

		Image_t cupicon;
		if (text.len && *text.ptr == ':')
		{
			text = MidMem (text, 1, text.len);
			if (!text.len || *text.ptr != ':')
			{
				mypar.param = text;
				InitializeIMG (&cupicon);
				CreateGenericCupImagesIMG (&mypar, &cupicon);
				goto append;
			}
		}

		if (logging >= 1)
			fprintf (stdlog, "# GenericCupIcon: |%.*s|\n", text.len, text.ptr);

		err = CreateIMG (&cupicon, true, 128, 128, col);
		if (err)
			break;

		ccp minus = memchr (text.ptr, '-', text.len);
		if (minus)
		{
			mypar.font = &red_36_bin_mgr;
			mypar.param = BeforeMem (text, minus);
			Image_t num;
			InitializeIMG (&num);
			err = CreateGenericTextIMG (&mypar, &num);
			if (err)
				break;
			if (num.width > 128)
				ResizeIMG (&num, false, 0, 128, num.height);
			PatchIMG (&cupicon, &cupicon, &num, PIM_RIGHT | PIM_TOP);
			ResetIMG (&num);

			text = BehindMem (text, minus + 1);
		}

		if (text.len)
		{
			mypar.font = &blue_40_bin_mgr;
			mypar.param = text;
			Image_t name;
			InitializeIMG (&name);
			err = CreateGenericTextIMG (&mypar, &name);
			if (err)
				break;
			if (name.width > 128)
				ResizeIMG (&name, false, 0, 128, name.height);
			PatchIMG (&cupicon, &cupicon, &name, PIM_BOTTOM);
			ResetIMG (&name);
		}

	append:;
		if (img->height)
			PatchIMG (img, img, &cupicon, PIM_INS_BOTTOM);
		else
			CopyIMG (img, false, &cupicon, false);
		ResetIMG (&cupicon);
	}

	img->is_cup_icon = true;
	img->test_mode = par->test_mode;
	return err;
}

///////////////////////////////////////////////////////////////////////////////

static enumError CreateGenericCupFileIMG (GenericImgParam_t *par, // valid parameters
	Image_t *img // pointer to valid img
)
{
	DASSERT (par);
	DASSERT (img);

	u8 *data = 0;
	size_t size;
	enumError err = LoadFileAlloc (par->param.ptr, 0, 0, &data, &size, 1000000, 0, 0, false);
	if (!err)
	{
		GenericImgParam_t mypar = *par;
		mypar.param.ptr = (ccp)data;
		mypar.param.len = size;
		err = CreateGenericCupIconIMG (&mypar, img);
	}
	if (data)
		FREE (data);
	return err;
}

///////////////////////////////////////////////////////////////////////////////

enumError CreateGenericIMG (GenericImgParam_t *par, // valid parameters
	Image_t *img, // pointer to valid img
	bool init_img // true: initialize 'img'
)
{
	DASSERT (par);
	DASSERT (img);
	if (init_img)
		InitializeIMG (img);
	else
		ResetIMG (img);

	par->cmd = ScanKeywordEx (0, par->cmd_name.ptr, par->cmd_name.len, LOUP_UPPER, generic_img_key);
	if (!par->cmd)
	{
		if (!par->ignore_unknown)
			ERROR0 (ERR_NOT_EXISTS, "Invalid keyword for virtual image: %.*s\n", par->cmd_name.len,
				par->cmd_name.ptr);
		return ERR_NOT_EXISTS;
	}

	mem_t param = par->param;
	if (par->cmd->opt & VIOPT_FONT)
	{
		ccp comma = memchr (param.ptr, ',', param.len);
		mem_t scan = BeforeMem (param, comma);
		param = BehindMem (param, comma ? comma + 1 : 0);

		if (scan.len)
			par->font = tolower (*scan.ptr) == 'r' ? &red_36_bin_mgr : &blue_40_bin_mgr;
	}

	if (par->cmd->opt & VIOPT_SIZE)
	{
		ccp comma = memchr (param.ptr, ',', param.len);
		mem_t scan = BeforeMem (param, comma);
		param = BehindMem (param, comma ? comma + 1 : 0);

		PRINT0 (" > scan |%.*s|%.*s|\n", scan.len, scan.ptr, param.len, param.ptr);
		if (scan.len)
		{
			char *next;
			par->width = str2ul (scan.ptr, &next, 10);
			par->height = *next == 'x' ? str2ul (next + 1, 0, 10) : par->width;
		}
	}

	if (par->cmd->opt & VIOPT_COLOR)
	{
		ccp comma = memchr (param.ptr, ',', param.len);
		mem_t scan = BeforeMem (param, comma);
		param = BehindMem (param, comma ? comma + 1 : 0);

		PRINT0 (" > scan |%.*s|%.*s|\n", scan.len, scan.ptr, param.len, param.ptr);
		if (scan.len)
		{
			u32 col = str2ul (scan.ptr, 0, 16);
			if (scan.len <= 6)
				col = col << 8 | 0xff;
			par->color.val = htonl (col);
		}
	}

	PRINT0 ("Virtual image found: |%.*s| = |%.*s| [%dx%d,%08x]\n", par->cmd_name.len,
		par->cmd_name.ptr, param.len, param.ptr, par->width, par->height, par->color);

	par->param = param;
	enumError err;
	switch (par->cmd->id)
	{
		case VICMD_BLANK:
			if (!par->width)
				par->width = 1;
			if (!par->height)
				par->height = 1;
			err = CreateIMG (img, false, par->width, par->height, par->color);
			break;

		case VICMD_TEXT:
			err = CreateGenericTextIMG (par, img);
			break;

		case VICMD_CUP_IMAGES:
			err = CreateGenericCupImagesIMG (par, img);
			break;

		case VICMD_CUP_ICON:
			err = CreateGenericCupIconIMG (par, img);
			break;

		case VICMD_CUP_FILE:
			err = CreateGenericCupFileIMG (par, img);
			break;

		default:
			return ERR_NOT_EXISTS;
	}

	if (!err && par->force_width != 128)
		ResizeIMG (img, false, img, par->force_width, 0);

	return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			MipmapOptions_t			///////////////
///////////////////////////////////////////////////////////////////////////////

void SetupMipmapOptions (MipmapOptions_t *mmo)
{
	DASSERT (mmo);

	// memset(mmo,0,sizeof(*mmo));
	mmo->valid = true;
	mmo->is_tpl = false;

	if (opt_n_images)
	{
		mmo->force = true;
		mmo->n_mipmap = opt_n_images - 1;
		mmo->n_image = opt_n_images;
		mmo->min_size = 1;
	}
	else
	{
		mmo->force = false;
		mmo->n_mipmap = opt_max_images - 1;
		mmo->n_image = opt_max_images;
		mmo->min_size = opt_min_mipmap_size;
	}

	PRINT ("MM-OPT/SETUP: %s\n", TextMipmapOptions (mmo));
}

///////////////////////////////////////////////////////////////////////////////

void SetupMipmapOptionsTPL (MipmapOptions_t *mmo)
{
	DASSERT (mmo);

	// memset(mmo,0,sizeof(*mmo));
	mmo->valid = true;
	mmo->force = true;
	mmo->is_tpl = true;
	mmo->n_mipmap = 0;
	mmo->n_image = 1;
	mmo->min_size = 1;

	PRINT ("MM-OPT/SETUP1: %s\n", TextMipmapOptions (mmo));
}

///////////////////////////////////////////////////////////////////////////////

void CopyMipmapOptions (MipmapOptions_t *dest, const MipmapOptions_t *src)
{
	DASSERT (dest);
	if (src)
	{
		memcpy (dest, src, sizeof (*dest));
		PRINT ("MM-OPT/COPY: %s\n", TextMipmapOptions (dest));
	}
	else
		SetupMipmapOptions (dest);
}

///////////////////////////////////////////////////////////////////////////////

void MipmapOptionsByImage (MipmapOptions_t *mmo, const Image_t *img)
{
	DASSERT (mmo);
	if (!mmo->valid)
		SetupMipmapOptions (mmo);

	if (!mmo->force && img)
	{
		int n_image = img->mipmap ? CountMipmapsIMG (img) + 1 : mmo->n_image;
		if (n_image < img->info_n_image)
			n_image = img->info_n_image;

		if (n_image > MAX_MIPMAPS)
			n_image = MAX_MIPMAPS + 1;
		else if (n_image < 1)
			n_image = opt_max_images > 0 ? opt_max_images : 1;

		mmo->n_image = n_image;
		mmo->n_mipmap = n_image - 1;
	}

	PRINT ("MM-OPT/IMG: %s\n", TextMipmapOptions (mmo));
}

///////////////////////////////////////////////////////////////////////////////

ccp InfoMipmapOptions (const MipmapOptions_t *mmo)
{
	if (!mmo)
		return "--";

	if (!mmo->valid)
		return "!!";

	char buf[20];
	int len = snprintf (
		buf, sizeof (buf), "%s%d/%d", mmo->force ? "f" : "m", mmo->n_mipmap, mmo->min_size);

	char *res = GetCircBuf (++len);
	memcpy (res, buf, len);
	return res;
}

///////////////////////////////////////////////////////////////////////////////

ccp TextMipmapOptions (const MipmapOptions_t *mmo)
{
	if (!mmo)
		return "--";

	if (!mmo->valid)
		return "INVALID!";

	char buf[100];
	int len = snprintf (buf, sizeof (buf), "force=%d, nm=%d, ni=%d, minsize=%d", mmo->force,
		mmo->n_mipmap, mmo->n_image, mmo->min_size);

	char *res = GetCircBuf (++len);
	memcpy (res, buf, len);
	return res;
}

///////////////////////////////////////////////////////////////////////////////

void PrintMipmapOptions (FILE *f, int indent, const MipmapOptions_t *mmo)
{
	DASSERT (f);
	DASSERT (mmo);

	indent = NormalizeIndent (indent);
	fprintf (f, "%*sMM-OPT: %s\n", indent, "", TextMipmapOptions (mmo));
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			mipmap helpers			///////////////
///////////////////////////////////////////////////////////////////////////////
// [[mipmap_info_t]]

typedef struct mipmap_info_t
{
	MipmapOptions_t mmo; // mipmap options
	uint n_mipmap; // number of mipmaps to write
	uint image_size; // calculated size of all images
	Image_t img; // transformed image
	const Image_t *src_img; // pointer to source image

	// tpl helpers

	uint img_head_off; // offset of image header
	uint img_data_off; // offset of image data
	uint img_data_size; // size of image data

	uint pal_head_off; // offset of palette header
	uint pal_data_off; // offset of palette header
	uint pal_data_size; // size of palette data

} mipmap_info_t;

///////////////////////////////////////////////////////////////////////////////

void ResetMMI (mipmap_info_t *mmi)
{
	DASSERT (mmi);
	ResetIMG (&mmi->img);
	memset (mmi, 0, sizeof (*mmi));
	SetupMipmapOptions (&mmi->mmo);
}

///////////////////////////////////////////////////////////////////////////////

static enumError PrepareImages (mipmap_info_t *mmi, // valid mipmap info
	const Image_t *src_img, // pointer to source image
	const MipmapOptions_t *mmo // NULL or mipmap options

	// RETURNS (if return value == ERR_OK):
	//	 mmi->n_mipmap      : number of mipmaps to store
	//   mmi->image_size    : total image size (main+mipmaps)
	//	 mmi->img.info_size : data size of main image
)
{
	DASSERT (mmi);
	DASSERT (src_img);

	memset (mmi, 0, sizeof (*mmi));
	mmi->src_img = src_img;

	CopyMipmapOptions (&mmi->mmo, mmo);
	MipmapOptionsByImage (&mmi->mmo, src_img);
	PRINT ("PrepareImages() MMO: %s\n", TextMipmapOptions (&mmi->mmo));

	//--- copy & convert images

	CopyIMG (&mmi->img, true, src_img, false);
	const bool is_tpl = mmi->mmo.is_tpl;
	if (!is_tpl)
	{
		Transform2InternIMG (&mmi->img);
		enumError err = ExecTransformIMG (&mmi->img);
		if (err)
			return err;
	}

	//--- calculate image size

	const ImageGeometry_t *geo = GetImageGeometry (mmi->img.iform);
	if (!geo || geo->is_x)
		return ERROR0 (ERR_INTERNAL, 0);

	uint size = 0;
	uint wd = mmi->img.width;
	uint ht = mmi->img.height;
	uint n_image = mmi->mmo.n_image;

	for (int ni = 0; ni < n_image; ni++)
	{
		size += CalcImageSize (
			wd, ht, geo->bits_per_pixel, geo->block_width, geo->block_height, 0, 0, 0, 0);
		if (!ni)
			mmi->img.info_size = size;
		if (!is_tpl)
		{
			wd /= 2;
			ht /= 2;
			DASSERT (opt_min_mipmap_size >= 1);
			if (wd < mmi->mmo.min_size || ht < mmi->mmo.min_size)
			{
				n_image = ni + 1;
				break;
			}
		}
	}

	mmi->n_mipmap = n_image - 1;
	mmi->image_size = size;

	PRINT ("SETUP IMAGES: N=1+%u, size=%u=0x%x, m0=%s\n", n_image - 1, size, size,
		InfoMipmapOptions (&mmi->mmo));
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError PrepareImagesTPL (mipmap_info_t *mmi, // valid mipmap info
	const Image_t *src_img // pointer to source image

	// RETURNS (if return value == ERR_OK):
	//	 mmi->n_mipmap      : number of mipmaps to store
	//   mmi->image_size    : total image size (main+mipmaps)
	//	 mmi->img.info_size : data size of main image
)
{
	DASSERT (mmi);
	DASSERT (src_img);

	MipmapOptions_t mmo;
	SetupMipmapOptionsTPL (&mmo);
	return PrepareImages (mmi, src_img, &mmo);
}

///////////////////////////////////////////////////////////////////////////////

static enumError WriteImageData (mipmap_info_t *mmi, // valid mipmap info
	u8 *data, // destination buffer
	u8 **p_dest // not NULL: store next destination
)
{
	DASSERT (mmi);
	DASSERT (data);

	const Image_t *img = &mmi->img;
	memcpy (data, img->data, img->info_size);
	if (p_dest)
		*p_dest = data + img->info_size;
	if (!mmi->n_mipmap)
		return ERR_OK;

	const ImageGeometry_t *geo = GetImageGeometry (img->iform);
	if (!geo || geo->is_x)
		return ERROR0 (ERR_INTERNAL, 0);

	Image_t temp;
	InitializeIMG (&temp);

	enumError err = ERR_OK;
	u8 *dest = data + img->info_size;
	img = mmi->src_img;
	DASSERT (img);

	uint wd = img->width;
	uint ht = img->height;
	uint ni;
	for (ni = 0; ni < mmi->n_mipmap; ni++)
	{
		if (img->mipmap)
			img = img->mipmap;

		wd /= 2;
		ht /= 2;
		DASSERT (wd && ht);
		err = ResizeIMG (&temp, false, img, wd, ht);
		if (err)
			break;
		// PRINT("RESIZE  %3u*%-3u ",wd,ht); HEXDUMP16(0,0,temp.data,16);

		err = ConvertIMG (&temp, false, 0, mmi->img.iform, mmi->img.pform);
		if (err)
			break;
		// PRINT("CONVERT %3u*%-3u ",wd,ht); HEXDUMP16(0,0,temp.data,16);

		DASSERT (dest + temp.data_size <= data + mmi->image_size);
		memcpy (dest, temp.data, temp.data_size);
		dest += temp.data_size;
	}
	if (p_dest)
		*p_dest = dest;

	ResetIMG (&temp);
	ResetIMG (&mmi->img);
	return err;
}

//
//-----------------------------------------------------------------------------

enumError SaveAJPG (Image_t *img, // valid image
	FILE *fo, // output file, if NULL then use path1+path2
	ccp path1, // NULL or part #1 of path
	ccp path2, // NULL or part #2 of path
	bool overwrite // true: force overwriting
)
{
	DASSERT (img);

	if (!path2 || !*path2)
	{
		path2 = path1;
		path1 = 0;
	}

	char pathbuf[PATH_MAX];
	ccp path = PathCatPP (pathbuf, sizeof (pathbuf), path1, path2);
	PRINT ("SaveAJPG() %s\n", path);

	Transform2XIMG (img);
	enumError err = ExecTransformIMG (img);
	if (err)
		return err;

	if (img->iform != IMG_X_RGB)
	{
		err = ConvertToRGB (img, img, PAL_AUTO);
		if (err)
			return err;
	}

	u8 *rgba_data = 0;
	bool alloced = false;
	if (img->xwidth == img->width && img->xheight == img->height)
	{
		rgba_data = img->data;
	}
	else
	{
		rgba_data = MALLOC (img->width * img->height * 4);
		alloced = true;
		u8 *dest = rgba_data;
		const u8 *src = img->data;
		for (uint y = 0; y < img->height; y++)
		{
			memcpy (dest, src, img->width * 4);
			dest += img->width * 4;
			src += img->xwidth * 4;
		}
	}

	uint8_t *out_data = 0;
	size_t out_size = 0;
	int quality = 80;
	if (!AjpgEncodeRGBA (rgba_data, img->width, img->height, quality, &out_data, &out_size))
	{
		if (alloced)
			FREE (rgba_data);
		return ERROR0 (ERR_WRITE_FAILED,
			"AJPG encode failed (requires even dimensions and max 2047x2047): %s\n", path);
	}

	if (alloced)
		FREE (rgba_data);

	File_t f;
	if (fo)
	{
		InitializeFile (&f);
		f.f = fo;
		f.is_writing = true;
	}
	else
	{
		err = CreateFileOpt (&f, true, path, testmode, overwrite ? path : 0);
		if (err || !f.f)
		{
			ResetFile (&f, 0);
			AjpgFree (out_data);
			return err;
		}
	}

	size_t stat = fwrite (out_data, 1, out_size, f.f);
	AjpgFree (out_data);

	if (stat != out_size)
	{
		err = ERROR0 (ERR_WRITE_FAILED, "Error while writing AJPG data: %s\n", path);
		RegisterFileError (&f, ERR_WRITE_FAILED);
	}

	if (opt_preserve)
		memcpy (&f.fatt, &img->fatt, sizeof (f.fatt));

	if (fo)
		f.f = 0;
	err = ResetFile (&f, opt_preserve);

	return err;
}

//-----------------------------------------------------------------------------

static enumError SaveNUT (Image_t *img, FILE *fo, ccp path, bool overwrite)
{
	DASSERT (img);
	DASSERT (path);

	enumError err = ERR_OK;
	if (img->iform != IMG_X_RGB)
	{
		err = ConvertToRGB (img, img, PAL_AUTO);
		if (err)
			return err;
	}

	const uint width = img->width;
	const uint height = img->height;
	const size_t raw_sz = (size_t)width * height * 4;
	u8 *raw_rgba = CALLOC (1, raw_sz);
	if (!raw_rgba)
		return ERR_CANT_CREATE;

	const u8 *src = img->data;
	for (uint y = 0; y < height; y++)
		memcpy (raw_rgba + y * width * 4, src + y * img->xwidth * 4, width * 4);

	u16 w16 = (u16)width;
	u16 h16 = (u16)height;
	u32 fmt = 0x0014; // RGBA8
	const u8 *tex_ptrs[1] = { raw_rgba };
	const size_t tex_szs[1] = { raw_sz };

	u8 *nut_data = 0;
	size_t nut_size = 0;
	err = CreateNUT (&nut_data, &nut_size, 1, &w16, &h16, &fmt, tex_ptrs, tex_szs);
	FREE (raw_rgba);

	if (err || !nut_data)
		return err ? err : ERR_CANT_CREATE;

	File_t f;
	if (fo)
	{
		InitializeFile (&f);
		f.f = fo;
		f.is_writing = true;
	}
	else
	{
		err = CreateFileOpt (&f, true, path, testmode, overwrite ? path : 0);
		if (err || !f.f)
		{
			ResetFile (&f, 0);
			FREE (nut_data);
			return err;
		}
	}

	size_t stat = fwrite (nut_data, 1, nut_size, f.f);
	FREE (nut_data);

	if (stat != nut_size)
	{
		err = ERROR0 (ERR_WRITE_FAILED, "Error while writing NUT data: %s\n", path);
		RegisterFileError (&f, ERR_WRITE_FAILED);
	}

	if (opt_preserve)
		memcpy (&f.fatt, &img->fatt, sizeof (f.fatt));

	if (fo)
		f.f = 0;
	err = ResetFile (&f, opt_preserve);

	return err;
}

//-----------------------------------------------------------------------------

static enumError SaveCTXB (Image_t *img, FILE *fo, ccp path, bool overwrite)
{
	DASSERT (img);
	DASSERT (path);

	enumError err = ERR_OK;
	if (img->iform != IMG_X_RGB)
	{
		err = ConvertToRGB (img, img, PAL_AUTO);
		if (err)
			return err;
	}

	const uint width = img->width;
	const uint height = img->height;
	const uint tw = (width + 7) & ~7u;
	const uint th = (height + 7) & ~7u;
	const uint img_data_size = tw * th * 4;

	u8 *img_data = CALLOC (1, img_data_size);
	if (!img_data)
		return ERR_CANT_CREATE;

	const u8 *src = img->data;
	for (uint y = 0; y < height; y++)
	{
		for (uint x = 0; x < width; x++)
		{
			const uint pos = ((y / 8) * (tw / 8) + (x / 8)) * 64 + morton8 (x & 7, y & 7);
			const u8 *s = src + (y * img->xwidth + x) * 4;
			u8 *d = img_data + 4 * pos;
			d[0] = s[0];
			d[1] = s[1];
			d[2] = s[2];
			d[3] = s[3];
		}
	}

	const uint hdr_size = 24;
	const uint chunk_size = 12 + 36;
	const uint tex_data_offset = hdr_size + chunk_size;
	const uint total_sz = tex_data_offset + img_data_size;

	u8 *ctxb = CALLOC (1, total_sz);
	if (!ctxb)
	{
		FREE (img_data);
		return ERR_CANT_CREATE;
	}

	memcpy (ctxb, "ctxb", 4);
	wr_le32 (ctxb + 4, total_sz);
	wr_le32 (ctxb + 8, 1);  // chunk count
	wr_le32 (ctxb + 12, 0);
	wr_le32 (ctxb + 16, hdr_size); // chunk offset
	wr_le32 (ctxb + 20, tex_data_offset); // tex data offset

	u8 *chunk = ctxb + hdr_size;
	memcpy (chunk, "tex ", 4);
	wr_le32 (chunk + 4, 36); // sec size
	wr_le32 (chunk + 8, 1);  // tex count

	u8 *tentry = chunk + 12;
	wr_le32 (tentry + 0, img_data_size);
	wr_le16 (tentry + 4, 0);
	wr_le16 (tentry + 6, 0);
	wr_le16 (tentry + 8, (u16)width);
	wr_le16 (tentry + 10, (u16)height);
	wr_le32 (tentry + 12, 0x14016752); // RGBA8
	wr_le32 (tentry + 16, 0);          // data rel offset

	ccp fname = FindFilename (path, 0);
	if (!fname) fname = "texture";
	char tname[16];
	memset (tname, 0, sizeof (tname));
	char *dot = strchr (fname, '.');
	size_t flen = dot ? (size_t)(dot - fname) : strlen (fname);
	if (flen > 15) flen = 15;
	memcpy (tname, fname, flen);
	memcpy (tentry + 20, tname, 16);

	memcpy (ctxb + tex_data_offset, img_data, img_data_size);
	FREE (img_data);

	File_t f;
	if (fo)
	{
		InitializeFile (&f);
		f.f = fo;
		f.is_writing = true;
	}
	else
	{
		err = CreateFileOpt (&f, true, path, testmode, overwrite ? path : 0);
		if (err || !f.f)
		{
			ResetFile (&f, 0);
			FREE (ctxb);
			return err;
		}
	}

	size_t stat = fwrite (ctxb, 1, total_sz, f.f);
	FREE (ctxb);

	if (stat != total_sz)
	{
		err = ERROR0 (ERR_WRITE_FAILED, "Error while writing CTXB data: %s\n", path);
		RegisterFileError (&f, ERR_WRITE_FAILED);
	}

	if (opt_preserve)
		memcpy (&f.fatt, &img->fatt, sizeof (f.fatt));

	if (fo)
		f.f = 0;
	err = ResetFile (&f, opt_preserve);

	return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			SaveIMG()			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError SaveIMG (Image_t *img, // pointer to valid img
	file_format_t fform, // file format
	const MipmapOptions_t *mmo, // NULL or mipmap options
	FILE *f, // output file, if NULL then use fname+overwrite
	ccp fname, // filename of source
	bool overwrite // true: force overwriting
)
{
	DASSERT (img);
	DASSERT (fname);

	PRINT ("SaveIMG(N=%u,o=%d) %s, mo=%s, %s\n", CountMipmapsIMG (img), overwrite,
		PrintFormat3 (fform, img->iform, img->pform), InfoMipmapOptions (mmo), fname);

	switch (fform)
	{
			// [[tpl-ex+]]
		case FF_CUPICON:
		case FF_TPL:
		case FF_TPLX:
			return SaveTPL (img, fform, f, fname, overwrite);
		case FF_BTI:
			return SaveBTI (img, mmo, f, fname, overwrite);
		case FF_TEX:
			return SaveTEX (img, mmo, f, fname, overwrite, false);
		case FF_TEX_CT:
			return SaveTEX (img, mmo, f, fname, overwrite, true);
		case FF_BREFT:
		case FF_BREFT_IMG:
			return SaveBREFTIMG (img, mmo, f, fname, overwrite);
		case FF_PNG:
			return SavePNG (img, true, f, fname, 0, 0, overwrite, 0);
		case FF_AJPG:
			return SaveAJPG (img, f, fname, 0, overwrite);
		case FF_CTXB:
			return SaveCTXB (img, f, fname, overwrite);
		case FF_NUT:
			return SaveNUT (img, f, fname, overwrite);

		default:
			return ERROR0 (ERR_INVALID_IFORM, "Can_t create image [file type=%s]: %s\n",
				GetNameFF (0, fform), fname);
	}
}

///////////////////////////////////////////////////////////////////////////////

void ResetRawTPL (tpl_raw_t *raw)
{
	if (raw)
	{
		for (int i = 0; i < raw->n_image; i++)
			ResetMMI (raw->mmi + i);
		FREE (raw->mmi);
		FreeString (raw->data.ptr);
		memset (raw, 0, sizeof (*raw));
	}
}

//-----------------------------------------------------------------------------

const tpl_signature_t TPLSignature0 = { "LE-CODE\0", 0, 0, 0, 0, 0, "Cup Icon" };

//-----------------------------------------------------------------------------

enumError CreateRawTPL (tpl_raw_t *raw, // results with alloced data
	Image_t *src_img, // pointer to valid source img
	file_format_t fform // FF_TPL or FF_TPLX
)
{
	DASSERT (src_img);
	DASSERT (raw);
	memset (raw, 0, sizeof (*raw));

	//--- special handling for cup icons

	bool create_tplx = fform == FF_TPLX;
	if (fform == FF_CUPICON || src_img->is_cup_icon && !n_transform) // no forced transformation
	{
		ConvertIMG (src_img, false, 0, IMG_CMPR, PAL_INVALID);
		create_tplx = true;
	}
	else
	{
		Transform2InternIMG (src_img);
		const enumError err = ExecTransformIMG (src_img);
		if (err)
			return err;
	}

	PRINT ("is_cup_icon=%d, test_mode=%d, ff=%s\n", src_img->is_cup_icon, src_img->test_mode,
		GetImageFormatName (src_img->iform, "?"));

	if (create_tplx)
		FreeMipmapsIMG (src_img);

	//--- TPLx calculations

	const ImageGeometry_t *geo = GetImageGeometry (src_img->iform);
	if (!geo)
		return ERROR0 (ERR_INTERNAL, 0);

	int ex_width, ex_height, ex_fill;
	if (create_tplx)
	{
		// add 1 for special line
		uint max_lines = MAX_IMAGE_HEIGHT / geo->block_height;
		const uint n_lines = (src_img->height + 2 * geo->block_height - 1) / geo->block_height;
		if (src_img->test_mode && max_lines >= n_lines)
			max_lines = (n_lines - 1) / geo->block_height * geo->block_height;
		const uint n_cols = (n_lines + max_lines - 1) / max_lines;

		ex_width = src_img->width * n_cols;
		ex_height = (n_lines + n_cols - 1) / n_cols * geo->block_height;
		ex_fill
			= (ex_width * ex_height - src_img->width * src_img->height) * geo->bits_per_pixel / 8;

		PRINT ("TPLx: %u*%u, cols=%d, lines=%d/%d, geo=%d*%d => %d*%d, fill:%x\n", src_img->width,
			src_img->height, n_cols, n_lines, max_lines, geo->block_width, geo->block_height,
			ex_width, ex_height, ex_fill);
	}
	else
	{
		ex_width = src_img->width < MAX_IMAGE_WIDTH ? src_img->width : MAX_IMAGE_WIDTH;
		ex_height = src_img->height < MAX_IMAGE_HEIGHT ? src_img->height : MAX_IMAGE_HEIGHT;
		ex_fill = 0;
	}

	//--- setup images

	u8 *data = 0;
	const int n_image = CountMipmapsIMG (src_img) + 1;
	mipmap_info_t *mmi = CALLOC (n_image, sizeof (*mmi));
	raw->n_image = n_image;
	raw->mmi = mmi;
	enumError err = ERR_OK;

	const uint align = 0x20;
	// [[tpl-ex+]]
	const uint tab_off = create_tplx ? sizeof (tpl_header_ex_t) : sizeof (tpl_header_t);
	uint data_off = tab_off + n_image * sizeof (tpl_imgtab_t);

	mipmap_info_t *m = mmi;
	const Image_t *img = src_img;

	//--- first loop: setup header offsets

	int i;
	for (i = 0; i < n_image; i++, m++, img = img->mipmap)
	{
		DASSERT (img);
		err = PrepareImagesTPL (m, img);
		if (err)
			return err;

		m->pal_head_off = 0;
		m->img_head_off = data_off;

		if (m->img.n_pal)
		{
			m->pal_head_off = data_off;
			m->pal_data_off = m->pal_head_off + sizeof (tpl_pal_header_t);
			m->pal_data_size = 2 * m->img.n_pal;
			m->img_head_off = ALIGN32 (m->pal_data_off + m->pal_data_size, 4);
		}
		data_off = ALIGN32 (m->img_head_off + sizeof (tpl_img_header_t), 4);
	}
	data_off = ALIGN32 (data_off, align);

	//--- second loop: setup data offsets

	for (i = 0, m = mmi; i < n_image; i++, m++)
	{
		m->img_data_off = data_off;
		uint data_size = m->image_size;
		m->img_data_size = data_size;
		if (!i)
			data_size += ex_fill;
		data_off = ALIGN32 (m->img_data_off + data_size, align);

		PRINT0 ("%6x %6x %6x | %6x %6x %6x | %6x = %6u\n", m->pal_head_off, m->pal_data_off,
			m->pal_data_size, m->img_head_off, m->img_data_off, m->img_data_size, data_off,
			data_off);
	}

	//--- alloc data & setup file header

	data = CALLOC (1, data_off);
	raw->data.ptr = (ccp)data;
	raw->data.len = data_off;
	const endian_func_t *endian = src_img->endian;
	raw->endian = endian;

	// [[tpl-ex+]]
	tpl_header_ex_t *tpl = (tpl_header_ex_t *)data;
	endian->wr32 (tpl->magic, TPL_MAGIC_NUM);
	endian->wr32 (&tpl->n_image, n_image);
	endian->wr32 (&tpl->imgtab_off, tab_off);

	// [[tpl-ex+]]
	if (create_tplx)
	{
		endian->wr32 (&tpl->ex_magic, TPL_EX_MAGIC_NUM);
		endian->wr32 (&tpl->ex_width, src_img->width);
		endian->wr32 (&tpl->ex_height, src_img->height);
		endian->wr32 (&tpl->ex_n_icon, src_img->height / src_img->width);
	}

	tpl_imgtab_t *tab = (tpl_imgtab_t *)(data + tab_off);

	//--- third loop: copy data

	for (i = 0, m = mmi; i < n_image; i++, m++)
	{
		endian->wr32 (&tab[i].image_off, m->img_head_off);
		endian->wr32 (&tab[i].palette_off, m->pal_head_off);

		if (m->img.n_pal)
		{
			tpl_pal_header_t *tp = (tpl_pal_header_t *)(data + m->pal_head_off);
			endian->wr16 (&tp->n_entry, m->img.n_pal);
			endian->wr32 (&tp->pform, m->img.pform);
			endian->wr32 (&tp->data_off, m->pal_data_off);
			memcpy (data + m->pal_data_off, m->img.pal, 2 * m->img.n_pal);
		}

		// [[tpl-ex+]]
		tpl_img_header_t *ti = (tpl_img_header_t *)(data + m->img_head_off);
		endian->wr16 (&ti->width, ex_width);
		endian->wr16 (&ti->height, ex_height);
		endian->wr32 (&ti->iform, m->img.iform);
		endian->wr32 (&ti->data_off, m->img_data_off);
		endian->wr32 (&ti->min_filter, 1);
		endian->wr32 (&ti->mag_filter, 1);

		u8 *dest;
		err = WriteImageData (m, data + m->img_data_off, &dest);
		if (err)
			return err;
		PRINT0 (
			"DEST: %p - %p = %zx\n", dest, data + m->img_data_off, dest - (data + m->img_data_off));

		if (!i && ex_fill)
		{
			memset (dest, 0, ex_fill);
			if (ex_fill >= sizeof (tpl_signature_t))
			{
				tpl_signature_t *sig = (tpl_signature_t *)(dest + ex_fill) - 1;
				*sig = TPLSignature0;
				endian->wr32 (&sig->width, src_img->width);
				endian->wr32 (&sig->height, src_img->height);
				endian->wr32 (&sig->n_icon, src_img->height / src_img->width);
				endian->wr16 (&sig->iform, src_img->iform);
				endian->wr16 (&sig->pform, src_img->pform);
			}
		}
	}

	raw->valid = true;
	return ERR_OK;
}

//-----------------------------------------------------------------------------

enumError SaveRawTPL (tpl_raw_t *raw, // raw data created by CreateRawTPL()
	FILE *f, // output file, if NULL then use fname+overwrite
	ccp fname, // filename of source
	bool overwrite // true: allow overwriting
)
{
	DASSERT (raw);
	DASSERT (fname);
	PRINT ("SaveRawTPL(o=%d) %s\n", overwrite, fname);

	return raw->valid ? SaveFILE2 (f, fname, 0, overwrite, raw->data.ptr, raw->data.len, 0)
					  : ERR_ERROR;
}

//-----------------------------------------------------------------------------

enumError SaveTPL (Image_t *src_img, // pointer to valid source img
	file_format_t fform, // FF_TPL or FF_TPLX
	FILE *f, // output file, if NULL then use fname+overwrite
	ccp fname, // filename of source
	bool overwrite // true: allow overwriting
)
{
	DASSERT (src_img);
	DASSERT (fname);

	PRINT ("SaveTPL(ff=%s,o=%d) %s\n", GetNameFF (fform, fform), overwrite, fname);

	tpl_raw_t raw;
	enumError err = CreateRawTPL (&raw, src_img, fform);
	if (raw.valid)
		err = SaveRawTPL (&raw, f, fname, overwrite);
	//	err = SaveFILE2(f,fname,0,overwrite,raw.data.ptr,raw.data.len,0);
	ResetRawTPL (&raw);
	return err;
}

///////////////////////////////////////////////////////////////////////////////

enumError SaveBTI (Image_t *src_img, // pointer to valid source img
	const MipmapOptions_t *mmo, // NULL or mipmap options
	FILE *f, // output file, if NULL then use fname+overwrite
	ccp fname, // filename of source
	bool overwrite // true: force overwriting
)
{
	DASSERT (src_img);
	DASSERT (fname);

	PRINT0 ("SaveBTI(o=%d) mo=%s, %s\n", overwrite, InfoMipmapOptions (mmo), fname);

	//--- setup images

	u8 *data = 0;
	// don't know how to store palettes [[2do]]
	const bool no_pal_stat = Transform2NoPaletteIMG (src_img);

	mipmap_info_t mmi;
	enumError err = PrepareImages (&mmi, src_img, mmo);
	if (err)
		goto abort;

	static int warn_count = 3;
	if (no_pal_stat && warn_count > 0)
	{
		warn_count--;
		ERROR0 (ERR_WARNING, "BTI files with palettes not supported yet. Image converted to '%s'.",
			PrintFormat3 (0, mmi.img.iform, mmi.img.pform));
	}

	//--- calculate image size

	const uint data_off = sizeof (bti_header_t);
	const uint total_size = data_off + mmi.image_size;
	PRINT ("TOTAL-SIZE: %x = %u\n", total_size, total_size);
	PRINT ("IMAGE-SIZE: %x = %u, %u*%u, N=1+%u\n", mmi.img.data_size, mmi.img.data_size,
		mmi.img.width, mmi.img.height, mmi.n_mipmap);

	//--- alloc and setup data

	data = CALLOC (1, total_size);
	const endian_func_t *endian = &be_func;

	bti_header_t *bti = (bti_header_t *)data;
	bti->iform = mmi.img.iform;
	bti->n_image = mmi.n_mipmap + 1;
	bti->unknown_14 = 1;
	bti->unknown_15 = 1;
	endian->wr16 (&bti->width, mmi.img.width);
	endian->wr16 (&bti->height, mmi.img.height);
	endian->wr32 (&bti->data_off, data_off);

	ccp point = strrchr (fname, '.');
	const bool is_special
		= point && (!strcasecmp (point, ".btiEnv") || !strcasecmp (point, ".btiMat"));
	if (!is_special)
	{
		bti->unknown_01 = 2;
		bti->wrap_s = 1;
		bti->wrap_t = 1;
	}

	err = WriteImageData (&mmi, data + data_off, 0);
	if (!err)
		err = SaveFILE2 (f, fname, 0, overwrite, data, total_size, 0);

abort:
	FREE (data);
	ResetMMI (&mmi);
	return err;
}

///////////////////////////////////////////////////////////////////////////////

enumError SaveTEX (Image_t *src_img, // pointer to valid source img
	const MipmapOptions_t *mmo, // NULL or mipmap options
	FILE *f, // output file, if NULL then use fname+overwrite
	ccp fname, // filename of source
	bool overwrite, // true: force overwriting
	bool ctcode_support // true: include a CT-CODE file
)
{
	DASSERT (src_img);
	DASSERT (fname);

	PRINT ("SaveTEX(o=%d,ct=%d) mo=%s, %s\n", overwrite, ctcode_support, InfoMipmapOptions (mmo),
		fname);

	//--- setup images

	u8 *data = 0;
	const bool no_pal_stat = Transform2NoPaletteIMG (src_img);

	mipmap_info_t mmi;
	enumError err = PrepareImages (&mmi, src_img, mmo);
	if (err)
		goto abort;

	static int warn_count = 3;
	if (no_pal_stat && warn_count > 0)
	{
		warn_count--;
		ERROR0 (ERR_WARNING, "TEX0 files don't support palettes, image converted to '%s'.",
			PrintFormat3 (0, mmi.img.iform, mmi.img.pform));
	}

	//--- calculate image size

	ccp realfile = strrchr (fname, '/');
	realfile = realfile ? realfile + 1 : fname;
	const uint filelen = strlen (realfile);
	const uint grp_off = 0x40;
	const uint data_size = grp_off + mmi.image_size;
	const uint total_size = data_size + 4 + ALIGN32 (filelen + 1, 4);

	PRINT ("TOTAL-SIZE: %x = %u\n", total_size, total_size);
	PRINT ("IMAGE-SIZE: %x = %u, %u*%u, N=1+%u\n", mmi.img.data_size, mmi.img.data_size,
		mmi.img.width, mmi.img.height, mmi.n_mipmap);

	//--- alloc and setup data

	data = CALLOC (1, total_size);
	const endian_func_t *endian = mmi.img.endian;

	brsub_header_t *bh = (brsub_header_t *)data;
	memcpy (bh->magic, TEX_MAGIC, sizeof (bh->magic));
	endian->wr32 (&bh->size, data_size);
	endian->wr32 (&bh->version, 3);
	endian->wr32 (&bh->grp_offset, grp_off);

	const uint n_grp = GetSectionNumBRSUB (data, data_size, endian);
	tex_info_t *ti = (tex_info_t *)(bh->grp_offset + n_grp);
	endian->wr32 (&ti->name_off, data_size + 4);
	endian->wr32 (data + data_size, filelen);
	memcpy (data + data_size + 4, realfile, filelen);
	endian->wr16 (&ti->width, mmi.img.width);
	endian->wr16 (&ti->height, mmi.img.height);
	endian->wr32 (&ti->iform, mmi.img.iform);
	endian->wr32 (&ti->n_image, mmi.n_mipmap + 1);
	endian->wrf4 (&ti->image_val, mmi.n_mipmap);

	err = WriteImageData (&mmi, data + grp_off, 0);
	if (!err)
		err = SaveFILE2 (f, fname, 0, overwrite, data, total_size, 0);

abort:
	FREE (data);
	ResetMMI (&mmi);
	return err;
}

///////////////////////////////////////////////////////////////////////////////

enumError SaveBREFTIMG (Image_t *src_img, // pointer to valid img
	const MipmapOptions_t *mmo, // NULL or mipmap options
	FILE *f, // output file, if NULL then use fname+overwrite
	ccp fname, // filename of source
	bool overwrite // true: force overwriting
)
{
	DASSERT (src_img);
	DASSERT (fname);

	PRINT ("SaveBREFTIMG(o=%d) mo=%s, %s\n", overwrite, InfoMipmapOptions (mmo), fname);

	//--- setup images

	u8 *data = 0;
	const bool no_pal_stat = Transform2NoPaletteIMG (src_img);

	mipmap_info_t mmi;
	enumError err = PrepareImages (&mmi, src_img, mmo);
	if (err)
		goto abort;

	static int warn_count = 3;
	if (no_pal_stat && warn_count > 0)
	{
		warn_count--;
		ERROR0 (ERR_WARNING, "BREFT files don't support palettes, image converted to '%s'.",
			PrintFormat3 (0, mmi.img.iform, mmi.img.pform));
	}

	//--- calculate image size

	const uint img_off = 0x20;
	const uint total_size = img_off + mmi.image_size;

	PRINT ("TOTAL-SIZE: %x = %u\n", total_size, total_size);
	PRINT ("IMAGE-SIZE: %x = %u, %u*%u, N=1+%u\n", mmi.img.data_size, mmi.img.data_size,
		mmi.img.width, mmi.img.height, mmi.n_mipmap);

	//--- alloc and setup data

	data = CALLOC (1, total_size);
	const endian_func_t *endian = mmi.img.endian;

	breft_image_t *bi = (breft_image_t *)data;
	endian->wr16 (&bi->width, mmi.img.width);
	endian->wr16 (&bi->height, mmi.img.height);
	endian->wr32 (&bi->img_size, mmi.image_size);
	bi->iform = mmi.img.iform;
	bi->n_mipmap = mmi.n_mipmap;

	err = WriteImageData (&mmi, data + img_off, 0);
	if (!err)
		err = SaveFILE2 (f, fname, 0, overwrite, data, total_size, 0);

abort:
	FREE (data);
	ResetMMI (&mmi);
	return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			PNG callback functions		///////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct png_info_t
{
	ccp head_msg; // header for error messages
	File_t *file; // file pointer
	u8 *cache; // cached data
	uint cache_size; // size of 'cache' data

} png_info_t;

///////////////////////////////////////////////////////////////////////////////

static void print_png_message (enumError err, png_structp png_ptr, ccp message)
{
	const png_info_t *pinfo = png_get_error_ptr (png_ptr);
	if (!pinfo)
		ERROR0 (err, "PNG error: %s", message);
	else if (pinfo->file)
	{
		ERROR0 (err, "%s: %s: %s", pinfo->head_msg, message, pinfo->file->fname);
		RegisterFileError (pinfo->file, err);
		ResetFile (pinfo->file, false);
	}
	else
		ERROR0 (err, "%s: %s", pinfo->head_msg, message);
}

///////////////////////////////////////////////////////////////////////////////

static void png_warning_func (png_structp png_ptr, ccp message)
{
	print_png_message (ERR_PNG, png_ptr, message);
	longjmp (png_jmpbuf (png_ptr), 1);
}

///////////////////////////////////////////////////////////////////////////////

static void png_error_func (png_structp png_ptr, ccp message)
{
	print_png_message (ERR_PNG, png_ptr, message);
	longjmp (png_jmpbuf (png_ptr), 1);
}

///////////////////////////////////////////////////////////////////////////////

static void png_read_func (png_structp png_ptr, png_bytep dest, png_size_t size)
{
	png_info_t *pinfo = png_get_io_ptr (png_ptr);
	DASSERT (pinfo);
	if (pinfo->cache_size)
	{
		const uint max_read = size < pinfo->cache_size ? size : pinfo->cache_size;
		memcpy (dest, pinfo->cache, max_read);
		pinfo->cache += max_read;
		pinfo->cache_size -= max_read;
		dest += max_read;
		size -= max_read;
		errno = 0;
	}

	if (size > 0)
		fread (dest, 1, size, pinfo->file->f);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			LoadPNG()			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError LoadPNG (Image_t *img, // destination image
	bool init_img, // true: initialize 'img' first
	bool mipmaps, // true: try to load mipmaps

	ccp path1, // NULL or part #1 of path
	ccp path2 // NULL or part #2 of path
)
{
	DASSERT (img);
	PRINT ("LoadPNG(mm=%d) %s / %s\n", mipmaps, path1 ? path1 : "", path2 ? path2 : "");

	if (init_img)
		InitializeIMG (img);
	else
		ResetIMG (img);

	//--- open file

	char pathbuf[PATH_MAX];
	ccp path = PathCatPP (pathbuf, sizeof (pathbuf), path1, path2);

	File_t f;
	enumError err = OpenFILE (&f, true, path, false, false);
	if (err)
	{
		ResetFile (&f, false);
		return err;
	}

	err = ReadPNG (img, mipmaps, &f, 0, 0);

	img->path = f.fname;
	f.fname = 0;
	img->path_alloced = true;
	memcpy (&img->fatt, &f.fatt, sizeof (img->fatt));

	ResetFile (&f, false);
	return err;
}

//
///////////////////////////////////////////////////////////////////////////////

enumError ReadPNG (Image_t *img, // destination image
	bool mipmaps, // true: try to load mipmaps
	File_t *f, // valid opened file
	u8 *cache, // pre read data
	uint cache_size // size of 'cache'
)
{
	DASSERT (img);
	DASSERT (f);
	DASSERT (f->f);

	//--- setup png

	png_info_t pinfo;
	memset (&pinfo, 0, sizeof (pinfo));
	pinfo.head_msg = "Open PNG";
	pinfo.file = f;
	pinfo.cache = cache;
	pinfo.cache_size = cache_size;

	png_structp png_ptr
		= png_create_read_struct (PNG_LIBPNG_VER_STRING, &pinfo, png_error_func, png_warning_func);
	if (!png_ptr)
		goto abort_init;

	png_infop info_ptr = png_create_info_struct (png_ptr);
	if (!info_ptr)
	{
		png_destroy_read_struct (&png_ptr, 0, 0);
		goto abort_init;
	}

	png_infop end_info = png_create_info_struct (png_ptr);
	if (!end_info)
	{
		png_destroy_read_struct (&png_ptr, &info_ptr, 0);
		goto abort_init;
	}

	if (setjmp (png_jmpbuf (png_ptr)))
	{
		png_destroy_read_struct (&png_ptr, &info_ptr, &end_info);
		PRINT ("ERR=%u\n", f->max_err);
		return f->max_err;
	}

	if (pinfo.cache_size)
	{
		DASSERT (pinfo.cache);
		png_set_read_fn (png_ptr, &pinfo, png_read_func);
	}
	else
		png_init_io (png_ptr, f->f);

	png_read_info (png_ptr, info_ptr);

	u32 width, height;
	int bit_depth, color_type, interlace;
	png_get_IHDR (png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace, 0, 0);
	noPRINT ("--> %u*%u*%u, ct=%d, il=%d\n", width, height, bit_depth, color_type, interlace);

	if (interlace != PNG_INTERLACE_NONE)
	{
		png_destroy_read_struct (&png_ptr, &info_ptr, &end_info);
		RegisterFileError (f, ERR_INVALID_IFORM);
		return ERROR0 (ERR_INVALID_IFORM, "Interlaced PNG not supported: %s\n", f->fname);
	}

	uint data_size;
	bool is_gray;
	img->alpha_status = -1;

	switch (color_type)
	{
		case PNG_COLOR_TYPE_GRAY_ALPHA:
			img->alpha_status = 0;
		case PNG_COLOR_TYPE_GRAY:
			is_gray = true;
			data_size = EXPAND8 (width) * EXPAND8 (height) * 2;
			img->iform = IMG_X_GRAY;
			break;

		case PNG_COLOR_TYPE_RGB_ALPHA:
		case PNG_COLOR_TYPE_PALETTE:
			img->alpha_status = 0;
		case PNG_COLOR_TYPE_RGB:
			is_gray = false;
			data_size = EXPAND8 (width) * EXPAND8 (height) * 4;
			img->iform = IMG_X_RGB;
			break;

		default:
			png_destroy_read_struct (&png_ptr, &info_ptr, &end_info);
			ERROR0 (ERR_INVALID_IFORM, "Unsupported PNG color type: %s\n", f->fname);
			RegisterFileError (f, ERR_INVALID_IFORM);
			return RegisterFileError (f, ERR_INVALID_IFORM);
	}

	if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb (png_ptr);

	if (png_get_valid (png_ptr, info_ptr, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha (png_ptr);

	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
		png_set_expand_gray_1_2_4_to_8 (png_ptr);

	if (bit_depth == 16)
		png_set_strip_16 (png_ptr);

	png_set_add_alpha (png_ptr, 0xff, PNG_FILLER_AFTER);
	png_read_update_info (png_ptr, info_ptr);

	u8 *data = MALLOC (data_size);

	img->data = data;
	img->data_alloced = true;
	img->data_size = data_size;
	img->width = width;
	img->height = height;
	img->xwidth = EXPAND8 (width);
	img->xheight = EXPAND8 (height);
	img->info_iform = img->iform;
	img->info_fform = FF_PNG;

	//--- read data

	const uint rowlen = (is_gray ? 2 : 4) * img->xwidth;

#if HAVE_PRINT
	png_get_IHDR (png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace, 0, 0);
	noPRINT ("--> %u*%u*%u, ct=%d, il=%d\n", width, height, bit_depth, color_type, interlace);

	const uint channels = png_get_channels (png_ptr, info_ptr);
	const uint rowbytes = png_get_rowbytes (png_ptr, info_ptr);

	PRINT ("ReadPNG() %u*%u, ch=%u, gray=%d, alpha=%d, rl=%u,%u\n", width, height, channels,
		is_gray, img->alpha_status, rowbytes, rowlen);
#endif

	while (height-- > 0)
	{
		png_read_row (png_ptr, (png_bytep)data, NULL);
		data += rowlen;
	}
	DASSERT (data <= img->data + data_size);

	//--- close png and file

	png_read_end (png_ptr, info_ptr);
	png_destroy_read_struct (&png_ptr, &info_ptr, &end_info);

	//--- mipmap support

	if (mipmaps)
	{
		ccp ext = strrchr (f->fname, '.');
		ccp file = strrchr (f->fname, '/');
		if (!ext || file && ext < file)
			ext = f->fname + strlen (f->fname);

		Image_t *mm_img = img;
		uint count;
		for (count = 1;; count++)
		{
			char mm_path[PATH_MAX];
			snprintf (mm_path, sizeof (mm_path), "%.*s.mm%u%s", (int)(ext - f->fname), f->fname,
				count, ext);
			PRINT ("Try open PNG: %s\n", mm_path);
			File_t F;
			enumError err = OpenFILE (&F, true, mm_path, true, false); // local err
			if (err)
			{
				ResetFile (&F, false);
				if (err != ERR_NOT_EXISTS)
					return err;
				break;
			}

			mm_img->mipmap = MALLOC (sizeof (*mm_img->mipmap));
			mm_img = mm_img->mipmap;
			InitializeIMG (mm_img);
			err = ReadPNG (mm_img, false, &F, 0, 0);
			memcpy (&mm_img->fatt, &F.fatt, sizeof (mm_img->fatt));
			ResetFile (&F, false);
			if (err)
				return err;
		}
		MM_COUNT (img);
	}

	return PatchListIMG (img);

	//--- abort

abort_init:
	ERROR0 (ERR_READ_FAILED, "Error while initializing PNG data: %s\n", f->fname);
	return RegisterFileError (f, ERR_READ_FAILED);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			SavePNG()			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError SavePNG (Image_t *img, // valid image
	bool mipmaps, // true: save mipmaps (auto file name)
	FILE *fo, // output file, if NULL then use path1+path2
	ccp path1, // NULL or part #1 of path
	ccp path2, // NULL or part #2 of path
	int store_alpha, // <0:no alpha, =0:auto alpha, >0:store alpha
	bool overwrite, // true: force overwriting
	StringField_t *file_list // not NULL: store filenames of created png files

)
{
	DASSERT (img);

	//--- setup path & ...

	if (!path2 || !*path2)
	{
		path2 = path1;
		path1 = 0;
	}

	char pathbuf[PATH_MAX];
	ccp path = PathCatPP (pathbuf, sizeof (pathbuf), path1, path2);
	PRINT ("SavePNG(mm=%d/%u) {%u,%u} alpha=%d : %s\n", mipmaps, CountMipmapsIMG (img),
		img->conv_count, img->seq_num, store_alpha, path);

	Transform2XIMG (img);
	enumError err = ExecTransformIMG (img);
	if (err)
		return err;

	if (!store_alpha)
		store_alpha = CheckAlphaIMG (img, false);

	if (GetPaletteCountIF (img->iform) && (store_alpha > 0 || img->n_pal > 0x100))
	{
		err = ConvertToRGB (img, img, PAL_AUTO);
		if (err)
			return err;
	}

	//--- setup export mode

	enum export_mode
	{
		MD_F_ALPHA = 1,
		MD_F_RGB = 2,
		MD_F_PAL = 4,

		MD_G = 0,
		MD_GA = MD_F_ALPHA,
		MD_RGB = MD_F_RGB,
		MD_RGBA = MD_F_RGB | MD_F_ALPHA,
		MD_PAL = MD_F_PAL,

	} export_mode;

	switch (img->iform)
	{
		case IMG_X_GRAY:
			export_mode = store_alpha < 0 ? MD_G : MD_GA;
			break;

		case IMG_X_RGB:
			export_mode = store_alpha < 0 ? MD_RGB : MD_RGBA;
			break;

		case IMG_X_PAL:
		case IMG_X_PAL4:
		case IMG_X_PAL8:
		case IMG_X_PAL14:
			export_mode = MD_PAL;
			break;

		default:
			return ERROR0 (ERR_INVALID_IFORM,
				"Image format 0x%02x [%s] not supported for PNG export: %s\n", img->iform,
				GetImageFormatName (img->iform, "?"), path);
	}

	//--- create file

	if (file_list)
	{
		ccp str = path + (path1 ? strlen (path1) : 0);
		if (*str == '/')
			str++;
		InsertStringField (file_list, str, false);
	}

	File_t f;
	if (fo)
	{
		InitializeFile (&f);
		f.f = fo;
		f.is_writing = true;
		mipmaps = false;
	}
	else
	{
		err = CreateFileOpt (&f, true, path, testmode, overwrite ? path : 0);
		if (err || !f.f)
		{
			ResetFile (&f, 0);
			return err;
		}
	}

	//--- setup png

	png_info_t pinfo;
	memset (&pinfo, 0, sizeof (pinfo));
	pinfo.head_msg = "Create PNG";
	pinfo.file = &f;

	png_structp png_ptr
		= png_create_write_struct (PNG_LIBPNG_VER_STRING, &pinfo, png_error_func, png_warning_func);
	if (!png_ptr)
		goto abort_init;

	png_infop info_ptr = png_create_info_struct (png_ptr);
	if (!info_ptr)
	{
		png_destroy_write_struct (&png_ptr, 0);
		goto abort_init;
	}

	if (setjmp (png_jmpbuf (png_ptr)))
	{
		png_destroy_write_struct (&png_ptr, &info_ptr);
		goto abort;
	}

	png_init_io (png_ptr, f.f);

	DASSERT (EXPAND8 (img->width) == img->xwidth);
	DASSERT (EXPAND8 (img->height) == img->xheight);

	switch (export_mode)
	{
		case MD_G:
			png_set_IHDR (png_ptr, info_ptr, img->width, img->height, 8, PNG_COLOR_TYPE_GRAY,
				PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
			break;

		case MD_GA:
			png_set_IHDR (png_ptr, info_ptr, img->width, img->height, 8, PNG_COLOR_TYPE_GRAY_ALPHA,
				PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
			break;

		case MD_RGB:
			png_set_IHDR (png_ptr, info_ptr, img->width, img->height, 8, PNG_COLOR_TYPE_RGB,
				PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
			break;

		case MD_RGBA:
			png_set_IHDR (png_ptr, info_ptr, img->width, img->height, 8, PNG_COLOR_TYPE_RGB_ALPHA,
				PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
			break;

		case MD_PAL:
			png_set_IHDR (png_ptr, info_ptr, img->width, img->height, 8, PNG_COLOR_TYPE_PALETTE,
				PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

			// iobuf to transform palette into 'no alpha'
			{
				png_colorp dest = (png_colorp)iobuf;
				DASSERT (img->n_pal * sizeof (*dest) <= sizeof (iobuf));
				const u8 *src = img->pal;
				uint c = img->n_pal;
				while (c-- > 0)
				{
					dest->red = *src++;
					dest->green = *src++;
					dest->blue = *src++;
					src++;
					dest++;
				}
				png_set_PLTE (png_ptr, info_ptr, (png_colorp)iobuf, img->n_pal);
			}
			break;
	}

	//---- write strings

	png_text ptext[5], *pt = ptext;
	memset (ptext, 0, sizeof (ptext));

	if (!opt_strip)
	{
		pt->compression = PNG_TEXT_COMPRESSION_NONE;
		pt->key = "creator";
		pt->text = TOOLSET_LONG;
		pt->text_length = strlen (pt->text);
		pt++;

		if (img->info_fform > FF_UNKNOWN)
		{
			pt->compression = PNG_TEXT_COMPRESSION_NONE;
			pt->key = "file-format";
			pt->text = (char *)GetNameFF (0, img->info_fform);
			pt->text_length = strlen (pt->text);
			pt++;
		}

		ccp info_text = GetImageFormatName (img->info_iform, 0);
		if (info_text)
		{
			pt->compression = PNG_TEXT_COMPRESSION_NONE;
			pt->key = "image-format";
			pt->text = (char *)info_text;
			pt->text_length = strlen (pt->text);
			pt++;
		}

		info_text = GetPaletteFormatName (img->info_pform, 0);
		if (info_text)
		{
			pt->compression = PNG_TEXT_COMPRESSION_NONE;
			pt->key = "palette-format";
			pt->text = (char *)info_text;
			pt->text_length = strlen (pt->text);
			pt++;
		}
	}

	DASSERT (pt - ptext <= sizeof (ptext) / sizeof (*ptext));
	png_set_text (png_ptr, info_ptr, ptext, pt - ptext);
	png_write_info (png_ptr, info_ptr);

	//--- write image data

	switch (export_mode)
	{
		case MD_G:
		{
			if (img->xwidth > sizeof (iobuf))
				goto abort_init;

			const u8 *data = img->data;
			uint ih = img->height;
			while (ih-- > 0)
			{
				u8 *dest = (u8 *)iobuf;
				uint iw = img->xwidth;
				while (iw-- > 0)
				{
					*dest++ = *data++;
					data++;
				}
				png_write_row (png_ptr, (png_bytep)iobuf);
			}
		}
		break;

		case MD_GA:
		{
			const uint rowlen = img->xwidth * 2;
			const u8 *data = img->data;
			uint ih = img->height;
			while (ih-- > 0)
			{
				png_write_row (png_ptr, (png_bytep)data);
				data += rowlen;
			}
		}
		break;

		case MD_RGB:
		{
			if (img->xwidth * 3 > sizeof (iobuf))
				goto abort_init;

			const u8 *data = img->data;
			uint ih = img->height;
			while (ih-- > 0)
			{
				u8 *dest = (u8 *)iobuf;
				uint iw = img->xwidth;
				while (iw-- > 0)
				{
					*dest++ = *data++;
					*dest++ = *data++;
					*dest++ = *data++;
					data++;
				}
				png_write_row (png_ptr, (png_bytep)iobuf);
			}
		}
		break;

		case MD_RGBA:
		{
			const uint rowlen = img->xwidth * 4;
			const u8 *data = img->data;
			uint ih = img->height;
			while (ih-- > 0)
			{
				png_write_row (png_ptr, (png_bytep)data);
				data += rowlen;
			}
		}
		break;

		case MD_PAL:
		{
			u8 *dest_end = (u8 *)iobuf + img->xwidth;
			const u16 *data = (u16 *)img->data;
			uint ih = img->height;
			while (ih-- > 0)
			{
				// transform index into single byte
				u8 *dest = (u8 *)iobuf;
				while (dest < dest_end)
					*dest++ = *data++;
				png_write_row (png_ptr, (png_bytep)iobuf);
			}
		}
		break;
	}

	//--- close png and file

	if (setjmp (png_jmpbuf (png_ptr)))
	{
		png_destroy_write_struct (&png_ptr, &info_ptr);
		goto abort;
	}

	png_write_end (png_ptr, info_ptr);
	png_destroy_write_struct (&png_ptr, &info_ptr);

	if (opt_preserve)
		memcpy (&f.fatt, &img->fatt, sizeof (f.fatt));

	if (fo)
		f.f = 0;
	err = ResetFile (&f, opt_preserve);

	if (mipmaps && img->mipmap)
	{
		ccp ext = strrchr (path2, '.');
		ccp file = strrchr (path2, '/');
		if (!ext || file && ext < file)
			ext = path2 + strlen (path2);

		uint count;
		for (count = 1; !err; count++)
		{
			img = img->mipmap;
			if (!img)
				break;

			char mm_path[PATH_MAX];
			snprintf (
				mm_path, sizeof (mm_path), "%.*s.mm%u%s", (int)(ext - path2), path2, count, ext);

			PRINT ("## SAVE -> %s\n", mm_path);
			err = SavePNG (img, false, 0, path1, mm_path, store_alpha, overwrite, file_list);
		}
	}
	return err;

	//--- abort

abort_init:
	ERROR0 (ERR_WRITE_FAILED, "Error while initializing PNG data: %s\n", path);
abort:
	RegisterFileError (&f, ERR_WRITE_FAILED);
	return ResetFile (&f, false);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			ExportPNG			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError ExportPNG (ccp path1, // NULL or part #1 of path
	ccp path2, // NULL or part #2 of path
	FileAttrib_t *fatt, // NULL or file attributes
	const void *img_data, // image data
	uint img_size, // image size, needed for validation
	uint img_index, // index of sub image, 0:main, >0:mipmaps
	bool mipmaps, // true: load and export mipmaps
	uint *n_image, // not null: store detected image count
	const endian_func_t *endian, // endian functions to read data
	FormatFieldItem_t *ffi, // not null: store detected image+pal format
	bool create_png, // false: do some calculations but don't create png
	StringField_t *file_list, // not NULL: store filenames of created png files
	palette_format_t ext_pform, // PAL_INVALID: no external palette
	uint ext_n_pal, const u8 *ext_pal)
{
	DASSERT (img_data);
	DASSERT (endian);

	char pathbuf[PATH_MAX];
	ccp path = PathCatPP (pathbuf, sizeof (pathbuf), path1, path2);
	TRACE ("ExportPNG(size=%u) %s\n", img_size, path);

#if ENABLE_EXPORT_TIMER
	static u64 total_time = 0;
	u64 start_time = GetTimerUSec ();
#endif

	Image_t img;
	enumError err = AssignIMG (&img, 1, img_data, img_size, img_index, mipmaps, endian, path);

	// BRRES TEX0 (and similar containers) carry no palette of their own --
	// AssignIMG() leaves img.pform == PAL_INVALID for an indexed iform in
	// that case. A caller that resolved the sibling PLT0 hands its raw,
	// still-encoded palette bytes through here; only step in when the
	// format actually needs one and AssignIMG() didn't already find one.
	if (!err && ext_pal && img.pform == PAL_INVALID && GetPaletteCountIF (img.iform))
	{
		img.pal = (u8 *)ext_pal;
		img.pal_size = ext_n_pal * 2;
		img.pal_alloced = false;
		img.n_pal = ext_n_pal;
		img.pform = ext_pform;
		img.info_pform = ext_pform;
	}

	if (n_image)
		*n_image = img.info_n_image;
	if (ffi)
	{
		ffi->iform = img.iform;
		ffi->pform = img.pform;
	}
	if (fatt)
		memcpy (&img.fatt, fatt, sizeof (img.fatt));

	if (!err)
	{
#if ENABLE_IMAGE_TYPE_LOG
		{
			static bool log_opened = false;
			static FILE *log = 0;
			if (!log_opened)
			{
				log_opened = true;
				ccp fname
					= IsDirectory ("pool", false) ? "pool/_image-format.log" : "_image-format.log";
				log = fopen (fname, "wb");
				if (log)
					printf (">>> IMAGE LOG OPENED: %s <<<\n", fname);
			}
			if (log)
				fprintf (log, "%02x [%-5s %-6s] : %4u * %4u : %s\n", iform, GetNameFF (0, fform),
					GetImageFormatName (iform, "?"), img.width, img.height, path);
		}
#endif

		if (create_png)
		{
			err = ConvertIMG (&img, false, 0, IMG_X_AUTO, PAL_INVALID);
			if (!err)
				err = SavePNG (&img, mipmaps, 0, path1, path2, 0, false, file_list);

#if ENABLE_EXPORT_TIMER
			start_time = GetTimerUSec () - start_time;
			total_time += start_time;
			printf ("\t\t--> TIME: %s [%s total]\n",
				PrintUSec (0, 0, start_time, ENABLE_EXPORT_TIMER),
				PrintUSec (0, 0, total_time, ENABLE_EXPORT_TIMER));
#endif
		}
	}
	ResetIMG (&img);
	return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		    scan file and image format		///////////////
///////////////////////////////////////////////////////////////////////////////

const KeywordTab_t cmdtab_transform[] = { //--- file formats

	// [[tpl-ex+]]
	{ FF_TPL, "TPL", 0, TM_IDX_FILE | TM_F_PAL },
	{ FF_TPLX, "TPLx", "TPLX", TM_IDX_FILE | TM_F_PAL },
	{ FF_CUPICON, "CUPICON", "CUPICONS", TM_IDX_FILE }, { FF_CUPICON, "CUP", "CUPS", TM_IDX_FILE },
	{ FF_BTI, "BTI", 0, TM_IDX_FILE | TM_F_PAL }, { FF_TEX, "TEX", "TEX0", TM_IDX_FILE },
	{ FF_BREFT_IMG, "BREFT-IMG", "BREFTIMG", TM_IDX_FILE },
	{ FF_BREFT_IMG, "REFT-IMG", "REFTIMG", TM_IDX_FILE },
	{ FF_BREFT_IMG, "BT-IMG", "BTIMG", TM_IDX_FILE }, { FF_PNG, "PNG", 0, TM_IDX_FILE },
	{ FF_AJPG, "AJPG", 0, TM_IDX_FILE },
	{ FF_CTXB, "CTXB", 0, TM_IDX_FILE },

	//--- image formats

	{ IMG_I4, "I4", 0, TM_IDX_IMG }, { IMG_I8, "I8", 0, TM_IDX_IMG },
	{ IMG_IA4, "IA4", 0, TM_IDX_IMG }, { IMG_IA8, "IA8", 0, TM_IDX_IMG },
	{ IMG_RGB565, "RGB565", "R565", TM_IDX_IMG }, { IMG_RGB5A3, "RGB5A3", "R3", TM_IDX_IMG },
	{ IMG_RGBA32, "RGBA32", "RGBA8", TM_IDX_IMG }, { IMG_RGBA32, "R32", "R8", TM_IDX_IMG },
	{ IMG_C4, "C4", "CI4", TM_IDX_IMG | TM_F_PAL }, { IMG_C8, "C8", "CI8", TM_IDX_IMG | TM_F_PAL },
	{ IMG_C14X2, "C14X2", "CI14X2", TM_IDX_IMG | TM_F_PAL }, { IMG_CMPR, "CMPR", 0, TM_IDX_IMG },

	//--- palette formats

	{ PAL_IA8, "PIA8", "P-IA8", TM_IDX_PAL }, { PAL_IA8, "P8", "P-8", TM_IDX_PAL },
	{ PAL_RGB565, "PRGB565", "P-RGB565", TM_IDX_PAL }, { PAL_RGB565, "P565", "P-565", TM_IDX_PAL },
	{ PAL_RGB5A3, "PRGB5A3", "P-RGB5A3", TM_IDX_PAL }, { PAL_RGB5A3, "P3", "P-3", TM_IDX_PAL },

	//--- switch PALETTE

	{ TF_ON, "PALETTE", 0, TM_IDX_PALETTE }, { TF_OFF, "-PALETTE", "NOPALETTE", TM_IDX_PALETTE },

	//--- switch COLOR

	{ TF_ON, "COLOR", 0, TM_IDX_COLOR }, { TF_OFF, "GRAY", "GREY", TM_IDX_COLOR },

	//--- switch ALPHA

	{ TF_ON, "ALPHA", 0, TM_IDX_ALPHA }, { TF_OFF, "-ALPHA", "NOALPHA", TM_IDX_ALPHA },

	//--- end of table

	{ 0, 0, 0, 0 }
};

///////////////////////////////////////////////////////////////////////////////
// [[transform_term_t]]

typedef struct transform_term_t
{
	ccp arg;
	char res[TM_IDX_N];
	uint opt[TM_IDX_N];
} transform_term_t;

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

static enumError ScanTransformKeyword (transform_term_t *term)
{
	DASSERT (term);
	ccp name = term->arg;

	while (*name > 0 && *name <= ' ')
		name++;

	char namebuf[20], *end = namebuf + sizeof (namebuf) - 1, *dest = namebuf;
	while (*name >= '0' && *name <= '9' || *name >= 'a' && *name <= 'z'
		|| *name >= 'A' && *name <= 'Z' || *name == '-')
	{
		if (dest < end)
			*dest++ = *name;
		name++;
	}
	while (*name > 0 && *name <= ' ')
		name++;
	term->arg = name;

	if (dest == namebuf)
		return ERR_OK;
	*dest = 0;

	const KeywordTab_t *cmd = ScanKeyword (0, namebuf, cmdtab_transform);
	if (!cmd)
		return ERROR0 (ERR_SYNTAX, "Invalid keyword for option --transform: %s\n", namebuf);

	uint idx = cmd->opt & TM_IDX_MASK;
	DASSERT (idx < TM_IDX_N);
	term->res[idx] = cmd->id;
	term->opt[idx] = cmd->opt;

	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError ScanTransformTerm (transform_term_t *term, ccp arg)
{
	DASSERT (term);
	memset (term, 0, sizeof (*term));
	memset (term->res, -1, sizeof (term->res));
	if (!arg)
		return ERR_OK;

	for (;;)
	{
		while (*arg > 0 && *arg <= ' ' || *arg == '.')
			arg++;
		term->arg = arg;
		enumError err = ScanTransformKeyword (term);
		if (err)
			return err;
		arg = term->arg;
		if (*arg != '.')
			return ERR_OK;
	}
}

///////////////////////////////////////////////////////////////////////////////

int ScanOptTransform (ccp arg)
{
	n_transform = 0;
	if (!arg)
		return 0;

	for (;;)
	{
		while (*arg > 0 && *arg <= ' ' || *arg == ',')
			arg++;
		if (!*arg)
			break;

		ccp src = 0, dest = arg;
		while (*arg && *arg != ',' && *arg != '=')
			arg++;
		if (*arg == '=')
		{
			src = dest;
			arg++;
			while (*arg > 0 && *arg <= ' ')
				arg++;
			dest = arg;
			while (*arg && *arg != ',' && *arg != '=')
				arg++;
		}

		if (!src && arg == dest)
			goto err_abort;

		transform_term_t dterm;
		enumError err = ScanTransformTerm (&dterm, dest);
		if (err)
			return err;
		if (dterm.arg != arg)
		{
			arg = dterm.arg;
			goto err_abort;
		}
		PRINT0 ("DEST: %d,%d,%d [%u,%u,%u]\n", dterm.res[0], dterm.res[1], dterm.res[2],
			dterm.opt[0], dterm.opt[1], dterm.opt[2]);

		for (;;)
		{
			transform_term_t sterm;
			enumError err = ScanTransformTerm (&sterm, src);
			if (err)
				return err;
			PRINT0 ("SRC: %d,%d,%d [%u,%u,%u]\n", sterm.res[0], sterm.res[1], sterm.res[2],
				sterm.opt[0], sterm.opt[1], sterm.opt[2]);

			if (n_transform == MAX_TRANSFORM)
			{
				ERROR0 (ERR_SYNTAX, "Option --transform: Only %u terms allowed!\n", MAX_TRANSFORM);
				return 1;
			}

			transform_t *t = transform + n_transform++;
			memcpy (t->src, sterm.res, sizeof (t->src));
			memcpy (t->dest, dterm.res, sizeof (t->dest));

			// special case: destionation is FF_CUPICON
			if (t->src[TM_IDX_FILE] == FF_CUPICON)
				t->src[TM_IDX_FILE] = FF_TPLX;
			if (t->dest[TM_IDX_FILE] == FF_CUPICON)
				t->dest[TM_IDX_IMG] = IMG_CMPR;

			src = sterm.arg;
			if (!src)
				break;
			if (*src == '+')
				src++;
			else if (*src == '=')
				break;
			else
			{
				arg = src;
				goto err_abort;
			}
		}
	}
	return 0;

err_abort:
	ERROR0 (ERR_SYNTAX, "Invalid parameter for option --transform: %.20s\n", arg);
	return 1;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

static ccp GetTransformName (uint index, int mode, ccp not_found_text)
{
	if (mode != -1)
	{
		const KeywordTab_t *ct;
		for (ct = cmdtab_transform; ct->name1; ct++)
			if (ct->id == mode && (ct->opt & TM_IDX_MASK) == index)
				return ct->name1;
	}
	return not_found_text;
}

///////////////////////////////////////////////////////////////////////////////

void DumpTransformList (FILE *f, int indent, bool force)
{
	DASSERT (f);
	if (!n_transform)
		return;

	fprintf (f,
		"\n"
		"%*s file        source formats             -> file     destination formats\n"
		"%*s type    image  palette pal  color alph -> type    image  palette pal  color alph\n"
		"%*s----------------------------------------------------------------------------------\n",
		indent, "", indent, "", indent, "");
	uint i;
	for (i = 0; i < n_transform; i++)
	{
		const transform_t *t = transform + i;
		fprintf (f,
			"%*s %-7s %-6s %-7s %-4.4s %-5s %-4.4s"
			" -> %-7s %-6s %-7s %-4.4s %-5s %-4.4s\n",
			indent, "", GetTransformName (0, t->src[0], "*"), GetTransformName (1, t->src[1], "*"),
			GetTransformName (2, t->src[2], "*"), GetTransformName (3, t->src[3], "*"),
			GetTransformName (4, t->src[4], "*"), GetTransformName (5, t->src[5], "*"),
			GetTransformName (0, t->dest[0], "*"), GetTransformName (1, t->dest[1], "*"),
			GetTransformName (2, t->dest[2], "*"), GetTransformName (3, t->dest[3], "*"),
			GetTransformName (4, t->dest[4], "*"), GetTransformName (5, t->dest[5], "*"));
	}
	fprintf (f, "\n");
}

///////////////////////////////////////////////////////////////////////////////

ccp PrintTransformTuple (ccp tuple)
{
	const uint bufsize = 50;
	char *buf = GetCircBuf (bufsize);
	char *dest = buf, *bufend = buf + bufsize - TM_IDX_N;

	uint idx;
	for (idx = 0; idx < TM_IDX_N; idx++)
	{
		if (tuple[idx] != -1)
		{
			ccp name = GetTransformName (idx, tuple[idx], 0);
			if (name)
			{
				*dest++ = '.';
				dest = StringCopyE (dest, bufend++, name);
			}
		}
	}
	*dest = 0;
	return dest == buf ? "*" : buf + 1;
}

///////////////////////////////////////////////////////////////////////////////

ccp PrintFormat3 (file_format_t fform, // file format
	image_format_t iform, // image format
	palette_format_t pform // palette format
)
{
	char tuple[TM_IDX_N];
	memset (tuple, -1, sizeof (tuple));
	tuple[TM_IDX_FILE] = fform == FF_BREFT ? FF_BREFT_IMG : fform;
	if (fform != FF_PNG)
	{
		tuple[TM_IDX_IMG] = iform;
		if (GetPaletteCountIF (iform))
			tuple[TM_IDX_PAL] = pform;
	}
	return PrintTransformTuple (tuple);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		    transformation functions		///////////////
///////////////////////////////////////////////////////////////////////////////

void SetupTransformIMG (Image_t *img)
{
	DASSERT (img);

	if (!img->tform_valid)
	{
		img->tform_valid = true;
		img->tform_exec = false;
		img->tform_gray = false;
		img->tform_noalpha = false;
		img->tform_fform0 = img->tform_fform = FF_INVALID;
		img->tform_iform0 = img->tform_iform = img->iform;
		img->tform_pform0 = img->tform_pform = img->pform;
	}
}

///////////////////////////////////////////////////////////////////////////////

bool Transform2InternIMG (Image_t *img)
{
	SetupTransformIMG (img);

	switch (img->tform_iform)
	{
		case IMG_X_GRAY:
			// Prefer the alpha-less format automatically if the source has
			// no real alpha data: it gives more precision for the same size
			// instead of wasting bits on an alpha channel nobody set.
			img->tform_iform = img->tform_noalpha || CheckAlphaIMG(img,false) < 0
					? IMG_I8 : IMG_IA4;
			return img->tform_exec = true;

		case IMG_X_RGB:
			img->tform_iform = img->tform_noalpha || CheckAlphaIMG(img,false) < 0
					? IMG_RGB565 : IMG_RGB5A3;
			return img->tform_exec = true;

		case IMG_X_PAL4:
			img->tform_iform = IMG_C4;
			return img->tform_exec = true;

		case IMG_X_PAL8:
			img->tform_iform = IMG_C8;
			return img->tform_exec = true;

		case IMG_X_PAL:
		case IMG_X_PAL14:
			img->tform_iform = IMG_C14X2;
			return img->tform_exec = true;

		default:
			return false;
	}
}

///////////////////////////////////////////////////////////////////////////////

bool Transform2XIMG (Image_t *img)
{
	SetupTransformIMG (img);

	switch (img->tform_iform)
	{
		case IMG_I4:
		case IMG_I8:
		case IMG_IA4:
		case IMG_IA8:
			img->tform_iform = IMG_X_GRAY;
			return img->tform_exec = true;

		case IMG_RGB565:
		case IMG_RGB5A3:
		case IMG_RGBA32:
		case IMG_CMPR:
			img->tform_iform = IMG_X_RGB;
			return img->tform_exec = true;

		case IMG_C4:
			img->tform_iform = IMG_X_PAL4;
			return img->tform_exec = true;

		case IMG_C8:
			img->tform_iform = IMG_X_PAL8;
			return img->tform_exec = true;

		case IMG_C14X2:
			img->tform_iform = IMG_X_PAL14;
			return img->tform_exec = true;

		default:
			return false;
	}
}

///////////////////////////////////////////////////////////////////////////////

bool Transform2XRGB (Image_t *img)
{
	SetupTransformIMG (img);
	if (img->tform_iform == IMG_X_RGB)
		return false;

	img->tform_iform = IMG_X_RGB;
	return img->tform_exec = true;
}

///////////////////////////////////////////////////////////////////////////////

bool Transform2PaletteIMG (Image_t *img)
{
	SetupTransformIMG (img);
	noPRINT ("Transform2PaletteIMG(%s)\n", GetImageFormatName (img->tform_iform, "?"));

	switch (img->tform_iform)
	{
		case IMG_I4:
			img->tform_iform = IMG_C4;
			img->tform_pform = PAL_IA8;
			return img->tform_exec = true;

		case IMG_I8:
		case IMG_IA4:
		case IMG_IA8:
			img->tform_iform = IMG_C8;
			img->tform_pform = PAL_IA8;
			return img->tform_exec = true;

		case IMG_RGB565:
			img->tform_iform = IMG_C8;
			img->tform_pform = PAL_RGB565;
			return img->tform_exec = true;

		case IMG_RGB5A3:
		case IMG_CMPR:
			img->tform_iform = IMG_C8;
			img->tform_pform = img->tform_noalpha ? PAL_RGB565 : PAL_RGB5A3;
			return img->tform_exec = true;

		case IMG_RGBA32:
			img->tform_iform = IMG_C14X2;
			img->tform_pform = img->tform_noalpha ? PAL_RGB565 : PAL_RGB5A3;
			return img->tform_exec = true;

		case IMG_X_GRAY:
		case IMG_X_RGB:
			img->tform_iform = IMG_X_PAL;
			img->tform_pform = PAL_X_RGB;
			return img->tform_exec = true;

		default:
			return false;
	}
}

///////////////////////////////////////////////////////////////////////////////

bool Transform2NoPaletteIMG (Image_t *img)
{
	SetupTransformIMG (img);

	switch (img->tform_iform)
	{
		case IMG_C4:
		case IMG_C8:
		case IMG_C14X2:
			img->tform_iform = PaletteToImageFormat (img->tform_pform, IMG_X_RGB);
			return img->tform_exec = true;

		case IMG_X_PAL4:
		case IMG_X_PAL8:
		case IMG_X_PAL14:
		case IMG_X_PAL:
			img->tform_iform = IMG_X_RGB;
			return img->tform_exec = true;

		default:
			return false;
	}
}

///////////////////////////////////////////////////////////////////////////////

bool Transform2GrayIMG (Image_t *img)
{
	SetupTransformIMG (img);
	PRINT ("TRANSFORM GRAY: %s\n", PrintFormat3 (0, img->tform_iform, img->tform_pform));

	switch (img->tform_iform)
	{
		case IMG_RGB565:
		case IMG_RGB5A3:
		case IMG_CMPR:
			img->tform_gray = true;
			img->tform_iform = IMG_I8;
			return img->tform_exec = true;

		case IMG_RGBA32:
			img->tform_gray = true;
			img->tform_iform = img->tform_noalpha ? IMG_I8 : IMG_IA8;
			return img->tform_exec = true;

		case IMG_C4:
		case IMG_C8:
		case IMG_C14X2:
			img->tform_gray = true;
			img->tform_pform = PAL_IA8;
			return img->tform_exec = true;

		case IMG_X_RGB:
		case IMG_X_PAL4:
		case IMG_X_PAL8:
		case IMG_X_PAL14:
		case IMG_X_PAL:
			img->tform_gray = true;
			img->tform_iform = IMG_X_GRAY;
			return img->tform_exec = true;

		default:
			return false;
	}
}

///////////////////////////////////////////////////////////////////////////////

bool Transform2ColorIMG (Image_t *img)
{
	SetupTransformIMG (img);

	switch (img->tform_iform)
	{
		case IMG_I4:
		case IMG_I8:
			img->tform_iform = IMG_RGB565;
			return img->tform_exec = true;

		case IMG_IA4:
		case IMG_IA8:
			img->tform_iform = IMG_RGB5A3;
			return img->tform_exec = true;

		case IMG_C4:
		case IMG_C8:
		case IMG_C14X2:
			if (img->tform_pform != PAL_RGB565)
				img->tform_pform = PAL_RGB5A3;
			return img->tform_exec = true;

		case IMG_X_GRAY:
			img->tform_iform = IMG_X_RGB;
			return img->tform_exec = true;

		default:
			return false;
	}
}

///////////////////////////////////////////////////////////////////////////////

bool Transform2AlphaIMG (Image_t *img)
{
	SetupTransformIMG (img);

	switch (img->tform_iform)
	{
		case IMG_I4:
			img->tform_iform = IMG_IA4;
			return img->tform_exec = true;

		case IMG_I8:
			img->tform_iform = IMG_IA8;
			return img->tform_exec = true;

		case IMG_RGB565:
			img->tform_iform = IMG_RGB5A3;
			return img->tform_exec = true;

		case IMG_C4:
		case IMG_C8:
		case IMG_C14X2:
			if (img->tform_pform != PAL_IA8)
				img->tform_pform = PAL_RGB5A3;
			return img->tform_exec = true;

		default:
			return false;
	}
}

///////////////////////////////////////////////////////////////////////////////

bool Transform2NoAlphaIMG (Image_t *img)
{
	SetupTransformIMG (img);

	switch (img->tform_iform)
	{
		case IMG_IA4:
			img->tform_iform = IMG_I4;
			img->tform_noalpha = true;
			return img->tform_exec = true;

		case IMG_IA8:
			img->tform_iform = IMG_I8;
			img->tform_noalpha = true;
			return img->tform_exec = true;

		case IMG_RGB5A3:
		case IMG_RGBA32:
			img->tform_iform = IMG_RGB565;
			img->tform_noalpha = true;
			return img->tform_exec = true;

		case IMG_C4:
		case IMG_C8:
		case IMG_C14X2:
			if (img->tform_pform != PAL_IA8)
				img->tform_iform = IMG_RGB565;
			img->tform_noalpha = true;
			return img->tform_exec = true;

		case IMG_X_GRAY:
		case IMG_X_RGB:
		case IMG_X_PAL4:
		case IMG_X_PAL8:
		case IMG_X_PAL14:
		case IMG_X_PAL:
			img->tform_noalpha = true;
			return img->tform_exec = true;

		default:
			return false;
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

bool TransformIMG (
	// The image is NOT converted, only the planned transformations
	// are calculated. Use ExecTransformIMG() to execute transformation
	//  -> returns TRUE if a rule match

	Image_t *img, // destination of transforming
	int indent // <0:  no logging
			   // >=0: log transform with indent
)
{
	SetupTransformIMG (img);

	uint i;
	for (i = 0; i < n_transform; i++)
	{
		const transform_t *t = transform + i;
		ccp s = t->src;
		if (s[TM_IDX_FILE] != FF_INVALID && s[TM_IDX_FILE] != img->tform_fform0
			|| s[TM_IDX_IMG] != FF_INVALID && s[TM_IDX_IMG] != img->tform_iform0
			|| s[TM_IDX_PAL] != FF_INVALID && s[TM_IDX_PAL] != img->tform_pform0)
		{
			continue;
		}

		PRINT ("TFORM-1: %s -> %s\n", PrintTransformTuple (t->src), PrintTransformTuple (t->dest));

		const int palette = s[TM_IDX_PALETTE];
		if (palette > 0)
		{
			if ((palette == TF_OFF) == (GetPaletteCountIF (img->iform) != 0))
				continue;
		}

		const int color = s[TM_IDX_COLOR];
		if (color > 0)
		{
			if ((color == TF_ON) == IsGrayIMG (img))
				continue;
		}

		const int alpha = s[TM_IDX_ALPHA];
		if (alpha > 0)
		{
			if ((alpha == TF_ON ? -1 : 1) == CheckAlphaIMG (img, false))
				continue;
		}

		noPRINT (
			"TFORM-2: %s -> %s\n", PrintTransformTuple (t->src), PrintTransformTuple (t->dest));

		if (indent >= 0)
		{
			printf ("%*s- Transform: %s -> %s\n", indent, "", PrintTransformTuple (t->src),
				PrintTransformTuple (t->dest));
		}

		if (t->dest[TM_IDX_PALETTE] == TF_ON)
			Transform2PaletteIMG (img);
		else if (t->dest[TM_IDX_PALETTE] == TF_OFF)
			Transform2NoPaletteIMG (img);

		if (t->dest[TM_IDX_COLOR] == TF_ON)
			Transform2ColorIMG (img);
		else if (t->dest[TM_IDX_COLOR] == TF_OFF)
			Transform2GrayIMG (img);

		if (t->dest[TM_IDX_ALPHA] == TF_ON)
			Transform2AlphaIMG (img);
		else if (t->dest[TM_IDX_ALPHA] == TF_OFF)
			Transform2NoAlphaIMG (img);

		if (t->dest[TM_IDX_FILE] != FF_INVALID)
			img->tform_fform = t->dest[TM_IDX_FILE];

		if (t->dest[TM_IDX_IMG] != IMG_INVALID)
			img->tform_iform = t->dest[TM_IDX_IMG];

		if (t->dest[TM_IDX_PAL] != PAL_INVALID)
			img->tform_pform = t->dest[TM_IDX_PAL];

		return img->tform_exec = true;
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

bool Transform3IMG (Image_t *img, // valid image
	file_format_t fform, // file format
	image_format_t iform, // image format
	palette_format_t pform, // palette format
	bool def_base // define *form values as base
)
{
	SetupTransformIMG (img);
	bool stat = false;

	fform = IsImageFF (fform, true);
	if (fform != FF_UNKNOWN && img->tform_fform != fform)
	{
		stat = true; // do not set img->tform_exec
		img->tform_fform = fform;
	}

	if (iform != IMG_INVALID && img->tform_iform != iform)
	{
		img->tform_exec = stat = true;
		img->tform_iform = iform;
	}

	if (pform != PAL_INVALID && img->tform_pform != pform)
	{
		img->tform_exec = stat = true;
		img->tform_pform = pform;
	}

	if (def_base)
	{
		img->tform_fform0 = fform;
		img->tform_iform0 = iform;
		img->tform_pform0 = pform;
	}

	return stat;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

enumError ExecTransformIMG (Image_t *img // image to transform
)
{
	DASSERT (img);

	enumError err = ERR_OK;
	if (!img->tform_valid || !img->tform_exec)
		goto abort;

	PRINT ("ExecTransformIMG() %s -> %s\n", PrintFormat3 (0, img->iform, img->pform),
		PrintFormat3 (0, img->tform_iform, img->tform_pform));

	if (img->tform_gray && !img->is_grayed)
	{
		err = ConvertIMG (img, false, 0, IMG_X_GRAY, PAL_AUTO);
		if (err)
			goto abort;
	}

	if (img->tform_noalpha && CheckAlphaIMG (img, false) >= 0)
	{
		err = ConvertIMG (img, false, 0, IMG_X_AUTO, PAL_AUTO);
		if (err)
			goto abort;

		switch (img->iform)
		{
			case IMG_X_GRAY:
			{
				uint n = img->xwidth * img->xheight;
				u8 *data = img->data + 1;
				while (n-- > 0)
				{
					*data = 0xff;
					data += 2;
				}
			}
			break;

			case IMG_X_RGB:
			{
				uint n = img->xwidth * img->xheight;
				u8 *data = img->data + 3;
				while (n-- > 0)
				{
					*data = 0xff;
					data += 4;
				}
			}
			break;

			case IMG_X_PAL:
			case IMG_X_PAL4:
			case IMG_X_PAL8:
			case IMG_X_PAL14:
				if (img->n_pal)
				{
					DASSERT (img->pal);
					uint n = img->n_pal;
					u8 *data = img->data + 1;
					while (n-- > 0)
					{
						*data = 0xff;
						data += 2;
					}
				}
				break;

			default:
				return ERROR0 (ERR_INTERNAL, 0);
		}
		img->alpha_status = -1;
	}

	err = ConvertIMG (img, false, 0, img->tform_iform, img->tform_pform);

abort:
	img->tform_valid = false;
	return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    TPL support			///////////////
///////////////////////////////////////////////////////////////////////////////

uint GetNImagesTPL (const u8 *data, // TPL data
	uint data_size, // size of tpl data
	const endian_func_t *endian // endian functions
)
{
	DASSERT (data);
	DASSERT (endian);

	// [[tpl-ex+]]
	return data_size >= sizeof (tpl_header_t) && endian->rd32 (data) == TPL_MAGIC_NUM
		? endian->rd32 (data + 4)
		: 0;
}

///////////////////////////////////////////////////////////////////////////////

bool SetupPointerTPL (const u8 *data, // TPL data
	uint data_size, // size of tpl data
	uint img_index, // index of image to extract
	const tpl_header_t **tpl_head, // not NULL: store pointer here
	const tpl_imgtab_t **tpl_tab, // not NULL: store pointer here
	const tpl_pal_header_t **tpl_pal, // not NULL: store pointer here
	const tpl_img_header_t **tpl_img, // not NULL: store pointer here
	const u8 **pal_data, // not NULL: store pointer to pal data
	const u8 **img_data, // not NULL: store pointer to img data
	const endian_func_t *endian // endian functions
)
{
	DASSERT (data);
	DASSERT (endian);

	// [[tpl-ex+]]
	if (data_size >= sizeof (tpl_header_t) && endian->rd32 (data) == TPL_MAGIC_NUM)
	{
		const tpl_header_t *tpl = (tpl_header_t *)data;
		const uint n_img = endian->rd32 (&tpl->n_image);
		u32 tab_off = endian->rd32 (&tpl->imgtab_off);
		if (tab_off < sizeof (tpl_header_t) || tab_off + n_img * sizeof (tpl_imgtab_t) > data_size)
		{
			// News Channel TPL fix: fallback to default offset
			tab_off = sizeof (tpl_header_t);
		}
		if (img_index < n_img && tab_off + n_img * sizeof (tpl_imgtab_t) <= data_size)
		{
			const tpl_imgtab_t *tab = (tpl_imgtab_t *)(data + tab_off) + img_index;
			const u32 img_off = endian->rd32 (&tab->image_off);
			if (img_off && img_off + sizeof (tpl_img_header_t) <= data_size)
			{
				const tpl_img_header_t *img = (tpl_img_header_t *)(data + img_off);
				const u32 img_data_off = endian->rd32 (&img->data_off);
				if (!img_data_off || img_data_off >= data_size)
					goto abort;

				if (tpl_head)
					*tpl_head = tpl;
				if (tpl_tab)
					*tpl_tab = tab;
				if (tpl_pal)
					*tpl_pal = 0;
				if (tpl_img)
					*tpl_img = img;
				if (pal_data)
					*pal_data = 0;
				if (img_data)
					*img_data = data + img_data_off;

				const u32 pal_off = endian->rd32 (&tab->palette_off);
				if (pal_off)
				{
					if (pal_off > sizeof (tpl_pal_header_t) >= data_size)
						goto abort;

					const tpl_pal_header_t *pal = (tpl_pal_header_t *)(data + pal_off);
					const u32 pal_data_off = endian->rd32 (&pal->data_off);
					if (!pal_data_off || pal_data_off >= data_size)
						goto abort;

					if (tpl_pal)
						*tpl_pal = pal;
					if (pal_data)
						*pal_data = data + pal_data_off;
				}
				return true;
			}
		}
	}

abort:
	if (tpl_head)
		*tpl_head = 0;
	if (tpl_tab)
		*tpl_tab = 0;
	if (tpl_pal)
		*tpl_pal = 0;
	if (tpl_img)
		*tpl_img = 0;
	if (pal_data)
		*pal_data = 0;
	if (img_data)
		*img_data = 0;
	return false;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    BTI support			///////////////
///////////////////////////////////////////////////////////////////////////////

uint GetNImagesBTI (const u8 *data, // bti data
	uint data_size // size of bti data
)
{
	DASSERT (data);
	if (IsValidBTI (data, data_size, 0, 0) >= VALID_ERROR)
		return 0;

	const bti_header_t *bti = (bti_header_t *)data;
	return bti->n_image;
}

///////////////////////////////////////////////////////////////////////////////
#if 0

bool SetupPointerBTI
(
    const u8			* data,		// BTI data
    uint			data_size,	// size of bti data
    uint			img_index,	// index of image to extract
    const bti_header_t		** bti_head,	// not NULL: store pointer here
    const bti_imgtab_t		** bti_tab,	// not NULL: store pointer here
    const bti_pal_header_t	** bti_pal,	// not NULL: store pointer here
    const bti_img_header_t	** bti_img,	// not NULL: store pointer here
    const u8			** pal_data,	// not NULL: store pointer to pal data
    const u8			** img_data,	// not NULL: store pointer to img data
    const endian_func_t		* endian	// endian functions
)
{
    DASSERT(data);
    DASSERT(endian);

    if ( data_size >= sizeof(bti_header_t) && endian->rd32(data) == BTI_MAGIC_NUM )
    {
      const bti_header_t *bti = (bti_header_t*)data;
      const uint n_img = endian->rd32(&bti->n_image);
      const u32 tab_off = endian->rd32(&bti->imgtab_off);
      if ( img_index < n_img && tab_off + n_img*sizeof(bti_imgtab_t) <= data_size )
      {
	const bti_imgtab_t *tab = (bti_imgtab_t*)( data + tab_off ) + img_index;
	const u32 img_off = endian->rd32(&tab->image_off);
	if ( img_off && img_off + sizeof(bti_img_header_t) <= data_size )
	{
	    const bti_img_header_t *img = (bti_img_header_t*)(data+img_off);
	    const u32 img_data_off = endian->rd32(&img->data_off);
	    if ( !img_data_off || img_data_off >= data_size )
		goto abort;

	    if (bti_head) *bti_head = bti;
	    if (bti_tab)  *bti_tab  = tab;
	    if (bti_pal)  *bti_pal  = 0;
	    if (bti_img)  *bti_img  = img;
	    if (pal_data) *pal_data = 0;
	    if (img_data) *img_data = data + img_data_off;

	    const u32 pal_off = endian->rd32(&tab->palette_off);
	    if (pal_off)
	    {
		if ( pal_off > sizeof(bti_pal_header_t) >= data_size )
		    goto abort;

		const bti_pal_header_t *pal = (bti_pal_header_t*)(data+pal_off);
		const u32 pal_data_off = endian->rd32(&pal->data_off);
		if ( !pal_data_off || pal_data_off >= data_size )
		    goto abort;

		if (bti_pal)  *bti_pal = pal;
		if (pal_data) *pal_data = data + pal_data_off;
	    }
	    return true;
	}
      }
    }

 abort:
    if (bti_head) *bti_head = 0;
    if (bti_tab)  *bti_tab  = 0;
    if (bti_pal)  *bti_pal  = 0;
    if (bti_img)  *bti_img  = 0;
    if (pal_data) *pal_data = 0;
    if (img_data) *img_data = 0;
    return false;
}

#endif
//
///////////////////////////////////////////////////////////////////////////////
///////////////			    END				///////////////
///////////////////////////////////////////////////////////////////////////////

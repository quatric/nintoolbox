// BNTX (Switch texture container) -- see lib-bntx.h.
//
// The block-linear address computation is the one given in the Tegra X1
// TRM, matching the reference BNTX tooling; the C here was checked against
// that reference's own swizzle implementation on randomised surfaces.

#include "lib-std.h"
#include "lib-bntx.h"
#include "astc/astc_wrapper.h"
#include "bcn-decoder/bcn_wrapper.h"

#include <math.h>

#define BNTX_MAX_OUTPUT (512u << 20)

static inline u16 brd16 (const u8 *p)
{
	return (u16)p[0] | (u16)p[1] << 8;
}
static inline u32 brd32 (const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}
static inline u64 brd64 (const u8 *p)
{
	return (u64)brd32 (p) | (u64)brd32 (p + 4) << 32;
}

static inline uint div_round_up (uint n, uint d)
{
	return d ? (n + d - 1) / d : 0;
}
static inline uint round_up (uint x, uint y)
{
	return y ? (((x - 1) | (y - 1)) + 1) : x;
}

//-----------------------------------------------------------------------------
///////////////		block-linear addressing			///////////////
//-----------------------------------------------------------------------------

// Tegra X1 TRM block-linear address for element (x,y).
static u64 addr_block_linear (
	uint x, uint y, uint image_width, uint bytes_per_pixel, u64 base_address, uint block_height)
{
	const uint width_in_gobs = div_round_up (image_width * bytes_per_pixel, 64);

	const u64 gob_address = base_address
		+ (u64)(y / (8 * block_height)) * 512 * block_height * width_in_gobs
		+ (u64)(x * bytes_per_pixel / 64) * 512 * block_height
		+ (u64)((y % (8 * block_height)) / 8) * 512;

	const uint xb = x * bytes_per_pixel;
	return gob_address + (u64)((xb % 64) / 32) * 256 + (u64)((y % 8) / 2) * 64
		+ (u64)((xb % 32) / 16) * 32 + (u64)(y % 2) * 16 + (xb % 16);
}

enumError BntxDeswizzle (u8 **dest, uint *dest_size, const u8 *src, uint src_size, uint width,
	uint height, uint blk_w, uint blk_h, uint bpp, uint tile_mode, uint block_height_log2,
	bool round_pitch)
{
	if (!dest || !src || !bpp || block_height_log2 > 5)
		return EINVAL;

	const uint block_height = 1u << block_height_log2;
	const uint w = div_round_up (width, blk_w);
	const uint h = div_round_up (height, blk_h);
	if (!w || !h)
		return EINVAL;

	const u64 linear_size = (u64)w * h * bpp;
	if (linear_size > BNTX_MAX_OUTPUT)
		return EFBIG;

	u64 pitch, surf_size;
	if (tile_mode == 1)
	{
		pitch = (u64)w * bpp;
		if (round_pitch)
			pitch = round_up ((uint)pitch, 32);
		surf_size = pitch * h;
	}
	else
	{
		pitch = round_up (w * bpp, 64);
		surf_size = pitch * round_up (h, block_height * 8);
	}
	if (surf_size > BNTX_MAX_OUTPUT)
		return EFBIG;

	u8 *out = CALLOC (1, (size_t)linear_size);
	if (!out)
		return ERR_CANT_CREATE;

	for (uint y = 0; y < h; y++)
		for (uint x = 0; x < w; x++)
		{
			const u64 pos = tile_mode == 1 ? (u64)y * pitch + (u64)x * bpp
										   : addr_block_linear (x, y, w, bpp, 0, block_height);
			const u64 pos_linear = ((u64)y * w + x) * bpp;

			// The reference skips any element whose swizzled address runs past
			// the surface; those stay zero rather than aborting the decode.
			if (pos + bpp > surf_size || pos + bpp > src_size)
				continue;
			memcpy (out + pos_linear, src + pos, bpp);
		}

	*dest = out;
	if (dest_size)
		*dest_size = (uint)linear_size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////			container parsing		///////////////
//-----------------------------------------------------------------------------

// TextureInfo field offsets, relative to the start of the BRTI block's
// payload (the block header is 16 bytes, the info follows it).
#define TI_TILE_MODE 0x02
#define TI_NUM_MIPS 0x06
#define TI_FORMAT 0x0c
#define TI_WIDTH 0x14
#define TI_HEIGHT 0x18
#define TI_LAYOUT 0x24
#define TI_IMAGE_SIZE 0x40
#define TI_COMP_SEL 0x48
#define TI_NAME_ADDR 0x50
#define TI_PTRS_ADDR 0x60
#define TI_SIZE 0x90

void ResetBNTX (bntx_t *bntx)
{
	if (!bntx)
		return;
	FREE (bntx->textures);
	memset (bntx, 0, sizeof (*bntx));
}

enumError ScanBNTX (bntx_t *bntx, const u8 *data, uint size)
{
	if (!bntx || !data || size < 0x40 || memcmp (data, "BNTX", 4))
		return EINVAL;

	// Main header: magic(8) version(4) bom(2) alignShift(1) targetAddrSize(1)
	// fileNameAddr(4) flag(2) firstBlkAddr(2) relocAddr(4) fileSize(4)
	if (brd16 (data + 12) != 0xFEFF)
		return EINVAL; // big-endian BNTX does not occur in practice
	const uint first_blk = brd16 (data + 22);

	// The texture container ("NX  " target) follows the 32-byte header.
	const uint tc = 0x20;
	if (tc + 0x30 > size)
		return EINVAL;
	const uint count = brd32 (data + tc + 4);
	const u64 info_ptrs_addr = brd64 (data + tc + 8);
	if (!count || count > 0x10000)
		return EINVAL;
	if (info_ptrs_addr + (u64)count * 8 > size)
		return EINVAL;
	(void)first_blk;

	bntx_texture_t *tex = CALLOC (count, sizeof (*tex));
	if (!tex)
		return ERR_CANT_CREATE;

	uint n = 0;
	for (uint i = 0; i < count; i++)
	{
		const u64 blk = brd64 (data + info_ptrs_addr + i * 8);
		if (blk + 16 + TI_SIZE > size)
			continue;
		if (memcmp (data + blk, "BRTI", 4))
			continue;
		const u8 *ti = data + blk + 16;

		const uint w = brd32 (ti + TI_WIDTH), h = brd32 (ti + TI_HEIGHT);
		if (!w || !h || w > 0x10000 || h > 0x10000)
			continue;

		const u64 name_addr = brd64 (ti + TI_NAME_ADDR);
		ccp name = "texture";
		// Names are length-prefixed (u16) strings in the string table.
		if (name_addr && name_addr + 2 < size)
		{
			const uint len = brd16 (data + name_addr);
			if (name_addr + 2 + len < size && !data[name_addr + 2 + len])
				name = (ccp)(data + name_addr + 2);
		}

		const u64 ptrs_addr = brd64 (ti + TI_PTRS_ADDR);
		if (ptrs_addr + 8 > size)
			continue;
		const u64 data_addr = brd64 (data + ptrs_addr);
		const uint image_size = brd32 (ti + TI_IMAGE_SIZE);
		if (!image_size || data_addr + image_size > size)
			continue;

		tex[n].name = name;
		tex[n].width = w;
		tex[n].height = h;
		tex[n].format = brd32 (ti + TI_FORMAT);
		tex[n].comp_sel = brd32 (ti + TI_COMP_SEL);
		tex[n].tile_mode = brd16 (ti + TI_TILE_MODE);
		tex[n].block_height_log2 = brd32 (ti + TI_LAYOUT) & 7;
		tex[n].n_mips = brd16 (ti + TI_NUM_MIPS);
		tex[n].data = data + data_addr;
		tex[n].data_size = image_size;
		n++;
	}

	if (!n)
	{
		FREE (tex);
		return EINVAL;
	}
	memset (bntx, 0, sizeof (*bntx));
	bntx->data = data;
	bntx->size = size;
	bntx->textures = tex;
	bntx->n_textures = n;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////			format decoding			///////////////
//-----------------------------------------------------------------------------

static inline u8 expand5b (uint v)
{
	return (u8)((v << 3) | (v >> 2));
}
static inline u8 expand6b (uint v)
{
	return (u8)((v << 2) | (v >> 4));
}

static u8 float_to_u8 (float value)
{
	if (!(value > 0.0f))
		return 0;
	if (value >= 1.0f)
		return 255;
	return (u8)(value * 255.0f + 0.5f);
}

static float half_to_float (u16 value)
{
	const uint exponent = (value >> 10) & 31, mantissa = value & 1023;
	float result;
	if (!exponent)
		result = mantissa ? ldexpf ((float)mantissa, -24) : 0.0f;
	else if (exponent == 31)
		result = mantissa ? 0.0f : INFINITY;
	else
		result = ldexpf (1.0f + (float)mantissa / 1024.0f, (int)exponent - 15);
	return value & 0x8000 ? -result : result;
}

static float unsigned_float_component (uint value, uint mantissa_bits)
{
	const uint mantissa_mask = (1u << mantissa_bits) - 1;
	const uint exponent = (value >> mantissa_bits) & 31, mantissa = value & mantissa_mask;
	if (!exponent)
		return mantissa ? ldexpf ((float)mantissa, 1 - 15 - (int)mantissa_bits) : 0.0f;
	if (exponent == 31)
		return mantissa ? 0.0f : INFINITY;
	return ldexpf (1.0f + (float)mantissa / (1u << mantissa_bits), (int)exponent - 15);
}

// Decodes one BC1 (DXT1) block to 16 RGBA pixels.
//
// Per the D3D/Khronos spec the c0 <= c1 "punch-through" mode -- three colours
// plus a transparent index 3 -- exists only in BC1. BC2 and BC3 always use
// the four-colour interpretation regardless of how c0 and c1 compare, which
// is why they pass bc1_alpha = false. (Some decoders, including the
// texture2ddecoder library used to check this code, apply BC1's rule to BC3
// as well and treat BC1 itself as fully opaque; in the c0 > c1 regime where
// those interpretations coincide, this implementation matches it exactly on
// randomised blocks.)
void decode_bc1_block (const u8 *b, u8 *out, bool bc1_alpha)
{
	const u16 c0 = brd16 (b), c1 = brd16 (b + 2);
	u8 pal[4][4];
	pal[0][0] = expand5b (c0 >> 11);
	pal[0][1] = expand6b ((c0 >> 5) & 63);
	pal[0][2] = expand5b (c0 & 31);
	pal[0][3] = 255;
	pal[1][0] = expand5b (c1 >> 11);
	pal[1][1] = expand6b ((c1 >> 5) & 63);
	pal[1][2] = expand5b (c1 & 31);
	pal[1][3] = 255;

	if (c0 > c1 || !bc1_alpha)
	{
		for (int i = 0; i < 3; i++)
		{
			pal[2][i] = (u8)((2 * pal[0][i] + pal[1][i]) / 3);
			pal[3][i] = (u8)((pal[0][i] + 2 * pal[1][i]) / 3);
		}
		pal[2][3] = pal[3][3] = 255;
	}
	else
	{
		for (int i = 0; i < 3; i++)
			pal[2][i] = (u8)((pal[0][i] + pal[1][i]) / 2);
		pal[2][3] = 255;
		pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0;
	}

	const u32 bits = brd32 (b + 4);
	for (int i = 0; i < 16; i++)
		memcpy (out + i * 4, pal[(bits >> (2 * i)) & 3], 4);
}

// BC2 (explicit 4-bit alpha) and BC3 (interpolated alpha) share the BC1
// colour half in their second 8 bytes.
void decode_bc2_block (const u8 *b, u8 *out)
{
	decode_bc1_block (b + 8, out, false);
	for (int i = 0; i < 16; i++)
	{
		const u8 byte = b[i / 2];
		const u8 a = (i & 1) ? (byte >> 4) : (byte & 0xF);
		out[i * 4 + 3] = (u8)(a * 17);
	}
}

void decode_bc3_block (const u8 *b, u8 *out)
{
	decode_bc1_block (b + 8, out, false);
	u8 a[8];
	a[0] = b[0];
	a[1] = b[1];
	if (a[0] > a[1])
		for (int i = 0; i < 6; i++)
			a[2 + i] = (u8)(((6 - i) * a[0] + (1 + i) * a[1]) / 7);
	else
	{
		for (int i = 0; i < 4; i++)
			a[2 + i] = (u8)(((4 - i) * a[0] + (1 + i) * a[1]) / 5);
		a[6] = 0;
		a[7] = 255;
	}
	u64 bits = 0;
	for (int i = 0; i < 6; i++)
		bits |= (u64)b[2 + i] << (8 * i);
	for (int i = 0; i < 16; i++)
		out[i * 4 + 3] = a[(bits >> (3 * i)) & 7];
}

void decode_bc4_block (const u8 *b, u8 *out)
{
	u8 a[8];
	a[0] = b[0];
	a[1] = b[1];
	if (a[0] > a[1])
		for (int i = 0; i < 6; i++)
			a[2 + i] = (u8)(((6 - i) * a[0] + (1 + i) * a[1]) / 7);
	else
	{
		for (int i = 0; i < 4; i++)
			a[2 + i] = (u8)(((4 - i) * a[0] + (1 + i) * a[1]) / 5);
		a[6] = 0;
		a[7] = 255;
	}
	u64 bits = 0;
	for (int i = 0; i < 6; i++)
		bits |= (u64)b[2 + i] << (8 * i);
	for (int i = 0; i < 16; i++)
	{
		const u8 v = a[(bits >> (3 * i)) & 7];
		out[i * 4 + 0] = v;
		out[i * 4 + 1] = v;
		out[i * 4 + 2] = v;
		out[i * 4 + 3] = 255;
	}
}

void decode_bc5_block (const u8 *b, u8 *out)
{
	u8 r[8], g[8];
	r[0] = b[0];
	r[1] = b[1];
	if (r[0] > r[1])
		for (int i = 0; i < 6; i++)
			r[2 + i] = (u8)(((6 - i) * r[0] + (1 + i) * r[1]) / 7);
	else
	{
		for (int i = 0; i < 4; i++)
			r[2 + i] = (u8)(((4 - i) * r[0] + (1 + i) * r[1]) / 5);
		r[6] = 0;
		r[7] = 255;
	}
	u64 rbits = 0;
	for (int i = 0; i < 6; i++)
		rbits |= (u64)b[2 + i] << (8 * i);

	const u8 *gb = b + 8;
	g[0] = gb[0];
	g[1] = gb[1];
	if (g[0] > g[1])
		for (int i = 0; i < 6; i++)
			g[2 + i] = (u8)(((6 - i) * g[0] + (1 + i) * g[1]) / 7);
	else
	{
		for (int i = 0; i < 4; i++)
			g[2 + i] = (u8)(((4 - i) * g[0] + (1 + i) * g[1]) / 5);
		g[6] = 0;
		g[7] = 255;
	}
	u64 gbits = 0;
	for (int i = 0; i < 6; i++)
		gbits |= (u64)gb[2 + i] << (8 * i);

	for (int i = 0; i < 16; i++)
	{
		out[i * 4 + 0] = r[(rbits >> (3 * i)) & 7];
		out[i * 4 + 1] = g[(gbits >> (3 * i)) & 7];
		out[i * 4 + 2] = 255;
		out[i * 4 + 3] = 255;
	}
}

static void decode_bc_signed_channel (const u8 *b, u8 *out, uint channel)
{
	int value[8];
	value[0] = (s8)b[0];
	value[1] = (s8)b[1];
	if (value[0] > value[1])
		for (int i = 0; i < 6; i++)
			value[2 + i] = ((6 - i) * value[0] + (1 + i) * value[1]) / 7;
	else
	{
		for (int i = 0; i < 4; i++)
			value[2 + i] = ((4 - i) * value[0] + (1 + i) * value[1]) / 5;
		value[6] = -127;
		value[7] = 127;
	}
	u64 bits = 0;
	for (int i = 0; i < 6; i++)
		bits |= (u64)b[2 + i] << (8 * i);
	for (int i = 0; i < 16; i++)
	{
		int v = value[(bits >> (3 * i)) & 7];
		if (v < -127)
			v = -127;
		out[i * 4 + channel] = (u8)((v + 127) * 255 / 254);
	}
}

void decode_bc4_signed_block (const u8 *b, u8 *out)
{
	memset (out, 0, 64);
	decode_bc_signed_channel (b, out, 0);
	for (int i = 0; i < 16; i++)
	{
		out[i * 4 + 1] = out[i * 4 + 2] = out[i * 4];
		out[i * 4 + 3] = 255;
	}
}

void decode_bc5_signed_block (const u8 *b, u8 *out)
{
	memset (out, 0, 64);
	decode_bc_signed_channel (b, out, 0);
	decode_bc_signed_channel (b + 8, out, 1);
	for (int i = 0; i < 16; i++)
	{
		out[i * 4 + 2] = 255;
		out[i * 4 + 3] = 255;
	}
}

// Format identifiers and ASTC footprints follow aboood40091/BNTX-Extractor.
// BC6H/BC7 use K0lb3/texture2ddecoder's MIT decoder; ASTC uses the existing
// Apache-2.0 drawElements-derived decoder in src/astc.
enumError DecodeBNTX_RGBA (u8 **dest, uint *width, uint *height, const bntx_t *bntx, uint index)
{
	if (!dest || !width || !height || !bntx || index >= bntx->n_textures)
		return EINVAL;
	const bntx_texture_t *t = bntx->textures + index;

	const uint fmt = (t->format >> 8) & 0xFF;
	const uint type = t->format & 0xFF;
	uint bpp = 0, blk_w = 1, blk_h = 1;
	enum
	{
		F_R8,
		F_RG8,
		F_R16,
		F_RGBA8,
		F_BGRA8,
		F_RGB565,
		F_RGB5A1,
		F_RGBA4,
		F_R11G11B10F,
		F_R32F,
		F_BC1,
		F_BC2,
		F_BC3,
		F_BC4,
		F_BC5,
		F_BC6,
		F_BC7,
		F_ASTC
	} kind;

	switch (fmt)
	{
		case 0x02:
			bpp = 1;
			kind = F_R8;
			break;
		case 0x07:
			bpp = 2;
			kind = F_RGB565;
			break; // R5G6B5
		case 0x08:
			bpp = 2;
			kind = F_RGB5A1;
			break; // R5G5B5A1
		case 0x09:
			bpp = 2;
			kind = F_RG8;
			break;
		case 0x0a:
			bpp = 2;
			kind = F_R16;
			break;
		case 0x0b:
			bpp = 4;
			kind = F_RGBA8;
			break; // R8G8B8A8
		case 0x0c:
			bpp = 4;
			kind = F_BGRA8;
			break; // legacy B8G8R8A8
		case 0x05:
			bpp = 2;
			kind = F_RGBA4;
			break; // R4G4B4A4
		case 0x0f:
			bpp = 4;
			kind = F_R11G11B10F;
			break;
		case 0x14:
			bpp = 4;
			kind = F_R32F;
			break;
		case 0x1a:
			bpp = 8;
			blk_w = blk_h = 4;
			kind = F_BC1;
			break;
		case 0x1b:
			bpp = 16;
			blk_w = blk_h = 4;
			kind = F_BC2;
			break;
		case 0x1c:
			bpp = 16;
			blk_w = blk_h = 4;
			kind = F_BC3;
			break;
		case 0x1d:
			bpp = 8;
			blk_w = blk_h = 4;
			kind = F_BC4;
			break;
		case 0x1e:
			bpp = 16;
			blk_w = blk_h = 4;
			kind = F_BC5;
			break;
		case 0x1f:
			bpp = 16;
			blk_w = blk_h = 4;
			kind = F_BC6;
			break;
		case 0x20:
			bpp = 16;
			blk_w = blk_h = 4;
			kind = F_BC7;
			break;
		case 0x2d:
			bpp = 16;
			blk_w = 4;
			blk_h = 4;
			kind = F_ASTC;
			break;
		case 0x2e:
			bpp = 16;
			blk_w = 5;
			blk_h = 4;
			kind = F_ASTC;
			break;
		case 0x2f:
			bpp = 16;
			blk_w = 5;
			blk_h = 5;
			kind = F_ASTC;
			break;
		case 0x30:
			bpp = 16;
			blk_w = 6;
			blk_h = 5;
			kind = F_ASTC;
			break;
		case 0x31:
			bpp = 16;
			blk_w = 6;
			blk_h = 6;
			kind = F_ASTC;
			break;
		case 0x32:
			bpp = 16;
			blk_w = 8;
			blk_h = 5;
			kind = F_ASTC;
			break;
		case 0x33:
			bpp = 16;
			blk_w = 8;
			blk_h = 6;
			kind = F_ASTC;
			break;
		case 0x34:
			bpp = 16;
			blk_w = 8;
			blk_h = 8;
			kind = F_ASTC;
			break;
		case 0x35:
			bpp = 16;
			blk_w = 10;
			blk_h = 5;
			kind = F_ASTC;
			break;
		case 0x36:
			bpp = 16;
			blk_w = 10;
			blk_h = 6;
			kind = F_ASTC;
			break;
		case 0x37:
			bpp = 16;
			blk_w = 10;
			blk_h = 8;
			kind = F_ASTC;
			break;
		case 0x38:
			bpp = 16;
			blk_w = 10;
			blk_h = 10;
			kind = F_ASTC;
			break;
		case 0x39:
			bpp = 16;
			blk_w = 12;
			blk_h = 10;
			kind = F_ASTC;
			break;
		case 0x3a:
			bpp = 16;
			blk_w = 12;
			blk_h = 12;
			kind = F_ASTC;
			break;
		default:
			return ERROR0 (ERR_INVALID_IFORM, "Unsupported BNTX texture format 0x%02x in '%s'\n",
				fmt, t->name);
	}

	u8 *linear = 0;
	uint linear_size = 0;
	enumError err = BntxDeswizzle (&linear, &linear_size, t->data, t->data_size, t->width,
		t->height, blk_w, blk_h, bpp, t->tile_mode, t->block_height_log2, true);
	if (err)
		return err;

	const uint w = t->width, h = t->height;
	if ((u64)w * h > BNTX_MAX_OUTPUT / 4)
	{
		FREE (linear);
		return EFBIG;
	}
	u8 *rgba = CALLOC (1, (size_t)w * h * 4);
	if (!rgba)
	{
		FREE (linear);
		return ERR_CANT_CREATE;
	}

	if (blk_w == 1)
	{
		for (uint y = 0; y < h; y++)
			for (uint x = 0; x < w; x++)
			{
				const u8 *p = linear + ((size_t)y * w + x) * bpp;
				u8 *d = rgba + 4 * ((size_t)y * w + x);
				switch (kind)
				{
					case F_R8:
					{
						int v = type == 2 ? (s8)p[0] : p[0];
						if (v < -127)
							v = -127;
						const u8 c = type == 2 ? (u8)((v + 127) * 255 / 254) : (u8)v;
						d[0] = d[1] = d[2] = c;
						d[3] = 255;
						break;
					}
					case F_RG8:
						if (type == 2)
						{
							int r = (s8)p[0], g = (s8)p[1];
							if (r < -127)
								r = -127;
							if (g < -127)
								g = -127;
							d[0] = (u8)((r + 127) * 255 / 254);
							d[1] = (u8)((g + 127) * 255 / 254);
						}
						else
						{
							d[0] = p[0];
							d[1] = p[1];
						}
						d[2] = 0;
						d[3] = 255;
						break;
					case F_R16:
					{
						const u16 value = brd16 (p);
						u8 c;
						if (type == 5)
							c = float_to_u8 (half_to_float (value));
						else if (type == 2)
						{
							int v = (s16)value;
							if (v < -32767)
								v = -32767;
							c = (u8)(((s64)v + 32767) * 255 / 65534);
						}
						else
							c = (u8)(((u32)value * 255 + 32767) / 65535);
						d[0] = d[1] = d[2] = c;
						d[3] = 255;
						break;
					}
					case F_RGBA8:
						if (type == 2)
							for (int c = 0; c < 4; c++)
							{
								int v = (s8)p[c];
								if (v < -127)
									v = -127;
								d[c] = (u8)((v + 127) * 255 / 254);
							}
						else
							memcpy (d, p, 4);
						break;
					case F_BGRA8:
						d[0] = p[2];
						d[1] = p[1];
						d[2] = p[0];
						d[3] = p[3];
						break;
					case F_RGB565:
					{
						const u16 c = brd16 (p);
						d[0] = expand5b (c >> 11);
						d[1] = expand6b ((c >> 5) & 63);
						d[2] = expand5b (c & 31);
						d[3] = 255;
						break;
					}
					case F_RGB5A1:
					{
						const u16 c = brd16 (p);
						d[0] = expand5b (c >> 11);
						d[1] = expand5b ((c >> 6) & 31);
						d[2] = expand5b ((c >> 1) & 31);
						d[3] = (c & 1) ? 255 : 0;
						break;
					}
					case F_RGBA4:
					{
						const u16 c = brd16 (p);
						d[0] = (u8)(((c >> 12) & 15) * 17);
						d[1] = (u8)(((c >> 8) & 15) * 17);
						d[2] = (u8)(((c >> 4) & 15) * 17);
						d[3] = (u8)((c & 15) * 17);
						break;
					}
					case F_R11G11B10F:
					{
						const u32 value = brd32 (p);
						d[0] = float_to_u8 (unsigned_float_component (value & 0x7ff, 6));
						d[1] = float_to_u8 (unsigned_float_component ((value >> 11) & 0x7ff, 6));
						d[2] = float_to_u8 (unsigned_float_component ((value >> 22) & 0x3ff, 5));
						d[3] = 255;
						break;
					}
					case F_R32F:
					{
						float value;
						memcpy (&value, p, sizeof (value));
						d[0] = d[1] = d[2] = float_to_u8 (value);
						d[3] = 255;
						break;
					}
					default:
						break;
				}
			}
	}
	else if (kind == F_BC6 || kind == F_BC7)
	{
		const int ok = kind == F_BC6
			? szs_decode_bc6 (linear, w, h, type == 2 || type == 0x0b, rgba)
			: szs_decode_bc7 (linear, w, h, rgba);
		if (!ok)
		{
			FREE (rgba);
			FREE (linear);
			return ERR_INVALID_DATA;
		}
	}
	else
	{
		const uint bw = div_round_up (w, blk_w), bh = div_round_up (h, blk_h);
		for (uint by = 0; by < bh; by++)
			for (uint bx = 0; bx < bw; bx++)
			{
				const u8 *blk = linear + ((size_t)by * bw + bx) * bpp;
				u8 px[12 * 12 * 4];
				switch (kind)
				{
					case F_BC1:
						decode_bc1_block (blk, px, true);
						break;
					case F_BC2:
						decode_bc2_block (blk, px);
						break;
					case F_BC3:
						decode_bc3_block (blk, px);
						break;
					case F_BC4:
						type == 2 ? decode_bc4_signed_block (blk, px) : decode_bc4_block (blk, px);
						break;
					case F_BC5:
						type == 2 ? decode_bc5_signed_block (blk, px) : decode_bc5_block (blk, px);
						break;
					case F_ASTC:
						astc_decompress_block (px, blk, blk_w, blk_h);
						break;
					default:
						memset (px, 0, sizeof (px));
						break;
				}
				for (uint iy = 0; iy < blk_h; iy++)
					for (uint ix = 0; ix < blk_w; ix++)
					{
						const uint x = bx * blk_w + ix, y = by * blk_h + iy;
						if (x >= w || y >= h)
							continue;
						memcpy (rgba + 4 * ((size_t)y * w + x), px + 4 * (iy * blk_w + ix), 4);
					}
			}
	}

	// BNTX selectors are 0/1 constants or 2..5 for source R/G/B/A. Applying
	// them after decompression also handles single/dual-channel formats and
	// normal-map channel remaps consistently.
	for (size_t i = 0, count = (size_t)w * h; i < count; i++)
	{
		u8 source[4];
		memcpy (source, rgba + i * 4, 4);
		for (uint channel = 0; channel < 4; channel++)
		{
			uint selector = (t->comp_sel >> (8 * channel)) & 0xff;
			if (!selector)
				selector = channel + 2; // old files may omit identity
			rgba[i * 4 + channel] = selector == 1 ? 255
				: selector >= 2 && selector <= 5  ? source[selector - 2]
												  : 0;
		}
	}

	FREE (linear);
	*dest = rgba;
	*width = w;
	*height = h;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////			format encoding			///////////////
//-----------------------------------------------------------------------------

static inline void bwr16 (u8 *p, u16 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}
static inline void bwr32 (u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}
static inline void bwr64 (u8 *p, u64 v)
{
	bwr32 (p, (u32)v);
	bwr32 (p + 4, (u32)(v >> 32));
}

enumError EncodeBNTX_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, ccp name)
{
	if (!dest || !rgba || !width || !height)
		return EINVAL;

	if (!name || !*name)
		name = "texture";

	uint bh_log2;
	if (height <= 16)
		bh_log2 = 0;
	else if (height <= 32)
		bh_log2 = 1;
	else if (height <= 64)
		bh_log2 = 2;
	else if (height <= 128)
		bh_log2 = 3;
	else
		bh_log2 = 4;

	const uint block_height = 1u << bh_log2;
	const uint bpp = 4;
	const uint pitch = round_up (width * bpp, 64);
	const uint surf_h = round_up (height, block_height * 8);
	const u64 surf_size = (u64)pitch * surf_h;

	if (surf_size > BNTX_MAX_OUTPUT)
		return EFBIG;

	u8 *swizzled = CALLOC (1, (size_t)surf_size);
	if (!swizzled)
		return ERR_CANT_CREATE;

	for (uint y = 0; y < height; y++)
		for (uint x = 0; x < width; x++)
		{
			const u64 pos = addr_block_linear (x, y, width, bpp, 0, block_height);
			if (pos + bpp <= surf_size)
				memcpy (swizzled + pos, rgba + 4 * ((size_t)y * width + x), 4);
		}

	const uint header_size = 0x200;
	const uint file_name_off = 0x100;
	ccp file_name = "output.bntx";
	const uint tex_name_off = 0x140;

	const size_t file_name_len = strlen (file_name);
	const size_t tex_name_len = strlen (name);

	const u64 total_size = (u64)header_size + surf_size;
	if (total_size > BNTX_MAX_OUTPUT)
	{
		FREE (swizzled);
		return EFBIG;
	}

	u8 *buf = CALLOC (1, (size_t)total_size);
	if (!buf)
	{
		FREE (swizzled);
		return ERR_CANT_CREATE;
	}

	// BNTX main header at 0x00
	memcpy (buf, "BNTX\0\0\0\0", 8);
	bwr32 (buf + 0x08, 0x00040000);
	bwr16 (buf + 0x0c, 0xfeff);
	buf[0x0e] = 12;
	buf[0x0f] = 64;
	bwr32 (buf + 0x10, file_name_off);
	bwr16 (buf + 0x14, 0);
	bwr16 (buf + 0x16, 0x20);
	bwr32 (buf + 0x18, 0);
	bwr32 (buf + 0x1c, (u32)total_size);

	// NX header at 0x20
	memcpy (buf + 0x20, "NX  ", 4);
	bwr32 (buf + 0x24, 1);
	bwr64 (buf + 0x28, 0x50); // info_ptrs_addr

	// info_ptrs at 0x50
	bwr64 (buf + 0x50, 0x60); // BRTI offset

	// data_ptrs at 0x58
	bwr64 (buf + 0x58, header_size); // texture data offset

	// BRTI header at 0x60
	memcpy (buf + 0x60, "BRTI", 4);
	bwr32 (buf + 0x64, 0xA0);
	bwr64 (buf + 0x68, 0xA0);

	// TextureInfo at 0x70
	u8 *ti = buf + 0x70;
	ti[0] = 0;
	ti[1] = 2;
	bwr16 (ti + 0x02, 0); // tile_mode = 0
	bwr16 (ti + 0x06, 1); // num_mips = 1
	bwr32 (ti + 0x08, 1); // num_samples = 1
	bwr32 (ti + 0x0c, 0x0b01); // format = RGBA8
	bwr32 (ti + 0x10, 0x20); // access_flags
	bwr32 (ti + 0x14, width);
	bwr32 (ti + 0x18, height);
	bwr32 (ti + 0x1c, 1);
	bwr32 (ti + 0x20, 1);
	bwr32 (ti + 0x24, bh_log2);
	bwr32 (ti + 0x28, 2);
	bwr32 (ti + 0x40, (u32)surf_size);
	bwr32 (ti + 0x44, 512);
	bwr32 (ti + 0x48, 0x05040302); // R,G,B,A selectors
	bwr64 (ti + 0x50, tex_name_off);
	bwr64 (ti + 0x60, 0x58);

	// String pool
	bwr16 (buf + file_name_off, (u16)file_name_len);
	memcpy (buf + file_name_off + 2, file_name, file_name_len);

	bwr16 (buf + tex_name_off, (u16)tex_name_len);
	memcpy (buf + tex_name_off + 2, name, tex_name_len);

	// Texture payload
	memcpy (buf + header_size, swizzled, (size_t)surf_size);
	FREE (swizzled);

	*dest = buf;
	if (dest_size)
		*dest_size = (uint)total_size;
	return ERR_OK;
}

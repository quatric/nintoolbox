// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Retro Studios TXTR textures.
//
// Two unrelated revisions share nothing but the name:
//
//  - "Retro TXTR" (Metroid Prime 1-3, Donkey Kong Country Returns, Wii):
//    a 12-byte big-endian header (u32 GX format 0..0xA, u16 width, u16 height,
//    u32 mipmap count), an optional palette header + palette for the C4/C8/
//    C14X2 formats, then GX-tiled pixel data for each mipmap level.
//    Spec: https://www.metroid2002.com/retromodding/wiki/TXTR_(Metroid_Prime)
//    Encode/decode ground truth: https://github.com/xchellx/txtrtool
//    (MIT; logic re-implemented here against this tree's own GX codec).
//
//  - "Tropical TXTR" (Donkey Kong Country: Tropical Freeze, Wii U):
//    an RFRM form (with "TXTR" form id) holding a HEAD chunk (texture type,
//    format, dimensions, tile mode, swizzle, mipmaps) and a GPU chunk with
//    LZSS-compressed GX2 surface data, plus META buffer descriptors.
//    Spec: https://www.metroid2002.com/retromodding/wiki/TXTR_(Tropical_Freeze)
//    and its Form/Chunk-Descriptor + LZSS_Compression pages.
//    Decoder reference: https://github.com/leamsii/DK-Tropical-Freeze-Model-Extractor
//    (txtr_extractor/txtr_mapper.py + lzz_decompress.py + gtx_extractor/).
//
//  - "MPR TXTR" (Metroid Prime Remastered, Switch, Retro Studios 2023): the
//    exact same RFRM/HEAD/GPU/META shape as Tropical TXTR, but every
//    multi-byte field is little-endian instead of big-endian, and the GPU
//    surface is a Tegra X1 block-linear surface (Switch NVN GPU formats --
//    uncompressed R8/RGBA8, BC1-7, or ASTC -- not GX2). The outer RFRM form
//    additionally carries a reader/writer version pair (47/51) right after
//    the "TXTR" form id that Tropical Freeze files never set this way; that
//    pair, not the (identical) magic/form-id bytes, is what tells the two
//    apart. The META chunk lives inside a trailing "FOOT" sub-form (after an
//    AINF chunk) rather than directly in the outer form, and its buffer
//    table is two parallel arrays (byte-range "read infos" plus compressed-
//    buffer descriptors that index into them) instead of Tropical's single
//    flat array; the LZSS payload format inside each buffer is otherwise
//    byte-identical to Tropical's (mode 0..3, same bit layout).
//    Reference: https://github.com/PrimeDecomp/retrotool
//    (lib/src/format/txtr.rs for the container + format enum,
//    lib/src/util/lzss.rs for the compression, and the vendored
//    tegra_swizzle crate for the block-linear GOB addressing -- the same
//    addressing this tree already implements in lib-bntx.c's
//    addr_block_linear() for BNTX, reused here unchanged).
//
// None of the three revisions has a file magic of its own in the classic
// sense: the old one starts directly with the format word, the other two
// with "RFRM" (and are distinguished from each other only by the
// reader/writer version pair above). All three collide with this tree's
// existing "TXTR"-magic DSB format (Animal Crossing DS menu texture), so
// detection order matters: DSB first (magic), then Tropical (RFRM + TXTR
// form id + BE parse succeeds), then MPR (RFRM + TXTR form id + LE parse
// succeeds), then old Retro (bare-header heuristic).
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_RETRO_TXTR_H
#define SZS_LIB_RETRO_TXTR_H 1

#include "types.h"

//-----------------------------------------------------------------------------
// Old Retro TXTR (Metroid Prime / DKCR, Wii)
//-----------------------------------------------------------------------------

typedef enum retro_txtr_format_t
{
	RETRO_TXTR_I4 = 0, // 4-bit intensity
	RETRO_TXTR_I8 = 1, // 8-bit intensity
	RETRO_TXTR_IA4 = 2, // 4-bit intensity + 4-bit alpha
	RETRO_TXTR_IA8 = 3, // 8-bit intensity + 8-bit alpha
	RETRO_TXTR_C4 = 4, // 4-bit palette index
	RETRO_TXTR_C8 = 5, // 8-bit palette index
	RETRO_TXTR_C14X2 = 6, // 14-bit palette index (unused by official textures)
	RETRO_TXTR_RGB565 = 7, // 16-bit RGB
	RETRO_TXTR_RGB5A3 = 8, // 16-bit RGB + alpha
	RETRO_TXTR_RGBA8 = 9, // 32-bit RGBA
	RETRO_TXTR_CMPR = 10, // 4bpp S3TC-derived compression
} retro_txtr_format_t;

typedef enum retro_txtr_pal_format_t
{
	RETRO_TXTR_PAL_IA8 = 0,
	RETRO_TXTR_PAL_RGB565 = 1,
	RETRO_TXTR_PAL_RGB5A3 = 2,
} retro_txtr_pal_format_t;

// Parsed old-Retro header. PALETTE points into the source buffer; the caller
// keeps the source alive or copies what it needs.
typedef struct retro_txtr_info_t
{
	u32 format; // retro_txtr_format_t
	uint width;
	uint height;
	uint mip_count;
	bool indexed;
	u32 pal_format; // retro_txtr_pal_format_t, valid if indexed
	uint pal_count;
	const u8 *palette; // pal_count BE u16 entries, valid if indexed
	const u8 *pix_data; // first mip level onward
	uint pix_size; // bytes from pix_data to end of file
	uint base_size; // expected bytes of mip level 0
} retro_txtr_info_t;

bool IsRetroTXTR (const u8 *data, uint size);
enumError ScanRetroTXTR (retro_txtr_info_t *info, const u8 *data, uint size);

// Decode mip level 0 to tightly packed width*height RGBA8. Supports every
// format except C14X2 (no official texture uses it; fails cleanly).
enumError DecodeRetroTXTR_RGBA (
	u8 **dest, uint *width, uint *height, const u8 *src, uint src_size);

// Encode one width*height RGBA8 image as a single-mip old-Retro TXTR.
// RETRO_FORMAT is a retro_txtr_format_t; indexed formats (C4/C8/C14X2) are
// rejected — they need palette quantization this encoder does not do.
enumError EncodeRetroTXTR_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width,
	uint height, uint retro_format);

//-----------------------------------------------------------------------------
// Tropical Freeze TXTR (Wii U)
//-----------------------------------------------------------------------------

typedef struct tropical_txtr_info_t
{
	uint tex_type; // 1 = 2D (the only type decoded)
	uint tex_format; // Retro id 0x00..0x21, see the wiki table
	uint width;
	uint height;
	uint depth; // must be 1 to decode
	uint tile_mode;
	uint swizzle_raw; // 0..7, translated to a GX2 swizzle on decode
	uint swizzle; // translated GX2 swizzle value
	uint mip_count;
	uint pitch; // derived (META base alignment / 2), like the reference
	uint alignment;
	uint decomp_size; // total decompressed GPU bytes (all buffers)
	uint comp_size; // total compressed GPU bytes (all buffers)
	const u8 *comp_data; // first compressed buffer
	uint comp_data_size; // bytes available at comp_data
	uint n_buffers;
} tropical_txtr_info_t;

bool IsTropicalTXTR (const u8 *data, uint size);
enumError ScanTropicalTXTR (tropical_txtr_info_t *info, const u8 *data, uint size);

// Decompress (Retro LZSS modes 0..3, zlib fallback) + GX2-detile mip level 0
// to tightly packed width*height RGBA8. Only 2D, depth-1 surfaces decode;
// anything else fails cleanly with EINVAL.
enumError DecodeTropicalTXTR_RGBA (
	u8 **dest, uint *width, uint *height, const u8 *src, uint src_size);

//-----------------------------------------------------------------------------
// Metroid Prime Remastered TXTR (Switch)
//-----------------------------------------------------------------------------

typedef struct mpr_txtr_info_t
{
	uint tex_type; // 1 = 2D (the only type decoded)
	uint tex_format; // retrotool ETextureFormat id (0..84)
	uint width;
	uint height;
	uint depth; // layers; must be 1 to decode
	uint mip_count;
	uint decomp_size; // total decompressed (still block-linear tiled) bytes
	const u8 *meta; // META chunk payload: unk1,unk2,alloc_category,gpu_offset,
		// align,decompressed_size,info_count,info[],buffer_count,buffers[]
	uint meta_size;
} mpr_txtr_info_t;

bool IsMPRTXTR (const u8 *data, uint size);
enumError ScanMPRTXTR (mpr_txtr_info_t *info, const u8 *data, uint size);

// Decompress every buffer (Retro LZSS modes 0..3, byte-identical to
// Tropical's), then Tegra block-linear detile mip level 0 to tightly packed
// width*height RGBA8. Only 2D, depth-1 surfaces decode; anything else fails
// cleanly with EINVAL. Supports the uncompressed R8/RGBA8, BC1-7 and ASTC
// (4x4..12x12) surface formats -- the ones seen in real game textures.
enumError DecodeMPRTXTR_RGBA (
	u8 **dest, uint *width, uint *height, const u8 *src, uint src_size);

#endif

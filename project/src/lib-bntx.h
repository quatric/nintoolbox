#ifndef SZS_LIB_BNTX_H
#define SZS_LIB_BNTX_H 1

#include "types.h"

// BNTX ("Binary NX TeXture"): the Switch texture container, also embedded
// inside BFRES/BFFNT/PTCL. Texture data is stored in the Tegra GPU's
// block-linear layout and has to be deswizzled before it means anything.

// Deswizzles one Tegra block-linear surface into linear order.
//
// WIDTH/HEIGHT are in pixels; BLK_W/BLK_H are the compression block size
// (1x1 for uncompressed formats, 4x4 for BC/ASTC), so the surface is
// measured in DIV_ROUND_UP(width,blk_w) x DIV_ROUND_UP(height,blk_h)
// elements of BPP bytes each. TILE_MODE 1 selects the linear (pitch) layout,
// anything else the block-linear one. BLOCK_HEIGHT_LOG2 comes from the
// texture's layout field and must be 0..5.
//
// Returns a malloc'd linear buffer of
// DIV_ROUND_UP(width,blk_w)*DIV_ROUND_UP(height,blk_h)*bpp bytes.
enumError BntxDeswizzle (u8 **dest, uint *dest_size, const u8 *src, uint src_size, uint width,
	uint height, uint blk_w, uint blk_h, uint bpp, uint tile_mode, uint block_height_log2,
	bool round_pitch);

// User data types supported in BNTX files (matching BntxLibrary)
typedef enum bntx_user_data_type_t
{
	BNTX_UD_INT32 = 0,
	BNTX_UD_SINGLE = 1,
	BNTX_UD_STRING = 2,
	BNTX_UD_BYTE = 3,
	BNTX_UD_WSTRING = 4
} bntx_user_data_type_t;

typedef struct bntx_user_data_t
{
	ccp name;
	bntx_user_data_type_t type;
	uint count;
	union
	{
		const s32 *i32; // points into the source buffer (INT32)
		const float *f32; // points into the source buffer (SINGLE)
		char **str; // owned UTF-8 copies (STRING, array of count offsets)
		const u8 *bytes; // points into the source buffer (BYTE)
		char **wstr; // owned UTF-8 decoded copies (WSTRING, array of count offsets)
	} val;
} bntx_user_data_t;

// Relocation table structs matching NintendoSDK / BntxLibrary (_RLT)
typedef struct bntx_reloc_section_t
{
	u64 pointer;
	u32 offset;
	s32 size;
	s32 first_entry_index;
	s32 entry_count;
} bntx_reloc_section_t;

typedef struct bntx_reloc_entry_t
{
	s32 pointers_offset;
	u16 array_count;
	u8 pointer_count;
	u8 padding_count;
} bntx_reloc_entry_t;

typedef struct bntx_reloc_table_t
{
	u32 offset;
	uint n_sections;
	bntx_reloc_section_t *sections; // owned
	uint n_entries;
	bntx_reloc_entry_t *entries; // owned
} bntx_reloc_table_t;

// One texture inside a BNTX container.
typedef struct bntx_texture_t
{
	ccp name; // points into the source buffer
	uint width, height;
	uint depth;
	uint array_count;
	uint dim;
	uint flags; // raw flags byte (BRTI info +0x00)
	uint swizzle; // raw swizzle value (BRTI info +0x04)
	uint format; // raw BNTX format word
	uint comp_sel; // four component selectors, low byte first
	uint tile_mode, block_height_log2;
	uint alignment; // surface alignment (BRTI info +0x44)
	uint n_mips;
	const u8 *data; // swizzled texture data
	uint data_size;
	u64 *mip_offsets; // relative to data buffer [n_mips]
	uint n_user_data;
	bntx_user_data_t *user_data;
} bntx_texture_t;

typedef struct bntx_t
{
	const u8 *data;
	uint size;
	char platform[5]; // "NX  ", "Ounc", "PC  "
	u16 version_major;
	u8 version_minor;
	u8 version_micro;
	uint n_textures;
	bntx_texture_t *textures; // owned
	bntx_reloc_table_t reloc_table; // owned
} bntx_t;

enumError ScanBNTX (bntx_t *bntx, const u8 *data, uint size);
void ResetBNTX (bntx_t *bntx);

// Decodes texture INDEX (and mip level) to tightly packed RGBA8. Supports all standard
// NintendoSDK/BntxLibrary channel formats and types.
enumError DecodeBNTX_Mip_RGBA (
	u8 **dest, uint *width, uint *height, const bntx_t *bntx, uint index, uint mip_level);
enumError DecodeBNTX_RGBA (u8 **dest, uint *width, uint *height, const bntx_t *bntx, uint index);

// Lossless "native" export in the spirit of BNTX-Extractor: deswizzle the
// Tegra surface of texture INDEX but keep the native block compression,
// wrapping it in a DDS header (EncodeBNTXNativeDDS, for BCn and the
// uncompressed R8/R8G8/RGB565/RGBA8 formats) or in a raw .astc file
// (EncodeBNTXNativeASTC, for ASTC textures) instead of decoding to RGBA8.
// Only single-face 2D textures are supported, like the reference tool.
enumError EncodeBNTXNativeDDS (u8 **dest, uint *dest_size, const bntx_t *bntx, uint index);
enumError EncodeBNTXNativeASTC (u8 **dest, uint *dest_size, const bntx_t *bntx, uint index);

// Returns true when texture INDEX can be exported with EncodeBNTXNativeDDS
// (WANT_DDS is true) or EncodeBNTXNativeASTC (WANT_DDS is false).
bool BntxCanNativeExport (const bntx_t *bntx, uint index, bool want_dds);

// Returns human-readable format string for a BNTX format word (matching BntxLibrary enums).
ccp GetBNTXFormatName (uint format);

// Prints structural inspection of a BNTX container to out.
void DumpStructureBNTX (FILE *out, const bntx_t *bntx, int indent);

// Encodes a single RGBA8 image to a standard Switch BNTX container.
enumError EncodeBNTX_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, ccp name);

// Block-preserving DDS -> BNTX encoder (SourceToBinaryCmd `-bntx` parity).
//
// Converts a DDS file's native blocks (BC1-BC7, RGBA8, including mipmaps) to
// a Switch BNTX container without decoding to RGBA8 first, swizzling each
// mip into the Tegra block-linear layout. Returns ERR_NOTHING_TO_DO when the
// DDS variant isn't suitable for the direct path (uncompressed pixel formats
// outside RGBA8, volume/array textures, ...); the caller should then fall
// back to decode-then-EncodeBNTX_RGBA.
enumError EncodeBNTX_FromDDS (u8 **dest, uint *dest_size, const u8 *dds, uint dds_size, ccp name);

// Multi-texture variant (SourceToBinaryCmd `-bntx` without `--split`):
// combines N DDS sources (each with its own native blocks, format and mip
// chain) into a single BNTX container with N textures. DDS_DATAS[i] points
// to the i-th DDS file image of DDS_SIZES[i] bytes, NAMES[i] gives its
// texture name (NULL/empty -> "textureN"). Unsupported DDS variants decline
// the whole batch with ERR_NOTHING_TO_DO so the caller can fall back to
// per-file RGBA8 encoding; truncated payloads fail with ERR_INVALID_DATA.
// Duplicate texture names are made unique by appending _1, _2, ... like the
// reference tool's RenameDuplicateString.
enumError EncodeBNTX_FromDDSList (u8 **dest, uint *dest_size, const u8 **dds_datas,
	const uint *dds_sizes, ccp const *names, uint n_tex);

// Single-block BC1..BC5 decoders (16 RGBA8 pixels out). Exposed so other
// containers using the same standard block-compression formats (e.g. BFLIM,
// see DecodeFLIM_RGBA in lib-nintendo.c) can reuse them instead of
// reimplementing the same verified block math.
void decode_bc1_block (const u8 *b, u8 *out, bool bc1_alpha);
void decode_bc2_block (const u8 *b, u8 *out);
void decode_bc3_block (const u8 *b, u8 *out);
void decode_bc4_block (const u8 *b, u8 *out);
void decode_bc5_block (const u8 *b, u8 *out);
void decode_bc4_signed_block (const u8 *b, u8 *out);
void decode_bc5_signed_block (const u8 *b, u8 *out);

#endif

#ifndef SZS_LIB_GTX_H
#define SZS_LIB_GTX_H 1

#include "types.h"

// GTX/GSH ("Gfx2" magic): the Wii U GX2 texture/shader container. Textures
// are stored in the Latte GPU's tiled surface layout (AMD-derived
// micro/macro-tile addressing, fixed at 2 pipes / 4 banks on Wii U hardware)
// and have to be detiled before the pixels mean anything -- a different,
// unrelated algorithm from BNTX's Tegra block-linear layout or BFLIM's
// simple 8x8 Morton scheme. See the long comment above GtxDetile() in
// lib-gtx.c for what's verified and what's deliberately left out.

typedef struct gtx_texture_t
{
	uint dim, width, height, depth, num_mips;
	uint format; // raw GX2SurfaceFormat word
	uint aa, use;
	uint tile_mode, swizzle, pitch;
	uint mip_offsets[13];
	uint view_first_mip, view_num_mips, view_first_slice, view_num_slices;
	u8 comp_sel[4];
	const u8 *data; // tiled image data (level 0), points into source
	uint data_size;
	const u8 *mip_data; // packed levels 1..n, points into source
	uint mip_data_size;
} gtx_texture_t;

typedef struct gtx_block_t
{
	uint type, offset, header_size, data_size;
	const u8 *data; // payload, points into source
} gtx_block_t;

typedef enum gtx_shader_stage_t
{
	GTX_SHADER_VERTEX,
	GTX_SHADER_PIXEL,
	GTX_SHADER_GEOMETRY,
	GTX_SHADER_COMPUTE
} gtx_shader_stage_t;

typedef struct gtx_shader_t
{
	gtx_shader_stage_t stage;
	const gtx_block_t *header, *program, *copy_program;
	const u8 *string_table, *relocations;
	uint string_table_size, n_relocations;
} gtx_shader_t;

typedef struct gtx_t
{
	const u8 *data;
	uint size;
	uint version_major, version_minor, gpu_version, alignment;
	uint n_blocks;
	gtx_block_t *blocks; // owned; payloads point into source
	uint n_textures;
	gtx_texture_t *textures; // owned
	uint n_shaders;
	gtx_shader_t *shaders; // owned; block pointers refer to blocks[]
} gtx_t;

enumError ScanGTX (gtx_t *gtx, const u8 *data, uint size);
void ResetGTX (gtx_t *gtx);

// Return the byte offset of one element in a level-0, single-sample GX2
// surface. This is shared by decoding, tiled encoding, and the
// differential AddrLib regression test. UINT64_MAX denotes an unsupported
// tile mode or invalid input.
u64 GetGX2SurfaceOffset (
	uint x, uint y, uint bpp, uint width, uint height, uint tile_mode, uint pitch, uint swizzle);

// Extended address form for array/3D slices, multisampling and depth-order
// micro-tiles. NUM_SAMPLES must be 1, 2, 4 or 8. This covers GX2 tile modes
// 0-15; UINT64_MAX denotes invalid input.
u64 GetGX2SurfaceOffsetEx (uint x, uint y, uint slice, uint sample, uint bpp, uint width,
	uint height, uint num_slices, uint num_samples, uint tile_mode, uint pitch, uint swizzle,
	bool is_depth);

// Decodes texture INDEX (level 0 only) to tightly packed RGBA8. Supports the
// common packed RGB/RGBA/R/RG formats and the BC1-5
// block-compressed ones, tile modes 0/1 (LINEAR_GENERAL/LINEAR_ALIGNED,
// plain row-major), 2/3 (1D_TILED_THIN1/THICK, micro-tiled), and the full
// 2D/2B macro-tiled family 4-11, including aspect ratios 1/2/4, bank swaps,
// and the GX2 pipe/bank swizzle. Use DecodeGX2SurfaceSlice_RGBA() for
// 3D/array/MSAA/depth subresources.
enumError DecodeGTX_RGBA (u8 **dest, uint *width, uint *height, const gtx_t *gtx, uint index);

// Decode a selected mip level. Level 0 is identical to DecodeGTX_RGBA().
// Packed mip offsets and dimensions are interpreted according to GX2Texture.
enumError DecodeGTXMip_RGBA (
	u8 **dest, uint *width, uint *height, const gtx_t *gtx, uint index, uint mip_level);

// Decode an explicit texture subresource, including arrays/cube faces/3D
// slices and MSAA samples. Mip 0 uses image data; higher levels use mipData.
enumError DecodeGTXSubresource_RGBA (u8 **dest, uint *width, uint *height, const gtx_t *gtx,
	uint index, uint mip_level, uint slice, uint sample);

// Same decode, but for a raw GX2Surface not wrapped in a Gfx2/GTX container
// -- e.g. a BFRES FTEX subfile, which embeds the identical surface fields
// (dim/width/height/format/tileMode/pitch) directly in its own header.
enumError DecodeGX2Surface_RGBA (u8 **dest, uint *width, uint *height, uint dim, uint w, uint h,
	uint format, uint tile_mode, uint pitch, uint swizzle, const u8 *data, uint data_size);

// Decode one slice/sample of a 1D/2D/3D/cube/array/MSAA surface. Depth
// formats are visualized as grayscale with stencil, when present, in alpha.
enumError DecodeGX2SurfaceSlice_RGBA (u8 **dest, uint *width, uint *height, uint dim, uint w,
	uint h, uint depth, uint format, uint aa, uint tile_mode, uint pitch, uint swizzle, uint slice,
	uint sample, const u8 *data, uint data_size);

// Lossless textual form of Latte's 64-bit control-flow instructions. The
// disassembly names known CF opcodes and retains both raw words; assembly
// accepts those word0=/word1= fields and reproduces the bytecode exactly.
enumError DisassembleLatteCF (char **text, const u8 *program, uint size);
enumError AssembleLatteCF (u8 **program, uint *size, const char *text);

// One input subresource for the general encoder. DATA is tightly packed in
// row-major element order.  For BC formats an element is one 4x4 BC block;
// otherwise it is one pixel.  Subresources are ordered by mip, then sample,
// then slice.  SIZE is checked exactly, so truncated input is rejected.
typedef struct gtx_encode_level_t
{
	const u8 *data;
	uint size;
	uint slices; // 0 = DEPTH (or max(DEPTH >> mip,1) for a 3D texture)
} gtx_encode_level_t;

// Description of a complete GX2 texture.  PITCH may be zero to select the
// minimum legal pitch for TILE_MODE. NUM_MIPS is limited to 14. DEPTH is the
// array/cube/3D slice count and AA is log2(samples), as in GX2Surface.
// LEVELS must contain NUM_MIPS entries; each entry contains every slice and
// sample of that mip in sample-major, slice-major order. A level can override
// its slice count, primarily for unusual caller-described surface layouts.
typedef struct gtx_encode_texture_t
{
	uint dim, width, height, depth, num_mips;
	uint format, aa, use, tile_mode, swizzle, pitch;
	// Optional for formats not in the built-in conversion table. Element BPP
	// must be byte-aligned; block dimensions default to 1x1. DEPTH_ORDER
	// selects GX2's depth micro-tile bit order.
	uint element_bpp, block_width, block_height;
	bool depth_order;
	uint view_first_mip, view_num_mips, view_first_slice, view_num_slices;
	u8 comp_sel[4];
	const gtx_encode_level_t *levels;
} gtx_encode_texture_t;

// Encode one or more complete GX2 textures into a Gfx2/GTX container. This
// is the lossless/raw-element entry point: built-in and caller-described GX2
// formats, all mip levels, dimensions/slices, samples and tile modes 0-15,
// without imposing an RGBA conversion.
enumError EncodeGTXTextures (
	u8 **dest, uint *dest_size, const gtx_encode_texture_t *textures, uint n_textures);

// Convert tightly packed RGBA8 pixels to any uncompressed format supported
// by the decoder and encode it as a single 2D texture. BC1-5 callers should
// use EncodeGTXTextures() with already compressed blocks, preserving encoder
// quality and signed/unsigned format semantics chosen by the caller.
enumError EncodeGTX_RGBA_Format (u8 **dest, uint *dest_size, const u8 *rgba, uint width,
	uint height, uint format, uint tile_mode);

// Compatibility wrapper: RGBA8, one 2D mip, tile mode 4.
enumError EncodeGTX_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height);

#endif

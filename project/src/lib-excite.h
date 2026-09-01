// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Monster Games Excite Truck / ExciteBots (Wii) asset formats.
//
// Ported from the standalone ExciteExtract research tool (Python), see the
// comments above ScanTEX() and DecodeExciteMSH() for format details.
//-----------------------------------------------------------------------------

#ifndef LIB_EXCITE_H
#define LIB_EXCITE_H

#include "types.h"
#include "lib-model-glb.h"

//-----------------------------------------------------------------------------
///////////////		.tex textures (GX pixel data + footer)		///////////////
//-----------------------------------------------------------------------------

typedef struct excite_tex_t
{
	u8 *rgba; // owned, width*height*4, row-major RGBA8
	uint width;
	uint height;
	uint gx_format; // the recovered GX texture format (0x0..0xE)
	float score; // lower = more confident classification
} excite_tex_t;

void ResetExciteTEX (excite_tex_t *tex);

// Decode one native GameCube/Wii GX tiled texture level. GX_FORMAT accepts
// I4..RGBA8 and CMPR (0..6,14), plus C4/C8 (8,9) when PALETTE is supplied.
// PAL_FORMAT is 0=IA8, 1=RGB565, 2=RGB5A3. The returned buffer is owned.
enumError DecodeGXTexture_RGBA (u8 **dest, uint width, uint height, uint gx_format, const u8 *data,
	uint data_size, const u8 *palette, uint palette_count, uint pal_format);

// Recognise and decode a .tex file. This covers Excite Truck's GX texture
// with trailing dimension footer/no stored format and ExciteBots' explicit
// 128-byte header variant (including I4/IA4 auxiliary-tail resources).
// Returns ERR_OK and
// fills *tex on success; ERR_NOTHING_TO_DO if this is not a recognisable
// Excite .tex payload.
enumError ScanTEX (excite_tex_t *tex, const u8 *data, uint size);

// Same recovery for GUI art (.art/.img), including both the older zero-footer
// representation and ExciteBots' explicit header representation. In the
// older form dimensions and format are recovered from tile-seam continuity.
// Some real
// samples are a colour+stencil pair (decoded as one image twice its real
// height, colour on top, stencil mask below); those are detected and
// recombined into one proper half-height RGBA image before returning -- see
// excite_art_looks_like_mask()/excite_art_recombine() in lib-excite.c.
enumError ScanART (excite_tex_t *tex, const u8 *data, uint size);

// Encode a width*height RGBA8 image to a .tex payload: a full box-filtered
// GX mip chain (capped at 10 levels, matching ScanTEX()'s search bound) in
// 'gx_format', followed by 128 zero-padding bytes. This exploits the
// dl==size-128 fallback that ScanTEX() actually uses on every real retail
// file (its footer field is never populated by the real games -- see the
// long comment above find_tex_footer() in lib-excite.c), so the result
// round-trips exactly through the existing, unmodified decoder. 'width' and
// 'height' must each be one of the standard GX texture dimensions (4..1024,
// power of two). 'gx_format' selects the pixel format (GX_I4 .. GX_CMPR, see
// lib-excite.c); pass -1 to auto-pick one from the image's alpha/greyscale
// content.
enumError EncodeExciteTEX_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, int gx_format);

// Same idea for .art/.img: a single GX level (no mip chain) whose byte
// length is exactly size-128 (a power of two -- guaranteed for standard GX
// dimensions), followed by a mandatory all-zero 128-byte tail (ScanART()
// hard-rejects anything else, unlike ScanTEX()'s footer). Parameters as
// EncodeExciteTEX_RGBA().
enumError EncodeExciteART_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, int gx_format);

// Encode ExciteBots' later, explicit 128-byte-header representation. Unlike
// the legacy encoders above, this layout records dimensions and mip count and
// therefore does not depend on the heuristic legacy classifier. LEVELS must
// be 1..10 and may not extend past the 1x1 level. RENDERER_CODE may be 0 to
// select the canonical code for GX_FORMAT, or an explicit value 0x40..0x4f.
// The special renderer codes 0x40/0x41 are only valid for I4/IA4 respectively
// and append the format's mandatory 1024-byte auxiliary renderer tail.
enumError EncodeExciteHeader_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width,
	uint height, int gx_format, uint levels, uint renderer_code);

// Convenient explicit-header variants used by wimgt. TEX writes a complete
// mip chain (up to the decoder's ten-level bound); ART writes one level.
// Auto format selection favours RGBA32 so the headered path is deterministic
// and lossless. Use EncodeExciteHeader_RGBA() to request I4/IA4 or another
// particular GX storage format.
enumError EncodeExciteTEXHeader_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, int gx_format);
enumError EncodeExciteARTHeader_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, int gx_format);

//-----------------------------------------------------------------------------
///////////////		.msh collision meshes				///////////////
//-----------------------------------------------------------------------------

// Decode a little-endian Monster Games PMsh collision resource: a six-word
// pointer-placeholder/count header followed by 24-byte spatial buckets,
// float32 XYZ positions and 60-byte indexed triangle records. Writes a
// COLLADA .dae to 'out_dae_path'. Returns ERR_NOTHING_TO_DO when the section
// counts, exact file size, or triangle position indices are invalid.
enumError DecodeExciteMSH (const u8 *data, uint size, ccp out_dae_path);

// Encode a model_t (from ParseDAE/ParseGLB) as a little-endian Monster Games
// PMsh collision resource -- the inverse of DecodeExciteMSH. Positions are
// deduplicated, per-triangle face/edge plane values are recomputed with the
// formulas recovered from the retail corpus (see lib-excite.c), and triangles
// are grouped into <=16-triangle buckets with exact bbox spheres. Returns
// ERR_INVALID_DATA for empty/degenerate geometry.
enumError EncodeExciteMSH (const model_t *model, ccp out_path);

//-----------------------------------------------------------------------------
///////////////		.mod 3D models (Monster Games NDL3/NDL2)	///////////////
//-----------------------------------------------------------------------------

// Decode a Monster Games "3LDN"/"2LDN" .mod model (Excite Truck / ExciteBots)
// to a model file beside 'out_path'. The format is chosen by 'out_path's
// extension: GLB by default, or COLLADA .dae if 'out_path' ends in ".dae".
// Geometry (positions + one UV channel, triangle/tristrip/fan/quad
// primitives) is read directly out of the embedded GX display list and its
// vertex-attribute-table register writes -- see the long comment above
// DecodeExciteMOD() in lib-excite.c for the exact format and validation
// against the full retail corpus of both games. Returns ERR_NOTHING_TO_DO
// if 'data' isn't a recognisable NDL3/NDL2 .mod file.
enumError DecodeExciteMOD (const u8 *data, uint size, ccp out_path);

// Encode a parsed model (COLLADA/GLB input, as produced by DecodeExciteMOD())
// as a geometry-only "3LDN" .mod file -- the inverse of DecodeExciteMOD().
// Positions are stored big-endian f32 and texcoords s16/shift-13, exactly the
// conventions found in the retail corpus; triangles are emitted as GX
// TRIANGLES draw calls with 2-byte index tuples. Models with more than 255
// unique positions or texcoords are rejected (GX tuple indices are single
// bytes). The optional texture-filename table of some retail files has an
// unrecovered layout and is not written. See the comment above
// EncodeExciteMOD() in lib-excite.c for details.
enumError EncodeExciteMOD (const model_t *model, ccp out_path);

#endif // LIB_EXCITE_H

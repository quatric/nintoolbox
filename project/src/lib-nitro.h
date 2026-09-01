#ifndef SZS_LIB_NITRO_H
#define SZS_LIB_NITRO_H 1

#include "types.h"
#include "lib-nintendo.h"

// Nintendo DS ("Nitro") graphics, texture, font, and layout subsystems.
// Reference implementations and format research from NitroPaint (Garhoogin).

//-----------------------------------------------------------------------------
// 1. Nitro 2D Sprites & Backgrounds
//-----------------------------------------------------------------------------

// Raw view of NCGR tile pixels (indices, not colours).
typedef struct nitro_ncgr_t
{
	const u8 *tiles; // tile pixel data
	uint tiles_size;
	uint n_tiles;
	uint bpp; // 4 or 8
	bool is_1d; // true: 1D linear mapping, false: 2D sheet mapping
	uint mapping_shift; // 0: 32K, 1: 64K, 2: 128K, 3: 256K
	uint tiles_x; // sheet width in tiles (for 2D mapping)
} nitro_ncgr_t;

// Palette entries as RGBA8, 16 colours per palette bank for 4bpp.
typedef struct nitro_nclr_t
{
	u8 *rgba; // 4 bytes per entry (owned)
	uint n_entries;
} nitro_nclr_t;

// Screen tilemap entries for background layouts.
typedef struct nitro_nscr_t
{
	const u8 *data;
	uint data_size;
	uint width; // in pixels
	uint height; // in pixels
	uint color_mode; // 0: 4bpp (16-color), 1: 8bpp (256-color)
	uint bg_type; // 0: Text, 1: Affine, 2: Extended
} nitro_nscr_t;

enumError ScanNitroNCGR (nitro_ncgr_t *ncgr, const u8 *data, uint size);
enumError ScanNitroNCLR (nitro_nclr_t *nclr, const u8 *data, uint size);
void ResetNitroNCLR (nitro_nclr_t *nclr);
enumError ScanNitroNSCR (nitro_nscr_t *nscr, const u8 *data, uint size);

// Renders one NCER cell into a tightly packed RGBA8 image.
enumError RenderNCERCell (u8 **dest, uint *width, uint *height, int *ox, int *oy,
	const nintendo_ncer_t *ncer, uint cell_index, const nitro_ncgr_t *ncgr,
	const nitro_nclr_t *nclr);

// Renders an NSCR screen tilemap into a tightly packed RGBA8 image using
// tile data from NCGR and palette from NCLR.
enumError RenderNSCR (u8 **dest, uint *width, uint *height, const nitro_nscr_t *nscr,
	const nitro_ncgr_t *ncgr, const nitro_nclr_t *nclr);

// Hudson / IS-Nitro / Asobimashou 2D format parsers:
enumError ScanHudsonNCL (nitro_nclr_t *nclr, const u8 *data, uint size);
enumError ScanHudsonNCG (nitro_ncgr_t *ncgr, const u8 *data, uint size);
enumError ScanHudsonNSC (nitro_nscr_t *nscr, const u8 *data, uint size);
enumError ScanISPltt (nitro_nclr_t *nclr, const u8 *data, uint size);
enumError ScanISChar (nitro_ncgr_t *ncgr, const u8 *data, uint size);
enumError ScanISScreen (nitro_nscr_t *nscr, const u8 *data, uint size);

//-----------------------------------------------------------------------------
// 2. Nitro 3D Texture Archives (NSBTX / BTX0 & NSBMD / BMD0 TEX0 Block)
//-----------------------------------------------------------------------------

typedef enum nitro_texfmt_t
{
	NITRO_TEXFMT_NONE    = 0,
	NITRO_TEXFMT_A3I5    = 1, // 3-bit alpha, 5-bit palette index (32 colors)
	NITRO_TEXFMT_PLTT4   = 2, // 2-bit palette index (4 colors)
	NITRO_TEXFMT_PLTT16  = 3, // 4-bit palette index (16 colors)
	NITRO_TEXFMT_PLTT256 = 4, // 8-bit palette index (256 colors)
	NITRO_TEXFMT_TEX4x4  = 5, // 4x4 texel compressed
	NITRO_TEXFMT_A5I3    = 6, // 5-bit alpha, 3-bit palette index (8 colors)
	NITRO_TEXFMT_DIRECT  = 7  // 16-bit direct color (BGR555 + 1-bit alpha)
} nitro_texfmt_t;

typedef struct nitro_tex_entry_t
{
	char name[17];
	uint width;
	uint height;
	nitro_texfmt_t format;
	bool col0_trans;
	const u8 *texels;
	uint texels_size;
	const u8 *comp_idx; // for TEX4x4 palette indexing
	uint comp_idx_size;
} nitro_tex_entry_t;

typedef struct nitro_pltt_entry_t
{
	char name[17];
	uint n_colors;
	const u8 *raw_data;
	u8 *rgba; // converted RGBA8 table (owned by struct)
} nitro_pltt_entry_t;

typedef struct nitro_tex0_t
{
	const u8 *raw_data;
	uint raw_size;

	nitro_tex_entry_t *textures;
	uint n_textures;

	nitro_pltt_entry_t *palettes;
	uint n_palettes;
} nitro_tex0_t;

void InitializeNitroTEX0 (nitro_tex0_t *tex0);
void ResetNitroTEX0 (nitro_tex0_t *tex0);
enumError ScanNitroTEX0 (nitro_tex0_t *tex0, const u8 *data, uint size);

// Decodes a specific texture index from TEX0 into RGBA8 buffer.
enumError DecodeNitroTexture_RGBA (u8 **dest, uint *width, uint *height,
	const nitro_tex0_t *tex0, uint tex_idx, int pltt_idx);

// Decodes the first/primary texture in a standalone NSBTX / NSBMD file.
enumError DecodeNSBTX_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, uint size);

// Creates a complete binary NSBTX (BTX0) archive from RGBA image pixels.
enumError CreateNSBTX (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height,
	nitro_texfmt_t fmt, ccp tex_name, ccp pltt_name);

// Single texture formats from NitroPaint:
enumError Decode5TX_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size);
enumError Encode5TX_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height);
enumError DecodeSPT_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size);
enumError EncodeSPT_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height);
enumError DecodeNTGA_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size);

//-----------------------------------------------------------------------------
// 3. Nitro Font Formats (NFTR / BNFR)
//-----------------------------------------------------------------------------

typedef struct nitro_nftr_glyph_t
{
	u16 char_code;
	u16 glyph_index;
	u8 width, advance;
} nitro_nftr_glyph_t;

typedef struct nitro_nftr_t
{
	uint version;
	bool is_bnfr;
	uint cell_w, cell_h;
	uint max_advance;
	uint bpp; // 1, 2, 4, 8
	uint linefeed;
	uint n_glyphs;
	const u8 *glyph_data;
	uint glyph_data_size;

	nitro_nftr_glyph_t *glyphs;
	uint n_mapped_glyphs;
} nitro_nftr_t;

void InitializeNitroNFTR (nitro_nftr_t *nftr);
void ResetNitroNFTR (nitro_nftr_t *nftr);
enumError ScanNitroNFTR (nitro_nftr_t *nftr, const u8 *data, uint size);

// Decodes NFTR/BNFR into a consolidated PNG font atlas + XML metrics descriptor.
enumError DecodeNFTR_Atlas (u8 **dest_atlas, uint *atlas_w, uint *atlas_h,
	char **dest_xml, const u8 *data, uint size);

// Encodes PNG font atlas + XML metrics descriptor into binary NFTR / BNFR file.
enumError EncodeNFTR_Atlas (u8 **dest, uint *dest_size,
	const u8 *atlas_rgba, uint atlas_w, uint atlas_h, ccp xml_str, bool is_bnfr);

//-----------------------------------------------------------------------------
// 4. Nitro 2D Layout Formats (BNLL / BNCL / BNBL)
//-----------------------------------------------------------------------------

enumError DecodeBNLL_Text (char **dest_text, const u8 *data, uint size);
enumError EncodeBNLL_Text (u8 **dest, uint *dest_size, ccp text);

enumError DecodeNCER_Text (char **dest_text, const u8 *data, uint size);
enumError EncodeNCER_Text (u8 **dest, uint *dest_size, ccp text);

enumError DecodeNANR_Text (char **dest_text, const u8 *data, uint size);
enumError EncodeNANR_Text (u8 **dest, uint *dest_size, ccp text);

#endif // SZS_LIB_NITRO_H

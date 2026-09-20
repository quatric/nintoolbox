// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Smart Bomb Interactive "BombShell" engine data packs (Bee Movie Game:
// Wii .xwi, PC .xdx9; the same .x* family holds Hot Wheels Velocity X, Pac-Man
// World Rally, Snoopy vs. the Red Baron and Bigfoot). Layout found from the
// Pac-Kart project (github.com/Pac-Kart) and checked against the Wii and PC
// discs of Bee Movie Game.
//
// The file is one memory image: u32 0x020100a0, u32 0x040100af, u32 version
// (0x138), u32 directory count, then 24-byte directories
//   {u32 ?, u32 type (1 interface, 3 link, 4 world), u32, u32, u32 size,
//    u32 datapack offset}
// where the datapack sits at 16 + 24 * count + offset. Wii files are
// big-endian with 32-byte alignment; PC files little-endian, unaligned.
// A datapack starts with a header (192 bytes on Wii, 188 on PC):
//   +4 sound bytes, +8 sound table words, +20 texture count, +32 sound count,
//   +108 word list length.
// Sounds: a table of {u32 record, u32} at the header end, records (relative to
// the table end E1 = table + aligned(+8 * 4)) hold {u32 data offset, u32 size,
// u32 sample rate, u32 flags}; the data is at E1 + offset (a whole FSB3 bank on
// Wii, a whole RIFF WAVE on PC) with its NUL-terminated source path right
// behind it. E is E1 + aligned(sound bytes) + aligned(+108 * 4); textures are
// a u32 offset list at E, each a 32-byte record at E + offset:
//   u8 x4, u8 format, u8 mip count - 1, u8 log2 width, u8 log2 height,
//   u32 pixels, u32 aux, u32 name, u32 alpha pixels
// (all offsets relative to E). Wii formats 0x45 / 0xc5 / 0xc6 are CMPR; 0xc5
// and 0xc6 keep the alpha as a second CMPR image (its red channel). PC formats
// are 0x45 DXT1, 0xca DXT5, 0xa0 BGRA8 and 0x18 BGR8. Only the base level is
// decoded. Wii models: the model patch list (+56 entries {u32 pointer, u16, u16}
// at datapack + u32(+0) + m, with m = patch list offset - u32(+0)) leads to
// {u16 ?, u16 ?, u32 name, ..., +16 list count, +20 list of {count, pointer
// array}} model records; a sub-mesh record (224 bytes, type u32(+0) 0 / 1) has
// vertex flags u32(+0xb0) (bit 0 position, 1 normal, 2 colour, 3 UV: one BE u16
// index each per vertex, in that order), float positions +180, colours +188
// (type 0) / +184 (type 1), float normals +192, s16 8.8 UVs +196 and +200
// {0, byte count, pointer} a GX display list {command, BE u16 count, vertices}.
// World data and the PC (.xdx9) models are not decoded.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_BOMBSHELL_H
#define SZS_LIB_BOMBSHELL_H 1

#include "lib-std.h"
#include "lib-model-glb.h"

typedef enum bombshell_kind_t
{
	BSA_TEXTURE,
	BSA_SOUND,
} bombshell_kind_t;

typedef struct bombshell_asset_t
{
	bombshell_kind_t kind;
	uint dir;		// directory index
	uint dir_type;		// directory type
	char name[96];		// unique, file-system safe, no extension
	char ext[8];		// sound extension: fsb / wav / bin
	u32 off, size;		// texture pixels / sound data (absolute)
	u32 off_alpha, off_aux;	// textures: alpha image and aux blob (absolute, 0 = none)
	uint width, height, format;
	uint index;		// textures: index within its directory
	bool big_endian;
} bombshell_asset_t;

// Wii models: NAME receives the model name; one mesh per sub-mesh of LOD 0.
uint CountBombshellModels (const u8 *d, size_t size);
// ASSETS (from ListBombshell) name the textures the sub-meshes refer to (patch list
// entry for the u32 at sub-mesh +48 / +56: its second u16 is the texture index).
// DIR / DIR_TYPE receive the directory of the model; textures are referenced as
// ../textures/NAME (models are written to DIR/models next to DIR/textures).
model_t *BuildBombshellModel (const u8 *d, size_t size, uint index, char *name, size_t name_size,
	const bombshell_asset_t *assets, uint n_assets, uint *dir, uint *dir_type);

bool IsBombshellPack (const u8 *d, size_t size);

// Malloc-owned list of all textures and sounds, or 0.
bombshell_asset_t *ListBombshell (const u8 *d, size_t size, uint *count);

// Decode a texture's base level to RGBA8 (malloc-owned).
enumError DecodeBombshellTexture (u8 **rgba, const u8 *d, size_t size, const bombshell_asset_t *a);

#endif

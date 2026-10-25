// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_MPH_H
#define LIB_MPH_H 1

#include "lib-nintendo.h"
#include "lib-model-glb.h"

// Metroid Prime Hunters (Nintendo DS). Format knowledge after NoneGiven's
// MphRead (https://github.com/NoneGiven/MphRead).

// Sound/asset archive ("SNDFILE\0", big-endian header: u32 file count, u32
// total size, 16 reserved bytes, then 64-byte records {char name[32], u32
// offset, u32 padded size, u32 size, 20 reserved}; payloads 32-byte aligned).
bool IsMphArchive (const u8 *data, uint size);
enumError ScanMphArchive (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

// Model (*_Model.bin): 100-byte header, 240-byte nodes, 132-byte materials,
// 4-byte meshes, 32-byte display-list records pointing at packed Nitro GX
// command streams, 40-byte texture and 16-byte palette records. The structure
// is validated by bounds only (the file carries no magic).
bool IsMphModel (const u8 *data, uint size);

// Parses MPH_MODEL into a model_t. Textures live in the model itself or in a
// separate *_Tex.bin of the same layout (TEX / TEX_SIZE, optional). When
// OUT_PNG_DIR is set, every (texture, palette) pair a material uses is written
// there as a PNG named <stem>_t<N>_p<M>.png and referenced by the materials.
model_t *ParseMphModel (const u8 *data, uint size, const u8 *tex, uint tex_size,
	ccp out_png_dir, ccp stem);

// Writes ParseMphModel's model as GLB with its PNG textures beside it.
enumError DecodeMphModel (const u8 *data, uint size, const u8 *tex, uint tex_size, ccp out_path);

// Reads the *_Tex.bin that sits beside MODEL_PATH (<name>_Model.bin ->
// <name>_Tex.bin). Returns a malloc'd buffer or NULL when there is none.
u8 *LoadMphTexFile (ccp model_path, uint *size);

#endif // LIB_MPH_H

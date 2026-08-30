#ifndef SZS_LIB_BCH_H
#define SZS_LIB_BCH_H 1

#include "types.h"

// BCH ("BCH\0") -- Nintendo's CTR H3D container: the 3DS runtime format for
// models, materials, shaders, textures, lookup tables and animations.
//
// This is NOT the same format as CGFX/BCRES despite both being 3DS graphics
// containers; they have different magics and different internal layouts.
//
// Every pointer in a BCH is stored unrelocated -- the raw file holds 0 or a
// section-relative value, and a relocation table at the end of the file
// carries the fixups. Nothing in the file can be followed until those have
// been applied, which is what ScanBCH does to its private copy of the data.

typedef enum bch_dict_id_t
{
	BCH_MODELS,
	BCH_MATERIALS,
	BCH_SHADERS,
	BCH_TEXTURES,
	BCH_LUTS,
	BCH_LIGHTS,
	BCH_CAMERAS,
	BCH_FOGS,
	BCH_SKELETAL_ANIMS,
	BCH_MATERIAL_ANIMS,
	BCH_VISIBILITY_ANIMS,
	BCH_LIGHT_ANIMS,
	BCH_CAMERA_ANIMS,
	BCH_FOG_ANIMS,
	BCH_N_DICTS
} bch_dict_id_t;

typedef struct bch_entry_t
{
	ccp name; // into the relocated buffer, never NULL
	u32 address; // absolute offset of the entry's data
} bch_entry_t;

typedef struct bch_dict_t
{
	uint n;
	bch_entry_t *entries; // owned
} bch_dict_t;

typedef struct bch_t
{
	u8 *data; // relocated copy of the whole file (owned)
	uint size;
	uint bc, fc; // backward/forward compatibility versions
	u32 contents_addr, strings_addr, commands_addr;
	u32 raw_data_addr, raw_ext_addr, reloc_addr;
	u32 contents_len, strings_len, commands_len;
	u32 raw_data_len, raw_ext_len, reloc_len;
	bch_dict_t dict[BCH_N_DICTS];
} bch_t;

ccp GetBCHDictName (bch_dict_id_t id);
void ResetBCH (bch_t *bch);
// Copies DATA, applies the relocation table to the copy, then reads the
// content dictionaries. The original buffer is not modified.
enumError ScanBCH (bch_t *bch, const u8 *data, uint size);

// Extracts the first model's geometry. BCH describes its vertex buffers and
// index buffers with PICA200 GPU command lists rather than plain structs, so
// this walks those command streams to recover the attribute layout, vertex
// stride and indices. Returns NULL when nothing could be decoded.
// Returns a model_t* (lib-model-dae.h); typed as void* here so this header
// stays independent of the model layer. Release it with FreeModel().
void *ParseBCH (const u8 *data, uint size);

// Decodes the texture at index `tex_idx` in `bch->dict[BCH_TEXTURES]` into an RGBA8 buffer.
enumError DecodeBCHTexture (u8 **dest, uint *width, uint *height, const bch_t *bch, uint tex_idx);

// Exports all textures in the BCH container as PNGs to dest_path_or_dir.
// If dest_path_or_dir points to a .dae file, textures are placed in the same directory.
enumError ExportBCHTextures (const bch_t *bch, const char *dest_path_or_dir);
enumError ExportBCHTexturesFromData (const u8 *data, uint size, const char *dest_path_or_dir);

#endif

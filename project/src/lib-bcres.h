#ifndef LIB_BCRES_H
#define LIB_BCRES_H

#include "lib-model-glb.h"
#include "lib-std.h"
#include <stdint.h>
#include <stddef.h>

// Parses a CGFX model's skeleton, geometry, materials and texture bindings into a model_t.
model_t *ParseBCRES (const uint8_t *data, size_t size);

// CGFX ("BCRES") container enumeration: the DATA block holds a series of
// (count, DICT offset) pairs, one per resource kind. All offsets in a CGFX
// are self-relative.
typedef struct cgfx_entry_t
{
	const char *name;
	uint32_t address;
} cgfx_entry_t;
typedef struct cgfx_dict_t
{
	unsigned n;
	cgfx_entry_t *entries;
} cgfx_dict_t;

#define CGFX_N_DICTS 16
#define CGFX_DICT_MODELS 0
#define CGFX_DICT_TEXTURES 1
#define CGFX_DICT_LUTS 2
#define CGFX_DICT_MATERIALS 3
#define CGFX_DICT_SHADERS 4
#define CGFX_DICT_CAMERAS 5
#define CGFX_DICT_LIGHTS 6
#define CGFX_DICT_FOGS 7
#define CGFX_DICT_SCENES 8
#define CGFX_DICT_SKELETAL_ANIM 9
#define CGFX_DICT_MATERIAL_ANIM 10
#define CGFX_DICT_VISIBILITY_ANIM 11
#define CGFX_DICT_CAMERA_ANIM 12
#define CGFX_DICT_LIGHT_ANIM 13
#define CGFX_DICT_FOG_ANIM 14
#define CGFX_DICT_EMITTERS 15

typedef struct cgfx_t
{
	const uint8_t *data;
	size_t size;
	uint32_t revision;
	cgfx_dict_t dict[CGFX_N_DICTS];
} cgfx_t;

const char *GetCGFXDictName (int id);
void ResetCGFX (cgfx_t *cgfx);
int ScanCGFX (cgfx_t *cgfx, const uint8_t *data, size_t size);

enumError DecodeCGFXTexture (
	u8 **dest, uint *width, uint *height, const cgfx_t *cgfx, uint tex_idx);
enumError ExportBCRESTextures (const cgfx_t *cgfx, const char *dest_path_or_dir);
enumError ExportBCRESTexturesFromData (const u8 *data, size_t size, const char *dest_path_or_dir);

#endif

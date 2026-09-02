#ifndef SZS_LIB_NUT_H
#define SZS_LIB_NUT_H 1

#include "types.h"
#include "file-type.h"

// Namco Texture container format (.nut / NTP3 / NTWU) used in Super Smash Bros. for 3DS
// and Super Smash Bros. for Wii U.

typedef struct nut_texture_t
{
	u32 total_size;
	u32 palette_size;
	u32 data_size;
	u16 header_size;
	u16 name_len;
	u16 width;
	u16 height;
	u32 num_mips;
	u32 pixel_format;
	u32 data_offset;
	u32 offset;
	u32 hash;
	u16 mipmaps;
	const u8 *data;
	char name[128];
} nut_texture_t;

typedef struct nut_t
{
	const u8 *data;
	size_t size;
	bool is_big_endian;
	u16 version;
	u16 count;
	u16 n_textures;
	nut_texture_t *textures;
} nut_t;

// Returns true if 'data' has a valid NTP3 / NTWU / NUT signature.
bool IsNUT (const u8 *data, size_t size);

// Scans and parses the NUT container.
enumError ScanNUT (nut_t *nut, const u8 *data, size_t size);
int ParseNUT (nut_t *nut, const uint8_t *data, size_t size);

// Frees allocated textures in nut.
void ResetNUT (nut_t *nut);
void FreeNUT (nut_t *nut);

// Extracts a single texture as DDS or raw binary payload.
enumError ExtractNUTTexture (const nut_t *nut, uint index, u8 **dest, size_t *dest_size, char *ext_out, size_t ext_max);

// Decodes a NUT texture to RGBA32 bitmap.
int DecodeNUTTextureToRGBA (const nut_texture_t *tex, uint8_t **out_rgba, uint32_t *out_w, uint32_t *out_h);

// Creates a basic uncompressed NUT (NTP3) container from RGBA8 or raw texture streams.
enumError CreateNUT (u8 **dest, size_t *dest_size, uint n_textures,
	const u16 *widths, const u16 *heights, const u32 *formats, const u8 *const *tex_data, const size_t *tex_sizes);

#endif // SZS_LIB_NUT_H

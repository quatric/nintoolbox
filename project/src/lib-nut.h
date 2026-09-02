#ifndef LIB_NUT_H
#define LIB_NUT_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
	uint32_t total_size;
	uint32_t data_size;
	uint16_t header_size;
	uint16_t width;
	uint16_t height;
	uint32_t pixel_format;
	uint32_t offset;
	uint32_t hash;
	uint16_t mipmaps;
	const uint8_t *data;
} nut_texture_t;

typedef struct
{
	uint16_t version;
	uint16_t count;
	nut_texture_t *textures;
} nut_t;

bool IsNUT (const uint8_t *data, size_t size);
int ParseNUT (nut_t *nut, const uint8_t *data, size_t size);
void FreeNUT (nut_t *nut);
int DecodeNUTTextureToRGBA (const nut_texture_t *tex, uint8_t **out_rgba, uint32_t *out_w, uint32_t *out_h);

#endif

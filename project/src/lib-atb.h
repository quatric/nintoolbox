// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_ATB_H
#define LIB_ATB_H 1

#include "lib-nintendo.h"

extern ccp ATB_SETUP_FILE;

typedef struct atb_anim_frame_t
{
	s16 pattern_index;
	s16 frame_length;
	s16 shift_x;
	s16 shift_y;
	s16 flip;
	s16 unk;
} atb_anim_frame_t;

typedef struct atb_bank_t
{
	uint frame_count;
	atb_anim_frame_t *frames;
} atb_bank_t;

typedef struct atb_layer_t
{
	u8 alpha;
	u8 flip;
	s16 texture_index;
	s16 tex_coord_tl_x;
	s16 tex_coord_tl_y;
	s16 tex_coord_w;
	s16 tex_coord_h;
	s16 shift_x;
	s16 shift_y;
	s16 vtx_tl_x, vtx_tl_y;
	s16 vtx_tr_x, vtx_tr_y;
	s16 vtx_br_x, vtx_br_y;
	s16 vtx_bl_x, vtx_bl_y;
} atb_layer_t;

typedef struct atb_pattern_t
{
	s16 center_x, center_y;
	s16 width, height;
	uint layer_count;
	atb_layer_t *layers;
} atb_pattern_t;

typedef struct atb_texture_t
{
	u8 bpp;
	u8 format;
	u16 palette_size;
	u16 width;
	u16 height;
	u32 image_size;
	u8 *palette_data;
	u8 *image_data;
} atb_texture_t;

typedef struct atb_archive_t
{
	u16 num_references;
	uint num_banks;
	atb_bank_t *banks;
	uint num_patterns;
	atb_pattern_t *patterns;
	uint num_textures;
	atb_texture_t *textures;
} atb_archive_t;

bool IsATB (const u8 *data, uint size);
enumError ScanATB (atb_archive_t *atb, const u8 *data, uint size);
void ResetATB (atb_archive_t *atb);
enumError CreateATB (u8 **dest, uint *dest_size, const atb_archive_t *atb);

bool looks_like_atb_dir (ccp dir);
enumError create_atb_dir (ccp source, ccp dest);

#endif // LIB_ATB_H

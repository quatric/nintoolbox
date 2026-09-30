#ifndef LIB_CVSPR_H
#define LIB_CVSPR_H

#include "lib-std.h"

// Magic: 0xBEEFF00D stored in little-endian as bytes: 0x0D, 0xF0, 0xEF, 0xBE
#define CV_SPR_MAGIC 0xbeeff00d

typedef struct cv_spr_header_t
{
	u32 magic;					 // 0x00: 0xbeeff00d (LE)
	u32 part_list_offset;		 // 0x04: byte offset to parts table
	u32 hitbox_list_offset;		 // 0x08: byte offset to hitboxes table
	u32 frame_list_offset;		 // 0x0c: byte offset to frames table
	u32 frame_delay_list_offset; // 0x10: byte offset to frame delays table
	u32 anim_list_offset;		 // 0x14: byte offset to animations table
	u32 unused_1;				 // 0x18
	u32 unused_2;				 // 0x1c
	u32 footer_offset;			 // 0x20: byte offset to footer
	u32 num_frames;				 // 0x24: frame count
	u32 num_anims;				 // 0x28: animation count
	u32 file_size;				 // 0x2c: total file size
} __attribute__((packed)) cv_spr_header_t;

typedef struct cv_spr_part_t
{
	s16 x_pos;
	s16 y_pos;
	u16 gfx_x;
	u16 gfx_y;
	u16 width;
	u16 height;
	u8 gfx_page;
	u8 flip_bits; // bit 0: vflip, bit 1: hflip
	u8 palette_idx;
	u8 unused;
} __attribute__((packed)) cv_spr_part_t;

typedef struct cv_spr_hitbox_t
{
	s16 x_pos;
	s16 y_pos;
	u16 width;
	u16 height;
} __attribute__((packed)) cv_spr_hitbox_t;

typedef struct cv_spr_frame_t
{
	u16 unknown;
	u8 num_hitboxes;
	u8 num_parts;
	u32 first_hitbox_offset; // relative to hitbox_list_offset
	u32 first_part_offset;	 // relative to part_list_offset
} __attribute__((packed)) cv_spr_frame_t;

typedef struct cv_spr_frame_delay_t
{
	u16 frame_index;
	u16 delay;
	u32 unknown;
} __attribute__((packed)) cv_spr_frame_delay_t;

typedef struct cv_spr_anim_t
{
	u32 num_frames;
	u32 first_delay_offset; // relative to frame_delay_list_offset
} __attribute__((packed)) cv_spr_anim_t;

bool IsCastlevaniaSprite (const void *data, size_t data_size, size_t file_size);
enumError DecodeCastlevaniaSprite_Text (FILE *f, const void *data, size_t size);

#endif // LIB_CVSPR_H

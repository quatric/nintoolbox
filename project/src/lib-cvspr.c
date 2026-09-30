#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib-cvspr.h"

bool IsCastlevaniaSprite (const void *data, size_t data_size, size_t file_size)
{
	if (!data || data_size < sizeof(cv_spr_header_t))
		return false;

	const u8 *p = (const u8 *)data;
	u32 magic = (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
	if (magic != CV_SPR_MAGIC)
		return false;

	u32 part_off = (u32)p[4] | ((u32)p[5] << 8) | ((u32)p[6] << 16) | ((u32)p[7] << 24);
	u32 hit_off = (u32)p[8] | ((u32)p[9] << 8) | ((u32)p[10] << 16) | ((u32)p[11] << 24);
	u32 frame_off = (u32)p[12] | ((u32)p[13] << 8) | ((u32)p[14] << 16) | ((u32)p[15] << 24);
	u32 fdelay_off = (u32)p[16] | ((u32)p[17] << 8) | ((u32)p[18] << 16) | ((u32)p[19] << 24);
	u32 anim_off = (u32)p[20] | ((u32)p[21] << 8) | ((u32)p[22] << 16) | ((u32)p[23] << 24);
	u32 footer_off = (u32)p[32] | ((u32)p[33] << 8) | ((u32)p[34] << 16) | ((u32)p[35] << 24);
	u32 num_frames = (u32)p[36] | ((u32)p[37] << 8) | ((u32)p[38] << 16) | ((u32)p[39] << 24);
	u32 expected_sz = (u32)p[44] | ((u32)p[45] << 8) | ((u32)p[46] << 16) | ((u32)p[47] << 24);

	if (file_size > 0 && expected_sz != file_size)
		return false;
	if (expected_sz < sizeof(cv_spr_header_t))
		return false;
	if (expected_sz > data_size && file_size == 0)
		return false;

	if (part_off < 0x2c || part_off > hit_off || hit_off > frame_off)
		return false;

	if ((hit_off - part_off) % sizeof(cv_spr_part_t) != 0)
		return false;
	if ((frame_off - hit_off) % sizeof(cv_spr_hitbox_t) != 0)
		return false;

	if (fdelay_off != 0)
	{
		if (fdelay_off < frame_off + num_frames * sizeof(cv_spr_frame_t))
			return false;
		if (anim_off != 0 && (anim_off < fdelay_off || (anim_off - fdelay_off) % sizeof(cv_spr_frame_delay_t) != 0))
			return false;
	}

	if (footer_off > expected_sz)
		return false;

	return true;
}

enumError DecodeCastlevaniaSprite_Text (FILE *f, const void *data, size_t size)
{
	if (!f || !data || size < sizeof(cv_spr_header_t))
		return ERR_INVALID_DATA;

	if (!IsCastlevaniaSprite(data, size, size))
		return ERR_INVALID_DATA;

	const u8 *p = (const u8 *)data;
	u32 part_off = (u32)p[4] | ((u32)p[5] << 8) | ((u32)p[6] << 16) | ((u32)p[7] << 24);
	u32 hit_off = (u32)p[8] | ((u32)p[9] << 8) | ((u32)p[10] << 16) | ((u32)p[11] << 24);
	u32 frame_off = (u32)p[12] | ((u32)p[13] << 8) | ((u32)p[14] << 16) | ((u32)p[15] << 24);
	u32 fdelay_off = (u32)p[16] | ((u32)p[17] << 8) | ((u32)p[18] << 16) | ((u32)p[19] << 24);
	u32 anim_off = (u32)p[20] | ((u32)p[21] << 8) | ((u32)p[22] << 16) | ((u32)p[23] << 24);
	u32 footer_off = (u32)p[32] | ((u32)p[33] << 8) | ((u32)p[34] << 16) | ((u32)p[35] << 24);
	u32 num_frames = (u32)p[36] | ((u32)p[37] << 8) | ((u32)p[38] << 16) | ((u32)p[39] << 24);
	u32 num_anims = (u32)p[40] | ((u32)p[41] << 8) | ((u32)p[42] << 16) | ((u32)p[43] << 24);
	u32 file_sz = (u32)p[44] | ((u32)p[45] << 8) | ((u32)p[46] << 16) | ((u32)p[47] << 24);

	u32 num_parts = (hit_off - part_off) / sizeof(cv_spr_part_t);
	u32 num_hitboxes = (frame_off - hit_off) / sizeof(cv_spr_hitbox_t);
	u32 num_delays = (fdelay_off != 0 && anim_off >= fdelay_off)
						 ? (anim_off - fdelay_off) / sizeof(cv_spr_frame_delay_t)
						 : 0;

	fprintf(f, "# Konami Castlevania DS Sprite Object (.dat / 0xBEEFF00D)\n");
	fprintf(f, "magic = 0x%08X\n", CV_SPR_MAGIC);
	fprintf(f, "file_size = %u\n", file_sz);
	fprintf(f, "part_count = %u\n", num_parts);
	fprintf(f, "hitbox_count = %u\n", num_hitboxes);
	fprintf(f, "frame_count = %u\n", num_frames);
	fprintf(f, "frame_delay_count = %u\n", num_delays);
	fprintf(f, "animation_count = %u\n", num_anims);
	fprintf(f, "footer_offset = 0x%X\n\n", footer_off);

	// Parts
	for (u32 i = 0; i < num_parts; i++)
	{
		size_t off = part_off + i * sizeof(cv_spr_part_t);
		if (off + sizeof(cv_spr_part_t) > size)
			break;

		s16 x = (s16)((u16)p[off] | ((u16)p[off + 1] << 8));
		s16 y = (s16)((u16)p[off + 2] | ((u16)p[off + 3] << 8));
		u16 gx = (u16)p[off + 4] | ((u16)p[off + 5] << 8);
		u16 gy = (u16)p[off + 6] | ((u16)p[off + 7] << 8);
		u16 w = (u16)p[off + 8] | ((u16)p[off + 9] << 8);
		u16 h = (u16)p[off + 10] | ((u16)p[off + 11] << 8);
		u8 page = p[off + 12];
		u8 flip = p[off + 13];
		u8 pal = p[off + 14];

		fprintf(f, "[[part]]\n");
		fprintf(f, "index = %u\n", i);
		fprintf(f, "x = %d\n", x);
		fprintf(f, "y = %d\n", y);
		fprintf(f, "width = %u\n", w);
		fprintf(f, "height = %u\n", h);
		fprintf(f, "gfx_x = %u\n", gx);
		fprintf(f, "gfx_y = %u\n", gy);
		fprintf(f, "gfx_page = %u\n", page);
		fprintf(f, "palette = %u\n", pal);
		fprintf(f, "vflip = %s\n", (flip & 1) ? "true" : "false");
		fprintf(f, "hflip = %s\n\n", (flip & 2) ? "true" : "false");
	}

	// Hitboxes
	for (u32 i = 0; i < num_hitboxes; i++)
	{
		size_t off = hit_off + i * sizeof(cv_spr_hitbox_t);
		if (off + sizeof(cv_spr_hitbox_t) > size)
			break;

		s16 x = (s16)((u16)p[off] | ((u16)p[off + 1] << 8));
		s16 y = (s16)((u16)p[off + 2] | ((u16)p[off + 3] << 8));
		u16 w = (u16)p[off + 4] | ((u16)p[off + 5] << 8);
		u16 h = (u16)p[off + 6] | ((u16)p[off + 7] << 8);

		fprintf(f, "[[hitbox]]\n");
		fprintf(f, "index = %u\n", i);
		fprintf(f, "x = %d\n", x);
		fprintf(f, "y = %d\n", y);
		fprintf(f, "width = %u\n", w);
		fprintf(f, "height = %u\n\n", h);
	}

	// Frames
	for (u32 i = 0; i < num_frames; i++)
	{
		size_t off = frame_off + i * sizeof(cv_spr_frame_t);
		if (off + sizeof(cv_spr_frame_t) > size)
			break;

		u16 unk = (u16)p[off] | ((u16)p[off + 1] << 8);
		u8 n_hit = p[off + 2];
		u8 n_pt = p[off + 3];
		u32 first_hit = (u32)p[off + 4] | ((u32)p[off + 5] << 8) | ((u32)p[off + 6] << 16) | ((u32)p[off + 7] << 24);
		u32 first_pt = (u32)p[off + 8] | ((u32)p[off + 9] << 8) | ((u32)p[off + 10] << 16) | ((u32)p[off + 11] << 24);

		int pt_idx = (first_pt % sizeof(cv_spr_part_t) == 0) ? (int)(first_pt / sizeof(cv_spr_part_t)) : -1;
		int hit_idx = (first_hit % sizeof(cv_spr_hitbox_t) == 0) ? (int)(first_hit / sizeof(cv_spr_hitbox_t)) : -1;

		fprintf(f, "[[frame]]\n");
		fprintf(f, "index = %u\n", i);
		fprintf(f, "part_count = %u\n", n_pt);
		fprintf(f, "part_start = %d\n", pt_idx);
		fprintf(f, "hitbox_count = %u\n", n_hit);
		fprintf(f, "hitbox_start = %d\n", hit_idx);
		if (unk != 0)
			fprintf(f, "unk = 0x%04X\n", unk);
		fprintf(f, "\n");
	}

	// Frame Delays
	for (u32 i = 0; i < num_delays; i++)
	{
		size_t off = fdelay_off + i * sizeof(cv_spr_frame_delay_t);
		if (off + sizeof(cv_spr_frame_delay_t) > size)
			break;

		u16 f_idx = (u16)p[off] | ((u16)p[off + 1] << 8);
		u16 delay = (u16)p[off + 2] | ((u16)p[off + 3] << 8);
		u32 unk = (u32)p[off + 4] | ((u32)p[off + 5] << 8) | ((u32)p[off + 6] << 16) | ((u32)p[off + 7] << 24);

		fprintf(f, "[[frame_delay]]\n");
		fprintf(f, "index = %u\n", i);
		fprintf(f, "frame_index = %u\n", f_idx);
		fprintf(f, "delay = %u\n", delay);
		if (unk != 0)
			fprintf(f, "unk = 0x%08X\n", unk);
		fprintf(f, "\n");
	}

	// Animations
	for (u32 i = 0; i < num_anims; i++)
	{
		size_t off = anim_off + i * sizeof(cv_spr_anim_t);
		if (off + sizeof(cv_spr_anim_t) > size)
			break;

		u32 n_f = (u32)p[off] | ((u32)p[off + 1] << 8) | ((u32)p[off + 2] << 16) | ((u32)p[off + 3] << 24);
		u32 first_d = (u32)p[off + 4] | ((u32)p[off + 5] << 8) | ((u32)p[off + 6] << 16) | ((u32)p[off + 7] << 24);

		int d_idx = (first_d % sizeof(cv_spr_frame_delay_t) == 0) ? (int)(first_d / sizeof(cv_spr_frame_delay_t)) : -1;

		fprintf(f, "[[animation]]\n");
		fprintf(f, "index = %u\n", i);
		fprintf(f, "frame_count = %u\n", n_f);
		fprintf(f, "delay_start = %d\n\n", d_idx);
	}

	return ERR_OK;
}

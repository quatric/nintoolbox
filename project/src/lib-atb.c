// SPDX-License-Identifier: GPL-2.0+
#include "lib-atb.h"
#include "lib-archive-util.h"
#include "dclib-file.h"
#include "lib-excite.h"
#include "lib-image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

ccp ATB_SETUP_FILE = "atb-setup.txt";

//-----------------------------------------------------------------------------
// Dynamic buffer helper
//-----------------------------------------------------------------------------

typedef struct dyn_buf_t
{
	u8 *data;
	uint size;
	uint alloc;
} dyn_buf_t;

static inline void dyn_init (dyn_buf_t *b)
{
	b->data = 0;
	b->size = 0;
	b->alloc = 0;
}

static inline void dyn_free (dyn_buf_t *b)
{
	FREE (b->data);
	b->data = 0;
	b->size = b->alloc = 0;
}

static inline bool dyn_reserve (dyn_buf_t *b, uint needed)
{
	if (b->size + needed <= b->alloc)
		return true;
	uint nalloc = b->alloc ? b->alloc * 2 : 1024;
	while (nalloc < b->size + needed)
		nalloc *= 2;
	u8 *ndata = REALLOC (b->data, nalloc);
	if (!ndata)
		return false;
	b->data = ndata;
	b->alloc = nalloc;
	return true;
}

static inline bool dyn_write (dyn_buf_t *b, const void *src, uint len)
{
	if (!dyn_reserve (b, len))
		return false;
	memcpy (b->data + b->size, src, len);
	b->size += len;
	return true;
}

static inline bool dyn_write_u8 (dyn_buf_t *b, u8 val)
{
	return dyn_write (b, &val, 1);
}

static inline bool dyn_write_be16 (dyn_buf_t *b, u16 val)
{
	u8 tmp[2];
	wr_be16 (tmp, val);
	return dyn_write (b, tmp, 2);
}

static inline bool dyn_write_be32 (dyn_buf_t *b, u32 val)
{
	u8 tmp[4];
	wr_be32 (tmp, val);
	return dyn_write (b, tmp, 4);
}

//-----------------------------------------------------------------------------
// Identification
//-----------------------------------------------------------------------------

bool IsATB (const u8 *data, uint size)
{
	if (!data || size < 20)
		return false;

	const u16 num_banks = rd_be16 (data);
	const u16 num_patterns = rd_be16 (data + 2);
	const u16 num_textures = rd_be16 (data + 4);
	const u32 bank_off = rd_be32 (data + 8);
	const u32 pattern_off = rd_be32 (data + 12);
	const u32 texture_off = rd_be32 (data + 16);

	if (num_banks > 4096 || num_patterns > 8192 || num_textures > 2048)
		return false;
	if (num_banks == 0 && num_patterns == 0 && num_textures == 0)
		return false;

	// unused tables must carry a zero or in-file offset, not arbitrary data
	if ((!num_banks && bank_off && (bank_off < 20 || bank_off > size))
		|| (!num_patterns && pattern_off && (pattern_off < 20 || pattern_off > size))
		|| (!num_textures && texture_off && (texture_off < 20 || texture_off > size)))
		return false;
	if (num_banks > 0)
	{
		if (bank_off < 20 || bank_off + (u64)num_banks * 8 > size)
			return false;

		// Bounds-only on the bank table itself let unrelated headerless
		// formats with a plausible-looking offset word (e.g. a Mii resource
		// archive's own sub-archive table, confirmed live on Wii RFL_Res.dat)
		// false-positive as ATB, because every real .atb sample happened to
		// also have num_textures>0 and only the texture table below got
		// walked. Require the same per-entry structural proof for banks that
		// ScanATB() itself needs to read a frame table, matching the rigor
		// the texture loop already has.
		for (u16 i = 0; i < num_banks; i++)
		{
			const u8 *bp = data + bank_off + i * 8;
			const u16 frame_cnt = rd_be16 (bp);
			const u32 frame_off = rd_be32 (bp + 4);
			if (frame_cnt > 0 && frame_off + (u64)frame_cnt * 12 > size)
				return false;
		}
	}
	if (num_patterns > 0)
	{
		if (pattern_off < 20 || pattern_off + (u64)num_patterns * 16 > size)
			return false;

		// Same reasoning as the bank loop above: walk every pattern's own
		// layer table instead of trusting the bounds-checked-only pattern
		// array header.
		for (u16 i = 0; i < num_patterns; i++)
		{
			const u8 *pp = data + pattern_off + i * 16;
			const u16 layer_cnt = rd_be16 (pp);
			const u32 layer_off = rd_be32 (pp + 12);
			if (layer_cnt > 0 && layer_off + (u64)layer_cnt * 32 > size)
				return false;
		}
	}
	if (num_textures > 0)
	{
		if (texture_off < 20 || texture_off + (u64)num_textures * 20 > size)
			return false;

		for (u16 i = 0; i < num_textures; i++)
		{
			const u8 *tp = data + texture_off + i * 20;
			const u8 fmt = tp[1];
			const u16 pal_size = rd_be16 (tp + 2);
			const u16 w = rd_be16 (tp + 4);
			const u16 h = rd_be16 (tp + 6);
			const u32 img_size = rd_be32 (tp + 8);
			const u32 pal_off = rd_be32 (tp + 12);
			const u32 img_off = rd_be32 (tp + 16);

			if (fmt > 0x0c)
				return false;
			if (w > 4096 || h > 4096)
				return false;
			if (img_off + (u64)img_size > size)
				return false;
			if (pal_size > 0 && pal_off + (u64)pal_size * 2 > size)
				return false;
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// Scan ATB
//-----------------------------------------------------------------------------

enumError ScanATB (atb_archive_t *atb, const u8 *data, uint size)
{
	if (!atb || !data || !IsATB (data, size))
		return ERR_INVALID_DATA;

	memset (atb, 0, sizeof (*atb));

	atb->num_banks = rd_be16 (data);
	atb->num_patterns = rd_be16 (data + 2);
	atb->num_textures = rd_be16 (data + 4);
	atb->num_references = rd_be16 (data + 6);

	const u32 bank_off = rd_be32 (data + 8);
	const u32 pattern_off = rd_be32 (data + 12);
	const u32 texture_off = rd_be32 (data + 16);

	// Parse Banks
	if (atb->num_banks > 0)
	{
		atb->banks = CALLOC (atb->num_banks, sizeof (atb_bank_t));
		if (!atb->banks)
			return ERR_CANT_CREATE;

		for (uint i = 0; i < atb->num_banks; i++)
		{
			const u8 *bp = data + bank_off + i * 8;
			const u16 frame_cnt = rd_be16 (bp);
			const u32 frame_off = rd_be32 (bp + 4);

			// Counts are only kept alongside a frame table that was actually
			// read: an out-of-bounds table (e.g. a non-ATB file that passed the
			// header sniff) left frame_count set with frames == NULL, and the
			// manifest writer then dereferenced NULL.
			if (frame_cnt > 0 && frame_off + (u64)frame_cnt * 12 <= size)
			{
				atb->banks[i].frame_count = frame_cnt;
				atb->banks[i].frames = CALLOC (frame_cnt, sizeof (atb_anim_frame_t));
				if (!atb->banks[i].frames)
					return ERR_CANT_CREATE;

				for (uint f = 0; f < frame_cnt; f++)
				{
					const u8 *fp = data + frame_off + f * 12;
					atb->banks[i].frames[f].pattern_index = (s16)rd_be16 (fp);
					atb->banks[i].frames[f].frame_length = (s16)rd_be16 (fp + 2);
					atb->banks[i].frames[f].shift_x = (s16)rd_be16 (fp + 4);
					atb->banks[i].frames[f].shift_y = (s16)rd_be16 (fp + 6);
					atb->banks[i].frames[f].flip = (s16)rd_be16 (fp + 8);
					atb->banks[i].frames[f].unk = (s16)rd_be16 (fp + 10);
				}
			}
		}
	}

	// Parse Patterns
	if (atb->num_patterns > 0)
	{
		atb->patterns = CALLOC (atb->num_patterns, sizeof (atb_pattern_t));
		if (!atb->patterns)
			return ERR_CANT_CREATE;

		for (uint i = 0; i < atb->num_patterns; i++)
		{
			const u8 *pp = data + pattern_off + i * 16;
			const u16 layer_cnt = rd_be16 (pp);
			atb->patterns[i].center_x = (s16)rd_be16 (pp + 2);
			atb->patterns[i].center_y = (s16)rd_be16 (pp + 4);
			atb->patterns[i].width = (s16)rd_be16 (pp + 6);
			atb->patterns[i].height = (s16)rd_be16 (pp + 8);

			const u32 layer_off = rd_be32 (pp + 12);
			if (layer_cnt > 0 && layer_off + (u64)layer_cnt * 32 <= size)
			{
				atb->patterns[i].layer_count = layer_cnt;
				atb->patterns[i].layers = CALLOC (layer_cnt, sizeof (atb_layer_t));
				if (!atb->patterns[i].layers)
					return ERR_CANT_CREATE;

				for (uint l = 0; l < layer_cnt; l++)
				{
					const u8 *lp = data + layer_off + l * 32;
					atb->patterns[i].layers[l].alpha = lp[0];
					atb->patterns[i].layers[l].flip = lp[1];
					atb->patterns[i].layers[l].texture_index = (s16)rd_be16 (lp + 2);
					atb->patterns[i].layers[l].tex_coord_tl_x = (s16)rd_be16 (lp + 4);
					atb->patterns[i].layers[l].tex_coord_tl_y = (s16)rd_be16 (lp + 6);
					atb->patterns[i].layers[l].tex_coord_w = (s16)rd_be16 (lp + 8);
					atb->patterns[i].layers[l].tex_coord_h = (s16)rd_be16 (lp + 10);
					atb->patterns[i].layers[l].shift_x = (s16)rd_be16 (lp + 12);
					atb->patterns[i].layers[l].shift_y = (s16)rd_be16 (lp + 14);
					atb->patterns[i].layers[l].vtx_tl_x = (s16)rd_be16 (lp + 16);
					atb->patterns[i].layers[l].vtx_tl_y = (s16)rd_be16 (lp + 18);
					atb->patterns[i].layers[l].vtx_tr_x = (s16)rd_be16 (lp + 20);
					atb->patterns[i].layers[l].vtx_tr_y = (s16)rd_be16 (lp + 22);
					atb->patterns[i].layers[l].vtx_br_x = (s16)rd_be16 (lp + 24);
					atb->patterns[i].layers[l].vtx_br_y = (s16)rd_be16 (lp + 26);
					atb->patterns[i].layers[l].vtx_bl_x = (s16)rd_be16 (lp + 28);
					atb->patterns[i].layers[l].vtx_bl_y = (s16)rd_be16 (lp + 30);
				}
			}
		}
	}

	// Parse Textures
	if (atb->num_textures > 0)
	{
		atb->textures = CALLOC (atb->num_textures, sizeof (atb_texture_t));
		if (!atb->textures)
			return ERR_CANT_CREATE;

		for (uint i = 0; i < atb->num_textures; i++)
		{
			const u8 *tp = data + texture_off + i * 20;
			atb->textures[i].bpp = tp[0];
			atb->textures[i].format = tp[1];
			atb->textures[i].palette_size = rd_be16 (tp + 2);
			atb->textures[i].width = rd_be16 (tp + 4);
			atb->textures[i].height = rd_be16 (tp + 6);
			atb->textures[i].image_size = rd_be32 (tp + 8);

			const u32 pal_off = rd_be32 (tp + 12);
			const u32 img_off = rd_be32 (tp + 16);

			if (atb->textures[i].palette_size > 0
				&& pal_off + atb->textures[i].palette_size * 2 <= size)
			{
				atb->textures[i].palette_data = MALLOC (atb->textures[i].palette_size * 2);
				if (atb->textures[i].palette_data)
					memcpy (atb->textures[i].palette_data, data + pal_off,
						atb->textures[i].palette_size * 2);
			}

			if (atb->textures[i].image_size > 0 && img_off + atb->textures[i].image_size <= size)
			{
				atb->textures[i].image_data = MALLOC (atb->textures[i].image_size);
				if (atb->textures[i].image_data)
					memcpy (
						atb->textures[i].image_data, data + img_off, atb->textures[i].image_size);
			}
		}
	}

	return ERR_OK;
}

void ResetATB (atb_archive_t *atb)
{
	if (!atb)
		return;

	if (atb->banks)
	{
		for (uint i = 0; i < atb->num_banks; i++)
			FREE (atb->banks[i].frames);
		FREE (atb->banks);
	}
	if (atb->patterns)
	{
		for (uint i = 0; i < atb->num_patterns; i++)
			FREE (atb->patterns[i].layers);
		FREE (atb->patterns);
	}
	if (atb->textures)
	{
		for (uint i = 0; i < atb->num_textures; i++)
		{
			FREE (atb->textures[i].palette_data);
			FREE (atb->textures[i].image_data);
		}
		FREE (atb->textures);
	}
	memset (atb, 0, sizeof (*atb));
}

//-----------------------------------------------------------------------------
// Create ATB
//-----------------------------------------------------------------------------

enumError CreateATB (u8 **dest, uint *dest_size, const atb_archive_t *atb)
{
	if (!dest || !dest_size || !atb)
		return ERR_INVALID_DATA;

	dyn_buf_t out;
	dyn_init (&out);

	// Header: 20 bytes
	dyn_write_be16 (&out, (u16)atb->num_banks);
	dyn_write_be16 (&out, (u16)atb->num_patterns);
	dyn_write_be16 (&out, (u16)atb->num_textures);
	dyn_write_be16 (&out, atb->num_references);
	dyn_write_be32 (&out, 0); // bank_off placeholder
	dyn_write_be32 (&out, 0); // pattern_off placeholder
	dyn_write_be32 (&out, 0); // texture_off placeholder

	// Write Patterns
	u32 pattern_off = 0;
	if (atb->num_patterns > 0)
	{
		pattern_off = out.size;
		wr_be32 (out.data + 12, pattern_off);

		uint pat_tab_pos = out.size;
		for (uint i = 0; i < atb->num_patterns; i++)
		{
			dyn_write_be16 (&out, (u16)atb->patterns[i].layer_count);
			dyn_write_be16 (&out, (u16)atb->patterns[i].center_x);
			dyn_write_be16 (&out, (u16)atb->patterns[i].center_y);
			dyn_write_be16 (&out, (u16)atb->patterns[i].width);
			dyn_write_be16 (&out, (u16)atb->patterns[i].height);
			dyn_write_be16 (&out, 0); // padding
			dyn_write_be32 (&out, 0); // layer_off placeholder
		}

		for (uint i = 0; i < atb->num_patterns; i++)
		{
			if (atb->patterns[i].layer_count > 0 && atb->patterns[i].layers)
			{
				uint cur_layer_off = out.size;
				wr_be32 (out.data + pat_tab_pos + i * 16 + 12, cur_layer_off);
				for (uint l = 0; l < atb->patterns[i].layer_count; l++)
				{
					const atb_layer_t *layer = &atb->patterns[i].layers[l];
					dyn_write_u8 (&out, layer->alpha);
					dyn_write_u8 (&out, layer->flip);
					dyn_write_be16 (&out, (u16)layer->texture_index);
					dyn_write_be16 (&out, (u16)layer->tex_coord_tl_x);
					dyn_write_be16 (&out, (u16)layer->tex_coord_tl_y);
					dyn_write_be16 (&out, (u16)layer->tex_coord_w);
					dyn_write_be16 (&out, (u16)layer->tex_coord_h);
					dyn_write_be16 (&out, (u16)layer->shift_x);
					dyn_write_be16 (&out, (u16)layer->shift_y);
					dyn_write_be16 (&out, (u16)layer->vtx_tl_x);
					dyn_write_be16 (&out, (u16)layer->vtx_tl_y);
					dyn_write_be16 (&out, (u16)layer->vtx_tr_x);
					dyn_write_be16 (&out, (u16)layer->vtx_tr_y);
					dyn_write_be16 (&out, (u16)layer->vtx_br_x);
					dyn_write_be16 (&out, (u16)layer->vtx_br_y);
					dyn_write_be16 (&out, (u16)layer->vtx_bl_x);
					dyn_write_be16 (&out, (u16)layer->vtx_bl_y);
				}
			}
		}
	}

	// Write Banks
	u32 bank_off = 0;
	if (atb->num_banks > 0)
	{
		bank_off = out.size;
		wr_be32 (out.data + 8, bank_off);

		uint bank_tab_pos = out.size;
		for (uint i = 0; i < atb->num_banks; i++)
		{
			dyn_write_be16 (&out, (u16)atb->banks[i].frame_count);
			dyn_write_be16 (&out, 0); // padding
			dyn_write_be32 (&out, 0); // frame_off placeholder
		}

		for (uint i = 0; i < atb->num_banks; i++)
		{
			if (atb->banks[i].frame_count > 0 && atb->banks[i].frames)
			{
				uint cur_frame_off = out.size;
				wr_be32 (out.data + bank_tab_pos + i * 8 + 4, cur_frame_off);
				for (uint f = 0; f < atb->banks[i].frame_count; f++)
				{
					const atb_anim_frame_t *frame = &atb->banks[i].frames[f];
					dyn_write_be16 (&out, (u16)frame->pattern_index);
					dyn_write_be16 (&out, (u16)frame->frame_length);
					dyn_write_be16 (&out, (u16)frame->shift_x);
					dyn_write_be16 (&out, (u16)frame->shift_y);
					dyn_write_be16 (&out, (u16)frame->flip);
					dyn_write_be16 (&out, (u16)frame->unk);
				}
			}
		}
	}

	// Write Textures
	u32 tex_off = 0;
	if (atb->num_textures > 0)
	{
		tex_off = out.size;
		wr_be32 (out.data + 16, tex_off);

		uint tex_tab_pos = out.size;
		for (uint i = 0; i < atb->num_textures; i++)
		{
			dyn_write_u8 (&out, atb->textures[i].bpp);
			dyn_write_u8 (&out, atb->textures[i].format);
			dyn_write_be16 (&out, atb->textures[i].palette_size);
			dyn_write_be16 (&out, atb->textures[i].width);
			dyn_write_be16 (&out, atb->textures[i].height);
			dyn_write_be32 (&out, atb->textures[i].image_size);
			dyn_write_be32 (&out, 0); // pal_off placeholder
			dyn_write_be32 (&out, 0); // img_off placeholder
		}

		// Align to 32 bytes with 0x88 padding
		while (out.size % 32 != 0)
			dyn_write_u8 (&out, 0x88);

		for (uint i = 0; i < atb->num_textures; i++)
		{
			if (atb->textures[i].palette_size > 0 && atb->textures[i].palette_data)
			{
				uint cur_pal_off = out.size;
				wr_be32 (out.data + tex_tab_pos + i * 20 + 12, cur_pal_off);
				dyn_write (&out, atb->textures[i].palette_data, atb->textures[i].palette_size * 2);
			}

			if (atb->textures[i].image_size > 0 && atb->textures[i].image_data)
			{
				uint cur_img_off = out.size;
				wr_be32 (out.data + tex_tab_pos + i * 20 + 16, cur_img_off);
				dyn_write (&out, atb->textures[i].image_data, atb->textures[i].image_size);
			}
		}
	}

	*dest = out.data;
	*dest_size = out.size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// Directory extraction & packing
//-----------------------------------------------------------------------------

bool looks_like_atb_dir (ccp dir)
{
	if (!dir || !*dir)
		return false;
	char setup_path[PATH_MAX];
	snprintf (setup_path, sizeof (setup_path), "%s/%s", dir, ATB_SETUP_FILE);
	struct stat st;
	if (!stat (setup_path, &st) && S_ISREG (st.st_mode))
		return true;
	const size_t len = strlen (dir);
	if (len > 6 && !strcasecmp (dir + len - 6, ".atb.d"))
		return true;
	return false;
}

enumError create_atb_dir (ccp source, ccp dest)
{
	char setup_path[PATH_MAX];
	snprintf (setup_path, sizeof (setup_path), "%s/%s", source, ATB_SETUP_FILE);

	FILE *f = fopen (setup_path, "r");
	if (!f)
		return ERROR0 (ERR_NOT_EXISTS, "Can't open ATB setup file: %s\n", setup_path);

	atb_archive_t atb;
	memset (&atb, 0, sizeof (atb));

	// Pre-pass or dynamic arrays
	atb_texture_t tex_list[256];
	memset (tex_list, 0, sizeof (tex_list));
	char tex_raw_names[256][PATH_MAX];
	char tex_png_names[256][PATH_MAX];
	memset (tex_raw_names, 0, sizeof (tex_raw_names));
	memset (tex_png_names, 0, sizeof (tex_png_names));

	atb_pattern_t pat_list[1024];
	memset (pat_list, 0, sizeof (pat_list));
	atb_bank_t bank_list[512];
	memset (bank_list, 0, sizeof (bank_list));

	atb_layer_t layer_pool[4096];
	uint layer_pool_cnt = 0;
	atb_anim_frame_t frame_pool[4096];
	uint frame_pool_cnt = 0;

	char line[512];
	while (fgets (line, sizeof (line), f))
	{
		char *p = line;
		while (*p && isspace ((u8)*p))
			p++;
		if (!*p || *p == '#')
			continue;

		uint ref_cnt = 0;
		if (sscanf (p, "references %u", &ref_cnt) == 1)
		{
			atb.num_references = (u16)ref_cnt;
			continue;
		}

		uint tidx = 0, fmt = 0, bpp = 0, ps = 0, w = 0, h = 0;
		char rname[256], pname[256];
		rname[0] = pname[0] = 0;
		if (sscanf (p, "texture %u format=%u bpp=%u pal_size=%u size=%u,%u raw=%255s png=%255s",
				&tidx, &fmt, &bpp, &ps, &w, &h, rname, pname)
			>= 7)
		{
			if (tidx < 256)
			{
				tex_list[tidx].format = (u8)fmt;
				tex_list[tidx].bpp = (u8)bpp;
				tex_list[tidx].palette_size = (u16)ps;
				tex_list[tidx].width = (u16)w;
				tex_list[tidx].height = (u16)h;
				snprintf (
					tex_raw_names[tidx], sizeof (tex_raw_names[tidx]), "%s/%s", source, rname);
				snprintf (
					tex_png_names[tidx], sizeof (tex_png_names[tidx]), "%s/%s", source, pname);
				if (tidx >= atb.num_textures)
					atb.num_textures = tidx + 1;
			}
			continue;
		}

		uint pidx = 0;
		int cx = 0, cy = 0, pw = 0, ph = 0;
		if (sscanf (p, "pattern %u center=%d,%d size=%d,%d", &pidx, &cx, &cy, &pw, &ph) == 5)
		{
			if (pidx < 1024)
			{
				pat_list[pidx].center_x = (s16)cx;
				pat_list[pidx].center_y = (s16)cy;
				pat_list[pidx].width = (s16)pw;
				pat_list[pidx].height = (s16)ph;
				if (pidx >= atb.num_patterns)
					atb.num_patterns = pidx + 1;
			}
			continue;
		}

		uint l_pidx = 0, alpha = 0, flip = 0;
		int tex_idx = 0, tlx = 0, tly = 0, tcw = 0, tch = 0, sx = 0, sy = 0;
		int vtlx = 0, vtly = 0, vtrx = 0, vtry = 0, vbrx = 0, vbry = 0, vblx = 0, vbly = 0;
		if (sscanf (p,
				"layer pattern=%u tex=%d alpha=%u flip=%u coord=%d,%d,%d,%d shift=%d,%d "
				"vtx=%d,%d,%d,%d,%d,%d,%d,%d",
				&l_pidx, &tex_idx, &alpha, &flip, &tlx, &tly, &tcw, &tch, &sx, &sy, &vtlx, &vtly,
				&vtrx, &vtry, &vbrx, &vbry, &vblx, &vbly)
			== 18)
		{
			if (l_pidx < 1024 && layer_pool_cnt < 4096)
			{
				atb_layer_t *l = &layer_pool[layer_pool_cnt++];
				l->alpha = (u8)alpha;
				l->flip = (u8)flip;
				l->texture_index = (s16)tex_idx;
				l->tex_coord_tl_x = (s16)tlx;
				l->tex_coord_tl_y = (s16)tly;
				l->tex_coord_w = (s16)tcw;
				l->tex_coord_h = (s16)tch;
				l->shift_x = (s16)sx;
				l->shift_y = (s16)sy;
				l->vtx_tl_x = (s16)vtlx;
				l->vtx_tl_y = (s16)vtly;
				l->vtx_tr_x = (s16)vtrx;
				l->vtx_tr_y = (s16)vtry;
				l->vtx_br_x = (s16)vbrx;
				l->vtx_br_y = (s16)vbry;
				l->vtx_bl_x = (s16)vblx;
				l->vtx_bl_y = (s16)vbly;

				if (!pat_list[l_pidx].layers)
					pat_list[l_pidx].layers = l;
				pat_list[l_pidx].layer_count++;
			}
			continue;
		}

		uint bidx = 0;
		if (sscanf (p, "bank %u", &bidx) == 1)
		{
			if (bidx >= atb.num_banks)
				atb.num_banks = bidx + 1;
			continue;
		}

		uint f_bidx = 0;
		int pat_idx = 0, flen = 0, fsx = 0, fsy = 0, fflip = 0, funk = 0;
		if (sscanf (p, "frame bank=%u pattern=%d length=%d shift=%d,%d flip=%d unk=%d", &f_bidx,
				&pat_idx, &flen, &fsx, &fsy, &fflip, &funk)
			== 7)
		{
			if (f_bidx < 512 && frame_pool_cnt < 4096)
			{
				atb_anim_frame_t *frame = &frame_pool[frame_pool_cnt++];
				frame->pattern_index = (s16)pat_idx;
				frame->frame_length = (s16)flen;
				frame->shift_x = (s16)fsx;
				frame->shift_y = (s16)fsy;
				frame->flip = (s16)fflip;
				frame->unk = (s16)funk;

				if (!bank_list[f_bidx].frames)
					bank_list[f_bidx].frames = frame;
				bank_list[f_bidx].frame_count++;
			}
			continue;
		}
	}
	fclose (f);

	// Load raw/image data for each texture
	for (uint i = 0; i < atb.num_textures; i++)
	{
		u8 *raw = 0;
		size_t rsize = 0;
		if (tex_raw_names[i][0]
			&& !LoadFileAlloc (tex_raw_names[i], 0, 0, &raw, &rsize, 0, 0, 0, false))
		{
			const uint psz = tex_list[i].palette_size * 2;
			if (rsize >= psz)
			{
				if (psz > 0)
				{
					tex_list[i].palette_data = MALLOC (psz);
					if (tex_list[i].palette_data)
						memcpy (tex_list[i].palette_data, raw, psz);
				}
				tex_list[i].image_size = (u32)(rsize - psz);
				tex_list[i].image_data = MALLOC (tex_list[i].image_size);
				if (tex_list[i].image_data)
					memcpy (tex_list[i].image_data, raw + psz, tex_list[i].image_size);
			}
			FREE (raw);
		}
	}

	atb.textures = tex_list;
	atb.patterns = pat_list;
	atb.banks = bank_list;

	u8 *out_atb = 0;
	uint out_size = 0;
	enumError err = CreateATB (&out_atb, &out_size, &atb);

	for (uint i = 0; i < atb.num_textures; i++)
	{
		FREE (tex_list[i].palette_data);
		FREE (tex_list[i].image_data);
	}

	if (err || !out_atb)
	{
		FREE (out_atb);
		return err ? err : ERR_CANT_CREATE;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, 0);
	if (!err && F.f)
	{
		if (fwrite (out_atb, 1, out_size, F.f) != out_size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing ATB failed: %s\n", dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (out_atb);
	return err;
}

// lib-koopatlas.c – Koopatlas Binary World Map (.kpbin / KP_m) parser & dumper
// Parses world maps from Newer Super Mario Bros. Wii and NSMBW mods.

#include "lib-koopatlas.h"
#include "lib-std.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

//
///////////////////////////////////////////////////////////////////////////////
//////////////////////////  Local string-buffer  //////////////////////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct kp_sb_t
{
	char *buf;
	size_t len;
	size_t cap;
} kp_sb_t;

static void ksb_init (kp_sb_t *s)
{
	s->cap = 8192;
	s->len = 0;
	s->buf = MALLOC (s->cap);
	if (s->buf) s->buf[0] = 0;
}

static void ksb_grow (kp_sb_t *s, size_t extra)
{
	if (!s->buf) return;
	if (s->len + extra + 1 <= s->cap) return;
	size_t nc = s->cap * 2 + extra + 128;
	char *nb = REALLOC (s->buf, nc);
	if (!nb) return;
	s->buf = nb;
	s->cap = nc;
}

static void ksb_putc (kp_sb_t *s, char c)
{
	ksb_grow (s, 1);
	if (s->buf) { s->buf[s->len++] = c; s->buf[s->len] = 0; }
}

static void ksb_puts (kp_sb_t *s, const char *str)
{
	if (!str) return;
	size_t l = strlen (str);
	ksb_grow (s, l);
	if (s->buf) { memcpy (s->buf + s->len, str, l + 1); s->len += l; }
}

static void ksb_printf (kp_sb_t *s, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));

static void ksb_printf (kp_sb_t *s, const char *fmt, ...)
{
	if (!s->buf) return;
	char tmp[512];
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (tmp, sizeof (tmp), fmt, ap);
	va_end (ap);
	ksb_puts (s, tmp);
}

//
///////////////////////////////////////////////////////////////////////////////
//////////////////////  Helpers  //////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

static inline float be_float (const u8 *p)
{
	union { u32 u; float f; } u;
	u.u = be32 (p);
	return u.f;
}

static char *get_string_at (const u8 *data, size_t size, uint off)
{
	if (!data || off >= size) return NULL;
	size_t len = strnlen ((const char*)data + off, size - off);
	char *s = MALLOC (len + 1);
	if (s) { memcpy (s, data + off, len); s[len] = 0; }
	return s;
}

static const char *layer_type_names[] = {
	"objects",
	"doodads",
	"paths"
};

static const char *node_type_names[] = {
	"passthrough",
	"stop",
	"level",
	"change",
	"world_change"
};

static const char *anim_type_names[] = {
	"x_pos",
	"y_pos",
	"angle",
	"x_scale",
	"y_scale",
	"opacity"
};

static const char *loop_type_names[] = {
	"contiguous",
	"loop",
	"reverse_loop"
};

static const char *curve_type_names[] = {
	"linear",
	"sin",
	"cos"
};

static const char *path_anim_names[] = {
	"walk", "walk_sand", "walk_snow", "walk_water",
	"jump", "jump_sand", "jump_snow", "jump_water",
	"ladder", "ladder_left", "ladder_right", "fall",
	"swim", "run", "pipe", "door",
	"tjumped", "enter_cave_up", "reserved_18", "invisible"
};

//
///////////////////////////////////////////////////////////////////////////////
////////////////////  IsKPBin  ////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

bool IsKPBin (const u8 *data, size_t size)
{
	if (!data || size < 0x2C)
		return false;
	return !memcmp (data, KPBIN_MAGIC, 4);
}

//
///////////////////////////////////////////////////////////////////////////////
////////////////////  ScanKPBin  //////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

enumError ScanKPBin (kpbin_t *kp, const u8 *data, size_t size)
{
	memset (kp, 0, sizeof (*kp));
	if (!data || size < 0x2C || memcmp (data, KPBIN_MAGIC, 4) != 0)
		return ERR_INVALID_DATA;

	kp->version     = (s32)be32 (data + 4);
	uint layer_cnt  = be32 (data + 8);
	uint layer_off  = be32 (data + 12);
	uint ts_cnt     = be32 (data + 16);
	uint unlock_off = be32 (data + 24);
	uint sector_off = be32 (data + 28);
	uint bg_off     = be32 (data + 32);
	uint world_off  = be32 (data + 36);
	uint world_cnt  = be32 (data + 40);

	kp->n_tilesets  = ts_cnt;
	kp->sector_offs = sector_off;
	kp->unlock_offs = unlock_off;

	// Background image name
	if (bg_off && bg_off < size)
		kp->bg_name = get_string_at (data, size, bg_off);

	// Parse Layers
	if (layer_cnt > 0 && layer_off + layer_cnt * 4 <= size)
	{
		kp->layers = CALLOC (layer_cnt, sizeof (kp_layer_t));
		if (!kp->layers)
			return ERR_OUT_OF_MEMORY;
		kp->n_layers = layer_cnt;

		for (uint i = 0; i < layer_cnt; i++)
		{
			uint loff = be32 (data + layer_off + i * 4);
			if (loff + 8 > size)
				continue;

			kp_layer_t *layer = &kp->layers[i];
			layer->type = be32 (data + loff);
			layer->alpha = data[loff + 4];

			if (layer->type == KP_LAYER_OBJECTS)
			{
				// Tile layer:
				//   loff + 8:  GXTexObj pointer (u32)
				//   loff + 12: sector bounds (4 * s32)
				//   loff + 28: real bounds (4 * s32)
				//   loff + 44: sector indices array of u16
				if (loff + 44 <= size)
				{
					uint ts_name_off = be32 (data + loff + 8);
					if (ts_name_off && ts_name_off < size)
						layer->tileset_name = get_string_at (data, size, ts_name_off);

					for (int b = 0; b < 4; b++)
					{
						layer->sector_bounds[b] = (s32)be32 (data + loff + 12 + b * 4);
						layer->real_bounds[b]   = (s32)be32 (data + loff + 28 + b * 4);
					}

					int w = layer->sector_bounds[2] - layer->sector_bounds[0] + 1;
					int h = layer->sector_bounds[3] - layer->sector_bounds[1] + 1;
					if (w > 0 && h > 0 && w < 1000 && h < 1000)
					{
						uint cnt = (uint)(w * h);
						if (loff + 44 + cnt * 2 <= size)
						{
							layer->tile_indices = CALLOC (cnt, sizeof (u16));
							if (layer->tile_indices)
							{
								layer->n_tile_indices = cnt;
								for (uint t = 0; t < cnt; t++)
									layer->tile_indices[t] = be16 (data + loff + 44 + t * 2);
							}
						}
					}
				}
			}
			else if (layer->type == KP_LAYER_DOODADS)
			{
				// Doodad layer:
				//   loff + 8: doodadCount (s32)
				//   loff + 12: array of doodad offsets (u32 * doodadCount)
				if (loff + 12 <= size)
				{
					uint dcnt = be32 (data + loff + 8);
					if (dcnt < 10000 && loff + 12 + dcnt * 4 <= size)
					{
						layer->doodads = CALLOC (dcnt, sizeof (kp_doodad_t));
						if (layer->doodads)
						{
							layer->n_doodads = dcnt;
							for (uint d = 0; d < dcnt; d++)
							{
								uint doff = be32 (data + loff + 12 + d * 4);
								if (doff + 28 > size)
									continue;

								kp_doodad_t *dood = &layer->doodads[d];
								dood->x     = be_float (data + doff + 0);
								dood->y     = be_float (data + doff + 4);
								dood->w     = be_float (data + doff + 8);
								dood->h     = be_float (data + doff + 12);
								dood->angle = be_float (data + doff + 16);
								dood->tex_offs = be32 (data + doff + 20);

								uint acnt = be32 (data + doff + 24);
								if (acnt > 0 && acnt < 100 && doff + 28 + acnt * 40 <= size)
								{
									dood->anims = CALLOC (acnt, sizeof (kp_doodad_anim_t));
									if (dood->anims)
									{
										dood->n_anims = acnt;
										for (uint a = 0; a < acnt; a++)
										{
											const u8 *ap = data + doff + 28 + a * 40;
											kp_doodad_anim_t *danim = &dood->anims[a];
											danim->loop         = be32 (ap + 0);
											danim->curve        = be32 (ap + 4);
											danim->frame_count  = be32 (ap + 8);
											danim->type         = be32 (ap + 12);
											danim->start        = be32 (ap + 16);
											danim->end          = be32 (ap + 20);
											danim->delay        = be32 (ap + 24);
											danim->delay_offset = be32 (ap + 28);
										}
									}
								}
							}
						}
					}
				}
			}
			else if (layer->type == KP_LAYER_PATHS)
			{
				// Path layer:
				//   loff + 8:  nodeCount (s32)
				//   loff + 12: nodesOffs (u32)
				//   loff + 16: pathCount (s32)
				//   loff + 20: pathsOffs (u32)
				if (loff + 24 <= size)
				{
					uint ncnt   = be32 (data + loff + 8);
					uint noff   = be32 (data + loff + 12);
					uint pcnt   = be32 (data + loff + 16);
					uint poff   = be32 (data + loff + 20);

					// Temporary arrays for pointer resolution
					uint *node_offsets = NULL;
					uint *path_offsets = NULL;

					if (ncnt > 0 && ncnt < 10000 && noff + ncnt * 4 <= size)
					{
						node_offsets = MALLOC (ncnt * sizeof (uint));
						for (uint n = 0; n < ncnt; n++)
							node_offsets[n] = be32 (data + noff + n * 4);

						layer->nodes = CALLOC (ncnt, sizeof (kp_node_t));
						if (layer->nodes)
							layer->n_nodes = ncnt;
					}

					if (pcnt > 0 && pcnt < 10000 && poff + pcnt * 4 <= size)
					{
						path_offsets = MALLOC (pcnt * sizeof (uint));
						for (uint p = 0; p < pcnt; p++)
							path_offsets[p] = be32 (data + poff + p * 4);

						layer->paths = CALLOC (pcnt, sizeof (kp_path_t));
						if (layer->paths)
							layer->n_paths = pcnt;
					}

					// Read nodes
					if (layer->nodes && node_offsets)
					{
						for (uint n = 0; n < ncnt; n++)
						{
							uint np_off = node_offsets[n];
							if (np_off + 32 > size)
								continue;

							kp_node_t *node = &layer->nodes[n];
							node->x = (s16)be16 (data + np_off + 0);
							node->y = (s16)be16 (data + np_off + 2);

							for (int ex = 0; ex < 4; ex++)
							{
								u32 exit_off = be32 (data + np_off + 4 + ex * 4);
								node->exit_paths[ex] = -1;
								if (path_offsets && exit_off != 0xFFFFFFFF)
								{
									for (uint p = 0; p < pcnt; p++)
										if (path_offsets[p] == exit_off)
											{ node->exit_paths[ex] = (int)p; break; }
								}
							}

							node->tile_layer_off = be32 (data + np_off + 20);
							node->dood_layer_off = be32 (data + np_off + 24);
							node->type           = data[np_off + 31];

							// Extra data after offset 40
							if (node->type == KP_NODE_LEVEL && np_off + 43 <= size)
							{
								node->level_world = data[np_off + 40];
								node->level_num   = data[np_off + 41];
								node->has_secret  = data[np_off + 42] != 0;
							}
							else if (node->type == KP_NODE_CHANGE && np_off + 47 <= size)
							{
								uint dm_off = be32 (data + np_off + 40);
								if (dm_off && dm_off < size)
									node->dest_map = get_string_at (data, size, dm_off);
								node->this_id    = data[np_off + 44];
								node->foreign_id = data[np_off + 45];
								node->transition = data[np_off + 46];
							}
							else if (node->type == KP_NODE_WORLD_CHANGE && np_off + 41 <= size)
							{
								node->world_id = data[np_off + 40];
							}
						}
					}

					// Read paths
					if (layer->paths && path_offsets)
					{
						for (uint p = 0; p < pcnt; p++)
						{
							uint pp_off = path_offsets[p];
							if (pp_off + 28 > size)
								continue;

							kp_path_t *path = &layer->paths[p];
							u32 start_off = be32 (data + pp_off + 0);
							u32 end_off   = be32 (data + pp_off + 4);

							path->start_node = -1;
							path->end_node   = -1;
							if (node_offsets)
							{
								for (uint n = 0; n < ncnt; n++)
								{
									if (node_offsets[n] == start_off) path->start_node = (int)n;
									if (node_offsets[n] == end_off)   path->end_node   = (int)n;
								}
							}

							path->tile_layer_off = be32 (data + pp_off + 8);
							path->dood_layer_off = be32 (data + pp_off + 12);
							path->is_available   = data[pp_off + 16];
							path->is_secret      = data[pp_off + 17];
							path->speed          = be_float (data + pp_off + 20);
							path->animation      = be32 (data + pp_off + 24);
						}
					}

					FREE (node_offsets);
					FREE (path_offsets);
				}
			}
		}
	}

	// Parse Worlds
	if (world_cnt > 0 && world_off + world_cnt * 48 <= size)
	{
		kp->worlds = CALLOC (world_cnt, sizeof (kp_world_t));
		if (kp->worlds)
		{
			kp->n_worlds = world_cnt;
			for (uint w = 0; w < world_cnt; w++)
			{
				const u8 *wp = data + world_off + w * 48;
				kp_world_t *world = &kp->worlds[w];
				uint name_off = be32 (wp + 0);
				if (name_off && name_off < size)
					world->name = get_string_at (data, size, name_off);

				world->fs_text_color[0] = be32 (wp + 4);
				world->fs_text_color[1] = be32 (wp + 8);
				world->fs_hint_color[0] = be32 (wp + 12);
				world->fs_hint_color[1] = be32 (wp + 16);
				world->hud_text_color[0]= be32 (wp + 20);
				world->hud_text_color[1]= be32 (wp + 24);

				world->unique_key     = wp[32];
				world->music_track_id = wp[33];
				world->world_id       = wp[34];
				world->title_world    = wp[35] + 1; // stored as 0-indexed in binary
				world->title_level    = wp[36] + 1;
			}
		}
	}

	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
////////////////////  ResetKPBin  /////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void ResetKPBin (kpbin_t *kp)
{
	if (!kp) return;
	FREE (kp->bg_name);

	if (kp->layers)
	{
		for (uint i = 0; i < kp->n_layers; i++)
		{
			kp_layer_t *layer = &kp->layers[i];
			FREE (layer->tileset_name);
			FREE (layer->tile_indices);
			if (layer->doodads)
			{
				for (uint d = 0; d < layer->n_doodads; d++)
					FREE (layer->doodads[d].anims);
				FREE (layer->doodads);
			}
			if (layer->nodes)
			{
				for (uint n = 0; n < layer->n_nodes; n++)
					FREE (layer->nodes[n].dest_map);
				FREE (layer->nodes);
			}
			FREE (layer->paths);
		}
		FREE (kp->layers);
	}

	if (kp->worlds)
	{
		for (uint w = 0; w < kp->n_worlds; w++)
			FREE (kp->worlds[w].name);
		FREE (kp->worlds);
	}

	memset (kp, 0, sizeof (*kp));
}

//
///////////////////////////////////////////////////////////////////////////////
////////////////////  DumpKPBinText  //////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

enumError DumpKPBinText (const kpbin_t *kp, char **out, size_t *out_size)
{
	if (!kp || !out)
		return ERR_MISSING_PARAM;

	kp_sb_t s;
	ksb_init (&s);

	ksb_puts (&s, "# Koopatlas Binary World Map (.kpbin)\n");
	ksb_printf (&s, "version = %d\n", kp->version);
	if (kp->bg_name)
		ksb_printf (&s, "background = \"%s\"\n", kp->bg_name);
	ksb_printf (&s, "layers = %u\n", kp->n_layers);
	ksb_printf (&s, "worlds = %u\n\n", kp->n_worlds);

	// Worlds
	for (uint w = 0; w < kp->n_worlds; w++)
	{
		const kp_world_t *world = &kp->worlds[w];
		ksb_printf (&s, "[WORLD %u]\n", world->world_id ? world->world_id : (w + 1));
		if (world->name)
			ksb_printf (&s, "name = \"%s\"\n", world->name);
		ksb_printf (&s, "music_track = %u\n", world->music_track_id);
		ksb_printf (&s, "title_screen = %u-%u\n", world->title_world, world->title_level);
		ksb_putc (&s, '\n');
	}

	// Layers
	for (uint i = 0; i < kp->n_layers; i++)
	{
		const kp_layer_t *layer = &kp->layers[i];
		const char *ltype = layer->type < 3 ? layer_type_names[layer->type] : "unknown";
		ksb_printf (&s, "[LAYER %u: %s]\n", i, ltype);
		ksb_printf (&s, "alpha = %u\n", layer->alpha);

		if (layer->type == KP_LAYER_OBJECTS)
		{
			if (layer->tileset_name)
				ksb_printf (&s, "tileset = \"%s\"\n", layer->tileset_name);
			ksb_printf (&s, "sector_bounds = [%d, %d, %d, %d]\n",
				layer->sector_bounds[0], layer->sector_bounds[1],
				layer->sector_bounds[2], layer->sector_bounds[3]);
			ksb_printf (&s, "real_bounds   = [%d, %d, %d, %d]\n",
				layer->real_bounds[0], layer->real_bounds[1],
				layer->real_bounds[2], layer->real_bounds[3]);
			ksb_printf (&s, "tile_count    = %u\n", layer->n_tile_indices);
		}
		else if (layer->type == KP_LAYER_DOODADS)
		{
			ksb_printf (&s, "doodads = %u\n", layer->n_doodads);
			for (uint d = 0; d < layer->n_doodads; d++)
			{
				const kp_doodad_t *dood = &layer->doodads[d];
				ksb_printf (&s, "  doodad %3u: pos=(%.1f, %.1f) size=(%.1f, %.1f) angle=%.1f anims=%u\n",
					d, dood->x, dood->y, dood->w, dood->h, dood->angle, dood->n_anims);
				for (uint a = 0; a < dood->n_anims; a++)
				{
					const kp_doodad_anim_t *da = &dood->anims[a];
					const char *at = da->type < 6 ? anim_type_names[da->type] : "custom";
					const char *lt = da->loop < 3 ? loop_type_names[da->loop] : "custom";
					const char *ct = da->curve < 3 ? curve_type_names[da->curve] : "custom";
					ksb_printf (&s, "    anim %u: type=%s loop=%s curve=%s frames=%u range=[%u..%u] delay=%u offset=%u\n",
						a, at, lt, ct, da->frame_count, da->start, da->end, da->delay, da->delay_offset);
				}
			}
		}
		else if (layer->type == KP_LAYER_PATHS)
		{
			ksb_printf (&s, "nodes = %u\n", layer->n_nodes);
			for (uint n = 0; n < layer->n_nodes; n++)
			{
				const kp_node_t *node = &layer->nodes[n];
				const char *ntype = node->type < 5 ? node_type_names[node->type] : "unknown";
				ksb_printf (&s, "  node %3u: pos=(%5d, %5d) type=%-12s exits=[%d,%d,%d,%d]",
					n, node->x, node->y, ntype,
					node->exit_paths[0], node->exit_paths[1],
					node->exit_paths[2], node->exit_paths[3]);

				if (node->type == KP_NODE_LEVEL)
					ksb_printf (&s, " level=%u-%u%s", node->level_world, node->level_num,
						node->has_secret ? " (secret)" : "");
				else if (node->type == KP_NODE_CHANGE)
					ksb_printf (&s, " dest=\"%s\" this=%u foreign=%u",
						node->dest_map ? node->dest_map : "",
						node->this_id, node->foreign_id);
				else if (node->type == KP_NODE_WORLD_CHANGE)
					ksb_printf (&s, " world=%u", node->world_id);
				ksb_putc (&s, '\n');
			}

			ksb_printf (&s, "paths = %u\n", layer->n_paths);
			for (uint p = 0; p < layer->n_paths; p++)
			{
				const kp_path_t *path = &layer->paths[p];
				const char *anim = path->animation < 20 ? path_anim_names[path->animation] : "custom";
				ksb_printf (&s, "  path %3u: nodes %d -> %d  speed=%.1f  anim=%-10s  avail=%u secret=%u\n",
					p, path->start_node, path->end_node,
					path->speed, anim, path->is_available, path->is_secret);
			}
		}

		ksb_putc (&s, '\n');
	}

	if (!s.buf)
	{
		*out = NULL;
		if (out_size) *out_size = 0;
		return ERR_OUT_OF_MEMORY;
	}
	*out = s.buf;
	if (out_size) *out_size = s.len;
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
////////////////////  DumpKPBinJson  //////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

enumError DumpKPBinJson (const kpbin_t *kp, char **out, size_t *out_size)
{
	if (!kp || !out)
		return ERR_MISSING_PARAM;

	kp_sb_t s;
	ksb_init (&s);

	ksb_puts (&s, "{\n");
	ksb_printf (&s, "  \"_t\": \"KPMap\",\n");
	ksb_printf (&s, "  \"version\": %d,\n", kp->version);
	ksb_printf (&s, "  \"bgName\": \"%s\",\n", kp->bg_name ? kp->bg_name : "");

	// Worlds array
	ksb_puts (&s, "  \"worlds\": [\n");
	for (uint w = 0; w < kp->n_worlds; w++)
	{
		const kp_world_t *world = &kp->worlds[w];
		ksb_puts (&s, "    {\n");
		ksb_printf (&s, "      \"name\": \"%s\",\n", world->name ? world->name : "");
		ksb_printf (&s, "      \"worldID\": %u,\n", world->world_id);
		ksb_printf (&s, "      \"musicTrackID\": %u,\n", world->music_track_id);
		ksb_printf (&s, "      \"titleScreenID\": \"%u-%u\"\n", world->title_world, world->title_level);
		ksb_printf (&s, "    }%s\n", (w + 1 < kp->n_worlds) ? "," : "");
	}
	ksb_puts (&s, "  ],\n");

	// Layers array
	ksb_puts (&s, "  \"layers\": [\n");
	for (uint i = 0; i < kp->n_layers; i++)
	{
		const kp_layer_t *layer = &kp->layers[i];
		const char *ltype = layer->type < 3 ? layer_type_names[layer->type] : "unknown";
		ksb_puts (&s, "    {\n");
		ksb_printf (&s, "      \"type\": \"%s\",\n", ltype);
		ksb_printf (&s, "      \"alpha\": %u", layer->alpha);

		if (layer->type == KP_LAYER_OBJECTS)
		{
			ksb_printf (&s, ",\n      \"tileset\": \"%s\",\n", layer->tileset_name ? layer->tileset_name : "");
			ksb_printf (&s, "      \"sectorBounds\": [%d, %d, %d, %d],\n",
				layer->sector_bounds[0], layer->sector_bounds[1],
				layer->sector_bounds[2], layer->sector_bounds[3]);
			ksb_printf (&s, "      \"realBounds\": [%d, %d, %d, %d]\n",
				layer->real_bounds[0], layer->real_bounds[1],
				layer->real_bounds[2], layer->real_bounds[3]);
		}
		else if (layer->type == KP_LAYER_PATHS)
		{
			ksb_puts (&s, ",\n      \"nodes\": [\n");
			for (uint n = 0; n < layer->n_nodes; n++)
			{
				const kp_node_t *node = &layer->nodes[n];
				const char *ntype = node->type < 5 ? node_type_names[node->type] : "unknown";
				ksb_printf (&s, "        { \"x\": %d, \"y\": %d, \"type\": \"%s\"",
					node->x, node->y, ntype);
				if (node->type == KP_NODE_LEVEL)
					ksb_printf (&s, ", \"level\": [%u, %u], \"secret\": %s",
						node->level_world, node->level_num, node->has_secret ? "true" : "false");
				else if (node->type == KP_NODE_CHANGE)
					ksb_printf (&s, ", \"dest\": \"%s\", \"thisID\": %u, \"foreignID\": %u",
						node->dest_map ? node->dest_map : "", node->this_id, node->foreign_id);
				else if (node->type == KP_NODE_WORLD_CHANGE)
					ksb_printf (&s, ", \"worldID\": %u", node->world_id);
				ksb_printf (&s, " }%s\n", (n + 1 < layer->n_nodes) ? "," : "");
			}
			ksb_puts (&s, "      ],\n");

			ksb_puts (&s, "      \"paths\": [\n");
			for (uint p = 0; p < layer->n_paths; p++)
			{
				const kp_path_t *path = &layer->paths[p];
				const char *anim = path->animation < 20 ? path_anim_names[path->animation] : "custom";
				ksb_printf (&s, "        { \"start\": %d, \"end\": %d, \"speed\": %.2f, \"anim\": \"%s\", \"available\": %u, \"secret\": %u }%s\n",
					path->start_node, path->end_node, path->speed, anim, path->is_available, path->is_secret,
					(p + 1 < layer->n_paths) ? "," : "");
			}
			ksb_puts (&s, "      ]\n");
		}
		else
		{
			ksb_putc (&s, '\n');
		}

		ksb_printf (&s, "    }%s\n", (i + 1 < kp->n_layers) ? "," : "");
	}
	ksb_puts (&s, "  ]\n");
	ksb_puts (&s, "}\n");

	if (!s.buf)
	{
		*out = NULL;
		if (out_size) *out_size = 0;
		return ERR_OUT_OF_MEMORY;
	}
	*out = s.buf;
	if (out_size) *out_size = s.len;
	return ERR_OK;
}

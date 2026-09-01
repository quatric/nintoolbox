#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lib-shp.h"
#include "lib-szs.h"
#include "lib-brres.h"

// byte order helpers
static inline u16 shp0_rd16 (const u8 *p) { return (u16)p[0] << 8 | p[1]; }

static inline u32 shp0_rd32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static inline float shp0_rdf (const u8 *p)
{
	union { u32 u; float f; } v;
	v.u = shp0_rd32(p);
	return v.f;
}

static inline void shp0_w16 (u8 *p, u16 v)
{
	p[0] = v >> 8;
	p[1] = (u8)v;
}

static inline void shp0_w32 (u8 *p, u32 v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = (u8)v;
}

static inline void shp0_wf (u8 *p, float f)
{
	union { u32 u; float f; } v;
	v.f = f;
	shp0_w32(p, v.u);
}

static inline uint shp0_align (uint val, uint align) { return (val + align - 1) & ~(align - 1); }

void InitializeSHP0 (shp0_t *shp)
{
	DASSERT (shp);
	memset (shp, 0, sizeof (*shp));
	shp->version = SHP0_DEFAULT_VERSION;
}

void ResetSHP0 (shp0_t *shp)
{
	if (!shp)
		return;

	FreeString (shp->fname);
	FreeString (shp->name);
	FreeString (shp->orig_path);

	for (uint i = 0; i < shp->n_entry; i++)
	{
		FreeString (shp->entry[i].name);
		for (uint j = 0; j < shp->entry[i].n_vset; j++) {
			FreeString (shp->entry[i].vset[j].name);
			FREE (shp->entry[i].vset[j].keyframes);
		}
		FREE (shp->entry[i].vset);
	}
	FREE (shp->entry);
	
	for (uint i = 0; i < shp->n_strings; i++)
		FreeString (shp->strings[i]);
	FREE (shp->strings);

	InitializeSHP0 (shp);
}

shp0_entry_t *AppendEntrySHP0 (shp0_t *shp, ccp name)
{
	DASSERT (shp);
	if (shp->n_entry == shp->n_entry_alloced)
	{
		shp->n_entry_alloced = shp->n_entry_alloced ? shp->n_entry_alloced * 2 : 8;
		shp->entry = REALLOC (shp->entry, shp->n_entry_alloced * sizeof (*shp->entry));
	}

	shp0_entry_t *e = shp->entry + shp->n_entry++;
	memset (e, 0, sizeof (*e));
	e->name = STRDUP (name ? name : "");
	e->flags = 3; // default enabled
	return e;
}

shp0_vertex_set_t *AppendVertexSetSHP0 (shp0_entry_t *entry, ccp name)
{
	DASSERT (entry);
	if (entry->n_vset == entry->n_vset_alloced)
	{
		entry->n_vset_alloced = entry->n_vset_alloced ? entry->n_vset_alloced * 2 : 8;
		entry->vset = REALLOC (entry->vset, entry->n_vset_alloced * sizeof (*entry->vset));
	}

	shp0_vertex_set_t *v = entry->vset + entry->n_vset++;
	memset (v, 0, sizeof (*v));
	v->name = STRDUP (name ? name : "");
	v->is_fixed = true;
	v->fixed_value = 0.0f;
	return v;
}

static ccp get_string(shp0_t *shp, uint index) {
	if (index < shp->n_strings)
		return shp->strings[index];
	return "?";
}

enumError ScanRawSHP0 (shp0_t *shp, bool init_shp, const void *data, uint data_size)
{
	DASSERT (shp);
	DASSERT (data);

	if (init_shp)
		InitializeSHP0 (shp);
	else
		ResetSHP0 (shp);

	const u8 *base = data;
	if (data_size < 0x28 || memcmp (base, "SHP0", 4))
		return ERR_WRONG_FILE_TYPE;

	const u32 version = shp0_rd32 (base + 8);
	if (version < SHP0_MIN_VERSION || version > SHP0_MAX_VERSION)
		return ERROR0 (ERR_INVALID_DATA, "SHP0: unsupported sub-version %u\n", version);
	shp->version = version;

	const uint head_size = version == 4 ? 0x2C : 0x28;
	if (data_size < head_size)
		return ERROR0 (ERR_INVALID_DATA, "SHP0: file too small\n");

	const u32 data_off = shp0_rd32 (base + 0x10);
	const u32 string_list_off = shp0_rd32 (base + 0x14);
	
	uint off = 0x18;
	if (version == 4)
		off += 4; // skip user_data_offset
	const u32 string_off = shp0_rd32 (base + off);
	off += 4;
	const u32 orig_path_off = shp0_rd32 (base + off);
	off += 4;
	const u32 n_frames = shp0_rd16 (base + off);
	off += 2;
	const u32 n_entries = shp0_rd16 (base + off);
	off += 2;
	const u32 loop = shp0_rd32 (base + off);

	if (string_off && string_off < data_size)
		shp->name = STRDUP ((ccp)(base + string_off));
	if (orig_path_off && orig_path_off < data_size)
		shp->orig_path = STRDUP ((ccp)(base + orig_path_off));
	shp->n_frames = n_frames;
	shp->loop = loop != 0;

	// Read string list
	if (string_list_off && string_list_off < data_size) {
		const u8 *str_list = base + string_list_off;
		shp->n_strings = n_entries;
		shp->n_strings_alloced = n_entries;
		shp->strings = CALLOC(n_entries, sizeof(ccp));
		for (uint i = 0; i < n_entries; i++) {
			u32 soff = shp0_rd32(str_list + i * 4);
			if (string_list_off + i * 4 + soff < data_size) {
				shp->strings[i] = STRDUP((ccp)(str_list + i * 4 + soff));
			} else {
				shp->strings[i] = STRDUP("?");
			}
		}
	}

	if (!data_off || data_off + 8 > data_size)
		return ERROR0 (ERR_INVALID_DATA, "SHP0: invalid group offset\n");
	const u8 *group = base + data_off;
	const u32 grp_n_entries = shp0_rd32 (group + 4);
	if (data_off + 8 + (u64)(grp_n_entries + 1) * 16 > data_size)
		return ERROR0 (ERR_INVALID_DATA, "SHP0: group entry table exceeds file size\n");

	for (u32 i = 1; i <= grp_n_entries; i++)
	{
		const u8 *rec = group + 8 + (size_t)i * 16;
		const u32 name_off = shp0_rd32 (rec + 8);
		const u32 entry_off = shp0_rd32 (rec + 12);
		const u8 *entry = group + entry_off;
		if (!entry_off || entry_off + 0x14 > data_size - data_off)
			return ERROR0 (ERR_INVALID_DATA, "SHP0: invalid entry offset\n");

		char namebuf[24];
		ccp name;
		if (name_off && data_off + name_off < data_size)
			name = (ccp)(base + data_off + name_off);
		else
		{
			snprintf (namebuf, sizeof (namebuf), "?0x%x", name_off);
			name = namebuf;
		}
		shp0_entry_t *e = AppendEntrySHP0 (shp, name);

		e->flags = shp0_rd32 (entry + 0);
		// entry + 4 is string offset
		u16 name_index = shp0_rd16 (entry + 8);
		u16 num_indices = shp0_rd16 (entry + 10);
		u32 fixed_flags = shp0_rd32 (entry + 12);
		u32 indicies_offset = shp0_rd32 (entry + 16); // Relative to entry
		
		const u8 *indicies = entry + indicies_offset;
		const u8 *entry_offsets = entry + indicies_offset - 4 * num_indices;
		
		for (u16 j = 0; j < num_indices; j++) {
			u16 v_name_index = shp0_rd16(indicies + j * 2);
			ccp v_name = get_string(shp, v_name_index);
			
			shp0_vertex_set_t *v = AppendVertexSetSHP0(e, v_name);
			if ((fixed_flags >> j) & 1) {
				v->is_fixed = true;
				v->fixed_value = shp0_rdf(entry_offsets + j * 4);
			} else {
				v->is_fixed = false;
				u32 voff = shp0_rd32(entry_offsets + j * 4);
				const u8 *kf_data = entry_offsets + j * 4 + voff;
				
				u16 kf_count = shp0_rd16(kf_data + 0);
				v->n_keyframes = kf_count;
				if (kf_count > 0) {
					v->keyframes = CALLOC(kf_count, sizeof(shp0_keyframe_t));
					const u8 *kf_entries = kf_data + 8;
					for (u16 k = 0; k < kf_count; k++) {
						v->keyframes[k].index = shp0_rdf(kf_entries + k * 12 + 0);
						v->keyframes[k].value = shp0_rdf(kf_entries + k * 12 + 4);
						v->keyframes[k].tangent = shp0_rdf(kf_entries + k * 12 + 8);
					}
				}
			}
		}
	}

	return ERR_OK;
}

enumError SaveRawSHP0 (shp0_t *shp, ccp fname, bool set_time)
{
	return ERROR0(ERR_NOT_IMPLEMENTED, "SHP0 raw saving not implemented yet.\n");
}

enumError SaveTextSHP0 (shp0_t *shp, ccp fname, bool set_time)
{
	return ERROR0(ERR_NOT_IMPLEMENTED, "SHP0 text saving not implemented yet.\n");
}

enumError ScanTextSHP0 (shp0_t *shp, bool init_shp, ccp src_fname)
{
	return ERROR0(ERR_NOT_IMPLEMENTED, "SHP0 text scanning not implemented yet.\n");
}


// SPDX-License-Identifier: GPL-2.0+
#include "lib-hbdf.h"
#include "lib-archive-util.h"
#include "lib-nsbmd.h"
#include "lib-std.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

bool IsHBDF (const u8 *data, uint size)
{
	if (!data || size < 8)
		return false;
	if (memcmp (data, "HBDF", 4) && memcmp (data, "HSDF", 4))
		return false;
	const u32 fsize = rd_le32 (data + 4);
	if (fsize < 8 || fsize > size)
		return false;
	return true;
}

// Simple LZ77 decompressor for DS HBDF chunks
static u8 *hbdf_decompress_lz77 (const u8 *src, size_t src_len, size_t *out_len)
{
	if (!src || src_len < 4)
		return NULL;
	const size_t declen = (size_t)src[1] | ((size_t)src[2] << 8) | ((size_t)src[3] << 16);
	if (declen == 0 || declen > 16 * 1024 * 1024)
		return NULL;

	u8 *dest = MALLOC (declen);
	if (!dest)
		return NULL;

	size_t sidx = 4;
	size_t didx = 0;
	size_t remaining = declen;

	while (remaining > 0 && sidx < src_len)
	{
		u8 flag_byte = src[sidx++];
		for (int b = 0; b < 8 && remaining > 0; b++)
		{
			if (flag_byte & 0x80)
			{
				if (sidx + 2 > src_len)
					break;
				const u16 pair = ((u16)src[sidx] << 8) | (u16)src[sidx + 1];
				sidx += 2;
				const size_t copy_len = (pair >> 12) + 3;
				const size_t disp = (pair & 0x0FFF) + 1;
				if (disp > didx)
					break;
				size_t cpos = didx - disp;
				for (size_t c = 0; c < copy_len && remaining > 0; c++)
				{
					dest[didx++] = dest[cpos++];
					remaining--;
				}
			}
			else
			{
				if (sidx >= src_len)
					break;
				dest[didx++] = src[sidx++];
				remaining--;
			}
			flag_byte <<= 1;
		}
	}

	if (out_len)
		*out_len = didx;
	return dest;
}

// Reads a NAME sub-block and returns allocated string
static char *hbdf_read_name_block (const u8 *data, size_t size, size_t *pos)
{
	if (*pos + 8 > size)
		return NULL;
	if (memcmp (data + *pos, "NAME", 4))
		return NULL;
	const u32 nsize = rd_le32 (data + *pos + 4);
	if (*pos + 8 + nsize > size)
		return NULL;

	char *str = CALLOC (nsize + 1, 1);
	if (str)
		memcpy (str, data + *pos + 8, nsize);
	*pos += 8 + nsize;
	return str;
}

// Helper to parse TEXS chunk and unpack embedded images / palettes
static void hbdf_unpack_texs (nintendo_sarc_entry_t *out, uint *out_cnt, const u8 *data, size_t texs_size, uint blk_idx)
{
	if (texs_size < 16)
		return;

	// Structure: [magic:4 "TEXS"][size:4][numInfos:2][numImages:2][numPalettes:2][pad:2]
	const u16 num_infos = rd_le16 (data + 8);
	const u16 num_images = rd_le16 (data + 10);
	const u16 num_palettes = rd_le16 (data + 12);

	size_t pos = 16;
	const uint total_subblocks = (uint)num_infos + (uint)num_images + (uint)num_palettes;

	for (uint s = 0; s < total_subblocks && pos + 8 <= texs_size; s++)
	{
		char subtag[5];
		memcpy (subtag, data + pos, 4);
		subtag[4] = '\0';
		const u32 sub_len = rd_le32 (data + pos + 4);
		if (pos + 8 + sub_len > texs_size)
			break;

		const u8 *sub_ptr = data + pos + 8;
		pos += 8;

		if (!strcmp (subtag, "IMGO"))
		{
			// IMGO format: [NAME block] [Format:4][Width:2][Height:2][Params:4][texSize:4][tex4x4Size:4][compFlags:4][ImageData...]
			size_t ipos = 0;
			char *tex_name = hbdf_read_name_block (sub_ptr, sub_len, &ipos);
			if (ipos + 24 <= sub_len)
			{
				const u16 w = rd_le16 (sub_ptr + ipos + 4);
				const u16 h = rd_le16 (sub_ptr + ipos + 6);
				const u32 tex_size = rd_le32 (sub_ptr + ipos + 12);
				const u32 comp_flags = rd_le32 (sub_ptr + ipos + 20);
				const size_t raw_data_pos = ipos + 24;

				if (raw_data_pos + tex_size <= sub_len)
				{
					char entry_name[128];
					snprintf (entry_name, sizeof (entry_name), "%02u_TEXS_%s_%ux%u.bin",
						blk_idx, tex_name && tex_name[0] ? tex_name : "image", w, h);

					if (comp_flags == 1)
					{
						size_t dec_sz = 0;
						u8 *dec = hbdf_decompress_lz77 (sub_ptr + raw_data_pos, tex_size, &dec_sz);
						if (dec)
						{
							OwnedEntryAdd (out, (*out_cnt)++, entry_name, dec, (u32)dec_sz);
							FREE (dec);
						}
						else
						{
							OwnedEntryAdd (out, (*out_cnt)++, entry_name, sub_ptr + raw_data_pos, tex_size);
						}
					}
					else
					{
						OwnedEntryAdd (out, (*out_cnt)++, entry_name, sub_ptr + raw_data_pos, tex_size);
					}
				}
			}
			if (tex_name)
				FREE (tex_name);
		}
		else if (!strcmp (subtag, "PLTO"))
		{
			// PLTO format: [NAME block][size:4][compFlags:4][PaletteData...]
			size_t ppos = 0;
			char *pal_name = hbdf_read_name_block (sub_ptr, sub_len, &ppos);
			if (ppos + 8 <= sub_len)
			{
				const u32 pal_sz = rd_le32 (sub_ptr + ppos);
				const u32 comp_flags = rd_le32 (sub_ptr + ppos + 4);
				const size_t pal_data_pos = ppos + 8;
				if (pal_data_pos + pal_sz <= sub_len)
				{
					char entry_name[128];
					snprintf (entry_name, sizeof (entry_name), "%02u_PLTO_%s.bin",
						blk_idx, pal_name && pal_name[0] ? pal_name : "pal");

					if (comp_flags == 1)
					{
						size_t dec_sz = 0;
						u8 *dec = hbdf_decompress_lz77 (sub_ptr + pal_data_pos, pal_sz, &dec_sz);
						if (dec)
						{
							OwnedEntryAdd (out, (*out_cnt)++, entry_name, dec, (u32)dec_sz);
							FREE (dec);
						}
						else
						{
							OwnedEntryAdd (out, (*out_cnt)++, entry_name, sub_ptr + pal_data_pos, pal_sz);
						}
					}
					else
					{
						OwnedEntryAdd (out, (*out_cnt)++, entry_name, sub_ptr + pal_data_pos, pal_sz);
					}
				}
			}
			if (pal_name)
				FREE (pal_name);
		}
		pos += sub_len;
	}
}

enumError ScanHBDF (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || !IsHBDF (data, size))
		return ERR_INVALID_DATA;

	*entries = 0;
	*n_entries = 0;

	// Count maximum potential entries
	uint block_cnt = 0;
	uint pos = 8;
	const u32 total_size = rd_le32 (data + 4);
	const uint limit = total_size <= size ? total_size : size;

	while (pos + 8 <= limit)
	{
		const u32 bsize = rd_le32 (data + pos + 4);
		if (bsize < 8 || pos + bsize > limit)
			break;
		block_cnt += 64; // Allow extra slots for texture/palette sub-blocks
		pos += bsize;
	}

	if (!block_cnt)
		return ERR_NOTHING_TO_DO;

	nintendo_sarc_entry_t *out = CALLOC (block_cnt, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint out_cnt = 0;
	pos = 8;
	uint i = 0;
	while (pos + 8 <= limit)
	{
		char tag[5];
		memcpy (tag, data + pos, 4);
		tag[4] = 0;

		const u32 bsize = rd_le32 (data + pos + 4);
		if (bsize < 8 || pos + bsize > limit)
			break;

		char name[64];
		snprintf (name, sizeof (name), "%02u_%s.bin", i, tag);
		OwnedEntryAdd (out, out_cnt++, name, data + pos, bsize);

		if (!strcmp (tag, "TEXS"))
		{
			hbdf_unpack_texs (out, &out_cnt, data + pos, bsize, i);
		}

		pos += bsize;
		i++;
	}

	*entries = out;
	*n_entries = out_cnt;
	return ERR_OK;
}

// ----------------------------------------------------------------------------
// HSDF/DS model geometry to model_t.
//
// Reference: MPLibrary/DS/HBDF/* (HsdfFile/ModelBlock/ObjectBlock/MeshBlock/
// StringTable/TextureBlock) + ToolWrappers/HBDF/HBDF.cs ToGeneric/LoadMesh.
// Display lists run through the shared NSBMD interpreter (AppendDSGXMesh)
// instead of a second copy. Per-object world transforms (parent chain)
// are baked into positions; normals get the rotation part, renormalized
// (the reference leaves normals untransformed). One output mesh is emitted
// per PolyGroup material range; FaceStart/FaceCount index the decoded
// triangle list exactly like the reference's ctx.indices[] window.
// SkinningBlock/ENVS/ANMF carry no fields in the reference and are skipped.

static float hbdf_lef32 (const u8 *p)
{
	u32 u = (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
	float f;
	memcpy (&f, &u, 4);
	return f;
}

static int32_t hbdf_les32 (const u8 *p)
{
	return (int32_t)((u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24);
}

// Column-major 4x4.
typedef struct { float m[16]; } hbdf_mat4_t;

static void hbdf_m4_ident (hbdf_mat4_t *o)
{
	memset (o, 0, sizeof (*o));
	o->m[0] = o->m[5] = o->m[10] = o->m[15] = 1.0f;
}

static void hbdf_m4_mul (hbdf_mat4_t *o, const hbdf_mat4_t *a, const hbdf_mat4_t *b)
{
	hbdf_mat4_t t;
	for (int c = 0; c < 4; c++)
		for (int r = 0; r < 4; r++)
			t.m[c * 4 + r] = a->m[r] * b->m[c * 4] + a->m[4 + r] * b->m[c * 4 + 1]
				+ a->m[8 + r] * b->m[c * 4 + 2] + a->m[12 + r] * b->m[c * 4 + 3];
	*o = t;
}

static void hbdf_m4_trs (hbdf_mat4_t *o, const float t[3], const float r[3],
	const float s[3], const float ms[3])
{
	const float cx = cosf (r[0]), sx = sinf (r[0]);
	const float cy = cosf (r[1]), sy = sinf (r[1]);
	const float cz = cosf (r[2]), sz = sinf (r[2]);
	// R = Rx * Ry * Rz.
	hbdf_mat4_t m;
	memset (&m, 0, sizeof (m));
	m.m[0] = cy * cz * s[0] * ms[0];
	m.m[1] = (sx * sy * cz + cx * sz) * s[0] * ms[0];
	m.m[2] = (-cx * sy * cz + sx * sz) * s[0] * ms[0];
	m.m[4] = -cy * sz * s[1] * ms[1];
	m.m[5] = (-sx * sy * sz + cx * cz) * s[1] * ms[1];
	m.m[6] = (cx * sy * sz + sx * cz) * s[1] * ms[1];
	m.m[8] = sy * s[2] * ms[2];
	m.m[9] = -sx * cy * s[2] * ms[2];
	m.m[10] = cx * cy * s[2] * ms[2];
	m.m[12] = t[0];
	m.m[13] = t[1];
	m.m[14] = t[2];
	m.m[15] = 1.0f;
	*o = m;
}

static void hbdf_m4_point (const hbdf_mat4_t *m, float *x, float *y, float *z)
{
	const float px = *x, py = *y, pz = *z;
	*x = m->m[0] * px + m->m[4] * py + m->m[8] * pz + m->m[12];
	*y = m->m[1] * px + m->m[5] * py + m->m[9] * pz + m->m[13];
	*z = m->m[2] * px + m->m[6] * py + m->m[10] * pz + m->m[14];
}

static void hbdf_m4_vec (const hbdf_mat4_t *m, float *x, float *y, float *z)
{
	const float px = *x, py = *y, pz = *z;
	*x = m->m[0] * px + m->m[4] * py + m->m[8] * pz;
	*y = m->m[1] * px + m->m[5] * py + m->m[9] * pz;
	*z = m->m[2] * px + m->m[6] * py + m->m[10] * pz;
	const float l = sqrtf (*x * *x + *y * *y + *z * *z);
	if (l > 1e-9f)
	{
		*x /= l;
		*y /= l;
		*z /= l;
	}
}

#define HBDF_MAX_OBJECTS 512
#define HBDF_MAX_MATERIALS 256
#define HBDF_MAX_ATTRS 256
#define HBDF_MAX_IMAGES 256

typedef struct
{
	char name[64];
	int attr_idx;
} hbdf_material_t;

typedef struct
{
	char name[64];
	char texname[64];
} hbdf_attr_t;

typedef struct
{
	u16 mat_idx;
	u16 face_start;
	u16 face_count;
} hbdf_poly_t;

typedef struct
{
	int type;
	int parent;
	char name[64];
	float t[3];
	float r[3];
	float s[3];
	float ms[3];
	hbdf_poly_t *polys;
	uint n_polys;
	const u8 *dl;
	uint dl_size;
} hbdf_object_t;

typedef struct
{
	hbdf_object_t *objects;
	uint n_objects;
	hbdf_material_t *materials;
	uint n_materials;
	hbdf_attr_t *attrs;
	uint n_attrs;
} hbdf_modelblk_t;

typedef struct
{
	char name[64];
	u32 format;
	u16 w;
	u16 h;
} hbdf_image_t;

static void hbdf_copy_name (char *dst, size_t cap, const u8 *src, size_t maxlen, size_t avail)
{
	size_t n = 0;
	while (n < maxlen && n + 1 < cap && n < avail && src[n])
	{
		dst[n] = (char)src[n];
		n++;
	}
	dst[n] = 0;
}

// Reads a NAME sub-block at *pos (magic + u32 size + bytes); advances *pos
// past it. Returns false on truncation.
static bool hbdf_read_name (char *dst, size_t cap, const u8 *data, size_t size, size_t *pos)
{
	if (*pos + 8 > size || memcmp (data + *pos, "NAME", 4))
		return false;
	const u32 nsize = rd_le32 (data + *pos + 4);
	if (*pos + 8 + nsize > size)
		return false;
	hbdf_copy_name (dst, cap, data + *pos + 8, nsize, nsize);
	*pos += 8 + nsize;
	return true;
}

// Builds an offset -> string map for a STRB payload (strings blob at blob,
// blob_size bytes, offsets relative to blob start).
typedef struct
{
	u32 off;
	char text[64];
} hbdf_strmap_t;

static const char *hbdf_str_lookup (
	const hbdf_strmap_t *map, uint n, u32 off)
{
	for (uint i = 0; i < n; i++)
		if (map[i].off == off)
			return map[i].text;
	return "";
}

// Parses one MDLF model block body (payload at data, len bytes) into blk.
// Returns false when the fixed header is truncated; unknown trailing
// blocks are skipped, never fatal.
static bool hbdf_parse_model (hbdf_modelblk_t *blk, const u8 *data, size_t len)
{
	memset (blk, 0, sizeof (*blk));
	if (len < 40 + 8)
		return false;
	// 10 u32 unknowns, then 4 u16 counts.
	const u16 num_objects = rd_le16 (data + 40);
	const u16 num_materials = rd_le16 (data + 42);
	const u16 num_textures = rd_le16 (data + 44);
	const u16 num_matrices = rd_le16 (data + 46);
	if (num_objects > HBDF_MAX_OBJECTS || num_materials > HBDF_MAX_MATERIALS
		|| num_textures > HBDF_MAX_ATTRS)
		return false;
	size_t pos = 48;
	// MaterialBlock structs (24 bytes).
	hbdf_material_t *mats = 0;
	u32 *mat_name_offs = 0;
	int *mat_attr_idx = 0;
	if (num_materials)
	{
		if (pos + (size_t)num_materials * 24 > len)
			return false;
		mats = CALLOC (num_materials, sizeof (*mats));
		mat_name_offs = CALLOC (num_materials, sizeof (*mat_name_offs));
		mat_attr_idx = CALLOC (num_materials, sizeof (*mat_attr_idx));
		if (!mats || !mat_name_offs || !mat_attr_idx)
		{
			FREE (mats);
			FREE (mat_name_offs);
			FREE (mat_attr_idx);
			return false;
		}
		for (uint i = 0; i < num_materials; i++)
		{
			const u8 *e = data + pos + (size_t)i * 24;
			mat_name_offs[i] = rd_le32 (e);
			mat_attr_idx[i] = (int16_t)rd_le16 (e + 18);
		}
		pos += (size_t)num_materials * 24;
	}
	// AttributeBlocks (48 bytes each).
	hbdf_attr_t *attrs = 0;
	u32 *attr_name_offs = 0;
	u32 *attr_tex_offs = 0;
	if (num_textures)
	{
		if (pos + (size_t)num_textures * 48 > len)
		{
			FREE (mats);
			FREE (mat_name_offs);
			FREE (mat_attr_idx);
			return false;
		}
		attrs = CALLOC (num_textures, sizeof (*attrs));
		attr_name_offs = CALLOC (num_textures, sizeof (*attr_name_offs));
		attr_tex_offs = CALLOC (num_textures, sizeof (*attr_tex_offs));
		if (!attrs || !attr_name_offs || !attr_tex_offs)
		{
			FREE (mats);
			FREE (mat_name_offs);
			FREE (mat_attr_idx);
			FREE (attrs);
			FREE (attr_name_offs);
			FREE (attr_tex_offs);
			return false;
		}
		for (uint i = 0; i < num_textures; i++)
		{
			const u8 *e = data + pos + (size_t)i * 48;
			attr_name_offs[i] = rd_le32 (e);
			attr_tex_offs[i] = rd_le16 (e + 46);
		}
		pos += (size_t)num_textures * 48;
	}
	// MatrixBlocks (40 bytes each): transforms unused by the reference
	// renderer, skipped but bounds-checked.
	if (pos + (size_t)num_matrices * 40 > len)
	{
		FREE (mats);
		FREE (mat_name_offs);
		FREE (mat_attr_idx);
		FREE (attrs);
		FREE (attr_name_offs);
		FREE (attr_tex_offs);
		return false;
	}
	pos += (size_t)num_matrices * 40;

	hbdf_object_t *objs = CALLOC (num_objects ? num_objects : 1, sizeof (*objs));
	if (!objs)
	{
		FREE (mats);
		FREE (mat_name_offs);
		FREE (mat_attr_idx);
		FREE (attrs);
		FREE (attr_name_offs);
		FREE (attr_tex_offs);
		return false;
	}
	uint n_objs = 0;
	hbdf_strmap_t *strmap = 0;
	uint n_strmap = 0;
	bool have_strmap = false;
	// Walk sibling blocks: OBJO objects (each optionally followed by its
	// MESH block), then the STRB string table. The table comes after all
	// objects, so the walk must continue past num_objects (the reference
	// reads the table separately after its object loop).
	while (pos + 8 <= len && (n_objs < num_objects || !have_strmap))
	{
		char tag[5];
		memcpy (tag, data + pos, 4);
		tag[4] = 0;
		const u32 bsize = rd_le32 (data + pos + 4);
		if (bsize < 8 || pos + bsize > len)
			break;
		if (!strcmp (tag, "OBJO"))
		{
			if (n_objs >= num_objects)
			{
				pos += bsize; // more objects than announced; skip
				continue;
			}
			if (bsize < 8 + 48)
				break;
			const u8 *o = data + pos + 8;
			hbdf_object_t *ob = objs + n_objs;
			ob->type = rd_le16 (o);
			ob->parent = (int16_t)rd_le16 (o + 2);
			const u32 name_off = rd_le32 (o + 8);
			for (int c = 0; c < 3; c++)
				ob->t[c] = hbdf_lef32 (o + 12 + c * 4);
			for (int c = 0; c < 3; c++)
				ob->r[c] = (float)hbdf_les32 (o + 24 + c * 4) / 16384.0f;
			for (int c = 0; c < 3; c++)
				ob->s[c] = hbdf_lef32 (o + 36 + c * 4);
			if (!ob->s[0])
				ob->s[0] = 1.0f;
			if (!ob->s[1])
				ob->s[1] = 1.0f;
			if (!ob->s[2])
				ob->s[2] = 1.0f;
			ob->ms[0] = ob->ms[1] = ob->ms[2] = 1.0f;
			// Stash the name offset in the name field until STRB resolves.
			snprintf (ob->name, sizeof (ob->name), "@%u", name_off);
			pos += bsize;
			n_objs++;
			// A MESH sibling right after its object belongs to it
			// (magic-driven; the reference keys off the object type).
			if (pos + 8 <= len && !memcmp (data + pos, "MESH", 4))
			{
				const u32 msize = rd_le32 (data + pos + 4);
				if (msize >= 8 + 32 && pos + msize <= len)
				{
					const u8 *m = data + pos + 8;
					for (int c = 0; c < 3; c++)
						ob->ms[c] = hbdf_lef32 (m + c * 4);
					if (!ob->ms[0])
						ob->ms[0] = 1.0f;
					if (!ob->ms[1])
						ob->ms[1] = 1.0f;
					if (!ob->ms[2])
						ob->ms[2] = 1.0f;
					const u16 nblocks = rd_le16 (m + 28);
					const u16 dsize = rd_le16 (m + 30);
					if (nblocks && nblocks <= 256
						&& 32 + (size_t)nblocks * 8 + dsize <= msize - 8)
					{
						ob->polys = CALLOC (nblocks, sizeof (*ob->polys));
						if (ob->polys)
						{
							for (uint g = 0; g < nblocks; g++)
							{
								const u8 *pg = m + 32 + (size_t)g * 8;
								ob->polys[g].mat_idx = rd_le16 (pg);
								ob->polys[g].face_start = rd_le16 (pg + 4);
								ob->polys[g].face_count = rd_le16 (pg + 6);
							}
							ob->n_polys = nblocks;
							ob->dl = m + 32 + (size_t)nblocks * 8;
							ob->dl_size = dsize;
						}
					}
				}
				pos += msize;
			}
		}
		else if (!strcmp (tag, "STRB"))
		{
			// String table: NUL strings from payload start; offsets
			// relative to it.
			const u8 *blob = data + pos + 8;
			const size_t blob_size = bsize - 8;
			uint cap = 64;
			strmap = CALLOC (cap, sizeof (*strmap));
			if (strmap)
			{
				size_t sp = 0;
				while (sp < blob_size)
				{
					if (n_strmap == cap)
					{
						cap *= 2;
						hbdf_strmap_t *nb = REALLOC (strmap, cap * sizeof (*nb));
						if (!nb)
							break;
						strmap = nb;
					}
					strmap[n_strmap].off = (u32)sp;
					hbdf_copy_name (strmap[n_strmap].text, sizeof (strmap[n_strmap].text),
						blob + sp, blob_size - sp, blob_size - sp);
					if (!blob[sp])
						sp++;
					else
					{
						while (sp < blob_size && blob[sp])
							sp++;
						sp++; // NUL
					}
					// 4-byte alignment like the reference reader.
					while (sp < blob_size && (sp & 3) && !blob[sp])
						sp++;
					n_strmap++;
					if (sp >= blob_size)
						break;
				}
			}
			pos += bsize;
			have_strmap = true;
			break; // STRB closes the model.
		}
		else
		{
			// SKIN/ENVS/ANMF carry no fields in the reference; any other
			// tag is skipped by its own size.
			pos += bsize;
		}
	}

	// Resolve names.
	for (uint i = 0; i < n_objs; i++)
	{
		if (objs[i].name[0] == '@')
		{
			const u32 off = (u32)strtoul (objs[i].name + 1, 0, 10);
			snprintf (objs[i].name, sizeof (objs[i].name), "%s",
				hbdf_str_lookup (strmap, n_strmap, off));
			if (!objs[i].name[0])
				snprintf (objs[i].name, sizeof (objs[i].name), "obj%u", i);
		}
	}
	for (uint i = 0; i < num_materials; i++)
	{
		snprintf (mats[i].name, sizeof (mats[i].name), "%s",
			hbdf_str_lookup (strmap, n_strmap, mat_name_offs[i]));
		if (!mats[i].name[0])
			snprintf (mats[i].name, sizeof (mats[i].name), "mat%u", i);
		mats[i].attr_idx = mat_attr_idx[i];
	}
	for (uint i = 0; i < num_textures; i++)
	{
		snprintf (attrs[i].name, sizeof (attrs[i].name), "%s",
			hbdf_str_lookup (strmap, n_strmap, attr_name_offs[i]));
		// TextureNameOffset indexes the same table by byte offset in the
		// reference (TextureNameOffset is u16); look it up directly.
		snprintf (attrs[i].texname, sizeof (attrs[i].texname), "%s",
			hbdf_str_lookup (strmap, n_strmap, attr_tex_offs[i]));
	}
	FREE (strmap);
	FREE (mat_name_offs);
	FREE (mat_attr_idx);
	FREE (attr_name_offs);
	FREE (attr_tex_offs);

	blk->objects = objs;
	blk->n_objects = n_objs;
	blk->materials = mats;
	blk->n_materials = num_materials;
	blk->attrs = attrs;
	blk->n_attrs = num_textures;
	return true;
}

static void hbdf_free_modelblk (hbdf_modelblk_t *blk)
{
	if (!blk)
		return;
	for (uint i = 0; i < blk->n_objects; i++)
		FREE (blk->objects[i].polys);
	FREE (blk->objects);
	FREE (blk->materials);
	FREE (blk->attrs);
	memset (blk, 0, sizeof (*blk));
}

// World matrix for object i (parent chain, cycle-guarded).
static void hbdf_world_matrix (
	const hbdf_modelblk_t *blk, uint idx, hbdf_mat4_t *out)
{
	hbdf_mat4_t acc;
	hbdf_m4_ident (&acc);
	uint cur = idx;
	for (int depth = 0; depth < 64; depth++)
	{
		if (cur >= blk->n_objects)
			break;
		const hbdf_object_t *ob = blk->objects + cur;
		hbdf_mat4_t local, tmp;
		hbdf_m4_trs (&local, ob->t, ob->r, ob->s, ob->ms);
		hbdf_m4_mul (&tmp, &local, &acc);
		acc = tmp;
		if (ob->parent < 0)
			break;
		cur = (uint)ob->parent;
	}
	*out = acc;
}

// Texture dimensions for a material (via its attribute's texture name).
static void hbdf_mat_tex_size (const hbdf_modelblk_t *blk,
	const hbdf_image_t *images, uint n_images, int mat_idx, uint *w, uint *h)
{
	*w = *h = 0;
	if (mat_idx < 0 || (uint)mat_idx >= blk->n_materials)
		return;
	const int ai = blk->materials[mat_idx].attr_idx;
	if (ai < 0 || (uint)ai >= blk->n_attrs)
		return;
	ccp want = blk->attrs[ai].texname;
	if (!want[0])
		return;
	for (uint i = 0; i < n_images; i++)
		if (!strcmp (images[i].name, want))
		{
			*w = images[i].w;
			*h = images[i].h;
			return;
		}
}

model_t *ParseHBDF (const u8 *data, uint size)
{
	if (!data || !IsHBDF (data, size))
		return 0;

	model_t *out = CALLOC (1, sizeof (*out));
	if (!out)
		return 0;

	const u32 total = rd_le32 (data + 4);
	const size_t limit = total <= size ? total : size;

	// First pass: collect texture dims (TEXS) for UV normalization.
	hbdf_image_t *images = 0;
	uint n_images = 0, cap_images = 0;
	size_t pos = 8;
	while (pos + 8 <= limit)
	{
		char tag[5];
		memcpy (tag, data + pos, 4);
		tag[4] = 0;
		const u32 bsize = rd_le32 (data + pos + 4);
		if (bsize < 8 || pos + bsize > limit)
			break;
		if (!strcmp (tag, "TEXS") && bsize >= 16)
		{
			const u16 n_info = rd_le16 (data + pos + 8);
			const u16 n_img = rd_le16 (data + pos + 10);
			const u16 n_pal = rd_le16 (data + pos + 12);
			size_t sp = pos + 16;
			const uint total_sub = (uint)n_info + n_img + n_pal;
			for (uint s = 0; s < total_sub && sp + 8 <= pos + bsize; s++)
			{
				char stag[5];
				memcpy (stag, data + sp, 4);
				stag[4] = 0;
				const u32 slen = rd_le32 (data + sp + 4);
				if (sp + 8 + slen > pos + bsize)
					break;
				if ((!strcmp (stag, "IMGO") || !strcmp (stag, "TEXO")) && slen >= 8)
				{
					// IMGO holds NAME then image fields; TEXO holds pad +
					// NAMEs (mapper, no dims) and is skipped here.
					if (!strcmp (stag, "IMGO"))
					{
						size_t cp = 0;
						char inm[64] = "";
						if (hbdf_read_name (inm, sizeof (inm), data + sp + 8, slen, &cp)
							&& cp + 24 <= slen)
						{
							const u8 *ip2 = data + sp + 8 + cp;
							const u16 w = rd_le16 (ip2 + 4);
							const u16 h = rd_le16 (ip2 + 6);
							if (n_images == cap_images)
							{
								cap_images = cap_images ? cap_images * 2 : 16;
								hbdf_image_t *nb = REALLOC (images,
									cap_images * sizeof (*nb));
								if (!nb)
									break;
								images = nb;
							}
							snprintf (images[n_images].name,
								sizeof (images[n_images].name), "%s", inm);
							images[n_images].w = w;
							images[n_images].h = h;
							n_images++;
						}
					}
				}
				sp += 8 + slen;
			}
		}
		pos += bsize;
	}

	// Second pass: models.
	pos = 8;
	while (pos + 8 <= limit)
	{
		char tag[5];
		memcpy (tag, data + pos, 4);
		tag[4] = 0;
		const u32 bsize = rd_le32 (data + pos + 4);
		if (bsize < 8 || pos + bsize > limit)
			break;
		if (!strcmp (tag, "MDLF") && bsize > 8)
		{
			hbdf_modelblk_t blk;
			if (hbdf_parse_model (&blk, data + pos + 8, bsize - 8))
			{
				// Materials.
				const uint mat_base = (uint)out->num_materials;
				if (blk.n_materials)
				{
					material_t *nm = REALLOC (out->materials,
						(out->num_materials + blk.n_materials) * sizeof (*nm));
					if (nm)
					{
						out->materials = nm;
						for (uint i = 0; i < blk.n_materials; i++)
						{
							material_t *mt = out->materials + out->num_materials++;
							memset (mt, 0, sizeof (*mt));
							snprintf (mt->name, sizeof (mt->name), "%s",
								blk.materials[i].name);
							const int ai = blk.materials[i].attr_idx;
							if (ai >= 0 && (uint)ai < blk.n_attrs
								&& blk.attrs[ai].texname[0])
							{
								snprintf (mt->textures[0], sizeof (mt->textures[0]),
									"%s", blk.attrs[ai].texname);
								mt->num_textures = 1;
							}
							mt->diffuse[0] = mt->diffuse[1] = mt->diffuse[2] = 0.8f;
							mt->diffuse[3] = 1.0f;
						}
					}
				}
				// Meshes.
				for (uint oi = 0; oi < blk.n_objects; oi++)
				{
					hbdf_object_t *ob = blk.objects + oi;
					if (!ob->dl || !ob->dl_size || !ob->n_polys)
						continue;
					// UV dims from the first ranged group's texture.
					uint tw = 0, th = 0;
					for (uint g = 0; g < ob->n_polys; g++)
					{
						if (ob->polys[g].face_count)
						{
							hbdf_mat_tex_size (&blk, images, n_images,
								ob->polys[g].mat_idx, &tw, &th);
							break;
						}
					}
					// Decode into a scratch model so the split meshes can
					// append to out without moving src underneath us.
					model_t scratch;
					memset (&scratch, 0, sizeof (scratch));
					const int midx = AppendDSGXMesh (&scratch, ob->dl, ob->dl_size,
						ob->name, tw, th);
					if (midx < 0)
						continue;
					mesh_t *src = scratch.meshes + midx;
					hbdf_mat4_t world;
					hbdf_world_matrix (&blk, oi, &world);
					// Split per PolyGroup material range.
					for (uint g = 0; g < ob->n_polys; g++)
					{
						uint start = ob->polys[g].face_start;
						uint count = ob->polys[g].face_count;
						if (!count || start >= src->num_vertices)
							continue;
						if (start + count > src->num_vertices)
							count = (uint)src->num_vertices - start;
						mesh_t nm;
						memset (&nm, 0, sizeof (nm));
						snprintf (nm.name, sizeof (nm.name), "%s_p%u",
							ob->name[0] ? ob->name : "mesh", g);
						nm.positions = MALLOC (count * sizeof (vec3_t));
						nm.normals = MALLOC (count * sizeof (vec3_t));
						nm.texcoords = MALLOC (count * sizeof (vec2_t));
						nm.vertices = MALLOC (count * sizeof (vertex_t));
						if (!nm.positions || !nm.normals || !nm.texcoords || !nm.vertices)
						{
							FREE (nm.positions);
							FREE (nm.normals);
							FREE (nm.texcoords);
							FREE (nm.vertices);
							continue;
						}
						for (uint j = 0; j < count; j++)
						{
							const uint sj = start + j;
							float px = src->positions[src->vertices[sj].position_idx].x;
							float py = src->positions[src->vertices[sj].position_idx].y;
							float pz = src->positions[src->vertices[sj].position_idx].z;
							hbdf_m4_point (&world, &px, &py, &pz);
							nm.positions[j].x = px;
							nm.positions[j].y = py;
							nm.positions[j].z = pz;
							float nx = 0, ny = 1, nz = 0;
							if (src->vertices[sj].normal_idx >= 0
								&& (uint)src->vertices[sj].normal_idx < src->num_normals)
							{
								nx = src->normals[src->vertices[sj].normal_idx].x;
								ny = src->normals[src->vertices[sj].normal_idx].y;
								nz = src->normals[src->vertices[sj].normal_idx].z;
							}
							hbdf_m4_vec (&world, &nx, &ny, &nz);
							nm.normals[j].x = nx;
							nm.normals[j].y = ny;
							nm.normals[j].z = nz;
							if (src->vertices[sj].texcoord_idx >= 0
								&& (uint)src->vertices[sj].texcoord_idx < src->num_texcoords)
								nm.texcoords[j] = src->texcoords[src->vertices[sj].texcoord_idx];
							nm.vertices[j].position_idx = (int)j;
							nm.vertices[j].normal_idx = (int)j;
							nm.vertices[j].tangent_idx = -1;
							nm.vertices[j].texcoord_idx = (int)j;
							nm.vertices[j].matrix_idx = -1;
							nm.vertices[j].color_idx[0] = -1;
							nm.vertices[j].color_idx[1] = -1;
							for (int e = 0; e < 7; e++)
								nm.vertices[j].extra_texcoord_idx[e] = -1;
						}
						nm.num_positions = nm.num_normals = nm.num_texcoords
							= nm.num_vertices = count;
						int mgi = ob->polys[g].mat_idx;
						nm.material_idx = (mgi >= 0 && (uint)mgi < blk.n_materials)
							? (int)(mat_base + mgi) : -1;
						mesh_t *grown = REALLOC (out->meshes,
							(out->num_meshes + 1) * sizeof (*grown));
						if (!grown)
						{
							FREE (nm.positions);
							FREE (nm.normals);
							FREE (nm.texcoords);
							FREE (nm.vertices);
							continue;
						}
						out->meshes = grown;
						out->meshes[out->num_meshes++] = nm;
					}
					FREE (src->positions);
					FREE (src->normals);
					FREE (src->texcoords);
					FREE (src->vertices);
					FREE (scratch.meshes);
				}
				hbdf_free_modelblk (&blk);
			}
		}
		pos += bsize;
	}

	FREE (images);

	if (!out->num_meshes)
	{
		FreeModel (out);
		return 0;
	}
	return out;
}

enumError DecodeHBDF (const u8 *data, uint size, ccp out_path)
{
	if (!data || !out_path)
		return ERR_INVALID_DATA;
	model_t *model = ParseHBDF (data, size);
	if (!model)
		return ERR_INVALID_DATA;
	const int rc = ExportModelToGLB (model, out_path);
	FreeModel (model);
	return rc == 0 ? ERR_OK : ERR_CANT_CREATE;
}

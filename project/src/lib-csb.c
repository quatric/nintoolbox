// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Paper Mario collision scene (.csb) + collision search table (.ctb).
// See lib-csb.h for the format documentation and attributions.
//-----------------------------------------------------------------------------

#include "lib-csb.h"
#include "lib-std.h"
#include "lib-zstd.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <float.h>

//-----------------------------------------------------------------------------
//--- endian helpers ----------------------------------------------------------
//-----------------------------------------------------------------------------

static u16 csb_rd16le (const u8 *p)
{
	return (u16)p[0] | (u16)p[1] << 8;
}

static u16 csb_rd16be (const u8 *p)
{
	return (u16)p[0] << 8 | (u16)p[1];
}

static u32 csb_rd32le (const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static u32 csb_rd32be (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | (u32)p[3];
}

static u64 csb_rd64le (const u8 *p)
{
	return (u64)p[0] | (u64)p[1] << 8 | (u64)p[2] << 16 | (u64)p[3] << 24
		| (u64)p[4] << 32 | (u64)p[5] << 40 | (u64)p[6] << 48 | (u64)p[7] << 56;
}

static u64 csb_rd64be (const u8 *p)
{
	return (u64)p[0] << 56 | (u64)p[1] << 48 | (u64)p[2] << 40 | (u64)p[3] << 32
		| (u64)p[4] << 24 | (u64)p[5] << 16 | (u64)p[6] << 8 | (u64)p[7];
}

static float csb_bits_f32 (u32 v)
{
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static u32 csb_f32_bits (float f)
{
	u32 v;
	memcpy (&v, &f, 4);
	return v;
}

static void csb_wr16le (u8 *p, u16 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

static void csb_wr16be (u8 *p, u16 v)
{
	p[0] = (u8)(v >> 8);
	p[1] = (u8)v;
}

static void csb_wr32le (u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

static void csb_wr32be (u8 *p, u32 v)
{
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)v;
}

static void csb_wr64le (u8 *p, u64 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
	p[4] = (u8)(v >> 32);
	p[5] = (u8)(v >> 40);
	p[6] = (u8)(v >> 48);
	p[7] = (u8)(v >> 56);
}

static void csb_wr64be (u8 *p, u64 v)
{
	p[0] = (u8)(v >> 56);
	p[1] = (u8)(v >> 48);
	p[2] = (u8)(v >> 40);
	p[3] = (u8)(v >> 32);
	p[4] = (u8)(v >> 24);
	p[5] = (u8)(v >> 16);
	p[6] = (u8)(v >> 8);
	p[7] = (u8)v;
}

//-----------------------------------------------------------------------------
//--- bounded reader ----------------------------------------------------------
//-----------------------------------------------------------------------------

typedef struct csb_reader_t
{
	const u8 *data;
	u32 size;
	u32 pos;
	bool be;
	bool err;
} csb_reader_t;

static u32 csb_remain (const csb_reader_t *r)
{
	return r->err || r->pos > r->size ? 0 : r->size - r->pos;
}

static void csb_need (csb_reader_t *r, u32 n)
{
	if (!r->err && n > csb_remain (r))
		r->err = true;
}

static u16 csb_rd16 (csb_reader_t *r)
{
	csb_need (r, 2);
	if (r->err)
		return 0;
	u16 v = r->be ? csb_rd16be (r->data + r->pos) : csb_rd16le (r->data + r->pos);
	r->pos += 2;
	return v;
}

static u32 csb_rd32 (csb_reader_t *r)
{
	csb_need (r, 4);
	if (r->err)
		return 0;
	u32 v = r->be ? csb_rd32be (r->data + r->pos) : csb_rd32le (r->data + r->pos);
	r->pos += 4;
	return v;
}

static u64 csb_rd64 (csb_reader_t *r)
{
	csb_need (r, 8);
	if (r->err)
		return 0;
	u64 v = r->be ? csb_rd64be (r->data + r->pos) : csb_rd64le (r->data + r->pos);
	r->pos += 8;
	return v;
}

static float csb_rdf32 (csb_reader_t *r)
{
	return csb_bits_f32 (csb_rd32 (r));
}

static u8 csb_rd08 (csb_reader_t *r)
{
	csb_need (r, 1);
	if (r->err)
		return 0;
	return r->data[r->pos++];
}

static void csb_rdvec (csb_reader_t *r, vec3_t *v)
{
	v->x = csb_rdf32 (r);
	v->y = csb_rdf32 (r);
	v->z = csb_rdf32 (r);
}

static void csb_skip (csb_reader_t *r, u32 n)
{
	csb_need (r, n);
	if (!r->err)
		r->pos += n;
}

// Read a NUL-terminated string at TAB+OFF (TAB_LEN bytes). Owned copy.
static char *csb_rd_str (csb_reader_t *r, const u8 *tab, u32 tab_len, u32 off)
{
	if (off >= tab_len)
	{
		r->err = true;
		return 0;
	}
	u32 max = tab_len - off, len = 0;
	while (len < max && tab[off + len])
		len++;
	if (len >= max)
	{
		r->err = true; // not NUL-terminated
		return 0;
	}
	char *out = MALLOC (len + 1);
	if (!out)
	{
		r->err = true;
		return 0;
	}
	memcpy (out, tab + off, len);
	out[len] = 0;
	return out;
}

//-----------------------------------------------------------------------------
//--- growable writer ---------------------------------------------------------
//-----------------------------------------------------------------------------

typedef struct csb_writer_t
{
	u8 *data;
	u32 size;
	u32 cap;
	bool be;
	bool err; // overrun or OOM
} csb_writer_t;

static void csb_wr_reserve (csb_writer_t *w, u32 n)
{
	if (w->err)
		return;
	u64 need = (u64)w->size + n;
	if (need > 0x40000000u)
	{
		w->err = true;
		return;
	}
	if (need > w->cap)
	{
		u64 ncap = w->cap ? (u64)w->cap * 2 : 1024;
		while (ncap < need)
			ncap *= 2;
		u8 *nd = REALLOC (w->data, (size_t)ncap);
		if (!nd)
		{
			w->err = true;
			return;
		}
		w->data = nd;
		w->cap = (u32)ncap;
	}
}

static void csb_wr08 (csb_writer_t *w, u8 v)
{
	csb_wr_reserve (w, 1);
	if (!w->err)
		w->data[w->size++] = v;
}

static void csb_wr16 (csb_writer_t *w, u16 v)
{
	csb_wr_reserve (w, 2);
	if (w->err)
		return;
	if (w->be)
		csb_wr16be (w->data + w->size, v);
	else
		csb_wr16le (w->data + w->size, v);
	w->size += 2;
}

static void csb_wr32 (csb_writer_t *w, u32 v)
{
	csb_wr_reserve (w, 4);
	if (w->err)
		return;
	if (w->be)
		csb_wr32be (w->data + w->size, v);
	else
		csb_wr32le (w->data + w->size, v);
	w->size += 4;
}

static void csb_wr64 (csb_writer_t *w, u64 v)
{
	csb_wr_reserve (w, 8);
	if (w->err)
		return;
	if (w->be)
		csb_wr64be (w->data + w->size, v);
	else
		csb_wr64le (w->data + w->size, v);
	w->size += 8;
}

static void csb_wrf32 (csb_writer_t *w, float f)
{
	csb_wr32 (w, csb_f32_bits (f));
}

static void csb_wrvec (csb_writer_t *w, const vec3_t *v)
{
	csb_wrf32 (w, v->x);
	csb_wrf32 (w, v->y);
	csb_wrf32 (w, v->z);
}

static void csb_wr_bytes (csb_writer_t *w, const void *src, u32 n)
{
	if (!n)
		return;
	csb_wr_reserve (w, n);
	if (w->err)
		return;
	memcpy (w->data + w->size, src, n);
	w->size += n;
}

static void csb_wr_align4 (csb_writer_t *w)
{
	while (!w->err && (w->size & 3))
		csb_wr08 (w, 0);
}

// Write NAME truncated to 63 chars, NUL-padded to 64 bytes.
static void csb_wr_fixed64 (csb_writer_t *w, ccp name)
{
	char buf[64];
	memset (buf, 0, sizeof (buf));
	if (name)
	{
		size_t len = strlen (name);
		if (len > 63)
			len = 63;
		memcpy (buf, name, len);
	}
	csb_wr_bytes (w, buf, 64);
}

// Build a NUL-joined, 4-byte-aligned string table. Always succeeds
// (empty list => empty table).
static void csb_wr_strtab (csb_writer_t *w, char **names, u32 n)
{
	u32 total = 0;
	for (u32 i = 0; i < n; i++)
		total += (u32)strlen (names[i]) + 1;
	u32 aligned = (total + 3) & ~3u;
	csb_wr32 (w, aligned);
	for (u32 i = 0; i < n; i++)
	{
		u32 len = (u32)strlen (names[i]) + 1;
		csb_wr_bytes (w, names[i], len);
	}
	while (!w->err && total < aligned)
	{
		csb_wr08 (w, 0);
		total++;
	}
}

//-----------------------------------------------------------------------------
//--- csb parsing -------------------------------------------------------------
//
// Mirrors CsbFile.Read with two deliberate deviations, both documented:
//  1. Mesh slice ends use [start, next_start or count) instead of upstream's
//     (count - 1) for the last mesh, which drops its final triangle.
//  2. Meshes attach to models[0] only (upstream appends the shared mesh list
//     to every model, which is meaningless for num_models > 1).
//-----------------------------------------------------------------------------

static void csb_free_model (csb_model_t *m)
{
	if (!m)
		return;
	for (u32 i = 0; i < m->n_meshes; i++)
		FREE (m->meshes[i].name);
	FREE (m->meshes);
	FREE (m->positions);
	FREE (m->triangles);
	memset (m, 0, sizeof (*m));
}

void FreeCSB (csb_t *csb)
{
	if (!csb)
		return;
	for (u32 i = 0; i < csb->n_objects; i++)
		FREE (csb->objects[i].name);
	FREE (csb->objects);
	FREE (csb->nodes);
	for (u32 i = 0; i < csb->n_models; i++)
		csb_free_model (csb->models + i);
	FREE (csb->models);
	memset (csb, 0, sizeof (*csb));
}

static bool csb_parse_model_body (csb_reader_t *r, csb_model_t *m, bool is_split)
{
	memset (m, 0, sizeof (*m));
	m->unknown0 = csb_rd32 (r);
	if (!is_split)
	{
		csb_rd32 (r); // id, always 0
		if (!r->be)
			csb_rd64 (r); // 0
		csb_rd32 (r); // 0
		csb_rd32 (r); // 0
	}
	else
	{
		m->node_index = csb_rd16 (r);
		csb_rd16 (r); // 0
		if (r->be)
			m->colflag = csb_rd32 (r);
		else
			m->colflag = csb_rd64 (r);
		m->mat_attr = csb_rd32 (r);
		if (!r->be)
			m->unknown4 = csb_rd32 (r); // 0
	}
	csb_need (r, 64);
	if (r->err)
		return false;
	memcpy (m->name, r->data + r->pos, 64);
	m->name[63] = 0;
	r->pos += 64;
	if (!is_split)
	{
		m->unknown5 = csb_rd32 (r);
		m->n_positions = csb_rd32 (r);
		m->n_triangles = csb_rd32 (r);
		csb_rdvec (r, &m->zero);
		csb_rdvec (r, &m->translate);
		csb_rdvec (r, &m->rotation);
	}
	else
	{
		m->unknown5 = csb_rd32 (r); // 1
		m->n_positions = csb_rd32 (r);
		m->n_triangles = csb_rd32 (r);
		csb_rdvec (r, &m->zero);
		csb_rdvec (r, &m->translate);
		csb_rdvec (r, &m->rotation);
	}
	// bbox
	csb_rdvec (r, &m->bbox.min);
	csb_rdvec (r, &m->bbox.max);
	// positions
	if ((u64)m->n_positions * 12 > csb_remain (r))
	{
		r->err = true;
		return false;
	}
	if (m->n_positions)
	{
		m->positions = MALLOC ((size_t)m->n_positions * sizeof (vec3_t));
		if (!m->positions)
		{
			r->err = true;
			return false;
		}
		for (u32 i = 0; i < m->n_positions; i++)
			csb_rdvec (r, m->positions + i);
	}
	// triangles: u32 a,b,c + vec3 normal (24 bytes each)
	if ((u64)m->n_triangles * 24 > csb_remain (r))
	{
		r->err = true;
		return false;
	}
	if (m->n_triangles)
	{
		m->triangles = MALLOC ((size_t)m->n_triangles * sizeof (csb_tri_t));
		if (!m->triangles)
		{
			r->err = true;
			return false;
		}
		for (u32 i = 0; i < m->n_triangles; i++)
		{
			m->triangles[i].a = csb_rd32 (r);
			m->triangles[i].b = csb_rd32 (r);
			m->triangles[i].c = csb_rd32 (r);
			csb_rdvec (r, &m->triangles[i].normal);
			if (!r->err
				&& (m->triangles[i].a >= m->n_positions
					|| m->triangles[i].b >= m->n_positions
					|| m->triangles[i].c >= m->n_positions))
				r->err = true; // index out of range
		}
	}
	return !r->err;
}

static bool csb_parse (csb_t *csb, const u8 *data, u32 size, bool be)
{
	memset (csb, 0, sizeof (*csb));
	csb->big_endian = be;
	if (!data || size < 8)
		return false;

	csb_reader_t rd = { data, size, 0, be, false };
	csb_reader_t *r = &rd;

	u32 n_spheres = csb_rd32 (r);
	if (n_spheres > 4096)
		return false;

	csb_object_t *objs = 0;
	u32 n_objs = 0, cap_objs = 0;

	// spheres: f32 unk + vec3 p1 + vec3 p2 + f32 radius (32 bytes)
	if ((u64)n_spheres * 32 > csb_remain (r))
		return false;
	for (u32 i = 0; i < n_spheres; i++)
	{
		if (n_objs >= cap_objs)
		{
			u32 ncap = cap_objs ? cap_objs * 2 : 16;
			csb_object_t *nd = REALLOC (objs, (size_t)ncap * sizeof (*nd));
			if (!nd)
			{
				FREE (objs);
				return false;
			}
			objs = nd;
			cap_objs = ncap;
		}
		csb_object_t *o = objs + n_objs++;
		memset (o, 0, sizeof (*o));
		o->is_sphere = true;
		o->unknown = csb_rdf32 (r);
		csb_rdvec (r, &o->p1);
		csb_rdvec (r, &o->p2);
		o->radius = csb_rdf32 (r);
		o->size.x = o->size.y = o->size.z = 1.0f;
		o->box_extra[2] = 1.0f;
		o->box_extra[7] = 1.0f;
	}

	u32 n_boxes = csb_rd32 (r);
	if (n_boxes > 4096)
	{
		FREE (objs);
		return false;
	}
	// boxes: f32 unk + vec3 p1 + vec3 p2 + vec3 size + vec3 rot + f32[9] (80 bytes)
	if ((u64)n_boxes * 80 > csb_remain (r))
	{
		FREE (objs);
		return false;
	}
	for (u32 i = 0; i < n_boxes; i++)
	{
		if (n_objs >= cap_objs)
		{
			u32 ncap = cap_objs ? cap_objs * 2 : 16;
			csb_object_t *nd = REALLOC (objs, (size_t)ncap * sizeof (*nd));
			if (!nd)
			{
				for (u32 k = 0; k < n_objs; k++)
					FREE (objs[k].name);
				FREE (objs);
				return false;
			}
			objs = nd;
			cap_objs = ncap;
		}
		csb_object_t *o = objs + n_objs++;
		memset (o, 0, sizeof (*o));
		o->unknown = csb_rdf32 (r);
		csb_rdvec (r, &o->p1);
		csb_rdvec (r, &o->p2);
		csb_rdvec (r, &o->size);
		csb_rdvec (r, &o->rotation);
		for (int k = 0; k < 9; k++)
			o->box_extra[k] = csb_rdf32 (r);
		o->radius = 0.7f;
	}

	if (csb_rd32 (r) != 0) // reserved object slot, always 0
	{
		for (u32 k = 0; k < n_objs; k++)
			FREE (objs[k].name);
		FREE (objs);
		return false;
	}
	csb->unknown = csb_rd32 (r);
	if (csb->unknown != 1)
	{
		for (u32 k = 0; k < n_objs; k++)
			FREE (objs[k].name);
		FREE (objs);
		return false;
	}
	for (int k = 0; k < 16; k++)
		if (csb_rd08 (r) != 0)
		{
			for (u32 j = 0; j < n_objs; j++)
				FREE (objs[j].name);
			FREE (objs);
			return false;
		}
	if (r->err)
	{
		for (u32 k = 0; k < n_objs; k++)
			FREE (objs[k].name);
		FREE (objs);
		return false;
	}

	// object name offsets + flags + node indices + string table
	u32 *obj_name_off = 0;
	if (n_objs)
	{
		if ((u64)n_objs * 4 > csb_remain (r))
		{
			FREE (objs);
			return false;
		}
		obj_name_off = MALLOC ((size_t)n_objs * sizeof (u32));
		if (!obj_name_off)
		{
			FREE (objs);
			return false;
		}
		for (u32 i = 0; i < n_objs; i++)
			obj_name_off[i] = csb_rd32 (r);
		u32 flag_size = be ? 4 : 8;
		if ((u64)n_objs * flag_size > csb_remain (r))
		{
			FREE (obj_name_off);
			FREE (objs);
			return false;
		}
		for (u32 i = 0; i < n_objs; i++)
			objs[i].colflag = be ? csb_rd32 (r) : csb_rd64 (r);
		if ((u64)n_objs * 2 > csb_remain (r))
		{
			FREE (obj_name_off);
			for (u32 k = 0; k < n_objs; k++)
				FREE (objs[k].name);
			FREE (objs);
			return false;
		}
		for (u32 i = 0; i < n_objs; i++)
			objs[i].node_index = csb_rd16 (r);
	}
	u32 obj_str_len = csb_rd32 (r);
	if (obj_str_len > csb_remain (r))
	{
		FREE (obj_name_off);
		for (u32 k = 0; k < n_objs; k++)
			FREE (objs[k].name);
		FREE (objs);
		return false;
	}
	const u8 *obj_strtab = data + r->pos;
	csb_skip (r, obj_str_len);
	for (u32 i = 0; i < n_objs; i++)
	{
		objs[i].name = csb_rd_str (r, obj_strtab, obj_str_len, obj_name_off[i]);
		if (r->err)
		{
			FREE (obj_name_off);
			for (u32 k = 0; k < n_objs; k++)
				FREE (objs[k].name);
			FREE (objs);
			return false;
		}
	}
	FREE (obj_name_off);
	csb->objects = objs;
	csb->n_objects = n_objs;

	csb->unknown3 = csb_rd16 (r);
	u32 n_models = csb_rd16 (r);
	u32 n_meshes = csb_rd32 (r);
	if (!n_models || n_models > 4096 || n_meshes > 65536)
	{
		FreeCSB (csb);
		return false;
	}
	u32 *mesh_name_off = 0, *mesh_tri_off = 0, *mesh_vtx_off = 0;
	u64 *mesh_flags = 0;
	u32 *mesh_attrs = 0;
	u16 *mesh_nodes = 0;
	if (n_meshes)
	{
		if ((u64)n_meshes * 12 > csb_remain (r))
		{
			FreeCSB (csb);
			return false;
		}
		mesh_name_off = MALLOC ((size_t)n_meshes * sizeof (u32));
		mesh_tri_off = MALLOC ((size_t)n_meshes * sizeof (u32));
		mesh_vtx_off = MALLOC ((size_t)n_meshes * sizeof (u32));
		mesh_flags = MALLOC ((size_t)n_meshes * sizeof (u64));
		mesh_attrs = MALLOC ((size_t)n_meshes * sizeof (u32));
		mesh_nodes = MALLOC ((size_t)n_meshes * sizeof (u16));
		if (!mesh_name_off || !mesh_tri_off || !mesh_vtx_off || !mesh_flags || !mesh_attrs
			|| !mesh_nodes)
		{
			r->err = true;
		}
		else
		{
			for (u32 i = 0; i < n_meshes; i++)
				mesh_name_off[i] = csb_rd32 (r);
			for (u32 i = 0; i < n_meshes; i++)
				mesh_tri_off[i] = csb_rd32 (r);
			for (u32 i = 0; i < n_meshes; i++)
				mesh_vtx_off[i] = csb_rd32 (r);
			u32 flag_size = be ? 4 : 8;
			if ((u64)n_meshes * flag_size > csb_remain (r))
				r->err = true;
			else
				for (u32 i = 0; i < n_meshes; i++)
					mesh_flags[i] = be ? csb_rd32 (r) : csb_rd64 (r);
			if ((u64)n_meshes * 4 > csb_remain (r))
				r->err = true;
			else
				for (u32 i = 0; i < n_meshes; i++)
					mesh_attrs[i] = csb_rd32 (r);
			csb_skip (r, n_meshes * 4); // model ids, always 0
			if ((u64)n_meshes * 2 > csb_remain (r))
				r->err = true;
			else
				for (u32 i = 0; i < n_meshes; i++)
					mesh_nodes[i] = csb_rd16 (r);
		}
		if (r->err)
		{
			FREE (mesh_name_off);
			FREE (mesh_tri_off);
			FREE (mesh_vtx_off);
			FREE (mesh_flags);
			FREE (mesh_attrs);
			FREE (mesh_nodes);
			FreeCSB (csb);
			return false;
		}
	}
	u32 mesh_str_len = csb_rd32 (r);
	if (mesh_str_len > csb_remain (r))
	{
		FREE (mesh_name_off);
		FREE (mesh_tri_off);
		FREE (mesh_vtx_off);
		FREE (mesh_flags);
		FREE (mesh_attrs);
		FREE (mesh_nodes);
		FreeCSB (csb);
		return false;
	}
	const u8 *mesh_strtab = data + r->pos;
	csb_skip (r, mesh_str_len);

	u32 n_nodes = csb_rd16 (r);
	if (!n_nodes || n_nodes > 65535 || (u64)n_nodes * 4 > csb_remain (r))
	{
		FREE (mesh_name_off);
		FREE (mesh_tri_off);
		FREE (mesh_vtx_off);
		FREE (mesh_flags);
		FREE (mesh_attrs);
		FREE (mesh_nodes);
		FreeCSB (csb);
		return false;
	}
	csb->nodes = MALLOC ((size_t)n_nodes * sizeof (csb_node_t));
	if (!csb->nodes)
	{
		FREE (mesh_name_off);
		FREE (mesh_tri_off);
		FREE (mesh_vtx_off);
		FREE (mesh_flags);
		FREE (mesh_attrs);
		FREE (mesh_nodes);
		FreeCSB (csb);
		return false;
	}
	csb->n_nodes = n_nodes;
	for (u32 i = 0; i < n_nodes; i++)
	{
		csb->nodes[i].id = csb_rd16 (r);
		csb->nodes[i].flags = csb_rd08 (r);
		csb->nodes[i].num_children = csb_rd08 (r);
	}

	// model headers are read first (names), then sliced after geometry
	csb->models = MALLOC ((size_t)n_models * sizeof (csb_model_t));
	if (!csb->models)
	{
		FREE (mesh_name_off);
		FREE (mesh_tri_off);
		FREE (mesh_vtx_off);
		FREE (mesh_flags);
		FREE (mesh_attrs);
		FREE (mesh_nodes);
		FreeCSB (csb);
		return false;
	}
	csb->n_models = n_models;
	for (u32 i = 0; i < n_models; i++)
		if (!csb_parse_model_body (r, csb->models + i, false))
		{
			FREE (mesh_name_off);
			FREE (mesh_tri_off);
			FREE (mesh_vtx_off);
			FREE (mesh_flags);
			FREE (mesh_attrs);
			FREE (mesh_nodes);
			FreeCSB (csb);
			return false;
		}

	// slice the shared mesh list into models[0] (see header comment)
	if (n_meshes && csb->n_models)
	{
		csb_model_t *m0 = csb->models;
		m0->meshes = MALLOC ((size_t)n_meshes * sizeof (csb_mesh_t));
		if (!m0->meshes)
		{
			FREE (mesh_name_off);
			FREE (mesh_tri_off);
			FREE (mesh_vtx_off);
			FREE (mesh_flags);
			FREE (mesh_attrs);
			FREE (mesh_nodes);
			FreeCSB (csb);
			return false;
		}
		for (u32 m = 0; m < n_meshes; m++)
		{
			csb_mesh_t *mesh = m0->meshes + m0->n_meshes;
			memset (mesh, 0, sizeof (*mesh));
			mesh->name = csb_rd_str (r, mesh_strtab, mesh_str_len, mesh_name_off[m]);
			if (r->err)
			{
				FREE (mesh_name_off);
				FREE (mesh_tri_off);
				FREE (mesh_vtx_off);
				FREE (mesh_flags);
				FREE (mesh_attrs);
				FREE (mesh_nodes);
				FreeCSB (csb);
				return false;
			}
			mesh->mat_attr = mesh_attrs[m];
			mesh->colflag = mesh_flags[m];
			mesh->node_index = mesh_nodes[m];
			// slice range: [start, next start or buffer count), clamped
			u32 ts = mesh_tri_off[m], vs = mesh_vtx_off[m];
			u32 te = m0->n_triangles, ve = m0->n_positions;
			if (m + 1 < n_meshes)
			{
				if (mesh_tri_off[m + 1] < te)
					te = mesh_tri_off[m + 1];
				if (mesh_vtx_off[m + 1] < ve)
					ve = mesh_vtx_off[m + 1];
			}
			if (ts > m0->n_triangles || vs > m0->n_positions || te < ts || ve < vs)
			{
				r->err = true;
				FREE (mesh_name_off);
				FREE (mesh_tri_off);
				FREE (mesh_vtx_off);
				FREE (mesh_flags);
				FREE (mesh_attrs);
				FREE (mesh_nodes);
				FreeCSB (csb);
				return false;
			}
			mesh->tri_start = ts;
			mesh->vtx_start = vs;
			mesh->n_tris = te - ts;
			mesh->n_vtx = ve - vs;
			m0->n_meshes++;
		}
	}
	FREE (mesh_name_off);
	FREE (mesh_tri_off);
	FREE (mesh_vtx_off);
	FREE (mesh_flags);
	FREE (mesh_attrs);
	FREE (mesh_nodes);

	if (csb_rd32 (r) != 0) // tail opener
	{
		FreeCSB (csb);
		return false;
	}
	csb->tail_b0 = csb_rd08 (r);
	csb->tail_b1 = csb_rd08 (r);
	if (csb->tail_b0 != 0xFE || csb->tail_b1 != 0x07 || csb_rd16 (r) != 0)
	{
		FreeCSB (csb);
		return false;
	}
	u32 n_split = csb_rd32 (r);
	if (n_split > 4096)
	{
		FreeCSB (csb);
		return false;
	}
	csb_rdvec (r, &csb->sub_bbox.min);
	csb_rdvec (r, &csb->sub_bbox.max);
	if (n_split)
	{
		if (r->err)
		{
			FreeCSB (csb);
			return false;
		}
		u32 total = csb->n_models + n_split;
		csb_model_t *nd
			= REALLOC (csb->models, (size_t)total * sizeof (csb_model_t));
		if (!nd)
		{
			FreeCSB (csb);
			return false;
		}
		csb->models = nd;
		for (u32 i = 0; i < n_split; i++)
		{
			memset (csb->models + csb->n_models, 0, sizeof (csb_model_t));
			if (!csb_parse_model_body (r, csb->models + csb->n_models, true))
			{
				FreeCSB (csb);
				return false;
			}
			csb->n_models++;
		}
	}
	if (r->err || r->pos != size) // exact consumption, no trailing bytes
	{
		FreeCSB (csb);
		return false;
	}
	return true;
}

int DetectCSBEndian (const u8 *data, u32 size)
{
	csb_t tmp;
	if (csb_parse (&tmp, data, size, false))
	{
		FreeCSB (&tmp);
		return 0;
	}
	if (csb_parse (&tmp, data, size, true))
	{
		FreeCSB (&tmp);
		return 1;
	}
	return -1;
}

bool IsCSB (const u8 *data, u32 size)
{
	return DetectCSBEndian (data, size) >= 0;
}

enumError ScanCSB (csb_t *csb, const u8 *data, u32 size)
{
	if (!csb)
		return ERR_INVALID_DATA;
	memset (csb, 0, sizeof (*csb));
	if (csb_parse (csb, data, size, false))
		return ERR_OK;
	if (csb_parse (csb, data, size, true))
		return ERR_OK;
	return ERR_NOTHING_TO_DO;
}

//-----------------------------------------------------------------------------
//--- csb writing (exact inverse of the reader) -------------------------------
//-----------------------------------------------------------------------------

static void csb_write_model_body (csb_writer_t *w, const csb_model_t *m, bool is_split)
{
	csb_wr32 (w, m->unknown0);
	if (!is_split)
	{
		csb_wr32 (w, 0);
		if (!w->be)
			csb_wr64 (w, 0);
		csb_wr32 (w, 0);
		csb_wr32 (w, 0);
	}
	else
	{
		csb_wr16 (w, (u16)m->node_index);
		csb_wr16 (w, 0);
		if (w->be)
			csb_wr32 (w, (u32)m->colflag);
		else
			csb_wr64 (w, m->colflag);
		csb_wr32 (w, m->mat_attr);
		if (!w->be)
			csb_wr32 (w, m->unknown4);
	}
	csb_wr_fixed64 (w, m->name);
	csb_wr32 (w, m->unknown5);
	csb_wr32 (w, m->n_positions);
	csb_wr32 (w, m->n_triangles);
	csb_wrvec (w, &m->zero);
	csb_wrvec (w, &m->translate);
	csb_wrvec (w, &m->rotation);
	csb_wrvec (w, &m->bbox.min);
	csb_wrvec (w, &m->bbox.max);
	for (u32 i = 0; i < m->n_positions; i++)
		csb_wrvec (w, m->positions + i);
	for (u32 i = 0; i < m->n_triangles; i++)
	{
		csb_wr32 (w, m->triangles[i].a);
		csb_wr32 (w, m->triangles[i].b);
		csb_wr32 (w, m->triangles[i].c);
		csb_wrvec (w, &m->triangles[i].normal);
	}
}

enumError SaveCSB (u8 **dest, u32 *dest_size, const csb_t *csb, bool big_endian)
{
	if (!dest || !dest_size || !csb || !csb->n_models)
		return ERR_INVALID_DATA;
	for (u32 i = 0; i < csb->n_models; i++)
	{
		const csb_model_t *m = csb->models + i;
		for (u32 t = 0; t < m->n_triangles; t++)
			if (m->triangles[t].a >= m->n_positions || m->triangles[t].b >= m->n_positions
				|| m->triangles[t].c >= m->n_positions)
				return ERR_INVALID_DATA;
		if (i == 0)
			for (u32 j = 0; j < m->n_meshes; j++)
			{
				const csb_mesh_t *mesh = m->meshes + j;
				if (mesh->node_index < 0 || mesh->node_index > 65535
					|| mesh->tri_start + mesh->n_tris > m->n_triangles
					|| mesh->vtx_start + mesh->n_vtx > m->n_positions)
					return ERR_INVALID_DATA;
			}
	}

	csb_writer_t wr = { 0, 0, 0, big_endian, false };
	csb_writer_t *w = &wr;

	// objects, spheres first (stable partition of file order)
	u32 n_spheres = 0;
	for (u32 i = 0; i < csb->n_objects; i++)
		if (csb->objects[i].is_sphere)
			n_spheres++;
	csb_wr32 (w, n_spheres);
	for (u32 i = 0; i < csb->n_objects; i++)
	{
		const csb_object_t *o = csb->objects + i;
		if (!o->is_sphere)
			continue;
		csb_wrf32 (w, o->unknown);
		csb_wrvec (w, &o->p1);
		csb_wrvec (w, &o->p2);
		csb_wrf32 (w, o->radius);
	}
	csb_wr32 (w, csb->n_objects - n_spheres);
	for (u32 i = 0; i < csb->n_objects; i++)
	{
		const csb_object_t *o = csb->objects + i;
		if (o->is_sphere)
			continue;
		csb_wrf32 (w, o->unknown);
		csb_wrvec (w, &o->p1);
		csb_wrvec (w, &o->p2);
		csb_wrvec (w, &o->size);
		csb_wrvec (w, &o->rotation);
		for (int k = 0; k < 9; k++)
			csb_wrf32 (w, o->box_extra[k]);
	}
	csb_wr32 (w, 0);
	csb_wr32 (w, csb->unknown ? csb->unknown : 1);
	for (int k = 0; k < 4; k++)
		csb_wr32 (w, 0);

	// object name offsets in the same (spheres-first) order
	u32 off = 0;
	for (int pass = 1; pass >= 0; pass--)
		for (u32 i = 0; i < csb->n_objects; i++)
		{
			const csb_object_t *o = csb->objects + i;
			if ((o->is_sphere ? 1 : 0) != pass)
				continue;
			csb_wr32 (w, off);
			off += (u32)strlen (o->name ? o->name : "") + 1;
		}
	for (int pass = 1; pass >= 0; pass--)
		for (u32 i = 0; i < csb->n_objects; i++)
		{
			const csb_object_t *o = csb->objects + i;
			if ((o->is_sphere ? 1 : 0) != pass)
				continue;
			if (big_endian)
				csb_wr32 (w, (u32)o->colflag);
			else
				csb_wr64 (w, o->colflag);
		}
	for (int pass = 1; pass >= 0; pass--)
		for (u32 i = 0; i < csb->n_objects; i++)
		{
			const csb_object_t *o = csb->objects + i;
			if ((o->is_sphere ? 1 : 0) != pass)
				continue;
			csb_wr16 (w, (u16)o->node_index);
		}
	{
		// object string table (spheres-first order)
		u32 total = off, aligned = (total + 3) & ~3u;
		csb_wr32 (w, aligned);
		for (int pass = 1; pass >= 0; pass--)
			for (u32 i = 0; i < csb->n_objects; i++)
			{
				const csb_object_t *o = csb->objects + i;
				if ((o->is_sphere ? 1 : 0) != pass)
					continue;
				ccp nm = o->name ? o->name : "";
				csb_wr_bytes (w, nm, (u32)strlen (nm) + 1);
			}
		while (total < aligned)
		{
			csb_wr08 (w, 0);
			total++;
		}
	}

	// mesh table over models[0]
	const csb_model_t *m0 = csb->models;
	csb_wr16 (w, big_endian ? 1 : (csb->unknown3 ? csb->unknown3 : 2));
	csb_wr16 (w, 1); // combined group always holds exactly models[0]
	csb_wr32 (w, m0->n_meshes);
	off = 0;
	for (u32 i = 0; i < m0->n_meshes; i++)
	{
		csb_wr32 (w, off);
		off += (u32)strlen (m0->meshes[i].name ? m0->meshes[i].name : "") + 1;
	}
	u32 tri_acc = 0;
	for (u32 i = 0; i < m0->n_meshes; i++)
	{
		csb_wr32 (w, tri_acc);
		tri_acc += m0->meshes[i].n_tris;
	}
	u32 vtx_acc = 0;
	for (u32 i = 0; i < m0->n_meshes; i++)
	{
		csb_wr32 (w, vtx_acc);
		vtx_acc += m0->meshes[i].n_vtx;
	}
	for (u32 i = 0; i < m0->n_meshes; i++)
	{
		if (big_endian)
			csb_wr32 (w, (u32)m0->meshes[i].colflag);
		else
			csb_wr64 (w, m0->meshes[i].colflag);
	}
	for (u32 i = 0; i < m0->n_meshes; i++)
		csb_wr32 (w, m0->meshes[i].mat_attr);
	for (u32 i = 0; i < m0->n_meshes; i++)
		csb_wr32 (w, 0);
	for (u32 i = 0; i < m0->n_meshes; i++)
		csb_wr16 (w, (u16)m0->meshes[i].node_index);
	{
		char **names = 0;
		if (m0->n_meshes)
		{
			names = MALLOC ((size_t)m0->n_meshes * sizeof (char *));
			if (!names)
				w->err = true;
			else
				for (u32 i = 0; i < m0->n_meshes; i++)
					names[i] = m0->meshes[i].name ? m0->meshes[i].name : (char *)"";
		}
		if (!w->err)
			csb_wr_strtab (w, names, m0->n_meshes);
		FREE (names);
	}

	// nodes
	if (csb->n_nodes > 65535)
		w->err = true;
	else
	{
		csb_wr16 (w, (u16)csb->n_nodes);
		for (u32 i = 0; i < csb->n_nodes; i++)
		{
			csb_wr16 (w, csb->nodes[i].id);
			csb_wr08 (w, csb->nodes[i].flags);
			csb_wr08 (w, csb->nodes[i].num_children);
		}
	}

	// models[0] body, then the single tail block, then split models
	csb_write_model_body (w, m0, false);
	csb_wr32 (w, 0);
	csb_wr08 (w, 0xFE);
	csb_wr08 (w, 0x07);
	csb_wr16 (w, 0);
	csb_wr32 (w, csb->n_models - 1);
	csb_wrvec (w, &csb->sub_bbox.min);
	csb_wrvec (w, &csb->sub_bbox.max);
	for (u32 i = 1; i < csb->n_models; i++)
		csb_write_model_body (w, csb->models + i, true);

	if (w->err)
	{
		FREE (w->data);
		return ERR_CANT_CREATE;
	}
	*dest = w->data;
	*dest_size = w->size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
//--- ctb parsing -------------------------------------------------------------
//-----------------------------------------------------------------------------

void FreeCTB (ctb_t *ctb)
{
	if (!ctb)
		return;
	for (u32 i = 0; i < ctb->n_nodes; i++)
		FREE (ctb->nodes[i].triangles);
	FREE (ctb->nodes);
	memset (ctb, 0, sizeof (*ctb));
}

static bool ctb_parse (ctb_t *ctb, const u8 *data, u32 size, bool be)
{
	memset (ctb, 0, sizeof (*ctb));
	ctb->big_endian = be;
	if (!data || size < 44)
		return false;
	// header sentinels: u32 0,0,0 + u32 1 (endian-visible only in the 1)
	u32 h0 = be ? csb_rd32be (data) : csb_rd32le (data);
	u32 h1 = be ? csb_rd32be (data + 4) : csb_rd32le (data + 4);
	u32 h2 = be ? csb_rd32be (data + 8) : csb_rd32le (data + 8);
	u32 h3 = be ? csb_rd32be (data + 12) : csb_rd32le (data + 12);
	if (h0 != 0 || h1 != 0 || h2 != 0 || h3 != 1)
		return false;

	csb_reader_t rd = { data, size, 16, be, false };
	csb_reader_t *r = &rd;
	ctb->num_model_groups = 1;
	ctb->root_size = csb_rdf32 (r);
	ctb->unk = csb_rdf32 (r);
	if (!(ctb->root_size > 0.0f) || ctb->root_size >= 1e30f
		|| csb_f32_bits (ctb->unk) != 0x3F800000u)
		return false;
	csb_rdvec (r, &ctb->root_position);
	u32 n_nodes = csb_rd32 (r);
	u32 n_root_tris = csb_rd32 (r);
	if (!n_nodes || n_nodes > 1000000 || n_root_tris > 10000000)
		return false;
	if ((u64)n_nodes * 24 > csb_remain (r))
		return false;
	ctb->nodes = MALLOC ((size_t)n_nodes * sizeof (ctb_node_t));
	if (!ctb->nodes)
		return false;
	ctb->n_nodes = n_nodes;
	for (u32 i = 0; i < n_nodes; i++)
	{
		ctb_node_t *n = ctb->nodes + i;
		memset (n, 0, sizeof (*n));
		csb_rdvec (r, &n->position);
		n->size = csb_rdf32 (r);
		n->node_id = csb_rd32 (r);
		n->child_bits = csb_rd08 (r);
		n->root_flag = csb_rd08 (r);
		n->padding = csb_rd16 (r);
		n->n_triangles = csb_rd32 (r);
		if (!(n->size > 0.0f) || n->size >= 1e30f)
			r->err = true;
		if ((i == 0 ? n->root_flag != 1 : n->root_flag != 0x7F)
			|| n->n_triangles > 10000000)
			r->err = true;
	}
	if (r->err)
	{
		FreeCTB (ctb);
		return false;
	}
	if (ctb->nodes[0].n_triangles != n_root_tris)
	{
		FreeCTB (ctb);
		return false;
	}
	// triangle index arrays
	u64 total_idx = 0;
	for (u32 i = 0; i < n_nodes; i++)
		total_idx += ctb->nodes[i].n_triangles;
	if (total_idx * 4 > csb_remain (r))
	{
		FreeCTB (ctb);
		return false;
	}
	for (u32 i = 0; i < n_nodes; i++)
	{
		ctb_node_t *n = ctb->nodes + i;
		if (!n->n_triangles)
			continue;
		n->triangles = MALLOC ((size_t)n->n_triangles * sizeof (u32));
		if (!n->triangles)
		{
			FreeCTB (ctb);
			return false;
		}
		for (u32 k = 0; k < n->n_triangles; k++)
			n->triangles[k] = csb_rd32 (r);
	}
	// walk the child_bits tree exactly like the reference loader; the walk
	// must not run past the node list
	u32 index = 0;
	bool walk_ok = true;
	// iterative depth-first walk with an explicit stack of (node, next slot)
	typedef struct
	{
		u32 node;
		int slot;
	} walk_frame_t;
	walk_frame_t *stack = MALLOC ((size_t)n_nodes * sizeof (walk_frame_t));
	if (!stack)
	{
		FreeCTB (ctb);
		return false;
	}
	u32 depth = 0;
	stack[0].node = 0;
	stack[0].slot = 0;
	while (depth < n_nodes && walk_ok)
	{
		walk_frame_t *f = stack + depth;
		ctb_node_t *n = ctb->nodes + f->node;
		if (f->slot >= 8)
		{
			if (!depth)
				break;
			depth--;
			continue;
		}
		int slot = f->slot++;
		if (!(n->child_bits & (1u << slot)))
			continue;
		index++;
		if (index >= n_nodes)
		{
			walk_ok = false;
			break;
		}
		if (depth + 1 >= n_nodes)
		{
			walk_ok = false;
			break;
		}
		depth++;
		stack[depth].node = index;
		stack[depth].slot = 0;
	}
	FREE (stack);
	if (!walk_ok || r->err || r->pos != size)
	{
		FreeCTB (ctb);
		return false;
	}
	return true;
}

int DetectCTBEndian (const u8 *data, u32 size)
{
	ctb_t tmp;
	if (ctb_parse (&tmp, data, size, false))
	{
		FreeCTB (&tmp);
		return 0;
	}
	if (ctb_parse (&tmp, data, size, true))
	{
		FreeCTB (&tmp);
		return 1;
	}
	return -1;
}

bool IsCTB (const u8 *data, u32 size)
{
	return DetectCTBEndian (data, size) >= 0;
}

enumError ScanCTB (ctb_t *ctb, const u8 *data, u32 size)
{
	if (!ctb)
		return ERR_INVALID_DATA;
	memset (ctb, 0, sizeof (*ctb));
	if (ctb_parse (ctb, data, size, false))
		return ERR_OK;
	if (ctb_parse (ctb, data, size, true))
		return ERR_OK;
	return ERR_NOTHING_TO_DO;
}

enumError SaveCTB (u8 **dest, u32 *dest_size, const ctb_t *ctb, bool big_endian)
{
	if (!dest || !dest_size || !ctb || !ctb->n_nodes)
		return ERR_INVALID_DATA;
	csb_writer_t wr = { 0, 0, 0, big_endian, false };
	csb_writer_t *w = &wr;
	csb_wr32 (w, 0);
	csb_wr32 (w, 0);
	csb_wr32 (w, 0);
	csb_wr32 (w, ctb->num_model_groups ? ctb->num_model_groups : 1);
	csb_wr32 (w, csb_f32_bits (ctb->nodes[0].size));
	csb_wr32 (w, csb_f32_bits (ctb->unk ? ctb->unk : 1.0f));
	csb_wrvec (w, &ctb->nodes[0].position);
	csb_wr32 (w, ctb->n_nodes);
	csb_wr32 (w, ctb->nodes[0].n_triangles);
	for (u32 i = 0; i < ctb->n_nodes; i++)
	{
		const ctb_node_t *n = ctb->nodes + i;
		csb_wrvec (w, &n->position);
		csb_wrf32 (w, n->size);
		csb_wr32 (w, n->node_id);
		csb_wr08 (w, n->child_bits);
		csb_wr08 (w, n->root_flag);
		csb_wr16 (w, n->padding);
		csb_wr32 (w, n->n_triangles);
	}
	for (u32 i = 0; i < ctb->n_nodes; i++)
	{
		const ctb_node_t *n = ctb->nodes + i;
		for (u32 k = 0; k < n->n_triangles; k++)
			csb_wr32 (w, n->triangles[k]);
	}
	if (w->err)
	{
		FREE (w->data);
		return ERR_CANT_CREATE;
	}
	*dest = w->data;
	*dest_size = w->size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
//--- ctb octree generation (mirrors OctreeGenerator, XZ-quadtree) ------------
//-----------------------------------------------------------------------------

typedef struct oct_node_t
{
	vec3_t pos;
	float scale;
	u32 *ids; // contained triangle ids (leaves only after build)
	u32 n_ids;
	struct oct_node_t *child[4];
} oct_node_t;

static void oct_free (oct_node_t *n)
{
	if (!n)
		return;
	for (int i = 0; i < 4; i++)
		oct_free (n->child[i]);
	FREE (n->ids);
	FREE (n);
}

// Effective overlap test from TriangleHelper (infinite-height XZ AABB test;
// the SAT code after the early return is dead upstream).
static bool csb_tri_overlaps (const csb_model_t *m, u32 tri, vec3_t center, float half)
{
	if (tri >= m->n_triangles)
		return false;
	const vec3_t *a = m->positions + m->triangles[tri].a;
	const vec3_t *b = m->positions + m->triangles[tri].b;
	const vec3_t *c = m->positions + m->triangles[tri].c;
	float minx = a->x, maxx = a->x, miny = a->y, maxy = a->y, minz = a->z, maxz = a->z;
	if (b->x < minx)
		minx = b->x;
	if (b->x > maxx)
		maxx = b->x;
	if (b->y < miny)
		miny = b->y;
	if (b->y > maxy)
		maxy = b->y;
	if (b->z < minz)
		minz = b->z;
	if (b->z > maxz)
		maxz = b->z;
	if (c->x < minx)
		minx = c->x;
	if (c->x > maxx)
		maxx = c->x;
	if (c->y < miny)
		miny = c->y;
	if (c->y > maxy)
		maxy = c->y;
	if (c->z < minz)
		minz = c->z;
	if (c->z > maxz)
		maxz = c->z;
	const float H = 10000.0f;
	if (minx > center.x + half || maxx < center.x - half)
		return false;
	if (miny > 0.0f + H || maxy < 0.0f - H)
		return false;
	if (minz > center.z + half || maxz < center.z - half)
		return false;
	return true;
}

#define CSB_OCT_MAXTRIS 10
#define CSB_OCT_MAXDEPTH 6

static bool oct_build (oct_node_t *n, const csb_model_t *m, const u32 *all, u32 n_all,
	int depth)
{
	u32 *contained = MALLOC ((size_t)n_all * sizeof (u32));
	if (!contained)
		return false;
	u32 n_contained = 0;
	for (u32 i = 0; i < n_all; i++)
		if (csb_tri_overlaps (m, all[i], n->pos, n->scale))
			contained[n_contained++] = all[i];
	if (n_contained > CSB_OCT_MAXTRIS && depth < CSB_OCT_MAXDEPTH)
	{
		float cs = n->scale / 2.0f;
		for (int i = 0; i < 4; i++)
		{
			oct_node_t *c = CALLOC (1, sizeof (*c));
			if (!c)
			{
				FREE (contained);
				return false;
			}
			c->scale = cs;
			c->pos.x = n->pos.x + ((i & 1) ? cs : -cs);
			c->pos.y = 0.0f;
			c->pos.z = n->pos.z + ((i & 2) ? cs : -cs);
			n->child[i] = c;
			if (!oct_build (c, m, all, n_all, depth + 1))
			{
				FREE (contained);
				return false;
			}
		}
		FREE (contained);
	}
	else
	{
		n->ids = contained;
		n->n_ids = n_contained;
	}
	return true;
}

static int csb_cmp_u32 (const void *a, const void *b)
{
	u32 x = *(const u32 *)a, y = *(const u32 *)b;
	return x < y ? -1 : x > y;
}

// Recursive subtree collection (depth is bounded by CSB_OCT_MAXDEPTH).
static bool oct_collect_rec (const oct_node_t *n, u32 **ids, u32 *cnt, u32 *cap)
{
	for (u32 i = 0; i < n->n_ids; i++)
	{
		if (*cnt >= *cap)
		{
			u32 ncap = *cap ? *cap * 2 : 64;
			u32 *nd = REALLOC (*ids, (size_t)ncap * sizeof (*nd));
			if (!nd)
				return false;
			*ids = nd;
			*cap = ncap;
		}
		(*ids)[(*cnt)++] = n->ids[i];
	}
	for (int i = 0; i < 4; i++)
		if (n->child[i] && !oct_collect_rec (n->child[i], ids, cnt, cap))
			return false;
	return true;
}

static bool oct_subtree_ids (const oct_node_t *n, u32 **out_ids, u32 *out_n)
{
	u32 *ids = 0, cnt = 0, cap = 0;
	if (!oct_collect_rec (n, &ids, &cnt, &cap))
	{
		FREE (ids);
		return false;
	}
	if (cnt)
	{
		qsort (ids, cnt, sizeof (*ids), csb_cmp_u32);
		u32 w = 1;
		for (u32 i = 1; i < cnt; i++)
			if (ids[i] != ids[w - 1])
				ids[w++] = ids[i];
		cnt = w;
	}
	*out_ids = ids;
	*out_n = cnt;
	return true;
}

enumError GenerateCTB (ctb_t *ctb, const csb_t *csb)
{
	if (!ctb || !csb)
		return ERR_INVALID_DATA;
	memset (ctb, 0, sizeof (*ctb));
	if (!csb->n_models)
		return ERR_OK;
	const csb_model_t *m = csb->models;
	ctb->big_endian = csb->big_endian;
	ctb->num_model_groups = 1;
	ctb->unk = 1.0f;
	if (!m->n_triangles || !m->n_positions)
		return ERR_OK; // nothing to index, like the reference tool

	float dx = m->bbox.max.x - m->bbox.min.x;
	float dz = m->bbox.max.z - m->bbox.min.z;
	float root_scale = (dx > dz ? dx : dz) * 0.68f;
	if (!(root_scale > 0.0f))
		return ERR_OK;
	vec3_t root_pos;
	root_pos.x = (m->bbox.min.x + m->bbox.max.x) / 2.0f;
	root_pos.y = 0.0f;
	root_pos.z = (m->bbox.min.z + m->bbox.max.z) / 2.0f;
	ctb->root_size = root_scale;
	ctb->root_position = root_pos;

	u32 *all = MALLOC ((size_t)m->n_triangles * sizeof (*all));
	if (!all)
		return ERR_OUT_OF_MEMORY;
	for (u32 i = 0; i < m->n_triangles; i++)
		all[i] = i;

	oct_node_t *root = CALLOC (1, sizeof (*root));
	bool ok = root != 0;
	if (ok)
	{
		root->pos = root_pos;
		root->scale = root_scale;
		// reference Build() subdivides the root first, then inserts the
		// full triangle list into each child
		float cs = root_scale / 2.0f;
		for (int i = 0; ok && i < 4; i++)
		{
			oct_node_t *c = CALLOC (1, sizeof (*c));
			if (!c)
				ok = false;
			else
			{
				c->scale = cs;
				c->pos.x = root_pos.x + ((i & 1) ? cs : -cs);
				c->pos.y = 0.0f;
				c->pos.z = root_pos.z + ((i & 2) ? cs : -cs);
				root->child[i] = c;
				ok = oct_build (c, m, all, m->n_triangles, 0);
			}
		}
	}
	FREE (all);
	if (!ok)
	{
		oct_free (root);
		return ERR_OUT_OF_MEMORY;
	}

	// flatten depth-first (mirrors SetupOctree: child bit per non-empty
	// child, leaf node_id = id + slot, inner node_id = slot, id += 8)
	u32 cap = 64;
	ctb->nodes = MALLOC (cap * sizeof (ctb_node_t));
	if (!ctb->nodes)
	{
		oct_free (root);
		return ERR_OUT_OF_MEMORY;
	}
	u32 *root_ids = 0, n_root_ids = 0;
	ok = oct_subtree_ids (root, &root_ids, &n_root_ids);
	if (ok)
	{
		ctb->nodes[0].position = root_pos;
		ctb->nodes[0].size = root_scale;
		ctb->nodes[0].node_id = 0;
		ctb->nodes[0].child_bits = 0;
		ctb->nodes[0].root_flag = 1;
		ctb->nodes[0].padding = 0;
		ctb->nodes[0].triangles = root_ids;
		ctb->nodes[0].n_triangles = n_root_ids;
		ctb->n_nodes = 1;
	}
	// stack of (oct node, ctb index, next slot, id base)
	typedef struct
	{
		const oct_node_t *on;
		u32 ci;
		int slot;
	} gen_frame_t;
	gen_frame_t *stack = 0;
	if (ok)
	{
		stack = MALLOC (64 * sizeof (*stack));
		ok = stack != 0;
	}
	int id = 1;
	if (ok)
	{
		stack[0].on = root;
		stack[0].ci = 0;
		stack[0].slot = 0;
		u32 sdepth = 0;
		while (ok)
		{
			gen_frame_t *f = stack + sdepth;
			if (f->slot >= 4)
			{
				id += 8;
				if (!sdepth)
					break;
				sdepth--;
				continue;
			}
			int slot = f->slot++;
			const oct_node_t *c = f->on->child[slot];
			if (!c)
				continue;
			u32 *ids = 0, n_ids = 0;
			if (!oct_subtree_ids (c, &ids, &n_ids))
			{
				ok = false;
				break;
			}
			if (!n_ids)
			{
				FREE (ids);
				continue;
			}
			if (ctb->n_nodes >= cap)
			{
				u32 ncap = cap * 2;
				ctb_node_t *nd = REALLOC (ctb->nodes, (size_t)ncap * sizeof (*nd));
				if (!nd)
				{
					FREE (ids);
					ok = false;
					break;
				}
				ctb->nodes = nd;
				cap = ncap;
			}
			bool is_leaf = !c->child[0] && !c->child[1] && !c->child[2] && !c->child[3];
			ctb_node_t *cn = ctb->nodes + ctb->n_nodes;
			memset (cn, 0, sizeof (*cn));
			cn->position = c->pos;
			cn->size = c->scale;
			cn->node_id = is_leaf ? (u32)(id + slot) : (u32)slot;
			cn->root_flag = 0x7F;
			cn->triangles = ids;
			cn->n_triangles = n_ids;
			ctb->nodes[f->ci].child_bits |= (u8)(1u << slot);
			sdepth++;
			stack[sdepth].on = c;
			stack[sdepth].ci = ctb->n_nodes;
			stack[sdepth].slot = 0;
			ctb->n_nodes++;
		}
	}
	FREE (stack);
	oct_free (root);
	if (!ok)
	{
		FreeCTB (ctb);
		return ERR_OUT_OF_MEMORY;
	}
	return ERR_OK;
}

//-----------------------------------------------------------------------------
//--- csb <-> model_t ---------------------------------------------------------
//-----------------------------------------------------------------------------

static int csb_mat_index (model_t *model, u32 attr, u64 flag)
{
	char name[64];
	snprintf (name, sizeof (name), CSB_MATERIAL_FMT, attr, (unsigned long long)flag);
	for (size_t i = 0; i < model->num_materials; i++)
		if (!strcmp (model->materials[i].name, name))
			return (int)i;
	material_t *nm = REALLOC (model->materials,
		(model->num_materials + 1) * sizeof (*nm));
	if (!nm)
		return -1;
	model->materials = nm;
	memset (model->materials + model->num_materials, 0, sizeof (*nm));
	snprintf (model->materials[model->num_materials].name,
		sizeof (model->materials[0].name), "%s", name);
	return (int)model->num_materials++;
}

// Append one decoded mesh (positions POOL[0..N_POS), triangle list TRIS) to MODEL.
static bool csb_emit_mesh (model_t *model, ccp name, u32 mat_attr, u64 colflag,
	const vec3_t *pool, u32 n_pos, const csb_tri_t *tris, u32 n_tris)
{
	// deduplicate positions (exact float bits) into a local pool
	vec3_t *lpos = 0;
	u32 n_lpos = 0, cap_lpos = 0;
	int *idxmap = 0;
	if (n_pos)
	{
		idxmap = MALLOC ((size_t)n_pos * sizeof (*idxmap));
		if (!idxmap)
			return false;
		for (u32 i = 0; i < n_pos; i++)
			idxmap[i] = -1;
	}
	u32 *tri_local = 0; // remapped a,b,c per triangle
	vec3_t *normals = 0;
	u32 n_out_tris = 0;
	if (n_tris)
	{
		tri_local = MALLOC ((size_t)n_tris * 3 * sizeof (*tri_local));
		normals = MALLOC ((size_t)n_tris * sizeof (*normals));
		if (!tri_local || !normals)
		{
			FREE (lpos);
			FREE (idxmap);
			FREE (tri_local);
			FREE (normals);
			return false;
		}
	}
	for (u32 t = 0; t < n_tris; t++)
	{
		// indices address the caller-supplied pool directly (combined
		// meshes share the model-global buffer, so no rebasing)
		u32 src[3] = { tris[t].a, tris[t].b, tris[t].c };
		for (int k = 0; k < 3; k++)
			if (src[k] >= n_pos)
			{
				FREE (lpos);
				FREE (idxmap);
				FREE (tri_local);
				FREE (normals);
				return false;
			}
		const vec3_t *pa = pool + src[0], *pb = pool + src[1], *pc = pool + src[2];
		// face normal
		float ux = pb->x - pa->x, uy = pb->y - pa->y, uz = pb->z - pa->z;
		float vx = pc->x - pa->x, vy = pc->y - pa->y, vz = pc->z - pa->z;
		float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
		float len2 = nx * nx + ny * ny + nz * nz;
		if (!(len2 >= 0.01f)) // degenerate (also catches NaN)
			continue;
		float inv = 1.0f / sqrtf (len2);
		u32 li[3];
		for (int k = 0; k < 3; k++)
		{
			if (idxmap[src[k]] < 0)
			{
				if (n_lpos >= cap_lpos)
				{
					u32 ncap = cap_lpos ? cap_lpos * 2 : 64;
					vec3_t *nd = REALLOC (lpos, (size_t)ncap * sizeof (*nd));
					if (!nd)
					{
						FREE (lpos);
						FREE (idxmap);
						FREE (tri_local);
						FREE (normals);
						return false;
					}
					lpos = nd;
					cap_lpos = ncap;
				}
				lpos[n_lpos] = pool[src[k]];
				idxmap[src[k]] = (int)n_lpos++;
			}
			li[k] = (u32)idxmap[src[k]];
		}
		tri_local[n_out_tris * 3 + 0] = li[0];
		tri_local[n_out_tris * 3 + 1] = li[1];
		tri_local[n_out_tris * 3 + 2] = li[2];
		normals[n_out_tris].x = nx * inv;
		normals[n_out_tris].y = ny * inv;
		normals[n_out_tris].z = nz * inv;
		n_out_tris++;
	}
	FREE (idxmap);
	if (!n_out_tris)
	{
		FREE (lpos);
		FREE (tri_local);
		FREE (normals);
		return true; // empty mesh: skip silently (exporter drops it anyway)
	}
	mesh_t *nm = REALLOC (model->meshes, (model->num_meshes + 1) * sizeof (*nm));
	if (!nm)
	{
		FREE (lpos);
		FREE (tri_local);
		FREE (normals);
		return false;
	}
	model->meshes = nm;
	mesh_t *mesh = model->meshes + model->num_meshes;
	memset (mesh, 0, sizeof (*mesh));
	snprintf (mesh->name, sizeof (mesh->name), "%s", name ? name : "mesh");
	mesh->material_idx = csb_mat_index (model, mat_attr, colflag);
	mesh->positions = lpos;
	mesh->num_positions = n_lpos;
	mesh->normals = normals;
	mesh->num_normals = n_out_tris;
	mesh->vertices = MALLOC ((size_t)n_out_tris * 3 * sizeof (vertex_t));
	if (!mesh->vertices)
	{
		// roll back the mesh slot (pools stay owned by the dead slot? no:
		// free them and keep counts unchanged)
		FREE (lpos);
		FREE (normals);
		FREE (tri_local);
		return false;
	}
	mesh->num_vertices = (size_t)n_out_tris * 3;
	for (u32 i = 0; i < n_out_tris; i++)
		for (int k = 0; k < 3; k++)
		{
			vertex_t *v = mesh->vertices + (size_t)i * 3 + k;
			v->position_idx = (int)tri_local[(size_t)i * 3 + k];
			v->normal_idx = (int)i;
			v->texcoord_idx = v->matrix_idx = -1;
			v->color_idx[0] = v->color_idx[1] = -1;
			for (int j = 0; j < 7; j++)
				v->extra_texcoord_idx[j] = -1;
		}
	FREE (tri_local);
	model->num_meshes++;
	return mesh->material_idx >= 0;
}

static model_t *csb_to_model (const csb_t *csb)
{
	model_t *model = CALLOC (1, sizeof (*model));
	if (!model)
		return 0;

	// geometry: combined meshes reference the global buffer via slices
	if (csb->n_models)
	{
		const csb_model_t *m0 = csb->models;
		for (u32 i = 0; i < m0->n_meshes; i++)
		{
			const csb_mesh_t *sm = m0->meshes + i;
			if (!sm->n_tris)
				continue;
			if (!csb_emit_mesh (model, sm->name, sm->mat_attr, sm->colflag,
					m0->positions, m0->n_positions,
					m0->triangles + sm->tri_start, sm->n_tris))
				goto fail;
		}
		// split models carry their own buffers
		for (u32 i = 1; i < csb->n_models; i++)
		{
			const csb_model_t *sm = csb->models + i;
			if (!sm->n_triangles)
				continue;
			if (!csb_emit_mesh (model, sm->name, sm->mat_attr, sm->colflag,
					sm->positions, sm->n_positions, sm->triangles,
					sm->n_triangles))
				goto fail;
		}
	}

	// trigger volumes ride as instances (plain scene nodes do not survive
	// a GLB round-trip as joints without a skin): one shared degenerate
	// carrier mesh plus one instance per object carrying center/size.
	if (csb->n_objects)
	{
		mesh_t *nm = REALLOC (model->meshes, (model->num_meshes + 1) * sizeof (*nm));
		if (!nm)
			goto fail;
		model->meshes = nm;
		mesh_t *carrier = model->meshes + model->num_meshes;
		memset (carrier, 0, sizeof (*carrier));
		snprintf (carrier->name, sizeof (carrier->name), "%s", CSB_TRIGGER_MESH);
		carrier->material_idx = csb_mat_index (model, 0, 0);
		carrier->positions = CALLOC (1, sizeof (vec3_t));
		carrier->normals = CALLOC (1, sizeof (vec3_t));
		carrier->vertices = CALLOC (3, sizeof (vertex_t));
		if (!carrier->positions || !carrier->normals || !carrier->vertices
			|| carrier->material_idx < 0)
		{
			FREE (carrier->positions);
			FREE (carrier->normals);
			FREE (carrier->vertices);
			goto fail;
		}
		carrier->num_positions = 1;
		carrier->normals[0].y = 1.0f;
		carrier->num_normals = 1;
		for (int k = 0; k < 3; k++)
		{
			carrier->vertices[k].position_idx = 0;
			carrier->vertices[k].normal_idx = 0;
			carrier->vertices[k].texcoord_idx = carrier->vertices[k].matrix_idx = -1;
			carrier->vertices[k].color_idx[0] = carrier->vertices[k].color_idx[1] = -1;
			for (int j = 0; j < 7; j++)
				carrier->vertices[k].extra_texcoord_idx[j] = -1;
		}
		carrier->num_vertices = 3;
		int carrier_idx = (int)model->num_meshes++;
		model->instances = CALLOC (csb->n_objects, sizeof (model_instance_t));
		if (!model->instances)
			goto fail;
		for (u32 i = 0; i < csb->n_objects; i++)
		{
			const csb_object_t *o = csb->objects + i;
			model_instance_t *in = model->instances + model->num_instances++;
			char base[128];
			snprintf (base, sizeof (base), "%s%s",
				o->is_sphere ? CSB_MAPOBJ_SPHERE_PREFIX : CSB_MAPOBJ_BOX_PREFIX,
				o->name);
			if (o->colflag)
				snprintf (in->name, sizeof (in->name), "%s#FLAG%llu", base,
					(unsigned long long)o->colflag);
			else
				snprintf (in->name, sizeof (in->name), "%s", base);
			in->mesh_idx = carrier_idx;
			in->parent_idx = -1;
			in->has_matrix = 0;
			in->translate = o->p1;
			if (o->is_sphere)
			{
				in->scale.x = in->scale.y = in->scale.z = o->radius;
			}
			else
			{
				in->scale = o->size;
				in->rotate = o->rotation;
			}
		}
	}
	return model;

fail:
	FreeModel (model);
	return 0;
}

enumError DecodeCSB (const u8 *data, u32 size, ccp out_path)
{
	if (!data || !out_path)
		return ERR_INVALID_DATA;
	csb_t csb;
	if (ScanCSB (&csb, data, size) != ERR_OK)
		return ERR_NOTHING_TO_DO;
	model_t *model = csb_to_model (&csb);
	FreeCSB (&csb);
	if (!model)
		return ERR_CANT_CREATE;
	int rc = ExportModelToGLB (model, out_path);
	FreeModel (model);
	return rc == 0 ? ERR_OK : ERR_CANT_CREATE;
}

//--- model -> csb ------------------------------------------------------------

// Parse "MAT{a}_FLAG{f}" (also "MAT{a}" / "FLAG{f}" alone, like the
// reference importer).
static void csb_parse_material (ccp name, u32 *attr, u64 *flag)
{
	*attr = 0;
	*flag = 0;
	if (!name)
		return;
	unsigned long long f = 0;
	unsigned int a = 0;
	if (sscanf (name, "MAT%u_FLAG%llu", &a, &f) == 2)
	{
		*attr = a;
		*flag = (u64)f;
	}
	else if (sscanf (name, "MAT%u", &a) == 1)
		*attr = a;
	else if (sscanf (name, "FLAG%llu", &f) == 1)
		*flag = (u64)f;
}

static void csb_compute_bbox (csb_bbox_t *bbox, const vec3_t *pos, u32 n)
{
	if (!n)
	{
		memset (bbox, 0, sizeof (*bbox));
		return;
	}
	bbox->min = bbox->max = pos[0];
	for (u32 i = 1; i < n; i++)
	{
		if (pos[i].x < bbox->min.x)
			bbox->min.x = pos[i].x;
		if (pos[i].y < bbox->min.y)
			bbox->min.y = pos[i].y;
		if (pos[i].z < bbox->min.z)
			bbox->min.z = pos[i].z;
		if (pos[i].x > bbox->max.x)
			bbox->max.x = pos[i].x;
		if (pos[i].y > bbox->max.y)
			bbox->max.y = pos[i].y;
		if (pos[i].z > bbox->max.z)
			bbox->max.z = pos[i].z;
	}
}

// Triangulate one model_t mesh into deduplicated positions + face-normal
// triangles (mirrors CsbImporter.ToTriangles, with exact float-bit vertex
// keys instead of float formatting).
typedef struct
{
	vec3_t *pos;
	u32 n_pos, cap_pos;
	csb_tri_t *tris;
	u32 n_tris, cap_tris;
} csb_geom_t;

static void csb_geom_free (csb_geom_t *g)
{
	FREE (g->pos);
	FREE (g->tris);
	memset (g, 0, sizeof (*g));
}

static int csb_geom_vertex (csb_geom_t *g, vec3_t v)
{
	for (u32 i = 0; i < g->n_pos; i++)
		if (!memcmp (g->pos + i, &v, sizeof (v)))
			return (int)i;
	if (g->n_pos >= g->cap_pos)
	{
		u32 ncap = g->cap_pos ? g->cap_pos * 2 : 64;
		vec3_t *nd = REALLOC (g->pos, (size_t)ncap * sizeof (*nd));
		if (!nd)
			return -1;
		g->pos = nd;
		g->cap_pos = ncap;
	}
	g->pos[g->n_pos] = v;
	return (int)g->n_pos++;
}

static bool csb_geom_add_mesh (csb_geom_t *g, const mesh_t *mesh, u32 *out_base_tri)
{
	*out_base_tri = g->n_tris;
	if (!mesh->num_vertices || !mesh->vertices)
		return true;
	for (size_t t = 0; t + 2 < mesh->num_vertices; t += 3)
	{
		int pi[3];
		vec3_t pv[3];
		for (int k = 0; k < 3; k++)
		{
			const vertex_t *v = mesh->vertices + t + k;
			if (v->position_idx < 0 || (size_t)v->position_idx >= mesh->num_positions)
				return false;
			pv[k] = mesh->positions[v->position_idx];
		}
		float ux = pv[1].x - pv[0].x, uy = pv[1].y - pv[0].y, uz = pv[1].z - pv[0].z;
		float vx = pv[2].x - pv[0].x, vy = pv[2].y - pv[0].y, vz = pv[2].z - pv[0].z;
		float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
		if (!(nx * nx + ny * ny + nz * nz >= 0.01f))
			continue; // degenerate
		float inv = 1.0f / sqrtf (nx * nx + ny * ny + nz * nz);
		for (int k = 0; k < 3; k++)
		{
			pi[k] = csb_geom_vertex (g, pv[k]);
			if (pi[k] < 0)
				return false;
		}
		if (g->n_tris >= g->cap_tris)
		{
			u32 ncap = g->cap_tris ? g->cap_tris * 2 : 64;
			csb_tri_t *nd = REALLOC (g->tris, (size_t)ncap * sizeof (*nd));
			if (!nd)
				return false;
			g->tris = nd;
			g->cap_tris = ncap;
		}
		csb_tri_t *tri = g->tris + g->n_tris++;
		tri->a = (u32)pi[0];
		tri->b = (u32)pi[1];
		tri->c = (u32)pi[2];
		tri->normal.x = nx * inv;
		tri->normal.y = ny * inv;
		tri->normal.z = nz * inv;
	}
	return true;
}

// Split "MAPOBJ_SPHERE_name[#FLAGf]" / "MAPOBJ_BOX_name[#FLAGf]".
// Returns 1 = sphere, 0 = box, -1 = no MAPOBJ joint.
static int csb_parse_mapobj (ccp jname, bool *is_sphere, char *name_buf, size_t name_size,
	u64 *flag)
{
	*flag = 0;
	size_t sl = strlen (CSB_MAPOBJ_SPHERE_PREFIX), bl = strlen (CSB_MAPOBJ_BOX_PREFIX);
	ccp base = 0;
	if (!strncmp (jname, CSB_MAPOBJ_SPHERE_PREFIX, sl))
	{
		*is_sphere = true;
		base = jname + sl;
	}
	else if (!strncmp (jname, CSB_MAPOBJ_BOX_PREFIX, bl))
	{
		*is_sphere = false;
		base = jname + bl;
	}
	else
		return -1;
	ccp hash = strchr (base, '#');
	size_t len = hash ? (size_t)(hash - base) : strlen (base);
	if (len >= name_size)
		len = name_size - 1;
	memcpy (name_buf, base, len);
	name_buf[len] = 0;
	if (hash)
	{
		unsigned long long f = 0;
		if (sscanf (hash, "#FLAG%llu", &f) == 1)
			*flag = (u64)f;
	}
	return *is_sphere ? 1 : 0;
}

static bool csb_append_object (csb_t *csb, bool is_sphere, ccp name, u64 flag, u32 node,
	const vec3_t *translate, const vec3_t *rotate, const vec3_t *scale)
{
	csb_object_t *nd = REALLOC (csb->objects, (csb->n_objects + 1) * sizeof (*nd));
	if (!nd)
		return false;
	csb->objects = nd;
	csb_object_t *o = csb->objects + csb->n_objects;
	memset (o, 0, sizeof (*o));
	o->is_sphere = is_sphere;
	o->p1 = o->p2 = *translate;
	o->colflag = flag;
	o->node_index = node;
	o->name = STRDUP (name ? name : "object");
	if (!o->name)
		return false;
	if (is_sphere)
	{
		o->radius = scale->x;
		o->size.x = o->size.y = o->size.z = 1.0f;
	}
	else
	{
		o->size = *scale;
		o->rotation = *rotate;
		o->radius = 0.7f;
	}
	o->box_extra[2] = 1.0f;
	o->box_extra[7] = 1.0f;
	csb->n_objects++;
	return true;
}

// Decompose a column-major glTF node matrix into the exporter-style
// translate / euler-degrees (R = Ry*Rx*Rz) / scale triple.
static void csb_decompose_matrix (const float m[16], vec3_t *t, vec3_t *r, vec3_t *s)
{
	t->x = m[12];
	t->y = m[13];
	t->z = m[14];
	float sx = sqrtf (m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
	float sy = sqrtf (m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
	float sz = sqrtf (m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
	if (!(sx > 1e-12f))
		sx = 1.0f;
	if (!(sy > 1e-12f))
		sy = 1.0f;
	if (!(sz > 1e-12f))
		sz = 1.0f;
	s->x = sx;
	s->y = sy;
	s->z = sz;
	float r00 = m[0] / sx, r10 = m[1] / sx, r20 = m[2] / sx;
	float r01 = m[4] / sy, r11 = m[5] / sy, r21 = m[6] / sy;
	float r22 = m[10] / sz;
	float syy = -r20;
	if (syy > 1.0f)
		syy = 1.0f;
	else if (syy < -1.0f)
		syy = -1.0f;
	float x, y = asinf (syy), z;
	if (fabsf (r20) < 0.9999f)
	{
		x = atan2f (r21, r22);
		z = atan2f (r10, r00);
	}
	else
	{
		z = 0.0f;
		x = r20 <= -1.0f ? atan2f (-r01, r11) : atan2f (r01, r11);
	}
	r->x = x * 57.29577951308232f;
	r->y = y * 57.29577951308232f;
	r->z = z * 57.29577951308232f;
}

// Build a fresh node tree: root(0) -> parents (<=100 entries each) ->
// one node per entry. ENTRIES lists entry kinds in order; returns the
// first node id of each entry in ENTRY_NODE (or false on OOM).
static bool csb_build_nodes (csb_t *csb, u32 n_entries, u32 *entry_node)
{
	u32 n_parents = (n_entries + 99) / 100;
	if (!n_entries)
		n_parents = 0;
	u32 total = 1 + n_parents + n_entries; // root + parents + entries
	if (total > 65535)
		return false;
	csb->nodes = CALLOC (total ? total : 1, sizeof (csb_node_t));
	if (!csb->nodes)
		return false;
	csb->n_nodes = total;
	if (!total)
		return true;
	csb->nodes[0].id = 0;
	csb->nodes[0].num_children = (u8)n_parents;
	u32 ni = 1;
	for (u32 p = 0; p < n_parents; p++)
	{
		u32 left = n_entries - p * 100;
		u32 take = left > 100 ? 100 : left;
		csb->nodes[ni].id = (u16)ni;
		csb->nodes[ni].num_children = (u8)take;
		ni++;
		for (u32 k = 0; k < take; k++)
		{
			u32 e = p * 100 + k;
			csb->nodes[ni].id = (u16)ni;
			csb->nodes[ni].num_children = 0;
			entry_node[e] = ni;
			ni++;
		}
	}
	return true;
}

static enumError csb_from_model (csb_t *csb, const model_t *model, bool big_endian,
	bool map_object)
{
	memset (csb, 0, sizeof (*csb));
	csb->big_endian = big_endian;
	csb->unknown = 1;
	csb->unknown3 = big_endian ? 1 : 2;
	csb->tail_b0 = 0xFE;
	csb->tail_b1 = 0x07;
	if (!model)
		return ERR_INVALID_DATA;

	// collect MAPOBJ instances first (deterministic instance order),
	// then collision meshes (skipping the trigger carrier mesh)
	typedef struct
	{
		u32 inst_idx;
		bool is_sphere;
	} mapobj_ref_t;
	mapobj_ref_t *mrefs = 0;
	u32 n_mrefs = 0;
	for (size_t j = 0; j < model->num_instances; j++)
	{
		bool is_sphere = false;
		char dummy[8];
		u64 f = 0;
		if (csb_parse_mapobj (model->instances[j].name, &is_sphere, dummy,
				sizeof (dummy), &f) < 0)
			continue;
		mapobj_ref_t *nd = REALLOC (mrefs, (n_mrefs + 1) * sizeof (*nd));
		if (!nd)
		{
			FREE (mrefs);
			return ERR_OUT_OF_MEMORY;
		}
		mrefs = nd;
		mrefs[n_mrefs].inst_idx = (u32)j;
		mrefs[n_mrefs].is_sphere = is_sphere;
		n_mrefs++;
	}
	u32 n_meshes = 0;
	for (size_t j = 0; j < model->num_meshes; j++)
		if (strcmp (model->meshes[j].name, CSB_TRIGGER_MESH))
			n_meshes++;
	u32 n_entries = n_mrefs + n_meshes;
	u32 *entry_node = 0;
	if (n_entries)
	{
		entry_node = MALLOC ((size_t)n_entries * sizeof (*entry_node));
		if (!entry_node)
		{
			FREE (mrefs);
			return ERR_OUT_OF_MEMORY;
		}
	}
	if (!csb_build_nodes (csb, n_entries, entry_node))
	{
		FREE (mrefs);
		FREE (entry_node);
		return ERR_OUT_OF_MEMORY;
	}
	// objects
	for (u32 i = 0; i < n_mrefs; i++)
	{
		const model_instance_t *in = model->instances + mrefs[i].inst_idx;
		bool is_sphere = false;
		char name[128];
		u64 flag = 0;
		csb_parse_mapobj (in->name, &is_sphere, name, sizeof (name), &flag);
		vec3_t t, r, s;
		if (in->has_matrix)
			csb_decompose_matrix (in->matrix, &t, &r, &s);
		else
		{
			t = in->translate;
			r = in->rotate;
			s = in->scale;
		}
		if (!csb_append_object (
				csb, is_sphere, name, flag, entry_node[i], &t, &r, &s))
		{
			FREE (mrefs);
			FREE (entry_node);
			FreeCSB (csb);
			return ERR_OUT_OF_MEMORY;
		}
	}

	if (!map_object)
	{
		// combined DEADBEEF model: all meshes share one buffer
		csb->models = CALLOC (1, sizeof (csb_model_t));
		if (!csb->models)
		{
			FREE (mrefs);
			FREE (entry_node);
			FreeCSB (csb);
			return ERR_OUT_OF_MEMORY;
		}
		csb->n_models = 1;
		csb_model_t *m0 = csb->models;
		snprintf (m0->name, sizeof (m0->name), "%s", CSB_COMBINED_MODEL);
		m0->unknown0 = big_endian ? 2 : 3;
		m0->unknown5 = 1;
		csb_geom_t g;
		memset (&g, 0, sizeof (g));
		m0->meshes = n_meshes ? CALLOC (n_meshes, sizeof (csb_mesh_t)) : 0;
		if (n_meshes && !m0->meshes)
		{
			FREE (mrefs);
			FREE (entry_node);
			FreeCSB (csb);
			return ERR_OUT_OF_MEMORY;
		}
		bool ok = true;
		u32 n_included = 0;
		for (u32 i = 0; ok && i < (u32)model->num_meshes; i++)
		{
			const mesh_t *sm = model->meshes + i;
			if (!strcmp (sm->name, CSB_TRIGGER_MESH))
				continue; // trigger carrier, not collision
			u32 base_tri = 0, base_vtx = g.n_pos;
			u32 mat_attr = 0;
			u64 colflag = 0;
			if ((size_t)sm->material_idx < model->num_materials)
				csb_parse_material (model->materials[sm->material_idx].name, &mat_attr,
					&colflag);
			if (!csb_geom_add_mesh (&g, sm, &base_tri))
				ok = false;
			else
			{
				csb_mesh_t *dm = m0->meshes + m0->n_meshes++;
				dm->name = STRDUP (sm->name);
				if (!dm->name)
					ok = false;
				else
				{
					dm->mat_attr = mat_attr;
					dm->colflag = colflag;
					dm->node_index = (int)entry_node[n_mrefs + n_included++];
					dm->tri_start = base_tri;
					dm->vtx_start = base_vtx;
					dm->n_tris = g.n_tris - base_tri;
					dm->n_vtx = g.n_pos - base_vtx;
				}
			}
		}
		if (ok)
		{
			m0->positions = g.pos;
			m0->n_positions = g.n_pos;
			m0->triangles = g.tris;
			m0->n_triangles = g.n_tris;
			csb_compute_bbox (&m0->bbox, g.pos, g.n_pos);
		}
		else
		{
			csb_geom_free (&g);
			FREE (mrefs);
			FREE (entry_node);
			FreeCSB (csb);
			return ERR_OUT_OF_MEMORY;
		}
		csb->sub_bbox.min.x = csb->sub_bbox.min.y = csb->sub_bbox.min.z = -999999.0f;
		csb->sub_bbox.max.x = csb->sub_bbox.max.y = csb->sub_bbox.max.z = 999999.0f;
	}
	else
	{
		// split map-object mode: one model per mesh, no combined buffer
		csb->models = CALLOC (n_meshes + 1, sizeof (csb_model_t));
		if (!csb->models)
		{
			FREE (mrefs);
			FREE (entry_node);
			FreeCSB (csb);
			return ERR_OUT_OF_MEMORY;
		}
		csb->n_models = n_meshes + 1;
		csb_model_t *m0 = csb->models;
		snprintf (m0->name, sizeof (m0->name), "%s", CSB_COMBINED_MODEL);
		m0->unknown0 = big_endian ? 2 : 3;
		m0->unknown5 = 1;
		bool ok = true;
		u32 n_included = 0;
		for (u32 i = 0; ok && i < (u32)model->num_meshes; i++)
		{
			const mesh_t *sm = model->meshes + i;
			if (!strcmp (sm->name, CSB_TRIGGER_MESH))
				continue; // trigger carrier, not collision
			csb_model_t *dm = csb->models + 1 + n_included;
			snprintf (dm->name, sizeof (dm->name), "%s", sm->name);
			dm->unknown0 = big_endian ? 2 : 3;
			dm->unknown5 = 1;
			dm->node_index = entry_node[n_mrefs + n_included];
			n_included++;
			if ((size_t)sm->material_idx < model->num_materials)
				csb_parse_material (model->materials[sm->material_idx].name,
					&dm->mat_attr, &dm->colflag);
			csb_geom_t g;
			memset (&g, 0, sizeof (g));
			u32 base_tri = 0;
			if (!csb_geom_add_mesh (&g, sm, &base_tri))
			{
				csb_geom_free (&g);
				ok = false;
			}
			else
			{
				dm->positions = g.pos;
				dm->n_positions = g.n_pos;
				dm->triangles = g.tris;
				dm->n_triangles = g.n_tris;
				csb_compute_bbox (&dm->bbox, g.pos, g.n_pos);
			}
		}
		if (!ok)
		{
			FREE (mrefs);
			FREE (entry_node);
			FreeCSB (csb);
			return ERR_OUT_OF_MEMORY;
		}
		// sub bounding over all split positions
		bool first = true;
		for (u32 i = 1; i < csb->n_models; i++)
		{
			csb_model_t *dm = csb->models + i;
			for (u32 v = 0; v < dm->n_positions; v++)
			{
				if (first)
				{
					csb->sub_bbox.min = csb->sub_bbox.max = dm->positions[v];
					first = false;
				}
				else
				{
					if (dm->positions[v].x < csb->sub_bbox.min.x)
						csb->sub_bbox.min.x = dm->positions[v].x;
					if (dm->positions[v].y < csb->sub_bbox.min.y)
						csb->sub_bbox.min.y = dm->positions[v].y;
					if (dm->positions[v].z < csb->sub_bbox.min.z)
						csb->sub_bbox.min.z = dm->positions[v].z;
					if (dm->positions[v].x > csb->sub_bbox.max.x)
						csb->sub_bbox.max.x = dm->positions[v].x;
					if (dm->positions[v].y > csb->sub_bbox.max.y)
						csb->sub_bbox.max.y = dm->positions[v].y;
					if (dm->positions[v].z > csb->sub_bbox.max.z)
						csb->sub_bbox.max.z = dm->positions[v].z;
				}
			}
		}
		if (first)
			memset (&csb->sub_bbox, 0, sizeof (csb->sub_bbox));
	}
	FREE (mrefs);
	FREE (entry_node);
	return ERR_OK;
}

//--- path helpers ------------------------------------------------------------

static bool csb_has_suffix (ccp path, ccp suffix)
{
	if (!path || !suffix)
		return false;
	size_t pl = strlen (path), sl = strlen (suffix);
	return pl >= sl && !strcasecmp (path + pl - sl, suffix);
}

static bool csb_compressed_suffix (ccp path)
{
	return csb_has_suffix (path, ".zst") || csb_has_suffix (path, ".zs")
		|| csb_has_suffix (path, ".zstd");
}

// Derive the .ctb sidecar path from the .csb destination:
// "<stem>.csb[.zst]" -> "<stem>.ctb[.zst]".
static void csb_sidecar_path (char *dest, size_t size, ccp csb_path)
{
	char base[PATH_MAX];
	snprintf (base, sizeof (base), "%s", csb_path);
	bool compr = false;
	for (int k = 0; k < 3; k++)
	{
		if (csb_has_suffix (base, ".zst") || csb_has_suffix (base, ".zs")
			|| csb_has_suffix (base, ".zstd"))
		{
			char *dot = strrchr (base, '.');
			if (dot)
				*dot = 0;
			compr = true;
		}
	}
	{
		char *dot = strrchr (base, '.');
		char *slash = strrchr (base, '/');
		if (dot && (!slash || dot > slash)
			&& (!strcasecmp (dot, ".csb") || !strcasecmp (dot, ".ctb")))
			*dot = 0;
	}
	snprintf (dest, size, "%s.ctb%s", base, compr ? ".zst" : "");
}

static enumError csb_write_file (ccp path, const u8 *data, u32 size)
{
	u8 *out = (u8 *)data;
	u32 out_size = size;
	u8 *compressed = 0;
	u32 compressed_size = 0;
	if (csb_compressed_suffix (path))
	{
		if (EncodeZSTD (&compressed, &compressed_size, data, size, 0) != ERR_OK)
			return ERR_CANT_CREATE;
		out = compressed;
		out_size = compressed_size;
	}
	enumError err = SaveFILE (path, 0, true, out, out_size, 0);
	FREE (compressed);
	return err;
}

enumError EncodeCSB (
	const model_t *model, ccp out_csb_path, bool big_endian, bool map_object)
{
	if (!model || !out_csb_path)
		return ERR_INVALID_DATA;
	csb_t csb;
	enumError err = csb_from_model (&csb, model, big_endian, map_object);
	if (err)
		return err;
	u8 *raw = 0;
	u32 raw_size = 0;
	err = SaveCSB (&raw, &raw_size, &csb, big_endian);
	if (!err)
		err = csb_write_file (out_csb_path, raw, raw_size);
	FREE (raw);
	// search table sidecar (combined mode with geometry only)
	if (!err && !map_object && csb.n_models && csb.models[0].n_triangles)
	{
		ctb_t ctb;
		memset (&ctb, 0, sizeof (ctb));
		if (!GenerateCTB (&ctb, &csb))
		{
			u8 *ctb_raw = 0;
			u32 ctb_size = 0;
			if (!SaveCTB (&ctb_raw, &ctb_size, &ctb, big_endian))
			{
				char sidecar[PATH_MAX];
				csb_sidecar_path (sidecar, sizeof (sidecar), out_csb_path);
				err = csb_write_file (sidecar, ctb_raw, ctb_size);
			}
			FREE (ctb_raw);
		}
		FreeCTB (&ctb);
	}
	FreeCSB (&csb);
	return err;
}

//-----------------------------------------------------------------------------
//--- text dumps --------------------------------------------------------------
//-----------------------------------------------------------------------------

enumError DumpCSB (FILE *f, const csb_t *csb)
{
	if (!f || !csb)
		return ERR_INVALID_DATA;
	fprintf (f, "CSB collision scene (%s-endian): %u object(s), %u node(s), %u model(s)\n",
		csb->big_endian ? "big" : "little", csb->n_objects, csb->n_nodes, csb->n_models);
	for (u32 i = 0; i < csb->n_objects; i++)
	{
		const csb_object_t *o = csb->objects + i;
		if (o->is_sphere)
			fprintf (f, "  object %u: sphere '%s' center (%g,%g,%g) radius %g flag 0x%llx node %u\n",
				i, o->name, o->p1.x, o->p1.y, o->p1.z, o->radius,
				(unsigned long long)o->colflag, o->node_index);
		else
			fprintf (f, "  object %u: box '%s' p1 (%g,%g,%g) size (%g,%g,%g) flag 0x%llx node %u\n",
				i, o->name, o->p1.x, o->p1.y, o->p1.z, o->size.x, o->size.y,
				o->size.z, (unsigned long long)o->colflag, o->node_index);
	}
	for (u32 i = 0; i < csb->n_models; i++)
	{
		const csb_model_t *m = csb->models + i;
		fprintf (f, "  model %u: '%s' %u vertices, %u triangles, bbox (%g,%g,%g)-(%g,%g,%g)\n",
			i, m->name, m->n_positions, m->n_triangles, m->bbox.min.x, m->bbox.min.y,
			m->bbox.min.z, m->bbox.max.x, m->bbox.max.y, m->bbox.max.z);
		for (u32 j = 0; j < m->n_meshes; j++)
		{
			const csb_mesh_t *mesh = m->meshes + j;
			fprintf (f, "    mesh %u: '%s' MAT%u FLAG%llu tris %u+%u vtx %u+%u node %d\n",
				j, mesh->name, mesh->mat_attr, (unsigned long long)mesh->colflag,
				mesh->tri_start, mesh->n_tris, mesh->vtx_start, mesh->n_vtx,
				mesh->node_index);
		}
		if (i)
			fprintf (f, "    split flags: MAT%u FLAG%llu node %u\n", m->mat_attr,
				(unsigned long long)m->colflag, m->node_index);
	}
	return ERR_OK;
}

enumError DumpCTB (FILE *f, const ctb_t *ctb)
{
	if (!f || !ctb)
		return ERR_INVALID_DATA;
	fprintf (f, "CTB collision table (%s-endian): %u node(s), root size %g at (%g,%g,%g)\n",
		ctb->big_endian ? "big" : "little", ctb->n_nodes, ctb->root_size,
		ctb->root_position.x, ctb->root_position.y, ctb->root_position.z);
	u32 show = ctb->n_nodes > 16 ? 16 : ctb->n_nodes;
	for (u32 i = 0; i < show; i++)
	{
		const ctb_node_t *n = ctb->nodes + i;
		fprintf (f, "  node %u: pos (%g,%g,%g) size %g id %u children 0x%02x tris %u\n", i,
			n->position.x, n->position.y, n->position.z, n->size, n->node_id,
			n->child_bits, n->n_triangles);
	}
	if (show < ctb->n_nodes)
		fprintf (f, "  ... (%u more nodes)\n", ctb->n_nodes - show);
	return ERR_OK;
}


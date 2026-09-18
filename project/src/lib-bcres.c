#include "lib-bcres.h"
#include "lib-brres-model.h"
#include "lib-nintendo.h"
#include "lib-image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

typedef struct
{
	const uint8_t *data;
	size_t size;
	size_t pos;
} bcres_stream_t;

static uint32_t read_u32 (bcres_stream_t *stream)
{
	if (stream->pos + 4 > stream->size)
		return 0;
	uint32_t val = stream->data[stream->pos] | (stream->data[stream->pos + 1] << 8)
		| (stream->data[stream->pos + 2] << 16) | (stream->data[stream->pos + 3] << 24);
	stream->pos += 4;
	return val;
}

static uint16_t read_u16 (bcres_stream_t *stream)
{
	if (stream->pos + 2 > stream->size)
		return 0;
	uint16_t val = stream->data[stream->pos] | (stream->data[stream->pos + 1] << 8);
	stream->pos += 2;
	return val;
}

static float read_f32 (bcres_stream_t *stream)
{
	uint32_t uval = read_u32 (stream);
	float fval;
	memcpy (&fval, &uval, sizeof (float));
	return fval;
}

static uint32_t get_rel_offset (bcres_stream_t *stream)
{
	uint32_t pos = (uint32_t)stream->pos;
	uint32_t offset = read_u32 (stream);
	if (offset != 0)
		offset += pos;
	return offset;
}

static void skip (bcres_stream_t *stream, size_t bytes)
{
	stream->pos += bytes;
}

static void seek_pos (bcres_stream_t *stream, size_t pos)
{
	stream->pos = pos;
}

// Reads a 4-byte magic into 'magic', bounds-checked. Returns false (and
// zero-fills 'magic') if the read would run past the buffer.
static bool read_magic (bcres_stream_t *stream, char magic[4])
{
	if (stream->pos + 4 > stream->size)
	{
		memset (magic, 0, 4);
		return false;
	}
	memcpy (magic, stream->data + stream->pos, 4);
	stream->pos += 4;
	return true;
}

//-----------------------------------------------------------------------------
// CGFX (BCRES) model geometry
//-----------------------------------------------------------------------------
//
// Unlike BCH, CGFX does NOT hide its geometry in PICA200 command lists: a
// shape points at a plain interleaved vertex buffer plus a list of attribute
// descriptors, and a face descriptor points at a plain index buffer. So this
// is a direct structure walk, not a command replay.
//
// Every pointer is a signed 32-bit offset relative to the location it is
// stored at, and is already resolved in the file (no relocation table).
//
// Layouts follow SPICA's CtrGfx readers; the offsets below were then each
// confirmed against a real file rather than assumed. Two checks did the most
// work: every mesh's Parent field must point back at the CMDL, and the
// attribute element sizes must sum to exactly the vertex stride the buffer
// declares.

#define CGFX_TC_MESH 0x01000000 // GfxMesh
#define CGFX_TC_SHAPE 0x10000001 // GfxShape
#define CGFX_TC_ATTRIBUTE 0x40000001 // GfxAttribute
#define CGFX_TC_INTERLEAVED 0x40000002 // GfxVertexBufferInterleaved
#define CGFX_TC_FIXED 0x80000000 // GfxVertexBufferFixed

// GfxGLDataType: plain OpenGL type tokens.
#define GL_BYTE_ 0x1400
#define GL_UNSIGNED_BYTE_ 0x1401
#define GL_SHORT_ 0x1402
#define GL_UNSIGNED_SHORT_ 0x1403
#define GL_INT_ 0x1404
#define GL_UNSIGNED_INT_ 0x1405
#define GL_FLOAT_ 0x1406

// PICAAttributeName (SPICA PICA/Commands/PICAAttributeName.cs). The decoder
// previously only knew Position(0)/Normal(1)/TexCoord0(4); BcmdlImporter's
// previously only knew Position(0)/Normal(1)/TexCoord0(4); BcmdlImporter's
// VerticesConverter handles the full set, so the remaining renderable
// attributes are added here: Tangent(2), Color(3), TexCoord1(5),
// TexCoord2(6), BoneIndex(7), BoneWeight(8).
#define CGFX_ATTR_POSITION 0
#define CGFX_ATTR_NORMAL 1
#define CGFX_ATTR_TANGENT 2
#define CGFX_ATTR_COLOR 3
#define CGFX_ATTR_TEXCOORD0 4
#define CGFX_ATTR_TEXCOORD1 5
#define CGFX_ATTR_TEXCOORD2 6
#define CGFX_ATTR_BONEINDEX 7
#define CGFX_ATTR_BONEWEIGHT 8

typedef struct cg_t
{
	const uint8_t *d;
	size_t size;
} cg_t;

static bool cg_ok (const cg_t *g, size_t off, size_t len)
{
	return off < g->size && len <= g->size - off;
}

static uint32_t cg_u32 (const cg_t *g, size_t o)
{
	if (!cg_ok (g, o, 4))
		return 0;
	return (uint32_t)g->d[o] | (uint32_t)g->d[o + 1] << 8 | (uint32_t)g->d[o + 2] << 16
		| (uint32_t)g->d[o + 3] << 24;
}

static int32_t cg_s32 (const cg_t *g, size_t o)
{
	return (int32_t)cg_u32 (g, o);
}

static float cg_f32 (const cg_t *g, size_t o)
{
	const uint32_t v = cg_u32 (g, o);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

// A self-relative pointer. 0 means null, not "offset 0".
static size_t cg_ptr (const cg_t *g, size_t o)
{
	const int32_t v = cg_s32 (g, o);
	if (!v)
		return 0;
	const int64_t t = (int64_t)o + v;
	return t > 0 && (uint64_t)t < g->size ? (size_t)t : 0;
}

static unsigned cg_gl_size (uint32_t fmt)
{
	switch (fmt)
	{
		case GL_BYTE_:
		case GL_UNSIGNED_BYTE_:
			return 1;
		case GL_SHORT_:
		case GL_UNSIGNED_SHORT_:
			return 2;
		case GL_INT_:
		case GL_UNSIGNED_INT_:
		case GL_FLOAT_:
			return 4;
	}
	return 0;
}

// Read element IDX of an attribute stored at P in format FMT.
static float cg_read (const cg_t *g, size_t p, uint32_t fmt, unsigned idx)
{
	const unsigned sz = cg_gl_size (fmt);
	const size_t o = p + (size_t)idx * sz;
	if (!cg_ok (g, o, sz))
		return 0;
	switch (fmt)
	{
		case GL_BYTE_:
			return (float)(int8_t)g->d[o];
		case GL_UNSIGNED_BYTE_:
			return (float)g->d[o];
		case GL_SHORT_:
			return (float)(int16_t)((uint16_t)g->d[o] | (uint16_t)g->d[o + 1] << 8);
		case GL_UNSIGNED_SHORT_:
			return (float)(uint16_t)((uint16_t)g->d[o] | (uint16_t)g->d[o + 1] << 8);
		case GL_INT_:
			return (float)cg_s32 (g, o);
		case GL_UNSIGNED_INT_:
			return (float)cg_u32 (g, o);
		case GL_FLOAT_:
			return cg_f32 (g, o);
	}
	return 0;
}

typedef struct cg_attr_t
{
	uint32_t name, fmt;
	int elements, offset;
	float scale;
} cg_attr_t;

// Skinning accumulator (BcmdlImporter GenerateSubMeshes keeps a per-submesh
// bone palette and remaps global bone ids to local ones; here the direction
// is reversed: per-vertex local palette indices resolve to global bone ids,
// and each unique (bones, weights) combination becomes one
// node_influence_t entry indexed by mesh_t::position_node).
static int cg_influence_add (node_influence_t **inf, size_t *n, size_t *cap,
	const int *bones, const float *weights, int count)
{
	if (!inf || !n || !cap || !bones || !weights || count < 1 || count > 4)
		return -1;
	for (size_t i = 0; i < *n; i++)
	{
		node_influence_t *e = &(*inf)[i];
		if (e->num_weights != (size_t)count)
			continue;
		bool same = true;
		for (int j = 0; j < count; j++)
		{
			float d = e->weights[j].weight - weights[j];
			if (e->weights[j].bone_idx != bones[j] || (d < -1e-6f || d > 1e-6f))
			{
				same = false;
				break;
			}
		}
		if (same)
			return (int)i;
	}
	if (*n >= 16384)
		return -1;
	if (*n >= *cap)
	{
		size_t ncap = *cap ? *cap * 2 : 64;
		node_influence_t *ninf = REALLOC (*inf, ncap * sizeof (node_influence_t));
		if (!ninf)
			return -1;
		*inf = ninf;
		*cap = ncap;
	}
	node_influence_t *e = &(*inf)[*n];
	e->weights = CALLOC ((size_t)count, sizeof (influence_t));
	if (!e->weights)
		return -1;
	e->num_weights = (size_t)count;
	for (int j = 0; j < count; j++)
	{
		e->weights[j].bone_idx = bones[j];
		e->weights[j].weight = weights[j];
	}
	return (int)(*n)++;
}

model_t *ParseBCRES (const uint8_t *data, size_t size)
{
	if (!data || size < 0x14 || memcmp (data, "CGFX", 4))
		return NULL;
	const cg_t gg = { data, size }, *g = &gg;

	const uint32_t header_len = (uint32_t)data[6] | (uint32_t)data[7] << 8;
	if (!cg_ok (g, header_len, 0x10) || memcmp (data + header_len, "DATA", 4))
		return NULL;

	// DATA section: pairs of (count, self-relative pointer to a dict), models
	// first. Only the model dict is needed here.
	const size_t dsec = header_len;
	if (!cg_u32 (g, dsec + 8))
		return NULL; // no models
	const size_t mdict = cg_ptr (g, dsec + 0x0c);
	if (!mdict || memcmp (data + mdict, "DICT", 4))
		return NULL;

	// Dict: magic, length, count, then a root node, then one 0x10-byte entry
	// per item ending in (name ptr, data ptr). Take the first model.
	if (!cg_u32 (g, mdict + 8))
		return NULL;
	const size_t ent0 = mdict + 0x0c + 0x10; // past root node
	const size_t cmdl = cg_ptr (g, ent0 + 0x0c);
	if (!cmdl || memcmp (data + cmdl + 4, "CMDL", 4))
		return NULL;

	// CMDL: GfxNode header, then a transform of 9 floats followed by TWO 3x4
	// matrices (12 floats each, not 4x4) -- that is what puts the mesh count
	// at +0xb4. Verified on a real file: those 33 floats read as scale(1,1,1),
	// rotation(0,0,0), translation(0,0,0) and two identity 3x4 matrices.
	const uint32_t n_mesh = cg_u32 (g, cmdl + 0xb4);
	const size_t p_mesh = cg_ptr (g, cmdl + 0xb8);
	const uint32_t n_shape = cg_u32 (g, cmdl + 0xc4);
	const size_t p_shape = cg_ptr (g, cmdl + 0xc8);
	if (!n_mesh || !p_mesh || !n_shape || !p_shape || n_mesh > 0x10000 || n_shape > 0x10000)
		return NULL;

	model_t *out = CALLOC (1, sizeof (model_t));
	if (!out)
		return NULL;

	out->meshes = CALLOC (n_mesh, sizeof (mesh_t));
	if (!out->meshes)
	{
		FREE (out);
		return NULL;
	}

	// Global skinning palette shared by all meshes of all models in this
	// container (see cg_influence_add). Stays empty for files without any
	// bone bindings, preserving the previous unskinned behaviour.
	node_influence_t *node_inf = NULL;
	size_t n_node_inf = 0, cap_node_inf = 0;

	const uint32_t n_mat = cg_u32 (g, cmdl + 0xbc);
	const size_t p_mat = cg_ptr (g, cmdl + 0xc0);
	if (n_mat && p_mat && cg_ok (g, p_mat, 0x10) && !memcmp (data + p_mat, "DICT", 4))
	{
		const uint32_t mat_dict_count = cg_u32 (g, p_mat + 8);
		if (mat_dict_count && mat_dict_count <= 0x1000)
		{
			out->materials = CALLOC (mat_dict_count, sizeof (material_t));
			if (out->materials)
			{
				out->num_materials = mat_dict_count;
				for (uint32_t mi = 0; mi < mat_dict_count; mi++)
				{
					const size_t me = p_mat + 0x0c + (size_t)(mi + 1) * 16;
					if (!cg_ok (g, me, 16))
						break;
					const size_t name_ptr = cg_ptr (g, me + 8);
					if (name_ptr && cg_ok (g, name_ptr, 1))
						snprintf (out->materials[mi].name, sizeof (out->materials[mi].name), "%s",
							(const char *)(data + name_ptr));
					else
						snprintf (out->materials[mi].name, sizeof (out->materials[mi].name),
							"mat_%u", mi);

					const size_t mtob = cg_ptr (g, me + 12);
					if (mtob && cg_ok (g, mtob, 0x20) && !memcmp (data + mtob + 4, "MTOB", 4))
					{
						if (cg_ok (g, mtob, 0x50))
						{
							out->materials[mi].ambient[0] = cg_f32 (g, mtob + 0x24);
							out->materials[mi].ambient[1] = cg_f32 (g, mtob + 0x28);
							out->materials[mi].ambient[2] = cg_f32 (g, mtob + 0x2c);
							out->materials[mi].diffuse[0] = cg_f32 (g, mtob + 0x30);
							out->materials[mi].diffuse[1] = cg_f32 (g, mtob + 0x34);
							out->materials[mi].diffuse[2] = cg_f32 (g, mtob + 0x38);
							out->materials[mi].diffuse[3] = cg_f32 (g, mtob + 0x3c);
							out->materials[mi].specular[0] = cg_f32 (g, mtob + 0x40);
							out->materials[mi].specular[1] = cg_f32 (g, mtob + 0x44);
							out->materials[mi].specular[2] = cg_f32 (g, mtob + 0x48);
						}

						// Scan for TXOB samplers inside MTOB
						const size_t scan_end = (mtob + 0x600 < size) ? (mtob + 0x600) : size;
						for (size_t off = mtob; off + 0x20 <= scan_end; off += 4)
						{
							if (cg_u32 (g, off) == 0x20000004
								&& !memcmp (data + off + 4, "TXOB", 4))
							{
								const size_t tex_ptr = cg_ptr (g, off + 0x18);
								if (tex_ptr && cg_ok (g, tex_ptr, 1) && data[tex_ptr])
								{
									const int cur_tex = out->materials[mi].num_textures;
									if (cur_tex < 8)
									{
										snprintf (out->materials[mi].textures[cur_tex],
											sizeof (out->materials[mi].textures[cur_tex]), "%s",
											(const char *)(data + tex_ptr));
										out->materials[mi].wrap_s[cur_tex] = 1;
										out->materials[mi].wrap_t[cur_tex] = 1;
										out->materials[mi].min_filter[cur_tex] = 1;
										out->materials[mi].mag_filter[cur_tex] = 1;
										out->materials[mi].num_textures++;
									}
								}
							}
						}
					}
				}
			}
		}
	}

	for (uint32_t mi = 0; mi < n_mesh; mi++)
	{
		const size_t me = cg_ptr (g, p_mesh + 4 * mi);
		if (!me || cg_u32 (g, me) != CGFX_TC_MESH)
			continue;

		// The Parent back-pointer must lead to this CMDL. This is the check
		// that pins the whole GfxMesh layout down.
		if (cg_ptr (g, me + 0x20) != cmdl)
			continue;

		const int32_t si = cg_s32 (g, me + 0x18);
		if (si < 0 || (uint32_t)si >= n_shape)
			continue;
		const size_t sh = cg_ptr (g, p_shape + 4 * si);
		if (!sh || cg_u32 (g, sh) != CGFX_TC_SHAPE)
			continue;

		const uint32_t n_sub = cg_u32 (g, sh + 0x2c);
		const size_t p_sub = cg_ptr (g, sh + 0x30);
		const uint32_t n_vb = cg_u32 (g, sh + 0x38);
		const size_t p_vb = cg_ptr (g, sh + 0x3c);
		if (!n_sub || !p_sub || !n_vb || !p_vb || n_sub > 0x10000 || n_vb > 0x100)
			continue;

		// Find the interleaved vertex buffer and its attributes. Fixed buffers
		// (CGFX_TC_FIXED) hold one constant value for the whole shape and
		// carry no per-vertex data, so they contribute nothing here.
		size_t vraw = 0, vstride = 0, n_vert = 0;
		cg_attr_t attrs[16];
		unsigned n_attrs = 0;
		for (uint32_t i = 0; i < n_vb; i++)
		{
			const size_t vb = cg_ptr (g, p_vb + 4 * i);
			if (!vb || cg_u32 (g, vb) != CGFX_TC_INTERLEAVED)
				continue;

			const uint32_t rawlen = cg_u32 (g, vb + 0x14);
			vraw = cg_ptr (g, vb + 0x18);
			vstride = (size_t)cg_s32 (g, vb + 0x24);
			if (!vraw || !vstride || vstride > 0x400)
			{
				vraw = 0;
				break;
			}
			if (!cg_ok (g, vraw, rawlen))
			{
				vraw = 0;
				break;
			}
			n_vert = rawlen / vstride;

			const uint32_t na = cg_u32 (g, vb + 0x28);
			const size_t pa = cg_ptr (g, vb + 0x2c);
			for (uint32_t k = 0; k < na && n_attrs < 16 && pa; k++)
			{
				const size_t a = cg_ptr (g, pa + 4 * k);
				if (!a || cg_u32 (g, a) != CGFX_TC_ATTRIBUTE)
					continue;
				cg_attr_t *at = attrs + n_attrs;
				at->name = cg_u32 (g, a + 0x04);
				at->fmt = cg_u32 (g, a + 0x24);
				at->elements = cg_s32 (g, a + 0x28);
				at->scale = cg_f32 (g, a + 0x2c);
				at->offset = cg_s32 (g, a + 0x30);
				if (!cg_gl_size (at->fmt) || at->elements < 1 || at->elements > 4 || at->offset < 0
					|| (size_t)at->offset >= vstride)
					continue;
				n_attrs++;
			}
			break;
		}
		if (!vraw || !n_attrs || !n_vert)
			continue;

		// Sanity gate: the declared attributes must fit within the declared
		// stride. They need not sum to it exactly: retail buffers pad the
		// stride (e.g. a 37-byte pos/nrm/uv0/color/boneindex layout in a
		// 40-byte stride). A layout misread still shows up here as an
		// overrun or an attribute ending past the stride.
		size_t asum = 0;
		bool attrs_fit = true;
		for (unsigned i = 0; i < n_attrs; i++)
		{
			const size_t asz = (size_t)cg_gl_size (attrs[i].fmt) * attrs[i].elements;
			asum += asz;
			if ((size_t)attrs[i].offset + asz > vstride)
				attrs_fit = false;
		}
		if (!attrs_fit || asum > vstride)
			continue;

		// Count indices across every face descriptor of every submesh first,
		// so the output arrays are sized once.
		size_t total_idx = 0;
		for (uint32_t s = 0; s < n_sub; s++)
		{
			const size_t sub = cg_ptr (g, p_sub + 4 * s);
			if (!sub)
				continue;
			const uint32_t nf = cg_u32 (g, sub + 0x0c);
			const size_t pf = cg_ptr (g, sub + 0x10);
			for (uint32_t f = 0; f < nf && pf; f++)
			{
				const size_t face = cg_ptr (g, pf + 4 * f);
				if (!face)
					continue;
				const uint32_t nfd = cg_u32 (g, face);
				const size_t pfd = cg_ptr (g, face + 4);
				for (uint32_t k = 0; k < nfd && pfd; k++)
				{
					const size_t fd = cg_ptr (g, pfd + 4 * k);
					if (!fd)
						continue;
					const uint32_t ilen = cg_u32 (g, fd + 0x08);
					total_idx += cg_u32 (g, fd) == GL_UNSIGNED_SHORT_ ? ilen / 2 : ilen;
				}
			}
		}
		if (!total_idx || total_idx > 0x1000000)
			continue;

		mesh_t *mesh = out->meshes + out->num_meshes;
		snprintf (mesh->name, sizeof (mesh->name), "mesh%u", mi);
		const int32_t mat_id = cg_s32 (g, me + 0x1c);
		mesh->material_idx = (mat_id >= 0 && (size_t)mat_id < out->num_materials)
			? mat_id
			: (out->num_materials > 0 ? 0 : -1);
		mesh->positions = CALLOC (total_idx, sizeof (vec3_t));
		mesh->normals = CALLOC (total_idx, sizeof (vec3_t));
		mesh->texcoords = CALLOC (total_idx, sizeof (vec2_t));
		// BcmdlImporter (ModelTools.ConvertMesh / VerticesConverter) carries
		// tangent, vertex colour and up to three UV sets through the same
		// interleaved buffer; model_t already has room for them, so decode
		// them here instead of dropping everything past TEXCOORD0.
		mesh->tangents = CALLOC (total_idx, sizeof (vec3_t));
		mesh->colors[0] = CALLOC (total_idx, sizeof (color4_t));
		mesh->extra_texcoords[0] = CALLOC (total_idx, sizeof (vec2_t));
		mesh->extra_texcoords[1] = CALLOC (total_idx, sizeof (vec2_t));
		mesh->vertices = CALLOC (total_idx, sizeof (vertex_t));
		if (!mesh->positions || !mesh->normals || !mesh->texcoords || !mesh->vertices
			|| !mesh->tangents || !mesh->colors[0] || !mesh->extra_texcoords[0]
			|| !mesh->extra_texcoords[1])
		{
			FREE (mesh->positions);
			FREE (mesh->normals);
			FREE (mesh->texcoords);
			FREE (mesh->tangents);
			FREE (mesh->colors[0]);
			mesh->colors[0] = NULL;
			FREE (mesh->extra_texcoords[0]);
			FREE (mesh->extra_texcoords[1]);
			FREE (mesh->vertices);
			memset (mesh, 0, sizeof (*mesh));
			continue;
		}
		for (size_t zi = 0; zi < total_idx; zi++)
		{
			mesh->vertices[zi].tangent_idx = -1;
			mesh->vertices[zi].color_idx[0] = -1;
			mesh->vertices[zi].color_idx[1] = -1;
			mesh->vertices[zi].extra_texcoord_idx[0] = -1;
			mesh->vertices[zi].extra_texcoord_idx[1] = -1;
			mesh->vertices[zi].extra_texcoord_idx[2] = -1;
			mesh->vertices[zi].extra_texcoord_idx[3] = -1;
			mesh->vertices[zi].extra_texcoord_idx[4] = -1;
			mesh->vertices[zi].extra_texcoord_idx[5] = -1;
			mesh->vertices[zi].extra_texcoord_idx[6] = -1;
		}

		bool has_nrm = false, has_uv = false;
		bool has_tan = false, has_col = false, has_uv1 = false, has_uv2 = false;
		for (unsigned a = 0; a < n_attrs; a++)
		{
			if (attrs[a].name == CGFX_ATTR_NORMAL)
				has_nrm = true;
			else if (attrs[a].name == CGFX_ATTR_TEXCOORD0)
				has_uv = true;
			else if (attrs[a].name == CGFX_ATTR_TANGENT)
				has_tan = true;
			else if (attrs[a].name == CGFX_ATTR_COLOR)
				has_col = true;
			else if (attrs[a].name == CGFX_ATTR_TEXCOORD1)
				has_uv1 = true;
			else if (attrs[a].name == CGFX_ATTR_TEXCOORD2)
				has_uv2 = true;
		}

		// Per-vertex skinning sources, if present. BoneIndex carries up to 4
		// local palette indices, BoneWeight the matching weights; a missing
		// weight stream means a rigid (weight 1) bind, matching SPICA's
		// VerticesConverter fallback.
		int bi_attr = -1, bw_attr = -1;
		for (unsigned a = 0; a < n_attrs; a++)
		{
			if (attrs[a].name == CGFX_ATTR_BONEINDEX && bi_attr < 0)
				bi_attr = (int)a;
			else if (attrs[a].name == CGFX_ATTR_BONEWEIGHT && bw_attr < 0)
				bw_attr = (int)a;
		}

		mesh->position_node = CALLOC (total_idx, sizeof (int));
		if (!mesh->position_node)
		{
			FREE (mesh->positions);
			FREE (mesh->normals);
			FREE (mesh->texcoords);
			FREE (mesh->tangents);
			FREE (mesh->colors[0]);
			mesh->colors[0] = NULL;
			FREE (mesh->extra_texcoords[0]);
			FREE (mesh->extra_texcoords[1]);
			FREE (mesh->vertices);
			memset (mesh, 0, sizeof (*mesh));
			continue;
		}
		for (size_t zi = 0; zi < total_idx; zi++)
			mesh->position_node[zi] = -1;

		size_t n = 0;
		for (uint32_t s = 0; s < n_sub; s++)
		{
			const size_t sub = cg_ptr (g, p_sub + 4 * s);
			if (!sub)
				continue;
			// Submesh bone palette: +0x00 entry count, +0x04 self-relative
			// table of u32 global bone ids, +0x08 skinning kind (0=None,
			// 1=Rigid, 2=Smooth, matching SPICA GfxSubMeshSkinning).
			// Verified against a retail CGFX whose three submeshes carry
			// [6,7]/[6]/[3,6,5,4] palettes under an 8-bone SOBJ.
			uint32_t sub_bones[64];
			unsigned n_sub_bones = 0;
			{
				const uint32_t bc = cg_u32 (g, sub);
				const size_t bptr = cg_ptr (g, sub + 4);
				if (bc && bc <= 64 && bptr && cg_ok (g, bptr, (size_t)bc * 4))
				{
					for (uint32_t t = 0; t < bc; t++)
						sub_bones[t] = cg_u32 (g, bptr + t * 4);
					n_sub_bones = bc;
				}
			}
			const uint32_t nf = cg_u32 (g, sub + 0x0c);
			const size_t pf = cg_ptr (g, sub + 0x10);
			for (uint32_t f = 0; f < nf && pf; f++)
			{
				const size_t face = cg_ptr (g, pf + 4 * f);
				if (!face)
					continue;
				const uint32_t nfd = cg_u32 (g, face);
				const size_t pfd = cg_ptr (g, face + 4);
				for (uint32_t k = 0; k < nfd && pfd; k++)
				{
					const size_t fd = cg_ptr (g, pfd + 4 * k);
					if (!fd)
						continue;
					const uint32_t ifmt = cg_u32 (g, fd);
					const uint32_t ilen = cg_u32 (g, fd + 0x08);
					const size_t iptr = cg_ptr (g, fd + 0x0c);
					if (!iptr || !cg_ok (g, iptr, ilen))
						continue;
					const bool is16 = ifmt == GL_UNSIGNED_SHORT_;
					const size_t cnt = is16 ? ilen / 2 : ilen;

					for (size_t x = 0; x < cnt && n < total_idx; x++)
					{
						const size_t vi = is16 ? (size_t)((uint16_t)data[iptr + x * 2]
													 | (uint16_t)data[iptr + x * 2 + 1] << 8)
											   : (size_t)data[iptr + x];
						if (vi >= n_vert)
							continue;
						const size_t vo = vraw + vi * vstride;

						for (unsigned a = 0; a < n_attrs; a++)
						{
							const size_t p = vo + attrs[a].offset;
							const int el = attrs[a].elements;
							const float sc = attrs[a].scale != 0.0f ? attrs[a].scale : 1.0f;
							if (attrs[a].name == CGFX_ATTR_POSITION)
							{
								mesh->positions[n].x = cg_read (g, p, attrs[a].fmt, 0) * sc;
								mesh->positions[n].y
									= el > 1 ? cg_read (g, p, attrs[a].fmt, 1) * sc : 0;
								mesh->positions[n].z
									= el > 2 ? cg_read (g, p, attrs[a].fmt, 2) * sc : 0;
							}
							else if (attrs[a].name == CGFX_ATTR_NORMAL)
							{
								mesh->normals[n].x = cg_read (g, p, attrs[a].fmt, 0) * sc;
								mesh->normals[n].y
									= el > 1 ? cg_read (g, p, attrs[a].fmt, 1) * sc : 0;
								mesh->normals[n].z
									= el > 2 ? cg_read (g, p, attrs[a].fmt, 2) * sc : 0;
							}
							else if (attrs[a].name == CGFX_ATTR_TEXCOORD0)
							{
								mesh->texcoords[n].u = cg_read (g, p, attrs[a].fmt, 0) * sc;
								mesh->texcoords[n].v
									= el > 1 ? cg_read (g, p, attrs[a].fmt, 1) * sc : 0;
							}
							else if (attrs[a].name == CGFX_ATTR_TANGENT)
							{
								mesh->tangents[n].x = cg_read (g, p, attrs[a].fmt, 0) * sc;
								mesh->tangents[n].y
									= el > 1 ? cg_read (g, p, attrs[a].fmt, 1) * sc : 0;
								mesh->tangents[n].z
									= el > 2 ? cg_read (g, p, attrs[a].fmt, 2) * sc : 0;
							}
							else if (attrs[a].name == CGFX_ATTR_COLOR)
							{
								mesh->colors[0][n].r = cg_read (g, p, attrs[a].fmt, 0) * sc;
								mesh->colors[0][n].g
									= el > 1 ? cg_read (g, p, attrs[a].fmt, 1) * sc : 0;
								mesh->colors[0][n].b
									= el > 2 ? cg_read (g, p, attrs[a].fmt, 2) * sc : 0;
								mesh->colors[0][n].a
									= el > 3 ? cg_read (g, p, attrs[a].fmt, 3) * sc : 1.0f;
							}
							else if (attrs[a].name == CGFX_ATTR_TEXCOORD1)
							{
								mesh->extra_texcoords[0][n].u
									= cg_read (g, p, attrs[a].fmt, 0) * sc;
								mesh->extra_texcoords[0][n].v
									= el > 1 ? cg_read (g, p, attrs[a].fmt, 1) * sc : 0;
							}
							else if (attrs[a].name == CGFX_ATTR_TEXCOORD2)
							{
								mesh->extra_texcoords[1][n].u
									= cg_read (g, p, attrs[a].fmt, 0) * sc;
								mesh->extra_texcoords[1][n].v
									= el > 1 ? cg_read (g, p, attrs[a].fmt, 1) * sc : 0;
							}
						}
						// Resolve skinning for this vertex: local palette
						// indices from the BoneIndex stream map through the
						// submesh's global bone table; a missing weight
						// stream means a rigid (weight 1) bind.
						{
							int combo_bones[4] = { -1, -1, -1, -1 };
							float combo_weights[4] = { 0, 0, 0, 0 };
							int combo_n = 0;
							if (bi_attr >= 0)
							{
								const cg_attr_t *ba = &attrs[(unsigned)bi_attr];
								int el = ba->elements;
								if (el > 4)
									el = 4;
								const size_t bpos = vo + (size_t)ba->offset;
								const float bsc = ba->scale != 0.0f ? ba->scale : 1.0f;
								const cg_attr_t *wa
									= bw_attr >= 0 ? &attrs[(unsigned)bw_attr] : NULL;
								int wel = wa ? wa->elements : 0;
								if (wel > 4)
									wel = 4;
								for (int j = 0; j < el && combo_n < 4; j++)
								{
									const int local
										= (int)(cg_read (g, bpos, ba->fmt, j) * bsc);
									int global = -1;
									if (local >= 0 && (unsigned)local < n_sub_bones)
										global = (int)sub_bones[(unsigned)local];
									else if (n_sub_bones)
										global = (int)sub_bones[0];
									if (global < 0)
										continue;
									float wgt = 0.0f;
									if (wa && j < wel)
									{
										const float wsc = wa->scale != 0.0f
											? wa->scale
											: 1.0f;
										wgt = cg_read (g, vo + (size_t)wa->offset, wa->fmt, j)
											* wsc;
									}
									else if (j == 0)
										wgt = 1.0f;
									if (wgt <= 0.0f)
										continue;
									combo_bones[combo_n] = global;
									combo_weights[combo_n] = wgt;
									combo_n++;
								}
								if (combo_n > 1)
								{
									float sum = 0.0f;
									for (int j = 0; j < combo_n; j++)
										sum += combo_weights[j];
									if (sum > 0.0f)
										for (int j = 0; j < combo_n; j++)
											combo_weights[j] /= sum;
								}
							}
							else if (n_sub_bones)
							{
								// No per-vertex stream: rigid bind to the
								// submesh's first palette entry (BcmdlImporter
								// binds unbound meshes to a bone the same way
								// so the model stays movable as a whole).
								combo_bones[0] = (int)sub_bones[0];
								combo_weights[0] = 1.0f;
								combo_n = 1;
							}
							if (combo_n > 0)
							{
								const int ii = cg_influence_add (&node_inf, &n_node_inf,
									&cap_node_inf, combo_bones, combo_weights, combo_n);
								if (ii >= 0)
									mesh->position_node[n] = ii;
							}
						}
						mesh->vertices[n].position_idx = (int)n;
						mesh->vertices[n].normal_idx = has_nrm ? (int)n : -1;
						mesh->vertices[n].texcoord_idx = has_uv ? (int)n : -1;
						mesh->vertices[n].tangent_idx = has_tan ? (int)n : -1;
						mesh->vertices[n].color_idx[0] = has_col ? (int)n : -1;
						mesh->vertices[n].extra_texcoord_idx[0] = has_uv1 ? (int)n : -1;
						mesh->vertices[n].extra_texcoord_idx[1] = has_uv2 ? (int)n : -1;
						n++;
					}
				}
			}
		}

		if (!n)
		{
			FREE (mesh->positions);
			FREE (mesh->normals);
			FREE (mesh->texcoords);
			FREE (mesh->tangents);
			FREE (mesh->colors[0]);
			mesh->colors[0] = NULL;
			FREE (mesh->extra_texcoords[0]);
			FREE (mesh->extra_texcoords[1]);
			FREE (mesh->position_node);
			FREE (mesh->vertices);
			memset (mesh, 0, sizeof (*mesh));
			continue;
		}
		mesh->num_positions = n;
		if (has_nrm)
			mesh->num_normals = n;
		else
		{
			FREE (mesh->normals);
			mesh->normals = NULL;
			mesh->num_normals = 0;
		}
		if (has_uv)
			mesh->num_texcoords = n;
		else
		{
			FREE (mesh->texcoords);
			mesh->texcoords = NULL;
			mesh->num_texcoords = 0;
		}
		if (has_tan)
			mesh->num_tangents = n;
		else
		{
			FREE (mesh->tangents);
			mesh->tangents = NULL;
			mesh->num_tangents = 0;
		}
		if (has_col)
			mesh->num_colors[0] = n;
		else
		{
			FREE (mesh->colors[0]);
			mesh->colors[0] = NULL;
			mesh->num_colors[0] = 0;
		}
		if (has_uv1)
			mesh->num_extra_texcoords[0] = n;
		else
		{
			FREE (mesh->extra_texcoords[0]);
			mesh->extra_texcoords[0] = NULL;
			mesh->num_extra_texcoords[0] = 0;
		}
		if (has_uv2)
			mesh->num_extra_texcoords[1] = n;
		else
		{
			FREE (mesh->extra_texcoords[1]);
			mesh->extra_texcoords[1] = NULL;
			mesh->num_extra_texcoords[1] = 0;
		}
		mesh->num_vertices = n;
		out->num_meshes++;
	}

	// Publish the accumulated skinning palette. With no bone bindings
	// anywhere the model stays unskinned exactly as before: per-mesh
	// position_node stubs are released instead of leaving -1 arrays behind.
	if (n_node_inf)
	{
		out->node_influences = node_inf;
		out->num_node_influences = n_node_inf;
	}
	else
	{
		for (size_t mi2 = 0; mi2 < out->num_meshes; mi2++)
		{
			FREE (out->meshes[mi2].position_node);
			out->meshes[mi2].position_node = NULL;
		}
		FREE (node_inf);
	}

	const size_t p_sobj = cg_ptr (g, cmdl + 0xe0);
	if (p_sobj && cg_ok (g, p_sobj, 0x2c) && !memcmp (data + p_sobj + 4, "SOBJ", 4))
	{
		const uint32_t n_bones = cg_u32 (g, p_sobj + 0x18);
		const size_t p_bdict = cg_ptr (g, p_sobj + 0x1c);
		if (n_bones && n_bones <= 0x1000 && p_bdict && cg_ok (g, p_bdict, 0x10)
			&& !memcmp (data + p_bdict, "DICT", 4))
		{
			out->joints = CALLOC (n_bones, sizeof (joint_t));
			if (out->joints)
			{
				out->num_joints = n_bones;
				for (uint32_t bi = 0; bi < n_bones; bi++)
				{
					const size_t bnode = p_bdict + 0x0c + (size_t)(bi + 1) * 16;
					if (!cg_ok (g, bnode, 16))
						break;
					const size_t bname_ptr = cg_ptr (g, bnode + 8);
					if (bname_ptr && cg_ok (g, bname_ptr, 1) && data[bname_ptr])
						snprintf (out->joints[bi].name, sizeof (out->joints[bi].name), "%s",
							(const char *)(data + bname_ptr));
					else
						snprintf (out->joints[bi].name, sizeof (out->joints[bi].name), "bone_%u", bi);

					const size_t bp = cg_ptr (g, bnode + 12);
					if (bp && cg_ok (g, bp, 0xd0))
					{
						out->joints[bi].parent_idx = cg_s32 (g, bp + 0x0c);
						out->joints[bi].scale.x = cg_f32 (g, bp + 0x20);
						out->joints[bi].scale.y = cg_f32 (g, bp + 0x24);
						out->joints[bi].scale.z = cg_f32 (g, bp + 0x28);
						if (out->joints[bi].scale.x == 0.0f && out->joints[bi].scale.y == 0.0f
							&& out->joints[bi].scale.z == 0.0f)
						{
							out->joints[bi].scale.x = 1.0f;
							out->joints[bi].scale.y = 1.0f;
							out->joints[bi].scale.z = 1.0f;
						}
						out->joints[bi].rotate.x = cg_f32 (g, bp + 0x2c) * (180.0f / (float)M_PI);
						out->joints[bi].rotate.y = cg_f32 (g, bp + 0x30) * (180.0f / (float)M_PI);
						out->joints[bi].rotate.z = cg_f32 (g, bp + 0x34) * (180.0f / (float)M_PI);
						out->joints[bi].translate.x = cg_f32 (g, bp + 0x38);
						out->joints[bi].translate.y = cg_f32 (g, bp + 0x3c);
						out->joints[bi].translate.z = cg_f32 (g, bp + 0x40);

						for (int m = 0; m < 12; m++)
							out->joints[bi].bind[m] = cg_f32 (g, bp + 0x74 + m * 4);
						for (int m = 0; m < 12; m++)
							out->joints[bi].inverse_bind[m] = cg_f32 (g, bp + 0xa4 + m * 4);
						out->joints[bi].has_inverse_bind = 1;
					}
				}
			}
		}
	}

	// No geometry is a failure, not an empty success: returning an empty
	// model_t would make the caller write a valid-looking but empty DAE.
	if (!out->num_meshes)
	{
		FreeModel (out);
		return NULL;
	}
	// The model abstraction intentionally covers only the renderable subset of
	// CGFX. Keep the original container too, so an untouched GLB can return to
	// its retail byte stream without discarding dictionaries, textures, and
	// other resources not represented by model_t.
	out->bcres_raw = MALLOC (size);
	if (out->bcres_raw)
	{
		memcpy (out->bcres_raw, data, size);
		out->bcres_raw_size = size;
	}
	return out;
}

//-----------------------------------------------------------------------------
// CGFX container enumeration
//-----------------------------------------------------------------------------

static const char *cgfx_dict_names[CGFX_N_DICTS] = { "Models", "Textures", "LUTs", "Materials",
	"Shaders", "Cameras", "Lights", "Fogs", "Scenes", "SkeletalAnimations", "MaterialAnimations",
	"VisibilityAnimations", "CameraAnimations", "LightAnimations", "FogAnimations", "Emitters" };

const char *GetCGFXDictName (int id)
{
	return id >= 0 && id < CGFX_N_DICTS ? cgfx_dict_names[id] : "?";
}

void ResetCGFX (cgfx_t *cgfx)
{
	if (!cgfx)
		return;
	for (int i = 0; i < CGFX_N_DICTS; i++)
		FREE (cgfx->dict[i].entries);
	memset (cgfx, 0, sizeof (*cgfx));
}

static uint32_t c_u32 (const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int32_t c_s32 (const uint8_t *p)
{
	return (int32_t)c_u32 (p);
}
static uint16_t c_u16 (const uint8_t *p)
{
	return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

int ScanCGFX (cgfx_t *cgfx, const uint8_t *data, size_t size)
{
	if (!cgfx || !data || size < 0x20 || memcmp (data, "CGFX", 4))
		return 0;
	memset (cgfx, 0, sizeof (*cgfx));
	cgfx->data = data;
	cgfx->size = size;
	cgfx->revision = c_u32 (data + 8);

	const uint16_t hdr_len = c_u16 (data + 6);
	if (hdr_len < 0x14 || (size_t)hdr_len + 8 > size)
		return 0;
	// The DATA block follows the header; its (count, dict offset) pairs start
	// right after the block's own magic and size.
	if (memcmp (data + hdr_len, "DATA", 4))
		return 0;
	const size_t base = (size_t)hdr_len + 8;

	for (int i = 0; i < CGFX_N_DICTS; i++)
	{
		const size_t o = base + (size_t)i * 8;
		if (o + 8 > size)
			break;
		const uint32_t count = c_u32 (data + o);
		if (!count || count > 0x10000)
			continue;
		const size_t dic = o + 4 + (size_t)c_s32 (data + o + 4);
		if (dic + 0x0c > size || memcmp (data + dic, "DICT", 4))
			continue;
		const uint32_t n = c_u32 (data + dic + 8);
		if (!n || n > 0x10000 || dic + 0x0c + (size_t)(n + 1) * 16 > size)
			continue;

		cgfx_entry_t *ent = CALLOC (n, sizeof (*ent));
		if (!ent)
			continue;
		unsigned got = 0;
		for (uint32_t k = 0; k < n; k++)
		{
			// Node: refBit(4) left(2) right(2) namePtr(4) dataPtr(4), all
			// offsets self-relative. Node 0 is the tree root.
			const size_t e = dic + 0x0c + (size_t)(k + 1) * 16;
			const size_t np = e + 8 + (size_t)c_s32 (data + e + 8);
			if (np >= size)
				continue;
			size_t q = np;
			while (q < size && data[q])
				q++;
			if (q >= size)
				continue;
			ent[got].name = (const char *)(data + np);
			ent[got].address = (uint32_t)(e + 12 + (size_t)c_s32 (data + e + 12));
			got++;
		}
		if (got)
		{
			cgfx->dict[i].entries = ent;
			cgfx->dict[i].n = got;
		}
		else
			FREE (ent);
	}
	return 1;
}

//-----------------------------------------------------------------------------
///////////////		CGFX / BCRES texture decoding and export	///////////////
//-----------------------------------------------------------------------------

enumError DecodeCGFXTexture (u8 **dest, uint *width, uint *height, const cgfx_t *cgfx, uint tex_idx)
{
	if (!dest || !width || !height || !cgfx || !cgfx->data
		|| tex_idx >= cgfx->dict[CGFX_DICT_TEXTURES].n)
		return EINVAL;

	const cg_t gg = { cgfx->data, cgfx->size }, *g = &gg;
	const uint32_t t_addr = cgfx->dict[CGFX_DICT_TEXTURES].entries[tex_idx].address;
	if (!t_addr || t_addr + 0x50 > cgfx->size)
		return EINVAL;

	if (memcmp (cgfx->data + t_addr + 4, "TXOB", 4) != 0)
		return EINVAL;

	const uint32_t h = cg_u32 (g, t_addr + 0x18);
	const uint32_t w = cg_u32 (g, t_addr + 0x1c);
	const uint32_t fmt = cg_u32 (g, t_addr + 0x34);
	uint32_t data_size = cg_u32 (g, t_addr + 0x44);
	const size_t data_ptr = cg_ptr (g, t_addr + 0x48);

	if (!w || !h || !data_ptr || data_ptr >= cgfx->size)
		return EINVAL;

	if (!data_size || data_ptr + data_size > cgfx->size)
		data_size = (uint32_t)(cgfx->size - data_ptr);

	const u8 *src = cgfx->data + data_ptr;
	return DecodePicaTexture (dest, width, height, src, w, h, fmt, data_size);
}

// PICA200 texture format ids (SPICA PICA/Commands/PICATextureFormat).
static const char *pica_format_names[14] = { "RGBA8", "RGB8", "RGBA5551", "RGB565",
	"RGBA4", "LA8", "HiLo8", "L8", "A8", "LA4", "L4", "A4", "ETC1", "ETC1A4" };

const char *GetPicaTextureFormatName (uint format)
{
	return format < 14 ? pica_format_names[format] : "UNKNOWN";
}

// TXOB layout facts shared with DecodeCGFXTexture above: the header carries
// the pixel size, hardware format id and payload size, so a texture can be
// described without decoding it. Mirrors BcmdlImporter TextureMeta
// (per-texture Format + dimensions), which its importer uses to re-encode
// edited PNGs back into the original hardware format.
enumError GetCGFXTextureMeta (cgfx_tex_meta_t *meta, const cgfx_t *cgfx, uint tex_idx)
{
	if (!meta || !cgfx || !cgfx->data || tex_idx >= cgfx->dict[CGFX_DICT_TEXTURES].n)
		return EINVAL;
	memset (meta, 0, sizeof (*meta));

	const cg_t gg = { cgfx->data, cgfx->size }, *g = &gg;
	const uint32_t t_addr = cgfx->dict[CGFX_DICT_TEXTURES].entries[tex_idx].address;
	if (!t_addr || t_addr + 0x50 > cgfx->size)
		return EINVAL;
	if (memcmp (cgfx->data + t_addr + 4, "TXOB", 4) != 0)
		return EINVAL;

	meta->width = cg_u32 (g, t_addr + 0x1c);
	meta->height = cg_u32 (g, t_addr + 0x18);
	meta->format = cg_u32 (g, t_addr + 0x34);
	uint32_t data_size = cg_u32 (g, t_addr + 0x44);
	const size_t data_ptr = cg_ptr (g, t_addr + 0x48);
	if (!meta->width || !meta->height || !data_ptr || data_ptr >= cgfx->size)
		return EINVAL;
	if (!data_size || data_ptr + data_size > cgfx->size)
		data_size = (uint32_t)(cgfx->size - data_ptr);
	meta->data_size = data_size;
	return ERR_OK;
}

static inline bool is_ext (ccp src, ccp ext)
{
	if (!src || !ext)
		return false;
	const size_t slen = strlen (src);
	const size_t elen = strlen (ext);
	return slen >= elen && !strcasecmp (src + slen - elen, ext);
}

enumError ExportBCRESTextures (const cgfx_t *cgfx, const char *dest_path_or_dir)
{
	if (!cgfx || !dest_path_or_dir || !cgfx->dict[CGFX_DICT_TEXTURES].n)
		return ERR_OK;

	char dir[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s", dest_path_or_dir);
	if (is_ext (dir, ".dae") || is_ext (dir, ".glb"))
	{
		char *slash = strrchr (dir, '/');
		if (slash)
			*slash = 0;
		else
			snprintf (dir, sizeof (dir), ".");
	}
	CreatePath (dir, true);

	enumError max_err = ERR_OK;
	for (uint i = 0; i < cgfx->dict[CGFX_DICT_TEXTURES].n; i++)
	{
		u8 *rgba = 0;
		uint w = 0, h = 0;
		enumError err = DecodeCGFXTexture (&rgba, &w, &h, cgfx, i);
		if (err || !rgba || !w || !h)
			continue;

		ccp name = cgfx->dict[CGFX_DICT_TEXTURES].entries[i].name;
		char clean_name[128];
		if (name && *name)
			snprintf (clean_name, sizeof (clean_name), "%s", name);
		else
			snprintf (clean_name, sizeof (clean_name), "tex_%03u", i);

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s.png", dir, clean_name);

		Image_t img;
		InitializeIMG (&img);
		const uint xw = EXPAND8 (w), xh = EXPAND8 (h);
		u8 *padded = xw == w && xh == h ? rgba : CALLOC (1, xw * xh * 4);
		if (padded != rgba)
		{
			for (uint y = 0; y < h; y++)
				memcpy (padded + (size_t)y * xw * 4, rgba + (size_t)y * w * 4, (size_t)w * 4);
			FREE (rgba);
		}
		img.data = padded;
		img.data_alloced = true;
		img.data_size = xw * xh * 4;
		img.width = w;
		img.xwidth = xw;
		img.height = h;
		img.xheight = xh;
		img.iform = img.info_iform = IMG_X_RGB;
		img.info_fform = FF_PNG;
		img.info_n_image = 1;
		img.endian = &be_func;

		err = SavePNG (&img, false, 0, out_path, 0, 0, true, 0);
		ResetIMG (&img);
		if (err && max_err < err)
			max_err = err;

		// TextureMeta sidecar (BcmdlImporter TextureMeta port): the PNG
		// alone loses the hardware format, so record it beside the image.
		// A future texture re-encoder consumes this to pick the original
		// PICA format instead of guessing; names are JSON-escaped.
		cgfx_tex_meta_t tm;
		if (!GetCGFXTextureMeta (&tm, cgfx, i))
		{
			char esc[256];
			size_t el = 0;
			for (const char *s = clean_name; *s && el + 2 < sizeof (esc); s++)
			{
				if (*s == '"' || *s == '\\')
					esc[el++] = '\\';
				if ((unsigned char)*s < 0x20)
					esc[el++] = '_';
				else
					esc[el++] = *s;
			}
			esc[el] = 0;
			char json[640];
			const int jl = snprintf (json, sizeof (json),
				"{\n  \"name\": \"%s\",\n  \"format\": %u,\n  \"format_name\": \"%s\",\n"
				"  \"width\": %u,\n  \"height\": %u,\n  \"data_size\": %u\n}\n",
				esc, tm.format, GetPicaTextureFormatName (tm.format),
				tm.width, tm.height, tm.data_size);
			if (jl > 0 && (size_t)jl < sizeof (json))
			{
				char json_path[PATH_MAX];
				snprintf (json_path, sizeof (json_path), "%s/%s.json", dir, clean_name);
				enumError jerr = SaveFILE (json_path, 0, true, json, (uint)jl, 0);
				if (jerr && max_err < jerr)
					max_err = jerr;
			}
		}
	}
	return max_err;
}

enumError ExportBCRESTexturesFromData (const u8 *data, size_t size, const char *dest_path_or_dir)
{
	if (!data || size < 0x20 || !dest_path_or_dir)
		return EINVAL;
	cgfx_t cgfx;
	if (!ScanCGFX (&cgfx, data, size))
		return EINVAL;
	enumError err = ExportBCRESTextures (&cgfx, dest_path_or_dir);
	ResetCGFX (&cgfx);
	return err;
}

//-----------------------------------------------------------------------------
///////////////			CGFX / BCRES encoding				   ///////////////
//-----------------------------------------------------------------------------

typedef struct
{
	uint8_t *data;
	size_t size;
	size_t cap;
} bcres_buf_t;

static void bc_buf_init (bcres_buf_t *b)
{
	b->cap = 8192;
	b->size = 0;
	b->data = CALLOC (1, b->cap);
}

static void bc_buf_free (bcres_buf_t *b)
{
	if (b->data)
		FREE (b->data);
	b->data = NULL;
	b->size = b->cap = 0;
}

static size_t bc_buf_reserve (bcres_buf_t *b, size_t len)
{
	if (b->size + len > b->cap)
	{
		while (b->size + len > b->cap)
			b->cap *= 2;
		b->data = REALLOC (b->data, b->cap);
		memset (b->data + b->size, 0, b->cap - b->size);
	}
	size_t pos = b->size;
	b->size += len;
	return pos;
}

static size_t bc_buf_align (bcres_buf_t *b, size_t alignment)
{
	size_t rem = b->size % alignment;
	if (rem != 0)
	{
		size_t pad = alignment - rem;
		bc_buf_reserve (b, pad);
	}
	return b->size;
}

static void bc_w16 (bcres_buf_t *b, size_t pos, uint16_t v)
{
	b->data[pos + 0] = (uint8_t)v;
	b->data[pos + 1] = (uint8_t)(v >> 8);
}

static void bc_w32 (bcres_buf_t *b, size_t pos, uint32_t v)
{
	b->data[pos + 0] = (uint8_t)v;
	b->data[pos + 1] = (uint8_t)(v >> 8);
	b->data[pos + 2] = (uint8_t)(v >> 16);
	b->data[pos + 3] = (uint8_t)(v >> 24);
}

static void bc_wf32 (bcres_buf_t *b, size_t pos, float v)
{
	uint32_t u;
	memcpy (&u, &v, sizeof (float));
	bc_w32 (b, pos, u);
}

static void bc_rel_ptr (bcres_buf_t *b, size_t pos, size_t target)
{
	if (target == 0)
		bc_w32 (b, pos, 0);
	else
	{
		int32_t rel = (int32_t)((int64_t)target - (int64_t)pos);
		bc_w32 (b, pos, (uint32_t)rel);
	}
}

typedef struct
{
	char *data;
	size_t size;
	size_t cap;
} bcres_strpool_t;

static void bc_strpool_init (bcres_strpool_t *p)
{
	p->cap = 1024;
	p->size = 0;
	p->data = CALLOC (1, p->cap);
}

static void bc_strpool_free (bcres_strpool_t *p)
{
	if (p->data)
		FREE (p->data);
	p->data = NULL;
	p->size = p->cap = 0;
}

static size_t bc_strpool_add (bcres_strpool_t *p, const char *str)
{
	if (!str || !*str)
		str = "default";
	size_t len = strlen (str);
	if (p->size > 0)
	{
		size_t pos = 0;
		while (pos < p->size)
		{
			if (!strcmp (p->data + pos, str))
				return pos;
			pos += strlen (p->data + pos) + 1;
		}
	}
	while (p->size + len + 1 > p->cap)
	{
		p->cap *= 2;
		p->data = REALLOC (p->data, p->cap);
	}
	size_t off = p->size;
	memcpy (p->data + off, str, len + 1);
	p->size += len + 1;
	return off;
}

typedef struct
{
	const char *name;
	uint32_t ref_bit;
	uint16_t left;
	uint16_t right;
	size_t name_pool_off;
	size_t data_off;
} bcres_patricia_node_t;

static bool bc_get_bit (const char *name, uint32_t bit)
{
	if (!name)
		return false;
	uint32_t pos = bit >> 3;
	uint32_t cbit = bit & 7;
	size_t len = strlen (name);
	if (pos < len)
		return ((name[pos] >> cbit) & 1) != 0;
	return false;
}

static uint16_t bc_patricia_traverse (
	const char *name, const bcres_patricia_node_t *nodes, uint16_t *out_root, uint32_t bit)
{
	uint16_t root_idx = 0;
	uint16_t out_idx = nodes[0].left;
	uint16_t left_idx = out_idx;

	while (nodes[root_idx].ref_bit > nodes[left_idx].ref_bit && nodes[left_idx].ref_bit > bit)
	{
		if (bc_get_bit (name, nodes[left_idx].ref_bit))
			out_idx = nodes[left_idx].right;
		else
			out_idx = nodes[left_idx].left;

		root_idx = left_idx;
		left_idx = out_idx;
	}

	if (out_root)
		*out_root = root_idx;
	return out_idx;
}

static void bc_build_patricia_tree (bcres_patricia_node_t *nodes, uint32_t count)
{
	if (!nodes || count == 0)
		return;
	nodes[0].ref_bit = 0xFFFFFFFF;
	nodes[0].left = count > 1 ? 1 : 0;
	nodes[0].right = 0;
	nodes[0].name = NULL;

	if (count <= 1)
		return;

	size_t max_len = 0;
	for (uint32_t i = 1; i < count; i++)
	{
		size_t l = strlen (nodes[i].name);
		if (l > max_len)
			max_len = l;
	}

	for (uint32_t i = 1; i < count; i++)
	{
		const char *name = nodes[i].name;
		uint32_t bit = (uint32_t)((max_len << 3) - 1);
		uint16_t root_dummy;
		uint16_t idx = bc_patricia_traverse (name, nodes, &root_dummy, 0);

		while (bc_get_bit (nodes[idx].name, bit) == bc_get_bit (name, bit))
		{
			if (bit == 0)
				break;
			bit--;
		}
		nodes[i].ref_bit = bit;

		if (bc_get_bit (name, bit))
		{
			nodes[i].left = bc_patricia_traverse (name, nodes, &root_dummy, bit);
			nodes[i].right = (uint16_t)i;
		}
		else
		{
			nodes[i].left = (uint16_t)i;
			nodes[i].right = bc_patricia_traverse (name, nodes, &root_dummy, bit);
		}

		uint16_t root_idx;
		bc_patricia_traverse (name, nodes, &root_idx, bit);
		if (bc_get_bit (name, nodes[root_idx].ref_bit))
			nodes[root_idx].right = (uint16_t)i;
		else
			nodes[root_idx].left = (uint16_t)i;
	}
}

static void bc_write_dict (bcres_buf_t *b, size_t dict_pos, const bcres_patricia_node_t *nodes,
	uint32_t count, size_t strtab_base)
{
	memcpy (b->data + dict_pos, "DICT", 4);
	uint32_t tree_len = count * 16 + 12;
	bc_w32 (b, dict_pos + 4, tree_len);
	bc_w32 (b, dict_pos + 8, count > 1 ? count - 1 : 0);

	for (uint32_t i = 0; i < count; i++)
	{
		size_t np = dict_pos + 12 + i * 16;
		bc_w32 (b, np + 0, nodes[i].ref_bit);
		bc_w16 (b, np + 4, nodes[i].left);
		bc_w16 (b, np + 6, nodes[i].right);
		if (i == 0)
		{
			bc_w32 (b, np + 8, 0);
			bc_w32 (b, np + 12, 0);
		}
		else
		{
			bc_rel_ptr (b, np + 8, strtab_base + nodes[i].name_pool_off);
			bc_rel_ptr (b, np + 12, nodes[i].data_off);
		}
	}
}

static void bc_joint_trs (float out[12], const joint_t *joint)
{
	const double dx = (double)joint->rotate.x * (M_PI / 180.0);
	const double dy = (double)joint->rotate.y * (M_PI / 180.0);
	const double dz = (double)joint->rotate.z * (M_PI / 180.0);
	const float cx = (float)cos (dx), sx = (float)sin (dx);
	const float cy = (float)cos (dy), sy = (float)sin (dy);
	const float cz = (float)cos (dz), sz = (float)sin (dz);
	const float rot[12] = { cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx, 0.0f,
		sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx, 0.0f, -sy, cy * sx, cy * cx,
		0.0f };
	float sx_val = joint->scale.x != 0.0f ? joint->scale.x : 1.0f;
	float sy_val = joint->scale.y != 0.0f ? joint->scale.y : 1.0f;
	float sz_val = joint->scale.z != 0.0f ? joint->scale.z : 1.0f;
	for (unsigned r = 0; r < 3; r++)
	{
		out[r * 4 + 0] = rot[r * 4 + 0] * sx_val;
		out[r * 4 + 1] = rot[r * 4 + 1] * sy_val;
		out[r * 4 + 2] = rot[r * 4 + 2] * sz_val;
	}
	out[3] = joint->translate.x;
	out[7] = joint->translate.y;
	out[11] = joint->translate.z;
}

static void bc_mul43 (float out[12], const float a[12], const float b[12])
{
	for (unsigned r = 0; r < 3; r++)
	{
		for (unsigned c = 0; c < 3; c++)
			out[r * 4 + c]
				= a[r * 4 + 0] * b[c + 0] + a[r * 4 + 1] * b[c + 4] + a[r * 4 + 2] * b[c + 8];
		out[r * 4 + 3]
			= a[r * 4 + 0] * b[3] + a[r * 4 + 1] * b[7] + a[r * 4 + 2] * b[11] + a[r * 4 + 3];
	}
}

static int bc_invert43 (float out[12], const float m[12])
{
	const double det = (double)m[0] * (m[5] * m[10] - m[6] * m[9])
		- (double)m[1] * (m[4] * m[10] - m[6] * m[8]) + (double)m[2] * (m[4] * m[9] - m[5] * m[8]);
	if (fabs (det) < 1e-20)
	{
		memset (out, 0, 12 * sizeof (float));
		out[0] = out[5] = out[10] = 1.0f;
		return 0;
	}
	const float d = (float)(1.0 / det);
	out[0] = (m[5] * m[10] - m[6] * m[9]) * d;
	out[1] = (m[2] * m[9] - m[1] * m[10]) * d;
	out[2] = (m[1] * m[6] - m[2] * m[5]) * d;
	out[4] = (m[6] * m[8] - m[4] * m[10]) * d;
	out[5] = (m[0] * m[10] - m[2] * m[8]) * d;
	out[6] = (m[2] * m[4] - m[0] * m[6]) * d;
	out[8] = (m[4] * m[9] - m[5] * m[8]) * d;
	out[9] = (m[1] * m[8] - m[0] * m[9]) * d;
	out[10] = (m[0] * m[5] - m[1] * m[4]) * d;
	out[3] = -(out[0] * m[3] + out[1] * m[7] + out[2] * m[11]);
	out[7] = -(out[4] * m[3] + out[5] * m[7] + out[6] * m[11]);
	out[11] = -(out[8] * m[3] + out[9] * m[7] + out[10] * m[11]);
	return 1;
}

// Interleaved attribute layout shared by the descriptor writer and the
// raw-buffer writer in CreateBCRES. Order follows BcmdlImporter
// CreateAttributes (position, normal, UV0-2, colour, tangent, then bone
// index/weight when the mesh is skinned); every stream is float so the
// decoder above reads values back exactly. Fills names/elements (up to 9
// entries) and returns the attribute count.
static unsigned bc_mesh_attr_list (const mesh_t *mesh, unsigned maxinf,
	uint32_t *names, int *els)
{
	unsigned na = 0;
	names[na] = 0;
	els[na] = 3;
	na++; // Position
	names[na] = 1;
	els[na] = 3;
	na++; // Normal
	names[na] = 4;
	els[na] = 2;
	na++; // TexCoord0
	if (mesh->num_extra_texcoords[0] > 0 && mesh->extra_texcoords[0])
	{
		names[na] = 5;
		els[na] = 2;
		na++; // TexCoord1
	}
	if (mesh->num_extra_texcoords[1] > 0 && mesh->extra_texcoords[1])
	{
		names[na] = 6;
		els[na] = 2;
		na++; // TexCoord2
	}
	if (mesh->num_colors[0] > 0 && mesh->colors[0])
	{
		names[na] = 3;
		els[na] = 4;
		na++; // Color
	}
	if (mesh->num_tangents > 0 && mesh->tangents)
	{
		names[na] = 2;
		els[na] = 3;
		na++; // Tangent
	}
	if (maxinf > 0)
	{
		if (maxinf > 4)
			maxinf = 4;
		names[na] = 7;
		els[na] = (int)maxinf;
		na++; // BoneIndex
		names[na] = 8;
		els[na] = (int)maxinf;
		na++; // BoneWeight
	}
	return na;
}

static bool bcres_float_close (float a, float b)
{
	float d = a - b;
	if (d < 0.0f)
		d = -d;
	return d < 0.001f;
}

static bool bcres_vec_close (const vec3_t *a, const vec3_t *b)
{
	return bcres_float_close (a->x, b->x) && bcres_float_close (a->y, b->y)
		&& bcres_float_close (a->z, b->z);
}

static bool bcres_vec2_close (const vec2_t *a, const vec2_t *b)
{
	return bcres_float_close (a->u, b->u) && bcres_float_close (a->v, b->v);
}

// True when a re-parse of the embedded original BCRES describes the same
// content as the model being encoded. The GLB pipeline passes through the
// exact floating-point values it decoded (f32 exports, no re-quantization),
// so the tiny tolerance only absorbs round-trip jitter while a real edit --
// a different vertex count, moved vertices, or an edited material/skeleton
// name set -- fails the comparison and forces a fresh build instead of
// silently returning the untouched original bytes.
static bool bcres_model_matches (const model_t *ref, const model_t *cur)
{
	if (!ref || !cur)
		return false;
	if (!ref->num_meshes || ref->num_meshes != cur->num_meshes)
		return false;
	if (ref->num_materials != cur->num_materials)
		return false;

	// A skeleton only survives the GLB pipeline when the mesh is actually
	// skinned; an unweighted CGFX skeleton (e.g. an SOBJ with no bindings)
	// is dropped on export and re-imports as zero joints. Only when the
	// GLB model actually carries joints are they worth comparing against
	// the original -- the digest of an unchanged model is zero there too.
	// A model that GAINED joints on the GLB side must have been edited, so
	// that case does not short-circuit to "unchanged".
	if (cur->num_joints > 0)
	{
		if (ref->num_joints != cur->num_joints)
			return false;
		for (size_t i = 0; i < ref->num_joints; i++)
			if (ref->joints[i].parent_idx != cur->joints[i].parent_idx
				|| strcmp (ref->joints[i].name, cur->joints[i].name))
				return false;
	}

	// Skinning is compared by presence only: the CGFX parser deduplicates
	// (bone, weight) combinations into a shared palette while the GLB
	// importer keeps one influence entry per vertex, so exact entry counts
	// legitimately differ for the same unchanged model. A model that gains
	// or loses skinning wholesale counts as edited.
	if ((ref->num_node_influences > 0) != (cur->num_node_influences > 0))
		return false;

	for (size_t m = 0; m < ref->num_meshes; m++)
	{
		const mesh_t *a = &ref->meshes[m];
		const mesh_t *b = &cur->meshes[m];
		if (a->num_vertices != b->num_vertices || a->num_positions != b->num_positions
			|| a->num_normals != b->num_normals || a->num_texcoords != b->num_texcoords
			|| a->num_tangents != b->num_tangents || a->num_colors[0] != b->num_colors[0]
			|| a->num_extra_texcoords[0] != b->num_extra_texcoords[0]
			|| a->num_extra_texcoords[1] != b->num_extra_texcoords[1]
			|| a->material_idx != b->material_idx)
			return false;

		// The GLB pipeline stores positions/normals/texcoords in index
		// order and re-assembles them back through vertices[].position_idx
		// (etc.) on import, so the two arrays are only directly comparable
		// through that mapping. If the underlying index permutation was
		// preserved -- vertices have not diverged in count or in the
		// data they reference -- the model is still the original one.
		for (size_t v = 0; v < a->num_vertices; v++)
		{
			int ap = a->vertices[v].position_idx;
			int bp = b->vertices[v].position_idx;
			int an = a->vertices[v].normal_idx;
			int bn = b->vertices[v].normal_idx;
			int at = a->vertices[v].texcoord_idx;
			int bt = b->vertices[v].texcoord_idx;
			if (ap != bp || an != bn || at != bt)
				return false;
			const int ag = a->vertices[v].tangent_idx;
			const int bg = b->vertices[v].tangent_idx;
			const int ac = a->vertices[v].color_idx[0];
			const int bc2 = b->vertices[v].color_idx[0];
			const int au1 = a->vertices[v].extra_texcoord_idx[0];
			const int bu1 = b->vertices[v].extra_texcoord_idx[0];
			const int au2 = a->vertices[v].extra_texcoord_idx[1];
			const int bu2 = b->vertices[v].extra_texcoord_idx[1];
			// Index mappings are only meaningful when the stream exists:
			// the GLB importer leaves a 0 stub where this parser stores
			// -1 for absent streams, and neither is wrong when the counts
			// above already agree the stream is absent on both sides.
			if (a->num_tangents > 0 && ag != bg)
				return false;
			if (a->num_colors[0] > 0 && ac != bc2)
				return false;
			if (a->num_extra_texcoords[0] > 0 && au1 != bu1)
				return false;
			if (a->num_extra_texcoords[1] > 0 && au2 != bu2)
				return false;
			if ((ap >= 0 && bp >= 0) && (a->positions && b->positions)
				&& !bcres_vec_close (&a->positions[ap], &b->positions[bp]))
				return false;
			if ((an >= 0 && bn >= 0) && (a->normals && b->normals)
				&& !bcres_vec_close (&a->normals[an], &b->normals[bn]))
				return false;
			if ((at >= 0 && bt >= 0) && (a->texcoords && b->texcoords)
				&& !bcres_vec2_close (&a->texcoords[at], &b->texcoords[bt]))
				return false;
			if ((ag >= 0 && bg >= 0) && (a->tangents && b->tangents)
				&& !bcres_vec_close (&a->tangents[ag], &b->tangents[bg]))
				return false;
			if ((ac >= 0 && bc2 >= 0) && (a->colors[0] && b->colors[0]))
			{
				const color4_t *ca = &a->colors[0][ac], *cb = &b->colors[0][bc2];
				if (!bcres_float_close (ca->r, cb->r) || !bcres_float_close (ca->g, cb->g)
					|| !bcres_float_close (ca->b, cb->b) || !bcres_float_close (ca->a, cb->a))
					return false;
			}
			if ((au1 >= 0 && bu1 >= 0) && (a->extra_texcoords[0] && b->extra_texcoords[0])
				&& !bcres_vec2_close (&a->extra_texcoords[0][au1], &b->extra_texcoords[0][bu1]))
				return false;
			if ((au2 >= 0 && bu2 >= 0) && (a->extra_texcoords[1] && b->extra_texcoords[1])
				&& !bcres_vec2_close (&a->extra_texcoords[1][au2], &b->extra_texcoords[1][bu2]))
				return false;
		}
	}

	for (size_t i = 0; i < ref->num_materials; i++)
	{
		const material_t *a = &ref->materials[i];
		const material_t *b = &cur->materials[i];

		// The GLB pipeline may collapse a material's multiple texture
		// references down to the one it can express, so counts are not
		// directly comparable. Every texture the GLB material still
		// references must exist in the original, or the material was
		// edited. The GLB path names textures after the original (with
		// optional image extension stripped), so exact string match
		// against the original set is the right test.
		for (int t = 0; t < b->num_textures; t++)
		{
			bool found = false;
			for (int u = 0; u < a->num_textures; u++)
			{
				if (!strcmp (a->textures[u], b->textures[t]))
				{
					found = true;
					break;
				}
			}
			if (!found)
				return false;
		}
	}

	return true;
}

// Reuse the embedded original container only when the model being encoded
// genuinely still describes it. Any mismatch routes to a fresh build.
static bool bcres_raw_matches (const model_t *model)
{
	if (!model->bcres_raw || !model->bcres_raw_size)
		return false;
	model_t *ref = ParseBCRES (model->bcres_raw, model->bcres_raw_size);
	if (!ref)
		return false;
	const bool ok = bcres_model_matches (ref, model);
	FreeModel (ref);
	return ok;
}

int CreateBCRES (const model_t *model, uint8_t **out_data, size_t *out_size)
{
	if (!model || !out_data || !out_size)
		return 0;
	if (model->bcres_raw && model->bcres_raw_size && bcres_raw_matches (model))
	{
		*out_data = MALLOC (model->bcres_raw_size);
		if (!*out_data)
			return 0;
		memcpy (*out_data, model->bcres_raw, model->bcres_raw_size);
		*out_size = model->bcres_raw_size;
		return 1;
	}
	if (!model->num_meshes)
		return 0;

	const uint32_t n_mesh = (uint32_t)model->num_meshes;
	const uint32_t n_mat = model->num_materials > 0 ? (uint32_t)model->num_materials : 1;
	const uint32_t n_bones = (uint32_t)model->num_joints;
	const bool has_skeleton = (n_bones > 0);
	const char *model_name = "Model";

	// Step 1: Collect strings into string pool
	bcres_strpool_t strpool;
	bc_strpool_init (&strpool);
	size_t model_name_str = bc_strpool_add (&strpool, model_name);
	size_t skeleton_name_str = has_skeleton ? bc_strpool_add (&strpool, "Skeleton") : 0;

	size_t *mesh_name_str = CALLOC (n_mesh, sizeof (size_t));
	size_t *shape_name_str = CALLOC (n_mesh, sizeof (size_t));
	for (uint32_t m = 0; m < n_mesh; m++)
	{
		char def_name[64];
		const char *mname = model->meshes[m].name[0] ? model->meshes[m].name : NULL;
		if (!mname)
		{
			snprintf (def_name, sizeof (def_name), "mesh%u", m);
			mname = def_name;
		}
		mesh_name_str[m] = bc_strpool_add (&strpool, mname);

		char sname[64];
		snprintf (sname, sizeof (sname), "shape%u", m);
		shape_name_str[m] = bc_strpool_add (&strpool, sname);
	}

	size_t *mat_name_str = CALLOC (n_mat, sizeof (size_t));
	size_t (*tex_name_str)[8] = CALLOC (n_mat, sizeof (*tex_name_str));
	for (uint32_t mi = 0; mi < n_mat; mi++)
	{
		if (model->materials && mi < model->num_materials)
		{
			const char *mat_name = model->materials[mi].name[0] ? model->materials[mi].name : "material";
			mat_name_str[mi] = bc_strpool_add (&strpool, mat_name);
			for (int t = 0; t < model->materials[mi].num_textures && t < 8; t++)
			{
				if (model->materials[mi].textures[t][0])
					tex_name_str[mi][t] = bc_strpool_add (&strpool, model->materials[mi].textures[t]);
			}
		}
		else
		{
			mat_name_str[mi] = bc_strpool_add (&strpool, "default_mat");
		}
	}

	size_t *bone_name_str = has_skeleton ? CALLOC (n_bones, sizeof (size_t)) : NULL;
	if (has_skeleton)
	{
		for (uint32_t bi = 0; bi < n_bones; bi++)
		{
			char bdef[64];
			const char *bname = model->joints[bi].name[0] ? model->joints[bi].name : NULL;
			if (!bname)
			{
				snprintf (bdef, sizeof (bdef), "bone%u", bi);
				bname = bdef;
			}
			bone_name_str[bi] = bc_strpool_add (&strpool, bname);
		}
	}

	// Step 2: Prepare bone hierarchy and transforms
	int *first_child = has_skeleton ? MALLOC (n_bones * sizeof (int)) : NULL;
	int *prev_sib = has_skeleton ? MALLOC (n_bones * sizeof (int)) : NULL;
	int *next_sib = has_skeleton ? MALLOC (n_bones * sizeof (int)) : NULL;
	float (*bone_local)[12] = has_skeleton ? CALLOC (n_bones, sizeof (*bone_local)) : NULL;
	float (*bone_world)[12] = has_skeleton ? CALLOC (n_bones, sizeof (*bone_world)) : NULL;
	float (*bone_inv)[12] = has_skeleton ? CALLOC (n_bones, sizeof (*bone_inv)) : NULL;
	int root_bone_idx = 0;

	if (has_skeleton)
	{
		for (uint32_t i = 0; i < n_bones; i++)
		{
			first_child[i] = -1;
			prev_sib[i] = -1;
			next_sib[i] = -1;
		}

		for (uint32_t i = 0; i < n_bones; i++)
		{
			int p = model->joints[i].parent_idx;
			if (p >= 0 && (uint32_t)p < n_bones)
			{
				if (first_child[p] == -1)
					first_child[p] = (int)i;
				else
				{
					int cur = first_child[p];
					while (next_sib[cur] != -1)
						cur = next_sib[cur];
					next_sib[cur] = (int)i;
					prev_sib[i] = cur;
				}
			}
			else
				root_bone_idx = (int)i;
		}

		for (uint32_t i = 0; i < n_bones; i++)
		{
			bc_joint_trs (bone_local[i], &model->joints[i]);
			if (model->joints[i].has_inverse_bind)
			{
				memcpy (bone_world[i], model->joints[i].bind, 12 * sizeof (float));
				memcpy (bone_inv[i], model->joints[i].inverse_bind, 12 * sizeof (float));
			}
			else
			{
				int p = model->joints[i].parent_idx;
				if (p >= 0 && (uint32_t)p < n_bones)
					bc_mul43 (bone_world[i], bone_world[p], bone_local[i]);
				else
					memcpy (bone_world[i], bone_local[i], 12 * sizeof (float));
				bc_invert43 (bone_inv[i], bone_world[i]);
			}
		}
	}

	// Step 3: Build buffer with bb_t
	bcres_buf_t bb;
	bc_buf_init (&bb);

	// 0x00: CGFX header (0x14)
	bc_buf_reserve (&bb, 0x14);
	// 0x14: DATA section header (8 bytes)
	bc_buf_reserve (&bb, 8);
	memcpy (bb.data + 0x14, "DATA", 4);

	// 0x1C: 16 Dict slots table (16 * 8 = 128 = 0x80 bytes)
	bc_buf_reserve (&bb, 0x80);

	// Models DICT at 0x9C
	size_t models_dict_off = bb.size;
	bc_buf_reserve (&bb, 0x2C);
	bc_w32 (&bb, 0x1C, 1); // count = 1
	bc_rel_ptr (&bb, 0x20, models_dict_off);

	// CMDL Object
	size_t cmdl_off = bb.size;
	size_t cmdl_size = has_skeleton ? 0xE4 : 0xE0;
	bc_buf_reserve (&bb, cmdl_size);

	bb.data[cmdl_off + 0] = has_skeleton ? 0x92 : 0x12;
	bb.data[cmdl_off + 1] = 0x00;
	bb.data[cmdl_off + 2] = 0x00;
	bb.data[cmdl_off + 3] = 0x40;
	memcpy (bb.data + cmdl_off + 4, "CMDL", 4);
	bc_w32 (&bb, cmdl_off + 8, 0x09000000);
	bc_w32 (&bb, cmdl_off + 0x18, 1); // BranchVisible
	bc_w32 (&bb, cmdl_off + 0x1C, 1); // IsBranchVisible

	bc_wf32 (&bb, cmdl_off + 0x30, 1.0f);
	bc_wf32 (&bb, cmdl_off + 0x34, 1.0f);
	bc_wf32 (&bb, cmdl_off + 0x38, 1.0f);
	// 3x4 identity matrix for local transform
	bc_wf32 (&bb, cmdl_off + 0x54, 1.0f);
	bc_wf32 (&bb, cmdl_off + 0x68, 1.0f);
	bc_wf32 (&bb, cmdl_off + 0x7C, 1.0f);
	// 3x4 identity matrix for world transform
	bc_wf32 (&bb, cmdl_off + 0x84, 1.0f);
	bc_wf32 (&bb, cmdl_off + 0x98, 1.0f);
	bc_wf32 (&bb, cmdl_off + 0xAC, 1.0f);

	bc_w32 (&bb, cmdl_off + 0xB4, n_mesh);
	bc_w32 (&bb, cmdl_off + 0xBC, n_mat);
	bc_w32 (&bb, cmdl_off + 0xC4, n_mesh);
	bc_w32 (&bb, cmdl_off + 0xD4, 1); // Flags = IsVisible

	// Tables for mesh & shape pointers
	size_t mesh_ptrs_table = bb.size;
	bc_buf_reserve (&bb, n_mesh * 4);
	bc_rel_ptr (&bb, cmdl_off + 0xB8, mesh_ptrs_table);

	size_t shape_ptrs_table = bb.size;
	bc_buf_reserve (&bb, n_mesh * 4);
	bc_rel_ptr (&bb, cmdl_off + 0xC8, shape_ptrs_table);

	// Materials DICT
	size_t mat_dict_off = bb.size;
	size_t mat_dict_size = (n_mat + 1) * 16 + 12;
	bc_buf_reserve (&bb, mat_dict_size);
	bc_rel_ptr (&bb, cmdl_off + 0xC0, mat_dict_off);

	// Material objects (MTOB)
	size_t *mtob_off = CALLOC (n_mat, sizeof (size_t));
	for (uint32_t mi = 0; mi < n_mat; mi++)
	{
		mtob_off[mi] = bb.size;
		int num_tex = (model->materials && mi < model->num_materials)
			? model->materials[mi].num_textures : 0;
		if (num_tex > 8)
			num_tex = 8;
		size_t mtob_size = 0x80 + num_tex * 0x30;
		bc_buf_reserve (&bb, mtob_size);

		size_t mo = mtob_off[mi];
		bc_w32 (&bb, mo + 0x00, 0x08000000);
		memcpy (bb.data + mo + 0x04, "MTOB", 4);
		bc_w32 (&bb, mo + 0x08, 0x06000003);
		bc_w32 (&bb, mo + 0x18, (uint32_t)num_tex);

		float amb[3] = { 0.2f, 0.2f, 0.2f };
		float diff[4] = { 0.8f, 0.8f, 0.8f, 1.0f };
		float spec[3] = { 0.0f, 0.0f, 0.0f };
		if (model->materials && mi < model->num_materials)
		{
			if (model->materials[mi].ambient[0] || model->materials[mi].ambient[1]
				|| model->materials[mi].ambient[2])
			{
				amb[0] = model->materials[mi].ambient[0];
				amb[1] = model->materials[mi].ambient[1];
				amb[2] = model->materials[mi].ambient[2];
			}
			if (model->materials[mi].diffuse[0] || model->materials[mi].diffuse[1]
				|| model->materials[mi].diffuse[2] || model->materials[mi].diffuse[3])
			{
				diff[0] = model->materials[mi].diffuse[0];
				diff[1] = model->materials[mi].diffuse[1];
				diff[2] = model->materials[mi].diffuse[2];
				diff[3] = model->materials[mi].diffuse[3];
			}
			if (model->materials[mi].specular[0] || model->materials[mi].specular[1]
				|| model->materials[mi].specular[2])
			{
				spec[0] = model->materials[mi].specular[0];
				spec[1] = model->materials[mi].specular[1];
				spec[2] = model->materials[mi].specular[2];
			}
		}
		bc_wf32 (&bb, mo + 0x24, amb[0]);
		bc_wf32 (&bb, mo + 0x28, amb[1]);
		bc_wf32 (&bb, mo + 0x2C, amb[2]);
		bc_wf32 (&bb, mo + 0x30, diff[0]);
		bc_wf32 (&bb, mo + 0x34, diff[1]);
		bc_wf32 (&bb, mo + 0x38, diff[2]);
		bc_wf32 (&bb, mo + 0x3C, diff[3]);
		bc_wf32 (&bb, mo + 0x40, spec[0]);
		bc_wf32 (&bb, mo + 0x44, spec[1]);
		bc_wf32 (&bb, mo + 0x48, spec[2]);
		bc_wf32 (&bb, mo + 0x4C, 1.0f);
		bc_wf32 (&bb, mo + 0x5C, 1.0f);
		bc_wf32 (&bb, mo + 0x6C, 1.0f);

		for (int t = 0; t < num_tex; t++)
		{
			size_t txo = mo + 0x80 + t * 0x30;
			bc_w32 (&bb, txo + 0x00, 0x20000004);
			memcpy (bb.data + txo + 0x04, "TXOB", 4);
			bc_w32 (&bb, txo + 0x08, 0x05000000);
			bc_w32 (&bb, txo + 0x20, 0x80000000);
			bc_w32 (&bb, txo + 0x24, 0xFFFFFF90);
			bc_w32 (&bb, txo + 0x28, 1);
		}
	}

	// Meshes (GfxMesh)
	size_t *mesh_off = CALLOC (n_mesh, sizeof (size_t));
	for (uint32_t m = 0; m < n_mesh; m++)
	{
		mesh_off[m] = bb.size;
		bc_buf_reserve (&bb, 0x30);
		bc_rel_ptr (&bb, mesh_ptrs_table + m * 4, mesh_off[m]);

		size_t mo = mesh_off[m];
		bc_w32 (&bb, mo + 0x00, 0x01000000); // CGFX_TC_MESH
		memcpy (bb.data + mo + 0x04, "SOBJ", 4);
		bc_w32 (&bb, mo + 0x18, m); // ShapeIndex
		int mat_idx = model->meshes[m].material_idx;
		if (mat_idx < 0 || (uint32_t)mat_idx >= n_mat)
			mat_idx = 0;
		bc_w32 (&bb, mo + 0x1C, (uint32_t)mat_idx);
		bc_rel_ptr (&bb, mo + 0x20, cmdl_off); // Parent back-pointer to CMDL!
		bc_w32 (&bb, mo + 0x24, 0x00010001); // Visible = 1
	}

	// Shapes (GfxShape)
	size_t *shape_off = CALLOC (n_mesh, sizeof (size_t));
	size_t *bbox_off = CALLOC (n_mesh, sizeof (size_t));
	size_t *submesh_tbl_off = CALLOC (n_mesh, sizeof (size_t));
	size_t *submesh_off = CALLOC (n_mesh, sizeof (size_t));
	size_t *face_tbl_off = CALLOC (n_mesh, sizeof (size_t));
	size_t *face_off = CALLOC (n_mesh, sizeof (size_t));
	size_t *fd_tbl_off = CALLOC (n_mesh, sizeof (size_t));
	size_t *fd_off = CALLOC (n_mesh, sizeof (size_t));
	size_t *vb_tbl_off = CALLOC (n_mesh, sizeof (size_t));
	size_t *vb_off = CALLOC (n_mesh, sizeof (size_t));
	size_t *attr_tbl_off = CALLOC (n_mesh, sizeof (size_t));
	size_t (*attr_off)[9] = CALLOC (n_mesh, sizeof (*attr_off));
	// Per-mesh encoder plan (BcmdlImporter ConvertMesh writes one
	// interleaved buffer with exactly the streams the mesh carries, plus a
	// per-submesh bone palette): attribute count, byte stride, palette of
	// global bone ids (max 20, the H3D per-submesh limit the importer also
	// splits on), widest influence list, skinning kind (0=None,1=Rigid,
	// 2=Smooth) and whether indices fit in a byte.
	unsigned *mesh_na = CALLOC (n_mesh, sizeof (*mesh_na));
	unsigned *mesh_stride = CALLOC (n_mesh, sizeof (*mesh_stride));
	uint32_t (*mesh_pal)[20] = CALLOC (n_mesh, sizeof (*mesh_pal));
	unsigned char *mesh_npal = CALLOC (n_mesh, sizeof (*mesh_npal));
	unsigned char *mesh_maxinf = CALLOC (n_mesh, sizeof (*mesh_maxinf));
	unsigned char *mesh_skind = CALLOC (n_mesh, sizeof (*mesh_skind));
	unsigned char *mesh_use_u8 = CALLOC (n_mesh, sizeof (*mesh_use_u8));

	for (uint32_t m = 0; m < n_mesh; m++)
	{
		shape_off[m] = bb.size;
		bc_buf_reserve (&bb, 0x48);
		bc_rel_ptr (&bb, shape_ptrs_table + m * 4, shape_off[m]);

		size_t so = shape_off[m];
		bc_w32 (&bb, so + 0x00, 0x10000001); // CGFX_TC_SHAPE
		memcpy (bb.data + so + 0x04, "SOBJ", 4);
		bc_w32 (&bb, so + 0x2C, 1); // n_sub = 1
		bc_w32 (&bb, so + 0x38, 1); // n_vb = 1

		// BoundingBox
		bbox_off[m] = bb.size;
		bc_buf_reserve (&bb, 0x3C);
		bc_rel_ptr (&bb, so + 0x1C, bbox_off[m]);

		const mesh_t *mesh = &model->meshes[m];

		// Encoder plan for this mesh: skinning palette (unique global bone
		// ids referenced through position_node, capped at the 20-entry
		// per-submesh limit BcmdlImporter also splits on), attribute list
		// and stride, and the index width (BcmdlImporter GenerateSubMeshes
		// picks U8 when every index fits in a byte, U16 otherwise).
		{
			uint32_t pal[20];
			unsigned pal_n = 0, pal_total = 0, maxinf = 0;
			if (mesh->position_node && mesh->vertices && model->node_influences)
			{
				for (size_t v = 0; v < mesh->num_vertices; v++)
				{
					const int pi = mesh->vertices[v].position_idx;
					if (pi < 0 || (size_t)pi >= mesh->num_positions)
						continue;
					const int ni = mesh->position_node[(size_t)pi];
					if (ni < 0 || (size_t)ni >= model->num_node_influences)
						continue;
					const node_influence_t *e = &model->node_influences[(size_t)ni];
					if (!e->num_weights || e->num_weights > 4)
						continue;
					if ((unsigned)e->num_weights > maxinf)
						maxinf = (unsigned)e->num_weights;
					for (size_t w = 0; w < e->num_weights; w++)
					{
						const int b = e->weights[w].bone_idx;
						if (b < 0 || (n_bones && (uint32_t)b >= n_bones))
							continue;
						bool seen = false;
						for (unsigned q = 0; q < pal_n; q++)
							if (pal[q] == (uint32_t)b)
							{
								seen = true;
								break;
							}
						if (!seen)
						{
							if (pal_n < 20)
								pal[pal_n++] = (uint32_t)b;
							pal_total++;
						}
					}
				}
			}
			const bool skinned = pal_total > 0 && pal_total <= 20 && maxinf > 0;
			if (skinned)
			{
				mesh_npal[m] = (unsigned char)pal_total;
				for (unsigned q = 0; q < pal_n; q++)
					mesh_pal[m][q] = pal[q];
				mesh_maxinf[m] = (unsigned char)maxinf;
				mesh_skind[m] = maxinf > 1 ? 2 : 1;
			}
			else
			{
				mesh_npal[m] = 0;
				mesh_maxinf[m] = 0;
				mesh_skind[m] = 0;
			}
			uint32_t an[9];
			int ae[9];
			const unsigned na = bc_mesh_attr_list (mesh, mesh_maxinf[m], an, ae);
			mesh_na[m] = na;
			unsigned st = 0;
			for (unsigned q = 0; q < na; q++)
				st += (unsigned)ae[q];
			mesh_stride[m] = st * 4;
			mesh_use_u8[m] = (mesh->num_vertices > 0 && mesh->num_vertices <= 256) ? 1 : 0;
		}

		float min_x = 1e30f, min_y = 1e30f, min_z = 1e30f;
		float max_x = -1e30f, max_y = -1e30f, max_z = -1e30f;
		size_t num_v = mesh->num_positions > 0 ? mesh->num_positions : mesh->num_vertices;
		for (size_t vi = 0; vi < num_v && mesh->positions; vi++)
		{
			float x = mesh->positions[vi].x;
			float y = mesh->positions[vi].y;
			float z = mesh->positions[vi].z;
			if (x < min_x) min_x = x;
			if (x > max_x) max_x = x;
			if (y < min_y) min_y = y;
			if (y > max_y) max_y = y;
			if (z < min_z) min_z = z;
			if (z > max_z) max_z = z;
		}
		if (min_x > max_x)
		{
			min_x = min_y = min_z = -1.0f;
			max_x = max_y = max_z = 1.0f;
		}
		bc_wf32 (&bb, bbox_off[m] + 0x00, (min_x + max_x) * 0.5f);
		bc_wf32 (&bb, bbox_off[m] + 0x04, (min_y + max_y) * 0.5f);
		bc_wf32 (&bb, bbox_off[m] + 0x08, (min_z + max_z) * 0.5f);
		bc_wf32 (&bb, bbox_off[m] + 0x0C, 1.0f);
		bc_wf32 (&bb, bbox_off[m] + 0x1C, 1.0f);
		bc_wf32 (&bb, bbox_off[m] + 0x2C, 1.0f);
		bc_wf32 (&bb, bbox_off[m] + 0x30, (max_x - min_x) > 0 ? (max_x - min_x) : 1.0f);
		bc_wf32 (&bb, bbox_off[m] + 0x34, (max_y - min_y) > 0 ? (max_y - min_y) : 1.0f);
		bc_wf32 (&bb, bbox_off[m] + 0x38, (max_z - min_z) > 0 ? (max_z - min_z) : 1.0f);

		// SubMesh table & SubMesh
		submesh_tbl_off[m] = bb.size;
		bc_buf_reserve (&bb, 4);
		bc_rel_ptr (&bb, so + 0x30, submesh_tbl_off[m]);

		submesh_off[m] = bb.size;
		bc_buf_reserve (&bb, 0x20);
		bc_rel_ptr (&bb, submesh_tbl_off[m], submesh_off[m]);
		// +0x00 bone-palette count, +0x04 table of global bone ids,
		// +0x08 skinning kind (0=None,1=Rigid,2=Smooth). An absent palette
		// leaves a null table, exactly like the retail static submeshes.
		bc_w32 (&bb, submesh_off[m] + 0x00, mesh_npal[m]);
		if (mesh_npal[m])
		{
			size_t bt = bc_buf_reserve (&bb, (size_t)mesh_npal[m] * 4);
			for (unsigned q = 0; q < mesh_npal[m]; q++)
				bc_w32 (&bb, bt + (size_t)q * 4, mesh_pal[m][q]);
			bc_rel_ptr (&bb, submesh_off[m] + 0x04, bt);
		}
		bc_w32 (&bb, submesh_off[m] + 0x08, mesh_skind[m]);
		bc_w32 (&bb, submesh_off[m] + 0x0C, 1); // nf = 1

		// Face table & Face
		face_tbl_off[m] = bb.size;
		bc_buf_reserve (&bb, 4);
		bc_rel_ptr (&bb, submesh_off[m] + 0x10, face_tbl_off[m]);

		face_off[m] = bb.size;
		bc_buf_reserve (&bb, 8);
		bc_rel_ptr (&bb, face_tbl_off[m], face_off[m]);
		bc_w32 (&bb, face_off[m] + 0x00, 1); // nfd = 1

		// FaceDescriptor table & FaceDescriptor
		fd_tbl_off[m] = bb.size;
		bc_buf_reserve (&bb, 4);
		bc_rel_ptr (&bb, face_off[m] + 0x04, fd_tbl_off[m]);

		fd_off[m] = bb.size;
		bc_buf_reserve (&bb, 0x2C);
		bc_rel_ptr (&bb, fd_tbl_off[m], fd_off[m]);
		uint32_t total_idx = (uint32_t)mesh->num_vertices;
		if (mesh_use_u8[m])
		{
			bc_w32 (&bb, fd_off[m] + 0x00, 0x1401); // GL_UNSIGNED_BYTE_
			bc_w32 (&bb, fd_off[m] + 0x04, 0x00000100);
			bc_w32 (&bb, fd_off[m] + 0x08, total_idx); // ilen = bytes
		}
		else
		{
			bc_w32 (&bb, fd_off[m] + 0x00, 0x1403); // GL_UNSIGNED_SHORT_
			bc_w32 (&bb, fd_off[m] + 0x04, 0x00000100);
			bc_w32 (&bb, fd_off[m] + 0x08, total_idx * 2); // ilen
		}

		// VertexBuffer table & VertexBuffer
		vb_tbl_off[m] = bb.size;
		bc_buf_reserve (&bb, 4);
		bc_rel_ptr (&bb, so + 0x3C, vb_tbl_off[m]);

		vb_off[m] = bb.size;
		bc_buf_reserve (&bb, 0x30);
		bc_rel_ptr (&bb, vb_tbl_off[m], vb_off[m]);
		const unsigned vstride = mesh_stride[m] ? mesh_stride[m] : 32;
		const unsigned vna = mesh_na[m] ? mesh_na[m] : 3;
		bc_w32 (&bb, vb_off[m] + 0x00, 0x40000002); // CGFX_TC_INTERLEAVED
		bc_w32 (&bb, vb_off[m] + 0x14, total_idx * vstride); // rawlen
		bc_w32 (&bb, vb_off[m] + 0x24, vstride); // vstride
		bc_w32 (&bb, vb_off[m] + 0x28, vna); // na

		// Attribute table & Attributes
		attr_tbl_off[m] = bb.size;
		bc_buf_reserve (&bb, (size_t)vna * 4);
		bc_rel_ptr (&bb, vb_off[m] + 0x2C, attr_tbl_off[m]);

		uint32_t enc_names[9];
		int enc_els[9];
		const unsigned enc_na = bc_mesh_attr_list (mesh, mesh_maxinf[m], enc_names, enc_els);
		unsigned enc_off = 0;
		for (unsigned a = 0; a < vna && a < enc_na; a++)
		{
			attr_off[m][a] = bb.size;
			bc_buf_reserve (&bb, 0x34);
			bc_rel_ptr (&bb, attr_tbl_off[m] + a * 4, attr_off[m][a]);

			size_t ao = attr_off[m][a];
			bc_w32 (&bb, ao + 0x00, 0x40000001); // CGFX_TC_ATTRIBUTE
			bc_w32 (&bb, ao + 0x04, enc_names[a]);
			bc_w32 (&bb, ao + 0x24, 0x1406); // GL_FLOAT
			bc_w32 (&bb, ao + 0x28, (uint32_t)enc_els[a]); // elements
			bc_wf32 (&bb, ao + 0x2C, 1.0f); // scale = 1.0f
			bc_w32 (&bb, ao + 0x30, enc_off); // offset
			enc_off += (unsigned)enc_els[a] * 4;
		}
	}

	// Skeleton (SOBJ) & Bones
	size_t sobj_off = 0;
	size_t bones_dict_off = 0;
	size_t *bone_off = has_skeleton ? CALLOC (n_bones, sizeof (size_t)) : NULL;

	if (has_skeleton)
	{
		sobj_off = bb.size;
		bc_buf_reserve (&bb, 0x2C);
		bc_rel_ptr (&bb, cmdl_off + 0xE0, sobj_off);

		bc_w32 (&bb, sobj_off + 0x00, 0x02000000);
		memcpy (bb.data + sobj_off + 0x04, "SOBJ", 4);
		bc_w32 (&bb, sobj_off + 0x18, n_bones);
		bc_w32 (&bb, sobj_off + 0x24, 1); // ScalingRule = Standard
		bc_w32 (&bb, sobj_off + 0x28, 2); // Flags = IsTranslationAnimEnabled

		bones_dict_off = bb.size;
		size_t bones_dict_size = (n_bones + 1) * 16 + 12;
		bc_buf_reserve (&bb, bones_dict_size);
		bc_rel_ptr (&bb, sobj_off + 0x1C, bones_dict_off);

		for (uint32_t bi = 0; bi < n_bones; bi++)
		{
			bone_off[bi] = bb.size;
			bc_buf_reserve (&bb, 0xE0);
		}

		// RootBone pointer in SOBJ!
		bc_rel_ptr (&bb, sobj_off + 0x20, bone_off[root_bone_idx]);

		for (uint32_t bi = 0; bi < n_bones; bi++)
		{
			size_t bo = bone_off[bi];
			bc_w32 (&bb, bo + 0x04, 0x19F);
			bc_w32 (&bb, bo + 0x08, bi);
			int p_idx = model->joints[bi].parent_idx;
			bc_w32 (&bb, bo + 0x0C, (uint32_t)p_idx);

			if (p_idx >= 0 && (uint32_t)p_idx < n_bones)
				bc_rel_ptr (&bb, bo + 0x10, bone_off[p_idx]);
			if (first_child[bi] >= 0)
				bc_rel_ptr (&bb, bo + 0x14, bone_off[first_child[bi]]);
			if (prev_sib[bi] >= 0)
				bc_rel_ptr (&bb, bo + 0x18, bone_off[prev_sib[bi]]);
			if (next_sib[bi] >= 0)
				bc_rel_ptr (&bb, bo + 0x1C, bone_off[next_sib[bi]]);

			float sx = model->joints[bi].scale.x != 0.0f ? model->joints[bi].scale.x : 1.0f;
			float sy = model->joints[bi].scale.y != 0.0f ? model->joints[bi].scale.y : 1.0f;
			float sz = model->joints[bi].scale.z != 0.0f ? model->joints[bi].scale.z : 1.0f;
			bc_wf32 (&bb, bo + 0x20, sx);
			bc_wf32 (&bb, bo + 0x24, sy);
			bc_wf32 (&bb, bo + 0x28, sz);

			// Rotate in radians
			bc_wf32 (&bb, bo + 0x2C, model->joints[bi].rotate.x * ((float)M_PI / 180.0f));
			bc_wf32 (&bb, bo + 0x30, model->joints[bi].rotate.y * ((float)M_PI / 180.0f));
			bc_wf32 (&bb, bo + 0x34, model->joints[bi].rotate.z * ((float)M_PI / 180.0f));

			bc_wf32 (&bb, bo + 0x38, model->joints[bi].translate.x);
			bc_wf32 (&bb, bo + 0x3C, model->joints[bi].translate.y);
			bc_wf32 (&bb, bo + 0x40, model->joints[bi].translate.z);

			for (int m = 0; m < 12; m++)
				bc_wf32 (&bb, bo + 0x44 + m * 4, bone_local[bi][m]);
			for (int m = 0; m < 12; m++)
				bc_wf32 (&bb, bo + 0x74 + m * 4, bone_world[bi][m]);
			for (int m = 0; m < 12; m++)
				bc_wf32 (&bb, bo + 0xA4 + m * 4, bone_inv[bi][m]);
		}
	}

	// String table
	bc_buf_align (&bb, 4);
	size_t strtab_off = bb.size;
	size_t spos = bc_buf_reserve (&bb, strpool.size);
	memcpy (bb.data + spos, strpool.data, strpool.size);
	bc_buf_align (&bb, 4);

	size_t data_sec_end = bb.size;
	size_t data_sec_len = data_sec_end - 0x14;
	bc_w32 (&bb, 0x18, (uint32_t)data_sec_len);

	// Connect string pointers
	bc_rel_ptr (&bb, cmdl_off + 0x0C, strtab_off + model_name_str);
	if (has_skeleton)
		bc_rel_ptr (&bb, sobj_off + 0x0C, strtab_off + skeleton_name_str);

	for (uint32_t m = 0; m < n_mesh; m++)
	{
		bc_rel_ptr (&bb, mesh_off[m] + 0x0C, strtab_off + mesh_name_str[m]);
		bc_rel_ptr (&bb, shape_off[m] + 0x0C, strtab_off + shape_name_str[m]);
	}

	for (uint32_t mi = 0; mi < n_mat; mi++)
	{
		size_t mo = mtob_off[mi];
		bc_rel_ptr (&bb, mo + 0x0C, strtab_off + mat_name_str[mi]);
		int num_tex = (model->materials && mi < model->num_materials)
			? model->materials[mi].num_textures : 0;
		if (num_tex > 8)
			num_tex = 8;
		for (int t = 0; t < num_tex; t++)
		{
			size_t txo = mo + 0x80 + t * 0x30;
			bc_rel_ptr (&bb, txo + 0x0C, strtab_off + mat_name_str[mi]);
			bc_rel_ptr (&bb, txo + 0x18, strtab_off + tex_name_str[mi][t]);
		}
	}

	if (has_skeleton)
	{
		for (uint32_t bi = 0; bi < n_bones; bi++)
			bc_rel_ptr (&bb, bone_off[bi] + 0x00, strtab_off + bone_name_str[bi]);
	}

	// Build & write DICTs
	// 1. Models DICT
	bcres_patricia_node_t model_nodes[2];
	memset (model_nodes, 0, sizeof (model_nodes));
	model_nodes[0].ref_bit = 0xFFFFFFFF;
	model_nodes[0].left = 1;
	model_nodes[0].right = 0;
	model_nodes[1].name = model_name;
	model_nodes[1].ref_bit = (uint32_t)((strlen (model_name) << 3) - 1);
	model_nodes[1].left = 0;
	model_nodes[1].right = 1;
	model_nodes[1].name_pool_off = model_name_str;
	model_nodes[1].data_off = cmdl_off;
	bc_write_dict (&bb, models_dict_off, model_nodes, 2, strtab_off);

	// 2. Materials DICT
	bcres_patricia_node_t *mat_nodes = CALLOC (n_mat + 1, sizeof (bcres_patricia_node_t));
	for (uint32_t mi = 0; mi < n_mat; mi++)
	{
		mat_nodes[mi + 1].name = strpool.data + mat_name_str[mi];
		mat_nodes[mi + 1].name_pool_off = mat_name_str[mi];
		mat_nodes[mi + 1].data_off = mtob_off[mi];
	}
	bc_build_patricia_tree (mat_nodes, n_mat + 1);
	bc_write_dict (&bb, mat_dict_off, mat_nodes, n_mat + 1, strtab_off);
	FREE (mat_nodes);

	// 3. Bones DICT
	if (has_skeleton)
	{
		bcres_patricia_node_t *bone_nodes = CALLOC (n_bones + 1, sizeof (bcres_patricia_node_t));
		for (uint32_t bi = 0; bi < n_bones; bi++)
		{
			bone_nodes[bi + 1].name = strpool.data + bone_name_str[bi];
			bone_nodes[bi + 1].name_pool_off = bone_name_str[bi];
			bone_nodes[bi + 1].data_off = bone_off[bi];
		}
		bc_build_patricia_tree (bone_nodes, n_bones + 1);
		bc_write_dict (&bb, bones_dict_off, bone_nodes, n_bones + 1, strtab_off);
		FREE (bone_nodes);
	}

	// Step 4: IMAG Section & Raw Buffers
	size_t imag_sec_start = bb.size;
	bc_buf_reserve (&bb, 8);
	memcpy (bb.data + imag_sec_start, "IMAG", 4);

	for (uint32_t m = 0; m < n_mesh; m++)
	{
		const mesh_t *mesh = &model->meshes[m];
		uint32_t total_idx = (uint32_t)mesh->num_vertices;
		const unsigned stride = mesh_stride[m] ? mesh_stride[m] : 32;
		const unsigned maxinf = mesh_maxinf[m];

		// Raw index buffer (U8 when every index fits in a byte, matching
		// BcmdlImporter GenerateSubMeshes; the decoder reads both widths).
		bc_buf_align (&bb, 4);
		size_t raw_idx_off = bb.size;
		bc_rel_ptr (&bb, fd_off[m] + 0x0C, raw_idx_off);

		size_t idx_bytes = mesh_use_u8[m] ? total_idx : (size_t)total_idx * 2;
		size_t ipos = bc_buf_reserve (&bb, idx_bytes);
		for (uint32_t v = 0; v < total_idx; v++)
		{
			if (mesh_use_u8[m])
				bb.data[ipos + v] = (uint8_t)v;
			else
				bc_w16 (&bb, ipos + (size_t)v * 2, (uint16_t)v);
		}

		// Raw vertex buffer, driven by the same attribute list the
		// descriptors above were built from.
		bc_buf_align (&bb, 4);
		size_t raw_vtx_off = bb.size;
		bc_rel_ptr (&bb, vb_off[m] + 0x18, raw_vtx_off);

		uint32_t raw_names[9];
		int raw_els[9];
		const unsigned raw_na = bc_mesh_attr_list (mesh, maxinf, raw_names, raw_els);
		size_t vtx_bytes = (size_t)total_idx * stride;
		size_t vpos = bc_buf_reserve (&bb, vtx_bytes);

		for (uint32_t v = 0; v < total_idx; v++)
		{
			int pi = mesh->vertices ? mesh->vertices[v].position_idx : (int)v;
			int ni = mesh->vertices ? mesh->vertices[v].normal_idx : (int)v;
			int ti = mesh->vertices ? mesh->vertices[v].texcoord_idx : (int)v;
			int gi = mesh->vertices ? mesh->vertices[v].tangent_idx : -1;
			int ci = mesh->vertices ? mesh->vertices[v].color_idx[0] : -1;
			int u1i = mesh->vertices ? mesh->vertices[v].extra_texcoord_idx[0] : -1;
			int u2i = mesh->vertices ? mesh->vertices[v].extra_texcoord_idx[1] : -1;

			vec3_t p = (pi >= 0 && (size_t)pi < mesh->num_positions && mesh->positions)
				? mesh->positions[pi] : (vec3_t){ 0, 0, 0 };
			vec3_t n = (ni >= 0 && (size_t)ni < mesh->num_normals && mesh->normals)
				? mesh->normals[ni] : (vec3_t){ 0, 1.0f, 0 };
			vec2_t uv = (ti >= 0 && (size_t)ti < mesh->num_texcoords && mesh->texcoords)
				? mesh->texcoords[ti] : (vec2_t){ 0, 0 };
			vec3_t tg = (gi >= 0 && (size_t)gi < mesh->num_tangents && mesh->tangents)
				? mesh->tangents[gi] : (vec3_t){ 0, 1.0f, 0 };
			color4_t col = { 1.0f, 1.0f, 1.0f, 1.0f };
			if (ci >= 0 && (size_t)ci < mesh->num_colors[0] && mesh->colors[0])
				col = mesh->colors[0][ci];
			vec2_t uv1 = (u1i >= 0 && (size_t)u1i < mesh->num_extra_texcoords[0]
					&& mesh->extra_texcoords[0])
				? mesh->extra_texcoords[0][u1i] : (vec2_t){ 0, 0 };
			vec2_t uv2 = (u2i >= 0 && (size_t)u2i < mesh->num_extra_texcoords[1]
					&& mesh->extra_texcoords[1])
				? mesh->extra_texcoords[1][u2i] : (vec2_t){ 0, 0 };

			// Skinning for this vertex: influence list through the mesh's
			// global-bone palette, stored as local palette indices.
			int loc[4] = { 0, 0, 0, 0 };
			float wgt[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			unsigned nw = 0;
			if (maxinf > 0 && mesh->position_node && model->node_influences)
			{
				const int pni = (pi >= 0 && (size_t)pi < mesh->num_positions)
					? mesh->position_node[(size_t)pi] : -1;
				if (pni >= 0 && (size_t)pni < model->num_node_influences)
				{
					const node_influence_t *e
						= &model->node_influences[(size_t)pni];
					for (size_t w = 0; w < e->num_weights && nw < maxinf && nw < 4; w++)
					{
						int gb = e->weights[w].bone_idx;
						unsigned li = 0;
						for (unsigned q = 0; q < mesh_npal[m]; q++)
							if ((int)mesh_pal[m][q] == gb)
							{
								li = q;
								break;
							}
						loc[nw] = (int)li;
						wgt[nw] = e->weights[w].weight;
						nw++;
					}
				}
				if (!nw && mesh_npal[m])
				{
					loc[0] = 0;
					wgt[0] = 1.0f;
					nw = 1;
				}
			}

			size_t o = vpos + (size_t)v * stride;
			for (unsigned k = 0; k < raw_na; k++)
			{
				switch (raw_names[k])
				{
					case 0: // Position
						bc_wf32 (&bb, o + 0, p.x);
						bc_wf32 (&bb, o + 4, p.y);
						bc_wf32 (&bb, o + 8, p.z);
						o += 12;
						break;
					case 1: // Normal
						bc_wf32 (&bb, o + 0, n.x);
						bc_wf32 (&bb, o + 4, n.y);
						bc_wf32 (&bb, o + 8, n.z);
						o += 12;
						break;
					case 4: // TexCoord0
						bc_wf32 (&bb, o + 0, uv.u);
						bc_wf32 (&bb, o + 4, uv.v);
						o += 8;
						break;
					case 5: // TexCoord1
						bc_wf32 (&bb, o + 0, uv1.u);
						bc_wf32 (&bb, o + 4, uv1.v);
						o += 8;
						break;
					case 6: // TexCoord2
						bc_wf32 (&bb, o + 0, uv2.u);
						bc_wf32 (&bb, o + 4, uv2.v);
						o += 8;
						break;
					case 3: // Color
						bc_wf32 (&bb, o + 0, col.r);
						bc_wf32 (&bb, o + 4, col.g);
						bc_wf32 (&bb, o + 8, col.b);
						bc_wf32 (&bb, o + 12, col.a);
						o += 16;
						break;
					case 2: // Tangent
						bc_wf32 (&bb, o + 0, tg.x);
						bc_wf32 (&bb, o + 4, tg.y);
						bc_wf32 (&bb, o + 8, tg.z);
						o += 12;
						break;
					case 7: // BoneIndex (local palette ids)
						for (unsigned j = 0; j < (unsigned)raw_els[k]; j++)
							bc_wf32 (&bb, o + (size_t)j * 4, (float)(j < nw ? loc[j] : 0));
						o += (size_t)raw_els[k] * 4;
						break;
					case 8: // BoneWeight
						for (unsigned j = 0; j < (unsigned)raw_els[k]; j++)
							bc_wf32 (&bb, o + (size_t)j * 4, j < nw ? wgt[j] : 0.0f);
						o += (size_t)raw_els[k] * 4;
						break;
					default:
						o += (size_t)raw_els[k] * 4;
						break;
				}
			}
		}
	}

	bc_buf_align (&bb, 4);
	size_t imag_sec_end = bb.size;
	size_t imag_sec_len = imag_sec_end - imag_sec_start;
	bc_w32 (&bb, imag_sec_start + 4, (uint32_t)imag_sec_len);

	// Finalize CGFX Header at 0x00
	memcpy (bb.data, "CGFX", 4);
	bc_w16 (&bb, 4, 0xFEFF);
	bc_w16 (&bb, 6, 0x0014);
	bc_w32 (&bb, 8, 0x05000000);
	bc_w32 (&bb, 0x0C, (uint32_t)imag_sec_end); // file length
	bc_w32 (&bb, 0x10, 2); // 2 sections: DATA and IMAG

	// Clean up temp arrays
	FREE (mesh_name_str);
	FREE (shape_name_str);
	FREE (mat_name_str);
	FREE (tex_name_str);
	if (bone_name_str) FREE (bone_name_str);
	if (first_child) FREE (first_child);
	if (prev_sib) FREE (prev_sib);
	if (next_sib) FREE (next_sib);
	if (bone_local) FREE (bone_local);
	if (bone_world) FREE (bone_world);
	if (bone_inv) FREE (bone_inv);
	FREE (mtob_off);
	FREE (mesh_off);
	FREE (shape_off);
	FREE (bbox_off);
	FREE (submesh_tbl_off);
	FREE (submesh_off);
	FREE (face_tbl_off);
	FREE (face_off);
	FREE (fd_tbl_off);
	FREE (fd_off);
	FREE (vb_tbl_off);
	FREE (vb_off);
	FREE (attr_tbl_off);
	FREE (attr_off);
	FREE (mesh_na);
	FREE (mesh_stride);
	FREE (mesh_pal);
	FREE (mesh_npal);
	FREE (mesh_maxinf);
	FREE (mesh_skind);
	FREE (mesh_use_u8);
	if (bone_off) FREE (bone_off);
	bc_strpool_free (&strpool);

	*out_data = bb.data;
	*out_size = bb.size;
	return 1;
}

enumError EncodeModelToBCRES (const model_t *model, const char *out_path)
{
	if (!model || !model->num_meshes || !out_path)
		return ERR_INVALID_DATA;

	uint8_t *data = NULL;
	size_t size = 0;
	if (!CreateBCRES (model, &data, &size) || !data || !size)
		return ERR_CANT_CREATE;

	enumError rc = SaveFILE (out_path, 0, true, data, (uint)size, 0);
	FREE (data);
	return rc;
}

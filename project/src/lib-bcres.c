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

// PICAAttributeName
#define CGFX_ATTR_POSITION 0
#define CGFX_ATTR_NORMAL 1
#define CGFX_ATTR_TEXCOORD0 4

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

		// Sanity gate: the declared attributes must account for exactly the
		// declared stride. A layout misread shows up here immediately.
		size_t asum = 0;
		for (unsigned i = 0; i < n_attrs; i++)
			asum += (size_t)cg_gl_size (attrs[i].fmt) * attrs[i].elements;
		if (asum != vstride)
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
		mesh->vertices = CALLOC (total_idx, sizeof (vertex_t));
		if (!mesh->positions || !mesh->normals || !mesh->texcoords || !mesh->vertices)
		{
			FREE (mesh->positions);
			FREE (mesh->normals);
			FREE (mesh->texcoords);
			FREE (mesh->vertices);
			memset (mesh, 0, sizeof (*mesh));
			continue;
		}

		bool has_nrm = false, has_uv = false;
		for (unsigned a = 0; a < n_attrs; a++)
		{
			if (attrs[a].name == CGFX_ATTR_NORMAL)
				has_nrm = true;
			else if (attrs[a].name == CGFX_ATTR_TEXCOORD0)
				has_uv = true;
		}

		size_t n = 0;
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
						}
						mesh->vertices[n].position_idx = (int)n;
						mesh->vertices[n].normal_idx = has_nrm ? (int)n : -1;
						mesh->vertices[n].texcoord_idx = has_uv ? (int)n : -1;
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
		mesh->num_vertices = n;
		out->num_meshes++;
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

int CreateBCRES (const model_t *model, uint8_t **out_data, size_t *out_size)
{
	if (!model || !model->num_meshes || !out_data || !out_size)
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
	size_t (*attr_off)[3] = CALLOC (n_mesh, sizeof (*attr_off));

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
		bc_w32 (&bb, submesh_off[m] + 0x00, has_skeleton ? 2 : 0);
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
		bc_w32 (&bb, fd_off[m] + 0x00, 0x1403); // GL_UNSIGNED_SHORT_
		bc_w32 (&bb, fd_off[m] + 0x04, 0x00000100);
		uint32_t total_idx = (uint32_t)mesh->num_vertices;
		bc_w32 (&bb, fd_off[m] + 0x08, total_idx * 2); // ilen

		// VertexBuffer table & VertexBuffer
		vb_tbl_off[m] = bb.size;
		bc_buf_reserve (&bb, 4);
		bc_rel_ptr (&bb, so + 0x3C, vb_tbl_off[m]);

		vb_off[m] = bb.size;
		bc_buf_reserve (&bb, 0x30);
		bc_rel_ptr (&bb, vb_tbl_off[m], vb_off[m]);
		bc_w32 (&bb, vb_off[m] + 0x00, 0x40000002); // CGFX_TC_INTERLEAVED
		bc_w32 (&bb, vb_off[m] + 0x14, total_idx * 32); // rawlen
		bc_w32 (&bb, vb_off[m] + 0x24, 32); // vstride = 32
		bc_w32 (&bb, vb_off[m] + 0x28, 3); // na = 3

		// Attribute table & Attributes
		attr_tbl_off[m] = bb.size;
		bc_buf_reserve (&bb, 3 * 4);
		bc_rel_ptr (&bb, vb_off[m] + 0x2C, attr_tbl_off[m]);

		for (int a = 0; a < 3; a++)
		{
			attr_off[m][a] = bb.size;
			bc_buf_reserve (&bb, 0x34);
			bc_rel_ptr (&bb, attr_tbl_off[m] + a * 4, attr_off[m][a]);

			size_t ao = attr_off[m][a];
			bc_w32 (&bb, ao + 0x00, 0x40000001); // CGFX_TC_ATTRIBUTE
			bc_w32 (&bb, ao + 0x24, 0x1406); // GL_FLOAT
			bc_wf32 (&bb, ao + 0x2C, 1.0f); // scale = 1.0f
			if (a == 0)
			{
				bc_w32 (&bb, ao + 0x04, 0); // Position
				bc_w32 (&bb, ao + 0x28, 3); // elements
				bc_w32 (&bb, ao + 0x30, 0); // offset
			}
			else if (a == 1)
			{
				bc_w32 (&bb, ao + 0x04, 1); // Normal
				bc_w32 (&bb, ao + 0x28, 3); // elements
				bc_w32 (&bb, ao + 0x30, 12); // offset
			}
			else
			{
				bc_w32 (&bb, ao + 0x04, 4); // TexCoord0
				bc_w32 (&bb, ao + 0x28, 2); // elements
				bc_w32 (&bb, ao + 0x30, 24); // offset
			}
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

		// Raw index buffer
		bc_buf_align (&bb, 4);
		size_t raw_idx_off = bb.size;
		bc_rel_ptr (&bb, fd_off[m] + 0x0C, raw_idx_off);

		size_t idx_bytes = total_idx * 2;
		size_t ipos = bc_buf_reserve (&bb, idx_bytes);
		for (uint32_t v = 0; v < total_idx; v++)
			bc_w16 (&bb, ipos + v * 2, (uint16_t)v);

		// Raw vertex buffer
		bc_buf_align (&bb, 4);
		size_t raw_vtx_off = bb.size;
		bc_rel_ptr (&bb, vb_off[m] + 0x18, raw_vtx_off);

		size_t vtx_bytes = total_idx * 32;
		size_t vpos = bc_buf_reserve (&bb, vtx_bytes);

		for (uint32_t v = 0; v < total_idx; v++)
		{
			int pi = mesh->vertices ? mesh->vertices[v].position_idx : (int)v;
			int ni = mesh->vertices ? mesh->vertices[v].normal_idx : (int)v;
			int ti = mesh->vertices ? mesh->vertices[v].texcoord_idx : (int)v;

			vec3_t p = (pi >= 0 && (size_t)pi < mesh->num_positions && mesh->positions)
				? mesh->positions[pi] : (vec3_t){ 0, 0, 0 };
			vec3_t n = (ni >= 0 && (size_t)ni < mesh->num_normals && mesh->normals)
				? mesh->normals[ni] : (vec3_t){ 0, 1.0f, 0 };
			vec2_t uv = (ti >= 0 && (size_t)ti < mesh->num_texcoords && mesh->texcoords)
				? mesh->texcoords[ti] : (vec2_t){ 0, 0 };

			bc_wf32 (&bb, vpos + v * 32 + 0, p.x);
			bc_wf32 (&bb, vpos + v * 32 + 4, p.y);
			bc_wf32 (&bb, vpos + v * 32 + 8, p.z);
			bc_wf32 (&bb, vpos + v * 32 + 12, n.x);
			bc_wf32 (&bb, vpos + v * 32 + 16, n.y);
			bc_wf32 (&bb, vpos + v * 32 + 20, n.z);
			bc_wf32 (&bb, vpos + v * 32 + 24, uv.u);
			bc_wf32 (&bb, vpos + v * 32 + 28, uv.v);
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


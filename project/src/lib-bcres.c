#include "lib-bcres.h"
#include "lib-brres-model.h"
#include "lib-nintendo.h"
#include "lib-image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

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

// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// TT Games NTT engine `.model` parser (see lib-nttmodel.h).
//
// Format (from KillzXGaming/NTT-Model-Dumper Model.cs, re-implemented):
//   file    = chunk*  (big-endian headers, little-endian payloads)
//   chunk   = u32 size | char[12] type | u32 version | payload[size-16]
//           (size excludes its own 4 bytes; next chunk at pos+size+4)
//   type ".CC4HSERHSER" = hierarchy: 3 zero-terminated strings
//   type ".CC4HSER2CSG" = scene: materials, then sub-meshes with DXTV
//     vertex buffers (attribute table + interleaved LE vertices + 16-byte
//     footer each) and a LE index list (u16/u32 + 70+4*nbuf-byte footer).
//
// Validation matches the other model parsers: every count/offset is
// bounds-checked, positions must be finite, indices must land inside the
// vertex buffer, and bad meshes are skipped rather than emitted as garbage.
// No skeleton is stored in this format, so output is always unskinned static
// geometry (positions + normals + UVs + colours + tangents).
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef __cplusplus
extern "C"
{
#endif
#include "types.h"
#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-model-glb.h"
#include "lib-nttmodel.h"
#ifdef __cplusplus
}
#endif

#define NTT_MAX_MATERIALS 4096
#define NTT_MAX_MESHES 4096
#define NTT_MAX_BUFFERS 16
#define NTT_MAX_ATTRS 32
#define NTT_MAX_VERTS (16u << 20)
#define NTT_MAX_INDICES (64u << 20)

static const char NTT_HIER[12] = ".CC4HSERHSER";
static const char NTT_CSG[12] = ".CC4HSER2CSG";

// Attribute types (Model.cs AttributeType).
enum
{
	NTT_POS = 0,
	NTT_NRM = 1,
	NTT_COL0 = 2,
	NTT_TAN = 3,
	NTT_COL1 = 4,
	NTT_UV01 = 5,
	NTT_UNK6 = 6,
	NTT_UV2 = 7,
	NTT_UNK8 = 8,
	NTT_BLEND_IDX = 9,
	NTT_BLEND_WT = 10,
	NTT_UNK11 = 11,
	NTT_LIGHT_DIR = 12,
	NTT_LIGHT_COL = 13,
};

// Attribute formats (Model.cs AttributeFormat).
enum
{
	NTT_VEC2F = 2,
	NTT_VEC3F = 3,
	NTT_VEC4F = 4,
	NTT_VEC2H = 5,
	NTT_VEC4H = 6,
	NTT_VEC4B = 7,
	NTT_VEC4BF = 8,
	NTT_COLOR4B = 9,
};

static uint ntt_stride_of (uint fmt)
{
	switch (fmt)
	{
		case NTT_VEC2F:
			return 8;
		case NTT_VEC3F:
			return 12;
		case NTT_VEC4F:
			return 16;
		case NTT_VEC2H:
			return 4;
		case NTT_VEC4H:
			return 8;
		case NTT_VEC4B:
		case NTT_VEC4BF:
		case NTT_COLOR4B:
			return 4;
		default:
			return 0;
	}
}

static float ntt_f32le (const u8 *p)
{
	float v;
	memcpy (&v, p, 4);
	return v;
}

static float ntt_half_le (u16 h)
{
	const uint sign = h >> 15, exp = (h >> 10) & 31, mant = h & 1023;
	float v;
	if (!exp)
		v = (float)mant / 16777216.0f;
	else if (exp == 31)
		v = mant ? 0.0f / 0.0f : 65504.0f;
	else
	{
		v = 1.0f + (float)mant / 1024.0f;
		int e = (int)exp - 15;
		while (e > 0)
		{
			v *= 2.0f;
			e--;
		}
		while (e < 0)
		{
			v *= 0.5f;
			e++;
		}
	}
	return sign ? -v : v;
}

static bool ntt_finite (float v)
{
	return v > -1e30f && v < 1e30f;
}

// Read one NUL-terminated string within [lo, hi). Advances *cur past the
// NUL and copies (truncated) into out when non-NULL. False when no NUL fits.
static bool ntt_zstr (const u8 *data, size_t *cur, size_t hi, char *out, size_t out_sz)
{
	size_t p = *cur;
	if (p >= hi)
		return false;
	size_t e = p;
	while (e < hi && data[e])
		e++;
	if (e >= hi)
		return false;
	if (out && out_sz)
	{
		size_t n = e - p;
		if (n >= out_sz)
			n = out_sz - 1;
		memcpy (out, data + p, n);
		out[n] = 0;
	}
	*cur = e + 1;
	return true;
}

// Model.cs ReadString(): two consecutive zero-terminated strings, the first
// is the value. Advances *cur past both.
static bool ntt_dstr (const u8 *data, size_t *cur, size_t hi, char *out, size_t out_sz)
{
	char dummy[1];
	if (!ntt_zstr (data, cur, hi, out, out_sz))
		return false;
	return ntt_zstr (data, cur, hi, dummy, sizeof (dummy));
}

typedef struct ntt_attr_t
{
	u8 type;
	u8 format;
	u8 offset;
} ntt_attr_t;

// Decode one LE attribute element to out[4]. Vec4Byte/Color4Byte stay raw
// (0..255); callers normalize colours. False on unknown format.
static bool ntt_decode_attr (float out[4], const u8 *p, uint fmt)
{
	out[0] = out[1] = out[2] = out[3] = 0.0f;
	switch (fmt)
	{
		case NTT_VEC2F:
			out[0] = ntt_f32le (p);
			out[1] = ntt_f32le (p + 4);
			return true;
		case NTT_VEC3F:
			out[0] = ntt_f32le (p);
			out[1] = ntt_f32le (p + 4);
			out[2] = ntt_f32le (p + 8);
			return true;
		case NTT_VEC4F:
			out[0] = ntt_f32le (p);
			out[1] = ntt_f32le (p + 4);
			out[2] = ntt_f32le (p + 8);
			out[3] = ntt_f32le (p + 12);
			return true;
		case NTT_VEC2H:
			out[0] = ntt_half_le (rd_le16 (p));
			out[1] = ntt_half_le (rd_le16 (p + 2));
			return true;
		case NTT_VEC4H:
			out[0] = ntt_half_le (rd_le16 (p));
			out[1] = ntt_half_le (rd_le16 (p + 2));
			out[2] = ntt_half_le (rd_le16 (p + 4));
			out[3] = ntt_half_le (rd_le16 (p + 6));
			return true;
		case NTT_VEC4B:
		case NTT_COLOR4B:
			out[0] = p[0];
			out[1] = p[1];
			out[2] = p[2];
			out[3] = p[3];
			return true;
		case NTT_VEC4BF:
			out[0] = p[0] / 255.0f;
			out[1] = p[1] / 255.0f;
			out[2] = p[2] / 255.0f;
			out[3] = p[3] / 255.0f;
			return true;
		default:
			return false;
	}
}

// Locate the chunk at pos. On success sets *chunk_size, *kind (0=hierarchy,
// 1=scene, -1=other) and the payload range. False when truncated/insane.
static bool ntt_chunk_at (const u8 *data, size_t size, size_t pos,
	u32 *chunk_size, int *kind, size_t *pay_off, size_t *pay_end)
{
	if (pos + 20 > size)
		return false;
	const u32 cs = rd_be32 (data + pos);
	if (cs < 16 || (u64)pos + 4 + (u64)cs > size)
		return false;
	int k = -1;
	if (!memcmp (data + pos + 4, NTT_HIER, 12))
		k = 0;
	else if (!memcmp (data + pos + 4, NTT_CSG, 12))
		k = 1;
	if (chunk_size)
		*chunk_size = cs;
	if (kind)
		*kind = k;
	if (pay_off)
		*pay_off = pos + 20;
	if (pay_end)
		*pay_end = pos + 4 + cs;
	return true;
}

// Validate one buffer header at *cur (DXTV magic, attribute table) without
// consuming vertex data. Fills attrs/strides, advances *cur past the table.
static bool ntt_scan_buffer_head (const u8 *data, size_t *cur, size_t hi,
	ntt_attr_t *attrs, uint *n_attrs, uint *stride)
{
	if (*cur + 12 > hi || memcmp (data + *cur, "DXTV", 4))
		return false;
	const u32 na = rd_be32 (data + *cur + 8);
	if (!na || na > NTT_MAX_ATTRS)
		return false;
	if (*cur + 12 + (size_t)na * 3 + 6 > hi)
		return false;
	uint st = 0;
	for (uint i = 0; i < na; i++)
	{
		const u8 ty = data[*cur + 12 + i * 3];
		const u8 fm = data[*cur + 12 + i * 3 + 1];
		const u8 of = data[*cur + 12 + i * 3 + 2];
		if (ty > NTT_LIGHT_COL)
			return false;
		const uint s = ntt_stride_of (fm);
		if (!s)
			return false;
		if (attrs)
		{
			attrs[i].type = ty;
			attrs[i].format = fm;
			attrs[i].offset = of;
		}
		st += s;
		(void)of;
	}
	if (!st || st > 256)
		return false;
	// Every attribute's span must sit inside the stride.
	for (uint i = 0; i < na; i++)
	{
		const u8 fm = attrs ? attrs[i].format : data[*cur + 12 + i * 3 + 1];
		const u8 of = attrs ? attrs[i].offset : data[*cur + 12 + i * 3 + 2];
		if ((uint)of + ntt_stride_of (fm) > st)
			return false;
	}
	*cur += 12 + (size_t)na * 3 + 6;
	if (n_attrs)
		*n_attrs = na;
	if (stride)
		*stride = st;
	return true;
}

// Structure-only validation of one scene payload (no allocation).
static bool ntt_scan_csg (const u8 *data, size_t off, size_t end)
{
	size_t cur = off;
	if (cur + 8 > end)
		return false;
	const u32 nmats = rd_be32 (data + cur);
	if (nmats > NTT_MAX_MATERIALS)
		return false;
	cur += 8; // numMaterials + unk/padding
	for (uint i = 0; i < nmats; i++)
	{
		char dummy[1];
		// FilePath + Name, each a double string (Model.cs ReadString x2).
		if (!ntt_zstr (data, &cur, end, dummy, sizeof (dummy)))
			return false;
		if (!ntt_zstr (data, &cur, end, dummy, sizeof (dummy)))
			return false;
		if (!ntt_zstr (data, &cur, end, dummy, sizeof (dummy)))
			return false;
		if (!ntt_zstr (data, &cur, end, dummy, sizeof (dummy)))
			return false;
		if (cur + 3 > end)
			return false;
		cur += 3;
	}
	if (cur + 12 > end)
		return false;
	cur += 4; // padding
	const u32 nmeshes = rd_be32 (data + cur);
	cur += 4;
	cur += 4; // trailing 1
	if (!nmeshes || nmeshes > NTT_MAX_MESHES)
		return false;
	for (uint m = 0; m < nmeshes; m++)
	{
		if (cur + 24 > end)
			return false;
		const u32 nbuf = rd_be32 (data + cur + 8);
		const u32 nverts = rd_be32 (data + cur + 20);
		if (!nbuf || nbuf > NTT_MAX_BUFFERS || !nverts || nverts > NTT_MAX_VERTS)
			return false;
		cur += 24;
		for (uint b = 0; b < nbuf; b++)
		{
			uint na = 0, st = 0;
			if (!ntt_scan_buffer_head (data, &cur, end, 0, &na, &st))
				return false;
			if ((u64)st * (u64)nverts + 16 > (u64)(end - cur))
				return false;
			cur += (size_t)st * nverts + 16;
		}
		if (cur + 8 > end)
			return false;
		const u32 nidx = rd_be32 (data + cur);
		const u32 ifmt = rd_be32 (data + cur + 4);
		if (ifmt != 2 && ifmt != 4)
			return false;
		if (nidx < 3 || nidx > NTT_MAX_INDICES)
			return false;
		const uint esz = ifmt == 2 ? 2 : 4;
		if ((u64)nidx * esz > (u64)(end - (cur + 8)))
			return false;
		cur += 8 + (size_t)nidx * esz;
		// Per-mesh footer: 70 bytes + 4 per buffer. nbuf validated above.
		if (cur + 70 + (size_t)nbuf * 4 > end)
			return false;
		cur += 70 + (size_t)nbuf * 4;
	}
	return cur <= end;
}

bool IsTTModel (const u8 *data, size_t size)
{
	if (!data || size < 20)
		return false;
	size_t pos = 0;
	bool found = false;
	while (pos < size)
	{
		u32 cs = 0;
		int kind = -1;
		size_t off = 0, end = 0;
		if (!ntt_chunk_at (data, size, pos, &cs, &kind, &off, &end))
			return false;
		if (kind == 0)
		{
			size_t cur = off;
			char dummy[1];
			if (!ntt_zstr (data, &cur, end, dummy, sizeof (dummy))
				|| !ntt_dstr (data, &cur, end, dummy, sizeof (dummy)))
				return false;
		}
		else if (kind == 1)
		{
			if (!ntt_scan_csg (data, off, end))
				return false;
			found = true;
		}
		pos += 4 + cs;
	}
	return found && pos == size;
}

static void ntt_basename (char out[64], const char *path)
{
	const char *s = strrchr (path, '/');
	const char *b = strrchr (path, '\\');
	if (b && (!s || b > s))
		s = b;
	s = s ? s + 1 : path;
	snprintf (out, 64, "%s", s);
}

static bool ntt_grow_meshes (model_t *model, size_t *cap)
{
	if (model->num_meshes < *cap)
		return true;
	const size_t next = *cap ? *cap * 2 : 16;
	void *mem = REALLOC (model->meshes, next * sizeof (*model->meshes));
	if (!mem)
		return false;
	model->meshes = mem;
	memset (model->meshes + *cap, 0, (next - *cap) * sizeof (*model->meshes));
	*cap = next;
	return true;
}

static bool ntt_grow_materials (model_t *model, size_t *cap)
{
	if (model->num_materials < *cap)
		return true;
	const size_t next = *cap ? *cap * 2 : 16;
	void *mem = REALLOC (model->materials, next * sizeof (*model->materials));
	if (!mem)
		return false;
	model->materials = mem;
	memset (model->materials + *cap, 0, (next - *cap) * sizeof (*model->materials));
	*cap = next;
	return true;
}

// Decode one buffer's vertices at explicit base VBASE into the preallocated
// attribute arrays. Returns false when an attribute fails to decode.
static bool ntt_decode_one_buffer (const u8 *data, size_t vbase,
	ntt_attr_t *attrs, uint na, uint st, uint nverts,
	vec3_t *pos, vec3_t *nrm, vec2_t *uv0, vec2_t *uv1,
	color4_t *col0, color4_t *col1, vec3_t *tan,
	bool have_pos, bool have_nrm, bool have_uv0, bool have_uv1,
	bool have_col0, bool have_col1, bool have_tan, bool *uv_ok)
{
	for (uint v = 0; v < nverts; v++)
	{
		for (uint i = 0; i < na; i++)
		{
			float f[4];
			const u8 *q = data + vbase + (size_t)st * v + attrs[i].offset;
			if (!ntt_decode_attr (f, q, attrs[i].format))
				return false;
			switch (attrs[i].type)
			{
				case NTT_POS:
					if (have_pos)
					{
						pos[v].x = f[0];
						pos[v].y = f[1];
						pos[v].z = f[2];
					}
					break;
				case NTT_NRM:
					if (have_nrm)
					{
						nrm[v].x = f[0];
						nrm[v].y = f[1];
						nrm[v].z = f[2];
					}
					break;
				case NTT_UV01:
					if (have_uv0)
					{
						if (!ntt_finite (f[0]) || !ntt_finite (f[1]))
							*uv_ok = false;
						else
						{
							uv0[v].u = f[0];
							uv0[v].v = f[1];
						}
					}
					break;
				case NTT_UV2:
					if (have_uv1)
					{
						if (!ntt_finite (f[0]) || !ntt_finite (f[1]))
							*uv_ok = false;
						else
						{
							uv1[v].u = f[0];
							uv1[v].v = f[1];
						}
					}
					break;
				case NTT_COL0:
					if (have_col0)
					{
						if (attrs[i].format == NTT_VEC4BF)
						{
							col0[v].r = f[0];
							col0[v].g = f[1];
							col0[v].b = f[2];
							col0[v].a = f[3];
						}
						else
						{
							col0[v].r = f[0] / 255.0f;
							col0[v].g = f[1] / 255.0f;
							col0[v].b = f[2] / 255.0f;
							col0[v].a = f[3] / 255.0f;
						}
					}
					break;
				case NTT_COL1:
					if (have_col1)
					{
						if (attrs[i].format == NTT_VEC4BF)
						{
							col1[v].r = f[0];
							col1[v].g = f[1];
							col1[v].b = f[2];
							col1[v].a = f[3];
						}
						else
						{
							col1[v].r = f[0] / 255.0f;
							col1[v].g = f[1] / 255.0f;
							col1[v].b = f[2] / 255.0f;
							col1[v].a = f[3] / 255.0f;
						}
					}
					break;
				case NTT_TAN:
					if (have_tan)
					{
						tan[v].x = f[0];
						tan[v].y = f[1];
						tan[v].z = f[2];
					}
					break;
				default:
					break;
			}
		}
	}
	return true;
}

// Positions must exist and be finite; normals only need finiteness.
static bool ntt_check_pos_nrm (vec3_t *pos, vec3_t *nrm,
	uint nverts, bool have_pos, bool have_nrm)
{
	if (!have_pos)
		return false;
	for (uint v = 0; v < nverts; v++)
		if (!ntt_finite (pos[v].x) || !ntt_finite (pos[v].y) || !ntt_finite (pos[v].z))
			return false;
	if (have_nrm)
		for (uint v = 0; v < nverts; v++)
			if (!ntt_finite (nrm[v].x) || !ntt_finite (nrm[v].y) || !ntt_finite (nrm[v].z))
				return false;
	return true;
}

model_t *ParseTTModel (const u8 *data, size_t size)
{
	if (!IsTTModel (data, size))
		return 0;

	model_t *model = CALLOC (1, sizeof (*model));
	if (!model)
		return 0;
	size_t mesh_cap = 0, mat_cap = 0;
	uint mesh_seq = 0;

	size_t pos = 0;
	while (pos < size)
	{
		u32 cs = 0;
		int kind = -1;
		size_t off = 0, end = 0;
		if (!ntt_chunk_at (data, size, pos, &cs, &kind, &off, &end))
			break;
		pos += 4 + cs;
		if (kind != 1)
			continue;

		size_t cur = off;
		const u32 nmats = rd_be32 (data + cur);
		cur += 8;
		for (uint i = 0; i < nmats; i++)
		{
			char fpath[256] = "", mname[64] = "";
			char second[1];
			if (!ntt_zstr (data, &cur, end, fpath, sizeof (fpath))
				|| !ntt_zstr (data, &cur, end, second, sizeof (second))
				|| !ntt_zstr (data, &cur, end, mname, sizeof (mname))
				|| !ntt_zstr (data, &cur, end, second, sizeof (second)))
				goto fail;
			if (cur + 3 > end)
				goto fail;
			cur += 3;
			if (!ntt_grow_materials (model, &mat_cap))
				goto fail;
			material_t *mat = model->materials + model->num_materials++;
			snprintf (mat->name, sizeof (mat->name), "%s", mname);
			if (fpath[0])
			{
				ntt_basename (mat->textures[0], fpath);
				mat->num_textures = 1;
			}
		}
		cur += 4; // padding
		const u32 nmeshes = rd_be32 (data + cur);
		cur += 8; // numMeshes + trailing 1

		for (uint m = 0; m < nmeshes; m++)
		{
			const u32 nbuf = rd_be32 (data + cur + 8);
			const u32 nverts = rd_be32 (data + cur + 20);
			cur += 24;

			// Buffers are interleaved header+data (header, LE vertices,
			// 16-byte footer each), so walk them in order: record each
			// table plus its vertex-data offset, skipping the data for
			// now. Tables are small (<=16 buffers x 32 attrs).
			ntt_attr_t attrs[NTT_MAX_BUFFERS * NTT_MAX_ATTRS];
			uint nas[NTT_MAX_BUFFERS];
			uint strides[NTT_MAX_BUFFERS];
			size_t voffs[NTT_MAX_BUFFERS];
			bool ok = true;
			for (uint b = 0; ok && b < nbuf; b++)
			{
				uint na = 0, st = 0;
				size_t head = cur;
				if (!ntt_scan_buffer_head (data, &head, end,
						attrs + b * NTT_MAX_ATTRS, &na, &st))
					ok = false;
				else if ((u64)st * (u64)nverts + 16 > (u64)(end - head))
					ok = false;
				else
				{
					nas[b] = na;
					strides[b] = st;
					voffs[b] = head;
					cur = head + (size_t)st * nverts + 16;
				}
			}
			if (!ok)
			{
				// Resync is impossible without the table sizes; the whole
				// file already passed IsTTModel, so this is unreachable.
				goto fail;
			}

			bool have_pos = false, have_nrm = false, have_uv0 = false;
			bool have_uv1 = false, have_col0 = false, have_col1 = false;
			bool have_tan = false;
			for (uint b = 0; b < nbuf; b++)
				for (uint i = 0; i < nas[b]; i++)
					switch ((attrs + b * NTT_MAX_ATTRS)[i].type)
					{
						case NTT_POS:
							have_pos = true;
							break;
						case NTT_NRM:
							have_nrm = true;
							break;
						case NTT_UV01:
							have_uv0 = true;
							break;
						case NTT_UV2:
							have_uv1 = true;
							break;
						case NTT_COL0:
							have_col0 = true;
							break;
						case NTT_COL1:
							have_col1 = true;
							break;
						case NTT_TAN:
							have_tan = true;
							break;
						default:
							break;
					}

			vec3_t *vpos = have_pos ? CALLOC (nverts, sizeof (*vpos)) : 0;
			vec3_t *vnrm = have_nrm ? CALLOC (nverts, sizeof (*vnrm)) : 0;
			vec2_t *vuv0 = have_uv0 ? CALLOC (nverts, sizeof (*vuv0)) : 0;
			vec2_t *vuv1 = have_uv1 ? CALLOC (nverts, sizeof (*vuv1)) : 0;
			color4_t *vcol0 = have_col0 ? CALLOC (nverts, sizeof (*vcol0)) : 0;
			color4_t *vcol1 = have_col1 ? CALLOC (nverts, sizeof (*vcol1)) : 0;
			vec3_t *vtan = have_tan ? CALLOC (nverts, sizeof (*vtan)) : 0;
			if ((have_pos && !vpos) || (have_nrm && !vnrm) || (have_uv0 && !vuv0)
				|| (have_uv1 && !vuv1) || (have_col0 && !vcol0)
				|| (have_col1 && !vcol1) || (have_tan && !vtan))
			{
				FREE (vpos);
				FREE (vnrm);
				FREE (vuv0);
				FREE (vuv1);
				FREE (vcol0);
				FREE (vcol1);
				FREE (vtan);
				goto fail;
			}

			bool uv_ok = true;
			bool dec_ok = true;
			for (uint b = 0; dec_ok && b < nbuf; b++)
				dec_ok = ntt_decode_one_buffer (data, voffs[b],
					attrs + b * NTT_MAX_ATTRS, nas[b], strides[b], nverts,
					vpos, vnrm, vuv0, vuv1, vcol0, vcol1, vtan,
					have_pos, have_nrm, have_uv0, have_uv1,
					have_col0, have_col1, have_tan, &uv_ok);
			if (dec_ok)
				dec_ok = ntt_check_pos_nrm (vpos, vnrm, nverts, have_pos, have_nrm);

			if (cur + 8 > end)
				dec_ok = false;
			u32 nidx = 0, ifmt = 0;
			const u8 *idxp = 0;
			uint esz = 0;
			if (dec_ok)
			{
				nidx = rd_be32 (data + cur);
				ifmt = rd_be32 (data + cur + 4);
				esz = ifmt == 2 ? 2 : ifmt == 4 ? 4 : 0;
				if (!esz || nidx < 3 || nidx > NTT_MAX_INDICES
					|| (u64)nidx * esz > (u64)(end - (cur + 8)))
					dec_ok = false;
				else
					idxp = data + cur + 8;
			}
			size_t after_idx = cur + 8 + (size_t)nidx * esz;
			size_t after_foot = after_idx + 70 + (size_t)nbuf * 4;
			if (dec_ok && after_foot > end)
				dec_ok = false;

			bool emit = false;
			if (dec_ok)
			{
				// Triangle list; drop a trailing partial triangle and all
				// degenerate ones, like the WMB list path.
				const uint ntri = nidx / 3;
				vertex_t *verts = CALLOC ((size_t)ntri * 3, sizeof (*verts));
				size_t nv = 0;
				if (verts)
				{
					for (uint t = 0; t < ntri; t++)
					{
						uint tri[3];
						for (uint k = 0; k < 3; k++)
						{
							const u8 *p = idxp + ((size_t)t * 3 + k) * esz;
							tri[k] = esz == 2 ? rd_le16 (p) : rd_le32 (p);
						}
						if (tri[0] >= nverts || tri[1] >= nverts || tri[2] >= nverts)
						{
							nv = 0;
							break;
						}
						if (tri[0] == tri[1] || tri[1] == tri[2] || tri[0] == tri[2])
							continue;
						for (uint k = 0; k < 3; k++)
						{
							vertex_t *vx = verts + nv++;
							vx->position_idx = (int)tri[k];
							vx->normal_idx = have_nrm ? (int)tri[k] : -1;
							vx->tangent_idx = have_tan ? (int)tri[k] : -1;
							vx->texcoord_idx = (have_uv0 && uv_ok) ? (int)tri[k] : -1;
							vx->matrix_idx = -1;
							vx->color_idx[0] = have_col0 ? (int)tri[k] : -1;
							vx->color_idx[1] = have_col1 ? (int)tri[k] : -1;
							for (uint e = 0; e < 7; e++)
								vx->extra_texcoord_idx[e] = -1;
							if (have_uv1 && uv_ok)
								vx->extra_texcoord_idx[0] = (int)tri[k];
						}
					}
				}
				if (nv)
				{
					if (!ntt_grow_meshes (model, &mesh_cap))
					{
						FREE (verts);
						FREE (vpos);
						FREE (vnrm);
						FREE (vuv0);
						FREE (vuv1);
						FREE (vcol0);
						FREE (vcol1);
						FREE (vtan);
						goto fail;
					}
					mesh_t *mesh = model->meshes + model->num_meshes;
					snprintf (mesh->name, sizeof (mesh->name), "Mesh_%u", mesh_seq);
					mesh->positions = vpos;
					mesh->num_positions = nverts;
					if (have_nrm)
					{
						mesh->normals = vnrm;
						mesh->num_normals = nverts;
					}
					else
						FREE (vnrm);
					if (have_uv0 && uv_ok)
					{
						mesh->texcoords = vuv0;
						mesh->num_texcoords = nverts;
					}
					else
						FREE (vuv0);
					if (have_uv1 && uv_ok)
					{
						mesh->extra_texcoords[0] = vuv1;
						mesh->num_extra_texcoords[0] = nverts;
					}
					else
						FREE (vuv1);
					if (have_col0)
					{
						mesh->colors[0] = vcol0;
						mesh->num_colors[0] = nverts;
					}
					else
						FREE (vcol0);
					if (have_col1)
					{
						mesh->colors[1] = vcol1;
						mesh->num_colors[1] = nverts;
					}
					else
						FREE (vcol1);
					if (have_tan)
					{
						mesh->tangents = vtan;
						mesh->num_tangents = nverts;
					}
					else
						FREE (vtan);
					mesh->vertices = verts;
					mesh->num_vertices = nv;
					mesh->material_idx = -1;
					model->num_meshes++;
					emit = true;
				}
				else
					FREE (verts);
			}
			if (!emit)
			{
				FREE (vpos);
				FREE (vnrm);
				FREE (vuv0);
				FREE (vuv1);
				FREE (vcol0);
				FREE (vcol1);
				FREE (vtan);
			}
			mesh_seq++;
			cur = after_foot <= end ? after_foot : end;
		}
	}

	if (!model->num_meshes)
		goto fail;
	return model;

fail:
	FreeModel (model);
	return 0;
}

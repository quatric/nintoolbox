// BFRES ("FRES") -- Nintendo's NintendoWare resource container.
//
// This handles the Wii U flavour (version 3.x, big endian), whose offsets
// are all *self-relative*: the stored value is added to the address of the
// field holding it. Geometry lives in FMDL -> FVTX (vertex buffers, laid out
// as interleaved GX2 attributes) and FSHP (shapes, each with a LOD carrying
// an index buffer).
//
// The Switch flavour reuses the "FRES" magic but is little endian with a
// completely different layout and keeps its textures in a separate BNTX; it
// is detected and rejected here rather than misparsed.

#include "lib-bfres.h"
#include "lib-brres-model.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern int DecodeFZIP (
	uint8_t **dest, unsigned int *dest_size, const uint8_t *src, unsigned int src_size);

typedef unsigned int uint;

static uint16_t rb16 (const uint8_t *p)
{
	return (uint16_t)p[0] << 8 | p[1];
}
static uint32_t rb32 (const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static int32_t rbs32 (const uint8_t *p)
{
	return (int32_t)rb32 (p);
}

// Half-precision float, as used by the 16-bit vertex attribute formats.
static float half_to_float (uint16_t h)
{
	const int sign = (h >> 15) & 1;
	int exp = (h >> 10) & 0x1F;
	int man = h & 0x3FF;
	float v;
	if (!exp)
		v = man ? (float)man / 16384.0f / 64.0f : 0.0f; // subnormal
	else if (exp == 31)
		v = man ? 0.0f : 1e30f; // NaN/Inf -> tame value
	else
	{
		float m = 1.0f + (float)man / 1024.0f;
		int e = exp - 15;
		v = m;
		while (e > 0)
		{
			v *= 2.0f;
			e--;
		}
		while (e < 0)
		{
			v /= 2.0f;
			e++;
		}
	}
	return sign ? -v : v;
}

// Resolves a self-relative offset stored at ADDR.
#define REL(base, addr) ((size_t)(addr) + (size_t)rbs32 ((base) + (addr)))

static const char *rel_string (const uint8_t *d, size_t size, size_t at)
{
	if (at + 4 > size)
		return NULL;
	const size_t p = REL (d, at);
	if (p >= size)
		return NULL;
	for (size_t q = p; q < size; q++)
		if (!d[q])
			return (const char *)(d + p);
	return NULL;
}

//-----------------------------------------------------------------------------
// GX2 vertex attribute formats. Only the ones that actually carry geometry
// are handled; anything else leaves the component at zero.
//-----------------------------------------------------------------------------

static int attr_read (const uint8_t *p, size_t avail, uint32_t fmt, float out[4])
{
	out[0] = out[1] = out[2] = 0.0f;
	out[3] = 1.0f;
	switch (fmt)
	{
		case 0x00000005: // 8_8 unorm
			if (avail < 2)
				return 0;
			out[0] = p[0] / 255.0f;
			out[1] = p[1] / 255.0f;
			return 1;
		case 0x00000205: // 8_8 snorm
			if (avail < 2)
				return 0;
			out[0] = (int8_t)p[0] / 127.0f;
			out[1] = (int8_t)p[1] / 127.0f;
			return 1;
		case 0x00000007: // 16_16 unorm (alias)
		case 0x00000008: // 16_16 unorm
			if (avail < 4)
				return 0;
			out[0] = rb16 (p) / 65535.0f;
			out[1] = rb16 (p + 2) / 65535.0f;
			return 1;
		case 0x00000207: // 16_16 snorm (alias)
		case 0x00000208: // 16_16 snorm
			if (avail < 4)
				return 0;
			out[0] = (int16_t)rb16 (p) / 32767.0f;
			out[1] = (int16_t)rb16 (p + 2) / 32767.0f;
			return 1;
		case 0x0000000A: // 8_8_8_8 unorm
			if (avail < 4)
				return 0;
			for (int i = 0; i < 4; i++)
				out[i] = p[i] / 255.0f;
			return 1;
		case 0x0000020A: // 8_8_8_8 snorm
			if (avail < 4)
				return 0;
			for (int i = 0; i < 4; i++)
				out[i] = (int8_t)p[i] / 127.0f;
			return 1;
		case 0x0000000B: // 10_10_10_2 unorm
		case 0x0000000C: // 10_10_10_2 unorm
		{
			if (avail < 4)
				return 0;
			const uint32_t v = rb32 (p);
			for (int i = 0; i < 3; i++)
				out[i] = (float)((v >> (i * 10)) & 0x3FF) / 1023.0f;
			out[3] = (float)((v >> 30) & 3) / 3.0f;
			return 1;
		}
		case 0x0000020B: // 10_10_10_2 snorm
		case 0x0000020C: // 10_10_10_2 snorm
		{
			if (avail < 4)
				return 0;
			const uint32_t v = rb32 (p);
			for (int i = 0; i < 3; i++)
			{
				int c = (v >> (i * 10)) & 0x3FF;
				if (c & 0x200)
					c -= 0x400;
				out[i] = (float)c / 511.0f;
			}
			int w = (v >> 30) & 3;
			if (w & 2)
				w -= 4;
			out[3] = (float)w;
			return 1;
		}
		case 0x00000806: // 16_16 float
		case 0x00000809: // 16_16 float
			if (avail < 4)
				return 0;
			out[0] = half_to_float (rb16 (p));
			out[1] = half_to_float (rb16 (p + 2));
			return 1;
		case 0x00000807: // 32 float
		case 0x00000808: // 32 float
		{
			if (avail < 4)
				return 0;
			union
			{
				uint32_t u;
				float f;
			} c;
			c.u = rb32 (p);
			out[0] = c.f;
			return 1;
		}
		case 0x0000080A: // 16_16_16_16 float
		case 0x00000811: // 16_16_16_16 float
			if (avail < 8)
				return 0;
			for (int i = 0; i < 4; i++)
				out[i] = half_to_float (rb16 (p + i * 2));
			return 1;
		case 0x0000080D: // 32_32 float
		case 0x0000080F: // 32_32 float
		{
			if (avail < 8)
				return 0;
			for (int i = 0; i < 2; i++)
			{
				union
				{
					uint32_t u;
					float f;
				} c;
				c.u = rb32 (p + i * 4);
				out[i] = c.f;
			}
			return 1;
		}
		case 0x00000813: // 32_32_32 float
		{
			if (avail < 12)
				return 0;
			for (int i = 0; i < 3; i++)
			{
				union
				{
					uint32_t u;
					float f;
				} c;
				c.u = rb32 (p + i * 4);
				out[i] = c.f;
			}
			return 1;
		}
		case 0x00000814: // 32_32_32_32 float
		{
			if (avail < 16)
				return 0;
			for (int i = 0; i < 4; i++)
			{
				union
				{
					uint32_t u;
					float f;
				} c;
				c.u = rb32 (p + i * 4);
				out[i] = c.f;
			}
			return 1;
		}
	}
	return 0;
}

//-----------------------------------------------------------------------------

typedef struct
{
	const uint8_t *pos, *nrm, *uv, *clr, *bone, *wt;
	uint32_t fmt_pos, fmt_nrm, fmt_uv, fmt_clr, fmt_bone, fmt_wt;
	uint stride_pos, stride_nrm, stride_uv, stride_clr, stride_bone, stride_wt;
	size_t avail_pos, avail_nrm, avail_uv, avail_clr, avail_bone, avail_wt;
	// Extra UV channels (_u1.._u6) and secondary color (_c1).
	const uint8_t *extra_uv[6];
	uint32_t fmt_extra_uv[6];
	uint stride_extra_uv[6];
	size_t avail_extra_uv[6];
	const uint8_t *clr1;
	uint32_t fmt_clr1;
	uint stride_clr1;
	size_t avail_clr1;
	// Tangent stream (_t0) for models that carry explicit tangent data.
	const uint8_t *tan;
	uint32_t fmt_tan;
	uint stride_tan;
	size_t avail_tan;
	uint count;
} fvtx_t;

// Reads one FVTX and locates its position/normal/uv attribute streams.
static int read_fvtx (const uint8_t *d, size_t size, size_t fv, fvtx_t *out)
{
	if (fv + 0x20 > size || memcmp (d + fv, "FVTX", 4))
		return 0;
	memset (out, 0, sizeof (*out));

	const uint n_attr = d[fv + 4], n_buf = d[fv + 5];
	out->count = rb32 (d + fv + 8);
	if (!out->count || !n_attr || !n_buf)
		return 0;

	const size_t attrs = REL (d, fv + 0x10);
	const size_t bufs = REL (d, fv + 0x18);
	if (attrs + (size_t)n_attr * 12 > size || bufs + (size_t)n_buf * 0x18 > size)
		return 0;

	for (uint i = 0; i < n_attr; i++)
	{
		const size_t a = attrs + i * 12;
		const char *name = rel_string (d, size, a);
		if (!name)
			continue;
		const uint bi = d[a + 4];
		const uint boff = rb16 (d + a + 6);
		const uint32_t fmt = rb32 (d + a + 8);
		if (bi >= n_buf)
			continue;

		const size_t b = bufs + (size_t)bi * 0x18;
		const uint32_t bsize = rb32 (d + b + 4);
		const uint stride = rb16 (d + b + 0x0c);
		const size_t data = REL (d, b + 0x14);
		if (!stride || data >= size || data + bsize > size)
			continue;
		if (boff >= stride)
			continue;

		const uint8_t *p = d + data + boff;
		const size_t avail = size - (data + boff);
		// Attribute names follow the "_p0"/"_n0"/"_u0"/"_c0"/"_t0" convention.
		if (!strncmp (name, "_p", 2) && !out->pos)
		{
			out->pos = p;
			out->fmt_pos = fmt;
			out->stride_pos = stride;
			out->avail_pos = avail;
		}
		else if (!strncmp (name, "_n", 2) && !out->nrm)
		{
			out->nrm = p;
			out->fmt_nrm = fmt;
			out->stride_nrm = stride;
			out->avail_nrm = avail;
		}
		else if (!strncmp (name, "_t", 2) && !out->tan)
		{
			out->tan = p;
			out->fmt_tan = fmt;
			out->stride_tan = stride;
			out->avail_tan = avail;
		}
		else if (name[0] == '_' && name[1] == 'u' && name[2] >= '0' && name[2] <= '6')
		{
			const uint ch = name[2] - '0';
			if (ch == 0 && !out->uv)
			{
				out->uv = p;
				out->fmt_uv = fmt;
				out->stride_uv = stride;
				out->avail_uv = avail;
			}
			else if (ch > 0 && !out->extra_uv[ch - 1])
			{
				out->extra_uv[ch - 1] = p;
				out->fmt_extra_uv[ch - 1] = fmt;
				out->stride_extra_uv[ch - 1] = stride;
				out->avail_extra_uv[ch - 1] = avail;
			}
		}
		else if (name[0] == '_' && name[1] == 'c' && name[2] >= '0' && name[2] <= '1')
		{
			const uint ch = name[2] - '0';
			if (ch == 0 && !out->clr)
			{
				out->clr = p;
				out->fmt_clr = fmt;
				out->stride_clr = stride;
				out->avail_clr = avail;
			}
			else if (ch == 1 && !out->clr1)
			{
				out->clr1 = p;
				out->fmt_clr1 = fmt;
				out->stride_clr1 = stride;
				out->avail_clr1 = avail;
			}
		}
	}
	return out->pos != NULL;
}

model_t *ParseBFRES (const uint8_t *data, size_t size)
{
	if (!data || size < 8)
		return NULL;

	if (!memcmp (data, "FZIP", 4))
	{
		uint8_t *dec = NULL;
		unsigned int dec_sz = 0;
		if (DecodeFZIP (&dec, &dec_sz, data, (unsigned int)size) == 0 && dec)
		{
			model_t *m = ParseBFRES (dec, dec_sz);
			if (!m)
				m = ParseBFRESSwitch (dec, dec_sz);
			free (dec);
			return m;
		}
	}

	if (size < 0x60 || memcmp (data, "FRES", 4))
		return NULL;

	// Wii U BFRES is big endian and version 3.x; Switch BFRES reuses the
	// magic with a different layout entirely.
	if (rb16 (data + 8) != 0xFEFF)
		return NULL;
	if (data[4] != 3)
		return NULL;

	const uint8_t *d = data;

	// Index group 0 is FMDL.
	const uint16_t n_fmdl = rb16 (d + 0x50);
	if (!n_fmdl)
		return NULL;
	const size_t grp = REL (d, 0x20);
	if (grp + 8 > size)
		return NULL;
	const uint32_t entries = rb32 (d + grp + 4);
	if (!entries || entries > 0x10000 || grp + 8 + (size_t)(entries + 1) * 16 > size)
		return NULL;

	// First model only: DAE has no multi-model concept here.
	const size_t e = grp + 8 + 16;
	const size_t m = REL (d, e + 12);
	if (m + 0x30 > size || memcmp (d + m, "FMDL", 4))
		return NULL;

	const uint16_t n_fvtx = rb16 (d + m + 0x20);
	const uint16_t n_fshp = rb16 (d + m + 0x22);
	if (!n_fvtx || !n_fshp)
		return NULL;

	const size_t fvtx_arr = REL (d, m + 0x10);
	const size_t fshp_grp = REL (d, m + 0x14);
	if (fshp_grp + 8 > size)
		return NULL;

	model_t *out = calloc (1, sizeof (model_t));
	if (!out)
		return NULL;
	out->meshes = calloc (n_fshp, sizeof (mesh_t));
	if (!out->meshes)
	{
		free (out);
		return NULL;
	}

	// FMAT materials (FMDL+0x18 index group; header layout verified against
	// mk8.tockdom.com's FMDL doc page byte-for-byte, plus KillzXGaming/
	// BfresLibrary's TextureRef.cs for the texture-ref array's [nameOffset,
	// ftexOffset] pair -- only the first texture ref per material is bound
	// (diffuse-slot heuristic; real files commonly have several ref'd
	// textures -- e.g. normal/specular -- that this fork's DAE export has
	// no material-model slot for yet). Texture *names* only, not pixel data
	// -- the actual FTEX decode-to-PNG happens in wszst.c's extraction
	// pass, so this only needs to match the names those PNGs get written
	// under (see extract_bfres_textures() in wszst.c).
	const uint16_t n_fmat = rb16 (d + m + 0x24);
	const size_t fmat_grp = REL (d, m + 0x18);
	if (n_fmat && fmat_grp + 8 <= size)
	{
		out->materials = calloc (n_fmat, sizeof (material_t));
		if (out->materials)
		{
			const uint32_t mat_entries = rb32 (d + fmat_grp + 4);
			for (uint32_t i = 0; i < mat_entries && i < n_fmat; i++)
			{
				const size_t me = fmat_grp + 8 + (size_t)(i + 1) * 16;
				if (me + 16 > size)
					break;
				const size_t fm = REL (d, me + 12);
				if (fm + 0x4C > size || memcmp (d + fm, "FMAT", 4))
					continue;

				material_t *mat = out->materials + out->num_materials++;
				const char *mname = rel_string (d, size, fm + 4);
				snprintf (
					mat->name, sizeof (mat->name), "%s", mname && *mname ? mname : "material");

				const uint8_t n_texref = d[fm + 0x11];
				if (n_texref)
				{
					const size_t texrefs = REL (d, fm + 0x28);
					// TextureRef: 8 bytes, [nameOffset:4][ftexOffset:4].
					if (texrefs + 8 <= size)
					{
						const char *tname = rel_string (d, size, texrefs);
						if (tname)
						{
							snprintf (mat->textures[0], sizeof (mat->textures[0]), "%s", tname);
							mat->texture_coord[0] = 0; // uv0
							mat->num_textures = 1;
						}
					}
				}
			}
		}
	}

	const uint32_t sh_entries = rb32 (d + fshp_grp + 4);
	for (uint32_t i = 0; i < sh_entries && i < n_fshp; i++)
	{
		const size_t se = fshp_grp + 8 + (size_t)(i + 1) * 16;
		if (se + 16 > size)
			break;
		const size_t sh = REL (d, se + 12);
		if (sh + 0x30 > size || memcmp (d + sh, "FSHP", 4))
			continue;

		const char *name = rel_string (d, size, sh + 4);
		const uint16_t vtx_index = rb16 (d + sh + 0x12); // FSHP+0x12: FVTX index
		if (vtx_index >= n_fvtx)
			continue;

		// Each FVTX is 0x20 bytes of header in the array.
		fvtx_t fvtx;
		if (!read_fvtx (d, size, fvtx_arr + (size_t)vtx_index * 0x20, &fvtx))
			continue;

		// LOD model: primitive type, index format, count, then the buffer.
		const size_t lod = REL (d, sh + 0x24);
		if (lod + 0x18 > size)
			continue;
		const uint32_t prim = rb32 (d + lod);
		const uint32_t ifmt = rb32 (d + lod + 4);
		const uint32_t icount = rb32 (d + lod + 8);
		if (prim != 4 || !icount || icount > 0x1000000)
			continue; // triangles only

		const size_t ibo = REL (d, lod + 0x14);
		if (ibo + 0x18 > size)
			continue;
		const size_t idata = REL (d, ibo + 0x14);
		// Index format 4 is 16-bit, 9 is 32-bit.
		const uint isz = ifmt == 9 ? 4 : 2;
		if (idata + (size_t)icount * isz > size)
			continue;

		mesh_t *mesh = out->meshes + out->num_meshes;
		snprintf (mesh->name, sizeof (mesh->name), "%s", name && *name ? name : "shape");
		const uint16_t fmat_idx = rb16 (d + sh + 0x0E); // FSHP+0x0E: FMAT index
		mesh->material_idx = fmat_idx < out->num_materials ? (int)fmat_idx : -1;

		mesh->positions = calloc (icount, sizeof (vec3_t));
		mesh->normals = calloc (icount, sizeof (vec3_t));
		mesh->texcoords = calloc (icount, sizeof (vec2_t));
		mesh->tangents = fvtx.tan ? calloc (icount, sizeof (vec3_t)) : NULL;
		mesh->colors[0] = fvtx.clr ? calloc (icount, sizeof (color4_t)) : NULL;
		mesh->colors[1] = fvtx.clr1 ? calloc (icount, sizeof (color4_t)) : NULL;
		mesh->vertices = calloc (icount, sizeof (vertex_t));
		{
			uint nuv = 0;
			for (uint k = 0; k < 6; k++)
				if (fvtx.extra_uv[k])
					nuv++;
			if (nuv)
				for (uint k = 0; k < 6; k++)
					if (fvtx.extra_uv[k])
						mesh->extra_texcoords[k] = calloc (icount, sizeof (vec2_t));
		}
		if (!mesh->positions || !mesh->normals || !mesh->texcoords || !mesh->vertices)
		{
			free (mesh->positions);
			free (mesh->normals);
			free (mesh->texcoords);
			free (mesh->tangents);
			free (mesh->colors[0]);
			free (mesh->colors[1]);
			for (uint k = 0; k < 6; k++)
				free (mesh->extra_texcoords[k]);
			memset (mesh, 0, sizeof (*mesh));
			continue;
		}

		uint n = 0;
		for (uint32_t k = 0; k < icount; k++)
		{
			const uint8_t *ip = d + idata + (size_t)k * isz;
			const uint32_t vi = isz == 4 ? rb32 (ip) : rb16 (ip);
			if (vi >= fvtx.count)
				continue;

			float v[4];
			if (attr_read (fvtx.pos + (size_t)vi * fvtx.stride_pos,
					fvtx.avail_pos - (size_t)vi * fvtx.stride_pos, fvtx.fmt_pos, v))
			{
				mesh->positions[n].x = v[0];
				mesh->positions[n].y = v[1];
				mesh->positions[n].z = v[2];
			}
			if (fvtx.nrm
				&& attr_read (fvtx.nrm + (size_t)vi * fvtx.stride_nrm,
					fvtx.avail_nrm - (size_t)vi * fvtx.stride_nrm, fvtx.fmt_nrm, v))
			{
				mesh->normals[n].x = v[0];
				mesh->normals[n].y = v[1];
				mesh->normals[n].z = v[2];
			}
			if (fvtx.uv
				&& attr_read (fvtx.uv + (size_t)vi * fvtx.stride_uv,
					fvtx.avail_uv - (size_t)vi * fvtx.stride_uv, fvtx.fmt_uv, v))
			{
				mesh->texcoords[n].u = v[0];
				mesh->texcoords[n].v = v[1];
			}
			if (fvtx.tan
				&& attr_read (fvtx.tan + (size_t)vi * fvtx.stride_tan,
					fvtx.avail_tan - (size_t)vi * fvtx.stride_tan, fvtx.fmt_tan, v))
			{
				mesh->tangents[n].x = v[0];
				mesh->tangents[n].y = v[1];
				mesh->tangents[n].z = v[2];
			}
			if (fvtx.clr
				&& attr_read (fvtx.clr + (size_t)vi * fvtx.stride_clr,
					fvtx.avail_clr - (size_t)vi * fvtx.stride_clr, fvtx.fmt_clr, v))
			{
				mesh->colors[0][n].r = v[0];
				mesh->colors[0][n].g = v[1];
				mesh->colors[0][n].b = v[2];
				mesh->colors[0][n].a = v[3];
			}
			if (fvtx.clr1
				&& attr_read (fvtx.clr1 + (size_t)vi * fvtx.stride_clr1,
					fvtx.avail_clr1 - (size_t)vi * fvtx.stride_clr1, fvtx.fmt_clr1, v))
			{
				mesh->colors[1][n].r = v[0];
				mesh->colors[1][n].g = v[1];
				mesh->colors[1][n].b = v[2];
				mesh->colors[1][n].a = v[3];
			}
			for (uint e = 0; e < 6; e++)
			{
				if (fvtx.extra_uv[e]
					&& attr_read (fvtx.extra_uv[e] + (size_t)vi * fvtx.stride_extra_uv[e],
						fvtx.avail_extra_uv[e] - (size_t)vi * fvtx.stride_extra_uv[e],
						fvtx.fmt_extra_uv[e], v))
				{
					mesh->extra_texcoords[e][n].u = v[0];
					mesh->extra_texcoords[e][n].v = v[1];
				}
			}

			mesh->vertices[n].position_idx = (int)n;
			mesh->vertices[n].normal_idx = fvtx.nrm ? (int)n : -1;
			mesh->vertices[n].texcoord_idx = fvtx.uv ? (int)n : -1;
			mesh->vertices[n].tangent_idx = fvtx.tan ? (int)n : -1;
			mesh->vertices[n].color_idx[0] = fvtx.clr ? (int)n : -1;
			mesh->vertices[n].color_idx[1] = fvtx.clr1 ? (int)n : -1;
			for (uint e = 0; e < 6; e++)
				mesh->vertices[n].extra_texcoord_idx[e] = fvtx.extra_uv[e] ? (int)n : -1;
			n++;
		}
		if (!n)
		{
			free (mesh->positions);
			free (mesh->normals);
			free (mesh->texcoords);
			free (mesh->tangents);
			free (mesh->colors[0]);
			free (mesh->colors[1]);
			for (uint k = 0; k < 6; k++)
				free (mesh->extra_texcoords[k]);
			free (mesh->vertices);
			memset (mesh, 0, sizeof (*mesh));
			continue;
		}
		mesh->num_positions = mesh->num_normals = mesh->num_texcoords = n;
		mesh->num_vertices = n;
		if (fvtx.tan)
			mesh->num_tangents = n;
		if (fvtx.clr)
			mesh->num_colors[0] = n;
		if (fvtx.clr1)
			mesh->num_colors[1] = n;
		for (uint e = 0; e < 6; e++)
			if (fvtx.extra_uv[e])
				mesh->num_extra_texcoords[e] = n;
		out->num_meshes++;
	}

	if (!out->num_meshes)
	{
		FreeModel (out);
		return NULL;
	}
	return out;
}

//-----------------------------------------------------------------------------
// Switch BFRES ("FRES", little endian, version-major-gated header layout).
//
// Header field offsets (name-offset, model-array, material-array, per-
// FMDL/FSHP/FVTX/FMAT prologue width) are ported from KillzXGaming's
// BfresLibrary (MIT) -- ResFileParser.Load()/ModelParser.Read()/
// ShapeParser.Read()/VertexBufferParser.Load()/Mesh.Load(), mirroring the
// already-verified name-resolution code in wszst.c's
// extract_bfres_switch_manifest(). What that code did NOT solve -- the
// vertex/index *data* location -- is solved here:
//
// - ResFileParser.Load()'s exact field sequence (hand-counted byte-by-byte
//   against the C# source, not guessed) places the `BufferInfo` pointer at
//   absolute header offset 0x90 (144) for version-major<9/<10 files (every
//   real sample seen so far). Cross-checked against the already-verified
//   `numModel` field position (0xBC for v8): counting forward from 0x90
//   through ExternalFiles/padding/StringTable/StringPoolSize lands exactly
//   on 0xBC, confirming the byte count is right, not just plausible.
// - BufferInfo's own struct is `u32 unk, u32 Size, s64 BufferOffset, u8[16]
//   padding` -- verified on a real file (AirBubble.bfres, Super Mario
//   Odyssey): `unk` read back exactly 34, BfresLibrary's own hardcoded
//   default for that field, and the bytes at `BufferOffset` decode as a
//   clean u16 triangle index list (0,1,2, 3,0,2, 0,4,1, ...).
// - Index and vertex buffer data all live in ONE pool starting at
//   `BufferOffset`: the index buffer for each Mesh at
//   `BufferOffset + FaceBufferOffset` (a local s32 in the Mesh struct), and
//   each FVTX's buffers (one buffer per attribute is common -- FVTX
//   attributes are NOT necessarily interleaved on Switch, unlike Wii U)
//   starting at `BufferOffset + (the FVTX's own local s32 offset)`,
//   8-byte-aligned, one after another, size/stride given by separate
//   per-buffer arrays. Verified end to end on the same real file: index
//   buffer (1560 * 2 bytes = 3120, matching its separately-stored Size
//   field exactly) is immediately followed with zero padding by vertex
//   buffer 0 (275 vertices * stride 12 = 3300 bytes, again matching its
//   separately-stored Size field exactly).
// - Per-attribute Format (and Mesh's PrimitiveType/IndexFormat) are stored
//   as their raw enum value but the reader temporarily swaps to
//   BIG-endian just for that one field (see VertexAttrib.Load() setting
//   `loader.ByteOrder = ByteOrder.BigEndian` around the Format read) even
//   though the rest of the file is little-endian -- confirmed by matching
//   real attribute bytes (0x05,0x18 -> big-endian 0x0518) against
//   BfresLibrary's own `SwitchAttribFormat` enum (0x0518 =
//   Format_32_32_32_Single, exactly a 3-float position attribute) rather
//   than assuming a plain little-endian read (which would give a
//   nonexistent format code).
//-----------------------------------------------------------------------------

static uint16_t le16 (const uint8_t *p)
{
	return (uint16_t)p[1] << 8 | p[0];
}
static uint32_t le32 (const uint8_t *p)
{
	return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}
static int32_t les32 (const uint8_t *p)
{
	return (int32_t)le32 (p);
}
static uint64_t le64 (const uint8_t *p)
{
	return (uint64_t)le32 (p + 4) << 32 | le32 (p);
}
static int64_t les64 (const uint8_t *p)
{
	return (int64_t)le64 (p);
}

// A handful of enum values are stored byte-order-swapped relative to the
// rest of the (little-endian) file -- see the comment above. Only the low
// 16 bits are ever non-zero on any real sample seen, so this just swaps
// the first two bytes rather than fully byte-reversing a 32-bit read.
static inline uint32_t swz16 (const uint8_t *p)
{
	return (uint32_t)p[0] << 8 | p[1];
}

static int attr_read_switch (const uint8_t *p, size_t avail, uint32_t fmt, float out[4])
{
	out[0] = out[1] = out[2] = 0.0f;
	out[3] = 1.0f;
	switch (fmt)
	{
		case 0x0112: // 16_16 unorm
			if (avail < 4)
				return 0;
			out[0] = le16 (p) / 65535.0f;
			out[1] = le16 (p + 2) / 65535.0f;
			return 1;
		case 0x0212: // 16_16 snorm
			if (avail < 4)
				return 0;
			out[0] = (int16_t)le16 (p) / 32767.0f;
			out[1] = (int16_t)le16 (p + 2) / 32767.0f;
			return 1;
		case 0x010b: // 8_8_8_8 unorm
			if (avail < 4)
				return 0;
			for (int i = 0; i < 4; i++)
				out[i] = p[i] / 255.0f;
			return 1;
		case 0x020b: // 8_8_8_8 snorm
			if (avail < 4)
				return 0;
			for (int i = 0; i < 4; i++)
				out[i] = (int8_t)p[i] / 127.0f;
			return 1;
		case 0x020e: // 10_10_10_2 snorm (verified: real normal attribute)
		{
			if (avail < 4)
				return 0;
			const uint32_t v = le32 (p);
			for (int i = 0; i < 3; i++)
			{
				int c = (v >> (i * 10)) & 0x3FF;
				if (c & 0x200)
					c -= 0x400;
				out[i] = (float)c / 511.0f;
			}
			return 1;
		}
		case 0x050a: // 16 float (single half, e.g. some scalar attribs)
			if (avail < 2)
				return 0;
			out[0] = half_to_float (le16 (p));
			return 1;
		case 0x0512: // 16_16 float (verified: real uv attribute)
			if (avail < 4)
				return 0;
			out[0] = half_to_float (le16 (p));
			out[1] = half_to_float (le16 (p + 2));
			return 1;
		case 0x0515: // 16_16_16_16 float
			if (avail < 8)
				return 0;
			for (int i = 0; i < 4; i++)
				out[i] = half_to_float (le16 (p + i * 2));
			return 1;
		case 0x0516: // 32 float
		{
			if (avail < 4)
				return 0;
			union
			{
				uint32_t u;
				float f;
			} c;
			c.u = le32 (p);
			out[0] = c.f;
			return 1;
		}
		case 0x0517: // 32_32 float
		{
			if (avail < 8)
				return 0;
			for (int i = 0; i < 2; i++)
			{
				union
				{
					uint32_t u;
					float f;
				} c;
				c.u = le32 (p + i * 4);
				out[i] = c.f;
			}
			return 1;
		}
		case 0x0518: // 32_32_32 float (verified: real position attribute)
		{
			if (avail < 12)
				return 0;
			for (int i = 0; i < 3; i++)
			{
				union
				{
					uint32_t u;
					float f;
				} c;
				c.u = le32 (p + i * 4);
				out[i] = c.f;
			}
			return 1;
		}
		case 0x0519: // 32_32_32_32 float
		{
			if (avail < 16)
				return 0;
			for (int i = 0; i < 4; i++)
			{
				union
				{
					uint32_t u;
					float f;
				} c;
				c.u = le32 (p + i * 4);
				out[i] = c.f;
			}
			return 1;
		}
	}
	return 0;
}

// Bytes consumed by the version-gated prologue before the first LoadString()
// in FMDL/FSHP/FMAT/FVTX sections -- same convention documented in wszst.c's
// bfres_switch_hdr_extra(), duplicated here to keep this file's Switch path
// self-contained.
static inline uint bfres_switch_hdr_extra (uint vmajor)
{
	return vmajor >= 9 ? 4 : 12;
}

static const char *rel_string_switch (const uint8_t *d, size_t size, int64_t off)
{
	if (off < 2 || (size_t)off + 2 > size)
		return NULL;
	const uint len = le16 (d + off);
	if ((size_t)off + 2 + len > size)
		return NULL;
	return (const char *)(d + off + 2);
}

static inline float read_le32f (const uint8_t *p)
{
	union
	{
		uint32_t u;
		float f;
	} c;
	c.u = le32 (p);
	return c.f;
}

typedef struct
{
	const uint8_t *pos, *nrm, *uv, *clr, *bone, *wt;
	uint32_t fmt_pos, fmt_nrm, fmt_uv, fmt_clr, fmt_bone, fmt_wt;
	uint stride_pos, stride_nrm, stride_uv, stride_clr, stride_bone, stride_wt;
	size_t avail_pos, avail_nrm, avail_uv, avail_clr, avail_bone, avail_wt;
	// Extra UV channels (_u1.._u6) and secondary color (_c1).
	const uint8_t *extra_uv[6];
	uint32_t fmt_extra_uv[6];
	uint stride_extra_uv[6];
	size_t avail_extra_uv[6];
	const uint8_t *clr1;
	uint32_t fmt_clr1;
	uint stride_clr1;
	size_t avail_clr1;
	const uint8_t *tan;
	uint32_t fmt_tan;
	uint stride_tan;
	size_t avail_tan;
	uint count;
} fvtx_switch_t;

// Reads unsigned 8-bit components from a Switch vertex attribute.
// Returns count of components read (1..4).
static int attr_read_uint8_switch (const uint8_t *p, size_t avail, uint32_t fmt, uint8_t out[4])
{
	out[0] = out[1] = out[2] = out[3] = 0;
	switch (fmt)
	{
		case 0x000b: // 8_8_8_8 uint
			if (avail < 4)
				return 0;
			for (int i = 0; i < 4; i++)
				out[i] = p[i];
			return 4;
		case 0x000a: // 8_8 uint
			if (avail < 2)
				return 0;
			out[0] = p[0];
			out[1] = p[1];
			return 2;
		case 0x0009: // 8 uint (scalar)
			if (avail < 1)
				return 0;
			out[0] = p[0];
			return 1;
	}
	return 0;
}

// Reads one FVTX (Switch): attribute list + one buffer per attribute
// (commonly non-interleaved, unlike Wii U), located via the shared
// BufferInfo pool base plus this FVTX's own local buffer offset.
static int read_fvtx_switch (
	const uint8_t *d, size_t size, size_t fv, uint vmajor, int64_t pool_base, fvtx_switch_t *out)
{
	memset (out, 0, sizeof (*out));
	if (fv + 0x60 > size || memcmp (d + fv, "FVTX", 4))
		return 0;

	const uint vhdr = bfres_switch_hdr_extra (vmajor);
	const int64_t attr_arr = les64 (d + fv + 4 + vhdr);
	const int64_t counts_off = fv + 4 + vhdr + 0x40;
	if ((size_t)counts_off + 16 > size)
		return 0;

	const int32_t vb_local_off = les32 (d + counts_off);
	const uint n_attr = d[counts_off + 4];
	const uint n_buf = d[counts_off + 5];
	// VertexBufferSizeOffset then VertexStrideSizeOffset are the two
	// ReadOffset() calls right before an 8-byte padding field that ends
	// exactly at counts_off (VertexBufferParser.Load()) -- so counting
	// backward from counts_off: padding(8) at counts_off-8,
	// VertexStrideSizeOffset(8) at counts_off-16, VertexBufferSizeOffset(8)
	// at counts_off-24. Verified against a real file (AirBubble.bfres):
	// these land on 2432/2480 respectively, and the values they point to
	// (stride 12/4/4, size 3300/1100/1100) match the real vertex count
	// (275) and attribute formats exactly, zero slack.
	const int64_t vtx_bufsize_off = les64 (d + counts_off - 24);
	const int64_t vtx_stride_off2 = les64 (d + counts_off - 16);
	const uint32_t vertex_count = (size_t)counts_off + 12 <= size ? le32 (d + counts_off + 8) : 0;

	if (!n_attr || !n_buf || n_buf > 8 || !vertex_count)
		return 0;
	if (attr_arr <= 0 || (size_t)attr_arr + (size_t)n_attr * 16 > size)
		return 0;
	if (vtx_bufsize_off <= 0 || vtx_stride_off2 <= 0
		|| (size_t)vtx_bufsize_off + (size_t)n_buf * 16 > size
		|| (size_t)vtx_stride_off2 + (size_t)n_buf * 16 > size)
		return 0;

	// Walk the buffer pool sequentially, 8-byte aligned, same order the
	// buffers are declared in (verified: on a real file this lands with
	// zero gap directly after the preceding index buffer).
	uint64_t bufpos[8];
	uint64_t cur = (uint64_t)pool_base + (uint32_t)vb_local_off;
	for (uint i = 0; i < n_buf; i++)
	{
		cur = (cur + 7) & ~(uint64_t)7;
		bufpos[i] = cur;
		const uint32_t bsize = le32 (d + vtx_bufsize_off + (size_t)i * 16);
		cur += bsize;
	}

	out->count = vertex_count;
	for (uint i = 0; i < n_attr; i++)
	{
		const size_t a = (size_t)attr_arr + (size_t)i * 16;
		const char *name = rel_string_switch (d, size, les64 (d + a));
		if (!name)
			continue;
		const uint32_t fmt = swz16 (d + a + 8);
		const uint boff = le16 (d + a + 12);
		const uint bi = le16 (d + a + 14);
		if (bi >= n_buf)
			continue;

		const uint stride = le32 (d + vtx_stride_off2 + (size_t)bi * 16);
		if (!stride || bufpos[bi] >= size)
			continue;
		const size_t data = (size_t)bufpos[bi];
		if (boff >= stride || data + boff >= size)
			continue;

		const uint8_t *p = d + data + boff;
		const size_t avail = size - (data + boff);
		if (!strncmp (name, "_p", 2) && !out->pos)
		{
			out->pos = p;
			out->fmt_pos = fmt;
			out->stride_pos = stride;
			out->avail_pos = avail;
		}
		else if (!strncmp (name, "_n", 2) && !out->nrm)
		{
			out->nrm = p;
			out->fmt_nrm = fmt;
			out->stride_nrm = stride;
			out->avail_nrm = avail;
		}
		else if (!strncmp (name, "_t", 2) && !out->tan)
		{
			out->tan = p;
			out->fmt_tan = fmt;
			out->stride_tan = stride;
			out->avail_tan = avail;
		}
		else if (name[0] == '_' && name[1] == 'u' && name[2] >= '0' && name[2] <= '6')
		{
			const uint ch = name[2] - '0';
			if (ch == 0 && !out->uv)
			{
				out->uv = p;
				out->fmt_uv = fmt;
				out->stride_uv = stride;
				out->avail_uv = avail;
			}
			else if (ch > 0 && !out->extra_uv[ch - 1])
			{
				out->extra_uv[ch - 1] = p;
				out->fmt_extra_uv[ch - 1] = fmt;
				out->stride_extra_uv[ch - 1] = stride;
				out->avail_extra_uv[ch - 1] = avail;
			}
		}
		else if (name[0] == '_' && name[1] == 'c' && name[2] >= '0' && name[2] <= '1')
		{
			const uint ch = name[2] - '0';
			if (ch == 0 && !out->clr)
			{
				out->clr = p;
				out->fmt_clr = fmt;
				out->stride_clr = stride;
				out->avail_clr = avail;
			}
			else if (ch == 1 && !out->clr1)
			{
				out->clr1 = p;
				out->fmt_clr1 = fmt;
				out->stride_clr1 = stride;
				out->avail_clr1 = avail;
			}
		}
		else if (!strncmp (name, "_b", 2) && !out->bone)
		{
			out->bone = p;
			out->fmt_bone = fmt;
			out->stride_bone = stride;
			out->avail_bone = avail;
		}
		else if (!strncmp (name, "_w", 2) && !out->wt)
		{
			out->wt = p;
			out->fmt_wt = fmt;
			out->stride_wt = stride;
			out->avail_wt = avail;
		}
	}
	return out->pos != NULL;
}

model_t *ParseBFRESSwitch (const uint8_t *data, size_t size)
{
	if (!data || size < 0x100 || memcmp (data, "FRES", 4))
		return NULL;
	if (le16 (data + 0x0C) != 0xFEFF)
		return NULL; // Switch BOM position/endianness

	const uint32_t version = le32 (data + 8);
	const uint vmajor = (version >> 16) & 0xFFFF;
	const uint8_t *d = data;

	const int64_t fmdl_arr = les64 (d + 0x28);
	if (fmdl_arr <= 0 || (size_t)fmdl_arr + 0x60 > size || memcmp (d + fmdl_arr, "FMDL", 4))
		return NULL;

	// BufferInfo pointer: see the long comment above this section for the
	// byte-by-byte derivation of offset 0x90 (verified against real data,
	// not assumed).
	if (size < 0x90 + 8)
		return NULL;
	const int64_t bufinfo = les64 (d + 0x90);
	if (bufinfo <= 0 || (size_t)bufinfo + 16 > size)
		return NULL;
	const int64_t pool_base = les64 (d + bufinfo + 8);
	if (pool_base <= 0 || (size_t)pool_base >= size)
		return NULL;

	const uint fhdr = bfres_switch_hdr_extra (vmajor);
	// name(8) + path(8) + skeleton(8) + vertex-buffer array(8) precede the
	// shapes-array field; materials/userdata/etc that follow it aren't
	// needed here since shapes are located by scanning for "FSHP" magics
	// below, not via a numShape count field.
	const int64_t shapes_val_field = fmdl_arr + 4 + fhdr + 32;

	if ((size_t)shapes_val_field + 8 > size)
		return NULL;
	const int64_t shapes_val = les64 (d + shapes_val_field);
	if (shapes_val <= 0 || (size_t)shapes_val + 0x60 > size || memcmp (d + shapes_val, "FSHP", 4))
		return NULL;

	// Count shapes by scanning for "FSHP" magics from the first one (same
	// approach the already-verified name-resolution manifest code uses --
	// FSHP entries aren't fixed-stride, so there's no clean array stride to
	// step through instead).
	uint n_fshp = 0;
	{
		const uint8_t *s = d + shapes_val;
		while (s && (size_t)(s - d) < size)
		{
			n_fshp++;
			s = memmem (s + 4, size - (s + 4 - d), "FSHP", 4);
		}
	}
	if (!n_fshp)
		return NULL;

	model_t *out = calloc (1, sizeof (model_t));
	if (!out)
		return NULL;
	out->meshes = calloc (n_fshp, sizeof (mesh_t));
	if (!out->meshes)
	{
		free (out);
		return NULL;
	}

	// Parse FSKL skeleton -- per Wexos's Wiki: FSKL pointer at FMDL+0x18
	// (v>=9) or FMDL+0x20 (v<9). Contains bone array + inverse bind matrices.
	{
		const int64_t fskl_off = les64 (d + fmdl_arr + 4 + fhdr + 16);
		if (fskl_off > 0 && (size_t)fskl_off + 0x40 <= size && !memcmp (d + fskl_off, "FSKL", 4))
		{
			const uint skdr = bfres_switch_hdr_extra (vmajor);
			const int64_t sk_base = fskl_off + 4 + skdr;
			// For v>=9: sk_base+0x10=bone array, sk_base+0x20=matrix, sk_base+0x38=num bones
			// For v<9:  sk_base+0x10=bone array, sk_base+0x28=matrix, sk_base+0x4C=num bones
			const int64_t bone_arr
				= (size_t)sk_base + 0x18 <= size ? les64 (d + sk_base + 0x10) : 0;
			const int64_t matrix_off
				= (size_t)sk_base + 0x28 <= size ? les64 (d + sk_base + 0x20) : 0;
			const uint16_t n_bones = vmajor >= 9
				? ((size_t)sk_base + 0x3A <= size ? le16 (d + sk_base + 0x38) : 0)
				: ((size_t)sk_base + 0x4E <= size ? le16 (d + sk_base + 0x4C) : 0);

			if (bone_arr > 0 && n_bones > 0 && n_bones < 4096)
			{
				// Bone struct size: 0x60 for v>=8, 0x48 for v<8 (use v>=8
				// since all Switch BFRES are v>=8)
				const size_t bone_stride = vmajor >= 8 ? 0x60 : 0x48;
				if ((size_t)bone_arr + n_bones * bone_stride > size)
				{ /* skip skeleton */
				}
				else
				{
					out->num_joints = n_bones;
					out->joints = calloc (n_bones, sizeof (joint_t));
					if (out->joints)
					{
						for (uint b = 0; b < n_bones; b++)
						{
							const size_t boff = (size_t)bone_arr + b * bone_stride;
							joint_t *j = out->joints + b;
							j->parent_idx = -1;

							const char *bname = rel_string_switch (d, size, les64 (d + boff));
							if (bname && *bname)
								snprintf (j->name, sizeof (j->name), "%s", bname);

							// Parent index at bone+0x2A (v>=8)
							if (vmajor >= 8 && boff + 0x2C <= size)
								j->parent_idx = (int)(int16_t)le16 (d + boff + 0x2A);

							// TRS at bone+0x38..0x5F (v>=8)
							if (vmajor >= 8 && boff + 0x60 <= size)
							{
								j->scale.x = read_le32f (d + boff + 0x38);
								j->scale.y = read_le32f (d + boff + 0x3C);
								j->scale.z = read_le32f (d + boff + 0x40);
								j->rotate.x = read_le32f (d + boff + 0x44);
								j->rotate.y = read_le32f (d + boff + 0x48);
								j->rotate.z = read_le32f (d + boff + 0x4C);
								j->translate.x = read_le32f (d + boff + 0x54);
								j->translate.y = read_le32f (d + boff + 0x58);
								j->translate.z = read_le32f (d + boff + 0x5C);
							}

							// Inverse bind matrix: 3x4 float (12 floats)
							if (matrix_off > 0)
							{
								const size_t moff = (size_t)matrix_off + b * 48;
								if (moff + 48 <= size)
								{
									for (int k = 0; k < 12; k++)
										j->inverse_bind[k] = read_le32f (d + moff + k * 4);
									j->has_inverse_bind = 1;
								}
							}
						}
					}
				}
			}
		}
	}

	// Parse FMAT materials if present so DAE materials and texture bindings resolve
	const int64_t mat_val_field = shapes_val_field + 16;
	const int64_t mat_val = (size_t)mat_val_field + 8 <= size ? les64 (d + mat_val_field) : -1;
	uint n_fmat = 0;
	if (mat_val > 0 && (size_t)mat_val + 0x20 <= size && !memcmp (d + mat_val, "FMAT", 4))
	{
		const uint8_t *m = d + mat_val;
		while (m && (size_t)(m - d) < size)
		{
			n_fmat++;
			m = memmem (m + 4, size - (m + 4 - d), "FMAT", 4);
		}
	}

	if (n_fmat)
	{
		out->materials = calloc (n_fmat, sizeof (material_t));
		if (out->materials)
		{
			int64_t mp = mat_val;
			for (uint mi = 0; mi < n_fmat && mp > 0 && (size_t)mp + 0x20 <= size; mi++)
			{
				if (memcmp (d + mp, "FMAT", 4))
					break;
				const uint mhdr = bfres_switch_hdr_extra (vmajor);
				const int64_t mp_base = mp + 4 + mhdr;
				const char *matname = rel_string_switch (d, size, les64 (d + mp_base));
				material_t *mat = out->materials + out->num_materials++;
				snprintf (mat->name, sizeof (mat->name), "%s",
					matname && *matname ? matname : "material");

				// Read all textures from this material's texture name array.
				// Per Wexos's Wiki: texture name array at mp+0x30 (v>=9) or
				// mp+0x38 (v<9); num textures at mp+0x9D (v>=9) or mp+0xAD (v<9).
				const int64_t tex_name_arr = vmajor >= 9
					? ((size_t)mp + 0x38 <= size ? les64 (d + mp + 0x30) : 0)
					: ((size_t)mp + 0x40 <= size ? les64 (d + mp + 0x38) : 0);
				const uint8_t n_tex = vmajor >= 9 ? ((size_t)mp + 0x9D < size ? d[mp + 0x9D] : 0)
												  : ((size_t)mp + 0xAD < size ? d[mp + 0xAD] : 0);
				if (tex_name_arr > 0 && n_tex > 0)
				{
					for (uint t = 0; t < n_tex && t < 8; t++)
					{
						const size_t off = (size_t)tex_name_arr + t * 8;
						if (off + 8 > size)
							break;
						const char *tname = rel_string_switch (d, size, les64 (d + off));
						if (tname && *tname)
						{
							snprintf (mat->textures[t], sizeof (mat->textures[t]), "%s", tname);
							mat->texture_coord[t] = (int)t;
							mat->num_textures = (int)(t + 1);
						}
					}
				}

				const uint8_t *next_m
					= mi + 1 < n_fmat ? memmem (d + mp + 4, size - (mp + 4), "FMAT", 4) : NULL;
				mp = next_m ? next_m - d : -1;
			}
		}
	}

	int64_t sh = shapes_val;

	// Dynamic node_influence accumulator -- one entry per unique bone-weight
	// combination across all shapes. position_node[] per mesh indexes into this.
	node_influence_t *node_inf = NULL;
	size_t n_node_inf = 0, cap_node_inf = 0;

	for (uint si = 0; si < n_fshp && sh > 0 && (size_t)sh + 0x60 <= size; si++)
	{
		if (memcmp (d + sh, "FSHP", 4))
			break;
		const uint shdr = bfres_switch_hdr_extra (vmajor);
		const int64_t sname_off = sh + 4 + shdr;
		const char *sname = rel_string_switch (d, size, les64 (d + sname_off));
		const int64_t fvtx = les64 (d + sname_off + 8);
		const int64_t mesh_arr_off_field = sname_off + 16;
		const int64_t mesh_arr = les64 (d + mesh_arr_off_field);
		// Per Wexos's Wiki: FMAT index at FSHP+0x52 (v>=9) / 0x5E (v<9).
		// Num LOD meshes at FSHP+0x5B (v>=9) / 0x67 (v<9).
		const uint8_t num_mesh = vmajor >= 9
			? ((size_t)sname_off + 0x54 <= size ? d[sname_off + 0x53] : 0)
			: ((size_t)sname_off + 0x58 <= size ? d[sname_off + 0x57] : 0);
		const uint16_t fmat_idx = vmajor >= 9
			? ((size_t)sname_off + 0x4C <= size ? le16 (d + sname_off + 0x4A) : 0)
			: ((size_t)sname_off + 0x50 <= size ? le16 (d + sname_off + 0x4E) : 0);

		// Per Wexos's Wiki: skin bone index array at FSHP+0x20 (v>=9)
		// or FSHP+0x28 (v<9) → both map to sname_off+0x18.
		// Count: FSHP+0x58 (v>=9) or FSHP+0x60 (v<9).
		const int64_t skin_bone_arr
			= (size_t)sname_off + 0x20 <= size ? les64 (d + sname_off + 0x18) : 0;
		const uint16_t n_skin_bones = vmajor >= 9
			? ((size_t)sname_off + 0x58 <= size ? le16 (d + sname_off + 0x50) : 0)
			: ((size_t)sname_off + 0x68 <= size ? le16 (d + sname_off + 0x60) : 0);

		do
		{
			if (fvtx <= 0 || !num_mesh || mesh_arr <= 0)
				break;
			fvtx_switch_t fv;
			if (!read_fvtx_switch (d, size, (size_t)fvtx, vmajor, pool_base, &fv))
				break;

			// First mesh only (LOD 0) -- same "no multi-LOD concept in a
			// plain DAE" scope ParseBFRES() already uses for Wii U.
			const int64_t mesh = mesh_arr;
			if ((size_t)mesh + 56 > size)
				break;
			const uint32_t face_off = le32 (d + mesh + 32);
			const uint32_t prim_raw = le32 (d + mesh + 36);
			const uint32_t ifmt_raw = le32 (d + mesh + 40);
			const uint32_t idx_count = le32 (d + mesh + 44);
			if (prim_raw != 3 || !idx_count || idx_count > 0x1000000)
				break; // triangles only
			const uint isz = ifmt_raw == 2 ? 4 : 2;

			const uint64_t idata = (uint64_t)pool_base + face_off;
			if (idata + (uint64_t)idx_count * isz > size)
				break;

			mesh_t *ms = out->meshes + out->num_meshes;
			snprintf (ms->name, sizeof (ms->name), "%s", sname && *sname ? sname : "shape");
			ms->material_idx = fmat_idx < out->num_materials ? (int)fmat_idx : -1;

			const int has_skin = fv.bone && fv.wt && n_skin_bones > 0 && skin_bone_arr > 0;

			ms->positions = calloc (idx_count, sizeof (vec3_t));
			ms->normals = calloc (idx_count, sizeof (vec3_t));
			ms->texcoords = calloc (idx_count, sizeof (vec2_t));
			ms->tangents = fv.tan ? calloc (idx_count, sizeof (vec3_t)) : NULL;
			ms->vertices = calloc (idx_count, sizeof (vertex_t));
			if (fv.clr)
			{
				ms->colors[0] = calloc (idx_count, sizeof (color4_t));
				ms->num_colors[0] = idx_count;
			}
			if (fv.clr1)
			{
				ms->colors[1] = calloc (idx_count, sizeof (color4_t));
				ms->num_colors[1] = idx_count;
			}
			{
				uint nuv = 0;
				for (uint kk = 0; kk < 6; kk++)
					if (fv.extra_uv[kk])
						nuv++;
				if (nuv)
					for (uint kk = 0; kk < 6; kk++)
						if (fv.extra_uv[kk])
							ms->extra_texcoords[kk] = calloc (idx_count, sizeof (vec2_t));
			}
			if (has_skin)
				ms->position_node = calloc (idx_count, sizeof (int));
			if (!ms->positions || !ms->normals || !ms->texcoords || !ms->vertices)
			{
				free (ms->positions);
				free (ms->normals);
				free (ms->texcoords);
				free (ms->tangents);
				free (ms->colors[0]);
				free (ms->colors[1]);
				for (uint kk = 0; kk < 6; kk++)
					free (ms->extra_texcoords[kk]);
				free (ms->vertices);
				free (ms->position_node);
				memset (ms, 0, sizeof (*ms));
				break;
			}

			uint n = 0;
			for (uint32_t k = 0; k < idx_count; k++)
			{
				const uint8_t *ip = d + idata + (size_t)k * isz;
				const uint32_t vi = isz == 4 ? le32 (ip) : le16 (ip);
				if (vi >= fv.count)
					continue;

				float v[4];
				if (attr_read_switch (fv.pos + (size_t)vi * fv.stride_pos,
						fv.avail_pos - (size_t)vi * fv.stride_pos, fv.fmt_pos, v))
				{
					ms->positions[n].x = v[0];
					ms->positions[n].y = v[1];
					ms->positions[n].z = v[2];
				}
				if (fv.nrm
					&& attr_read_switch (fv.nrm + (size_t)vi * fv.stride_nrm,
						fv.avail_nrm - (size_t)vi * fv.stride_nrm, fv.fmt_nrm, v))
				{
					ms->normals[n].x = v[0];
					ms->normals[n].y = v[1];
					ms->normals[n].z = v[2];
				}
				if (fv.uv
					&& attr_read_switch (fv.uv + (size_t)vi * fv.stride_uv,
						fv.avail_uv - (size_t)vi * fv.stride_uv, fv.fmt_uv, v))
				{
					ms->texcoords[n].u = v[0];
					ms->texcoords[n].v = v[1];
				}
				if (fv.tan && ms->tangents
					&& attr_read_switch (fv.tan + (size_t)vi * fv.stride_tan,
						fv.avail_tan - (size_t)vi * fv.stride_tan, fv.fmt_tan, v))
				{
					ms->tangents[n].x = v[0];
					ms->tangents[n].y = v[1];
					ms->tangents[n].z = v[2];
				}

				if (fv.clr && ms->colors[0]
					&& attr_read_switch (fv.clr + (size_t)vi * fv.stride_clr,
						fv.avail_clr - (size_t)vi * fv.stride_clr, fv.fmt_clr, v))
				{
					ms->colors[0][n].r = v[0];
					ms->colors[0][n].g = v[1];
					ms->colors[0][n].b = v[2];
					ms->colors[0][n].a = v[3];
				}
				if (fv.clr1 && ms->colors[1]
					&& attr_read_switch (fv.clr1 + (size_t)vi * fv.stride_clr1,
						fv.avail_clr1 - (size_t)vi * fv.stride_clr1, fv.fmt_clr1, v))
				{
					ms->colors[1][n].r = v[0];
					ms->colors[1][n].g = v[1];
					ms->colors[1][n].b = v[2];
					ms->colors[1][n].a = v[3];
				}
				for (uint e = 0; e < 6; e++)
				{
					if (fv.extra_uv[e] && ms->extra_texcoords[e]
						&& attr_read_switch (fv.extra_uv[e] + (size_t)vi * fv.stride_extra_uv[e],
							fv.avail_extra_uv[e] - (size_t)vi * fv.stride_extra_uv[e],
							fv.fmt_extra_uv[e], v))
					{
						ms->extra_texcoords[e][n].u = v[0];
						ms->extra_texcoords[e][n].v = v[1];
					}
				}

				ms->vertices[n].position_idx = (int)n;
				ms->vertices[n].normal_idx = fv.nrm ? (int)n : -1;
				ms->vertices[n].texcoord_idx = fv.uv ? (int)n : -1;
				ms->vertices[n].tangent_idx = fv.tan ? (int)n : -1;
				ms->vertices[n].color_idx[0] = fv.clr ? (int)n : -1;
				ms->vertices[n].color_idx[1] = fv.clr1 ? (int)n : -1;
				for (uint e = 0; e < 6; e++)
					ms->vertices[n].extra_texcoord_idx[e] = fv.extra_uv[e] ? (int)n : -1;

				// Skin bone data: read per-vertex bone indices + weights,
				// remap through skin_bone_idx table, accumulate unique
				// weight combinations as node_influence entries.
				if (has_skin && ms->position_node)
				{
					uint8_t bi[4] = { 0, 0, 0, 0 };
					float bw[4] = { 0, 0, 0, 0 };
					attr_read_uint8_switch (fv.bone + (size_t)vi * fv.stride_bone,
						fv.avail_bone - (size_t)vi * fv.stride_bone, fv.fmt_bone, bi);
					{
						float wb[4];
						if (attr_read_switch (fv.wt + (size_t)vi * fv.stride_wt,
								fv.avail_wt - (size_t)vi * fv.stride_wt, fv.fmt_wt, wb))
						{
							bw[0] = wb[0];
							bw[1] = wb[1];
							bw[2] = wb[2];
							bw[3] = wb[3];
						}
					}

					// Remap local bone indices through skin_bone_idx table
					// and normalize weights.
					influence_t weights[4];
					uint nw = 0;
					float wsum = 0;
					for (int b = 0; b < 4; b++)
					{
						if (bw[b] <= 0.0f)
							continue;
						if (bi[b] >= n_skin_bones)
							continue;
						const size_t idx_off = (size_t)skin_bone_arr + bi[b] * 2;
						if (idx_off + 2 > size)
							continue;
						const uint16_t fskl_bone = le16 (d + idx_off);
						if (fskl_bone >= out->num_joints)
							continue;
						weights[nw].bone_idx = (int)fskl_bone;
						weights[nw].weight = bw[b];
						wsum += bw[b];
						nw++;
					}
					// Normalize weights to sum to 1
					if (nw > 0 && wsum > 0.0f && wsum != 1.0f)
						for (uint i = 0; i < nw; i++)
							weights[i].weight /= wsum;

					// Find or create matching node_influence
					int ni_idx = -1;
					if (nw > 0)
					{
						// Linear scan for matching existing entry
						for (size_t ii = 0; ii < n_node_inf; ii++)
						{
							node_influence_t *ex = &node_inf[ii];
							if (ex->num_weights != nw)
								continue;
							int match = 1;
							for (uint w = 0; w < nw; w++)
							{
								if (ex->weights[w].bone_idx != weights[w].bone_idx
									|| ex->weights[w].weight != weights[w].weight)
								{
									match = 0;
									break;
								}
							}
							if (match)
							{
								ni_idx = (int)ii;
								break;
							}
						}
						// Create new entry if not found
						if (ni_idx < 0)
						{
							if (n_node_inf == cap_node_inf)
							{
								cap_node_inf = cap_node_inf ? cap_node_inf * 2 : 256;
								node_inf = realloc (node_inf, cap_node_inf * sizeof (*node_inf));
							}
							influence_t *wl = calloc (nw, sizeof (*wl));
							if (wl)
							{
								memcpy (wl, weights, nw * sizeof (*wl));
								node_inf[n_node_inf].weights = wl;
								node_inf[n_node_inf].num_weights = nw;
								ni_idx = (int)n_node_inf++;
							}
						}
					}
					ms->position_node[n] = ni_idx;
				}

				n++;
			}
			if (n)
			{
				ms->num_positions = ms->num_normals = ms->num_texcoords = n;
				ms->num_vertices = n;
				if (fv.tan)
					ms->num_tangents = n;
				if (fv.clr1)
					ms->num_colors[1] = n;
				for (uint e = 0; e < 6; e++)
					if (fv.extra_uv[e])
						ms->num_extra_texcoords[e] = n;
				out->num_meshes++;
			}
			else
			{
				free (ms->positions);
				free (ms->normals);
				free (ms->texcoords);
				free (ms->tangents);
				free (ms->vertices);
				free (ms->colors[0]);
				free (ms->colors[1]);
				for (uint kk = 0; kk < 6; kk++)
					free (ms->extra_texcoords[kk]);
				free (ms->position_node);
				memset (ms, 0, sizeof (*ms));
			}
		} while (0);

		const uint8_t *next
			= si + 1 < n_fshp ? memmem (d + sh + 4, size - (sh + 4), "FSHP", 4) : NULL;
		sh = next ? next - d : -1;
	}

	if (!out->num_meshes)
	{
		for (size_t i = 0; i < n_node_inf; i++)
			free (node_inf[i].weights);
		free (node_inf);
		FreeModel (out);
		return NULL;
	}

	// Transfer accumulated node_influences to model
	if (n_node_inf > 0 && node_inf)
	{
		out->node_influences = node_inf;
		out->num_node_influences = n_node_inf;
	}
	else
		free (node_inf);

	return out;
}

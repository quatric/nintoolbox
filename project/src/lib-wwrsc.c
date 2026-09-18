// SPDX-License-Identifier: GPL-2.0+
#include "lib-std.h"
#include "lib-wwrsc.h"
#include "lib-model-glb.h"
#include <string.h>
#include <math.h>

static inline u16 ww_be16 (const u8 *p)
{
	return (u16)((u16)p[0] << 8 | p[1]);
}

static inline s16 ww_be16s (const u8 *p)
{
	return (s16)ww_be16 (p);
}

static inline u32 ww_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static inline float ww_bef32 (const u8 *p)
{
	u32 u = ww_be32 (p);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

static inline void ww_wr16 (u8 *p, u16 v)
{
	p[0] = (u8)(v >> 8);
	p[1] = (u8)v;
}

static inline void ww_wr32 (u8 *p, u32 v)
{
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)v;
}

static inline void ww_wrf32 (u8 *p, float f)
{
	u32 u;
	memcpy (&u, &f, 4);
	ww_wr32 (p, u);
}

ccp WWRExtension (ww_rtype_t type)
{
	switch (type)
	{
		case WW_TEXTURE_CONTAINER:
			return ".tpl";
		case WW_MESSAGE:
			return ".ww_message";
		case WW_RIGGED_MODEL:
			return ".ww_rigged_model";
		case WW_STATIC_MODEL:
			return ".ww_static_model";
		case WW_SKEL_ANIMATION:
			return ".ww_skel_animation";
		case WW_ANIMATION:
			return ".ww_animation";
		case WW_MAP_MODEL:
			return ".ww_map_model";
		case WW_COLLISION:
			return ".ww_collison";
		case WW_SPECIAL_COLLISION:
			return ".ww_phys_collison";
		case WW_LIGHTING:
			return ".ww_lights";
		case WW_MAP_PARAMS:
			return ".ww_map_placements";
		case WW_SPAWNS:
			return ".ww_spawn_placements";
		default:
			return ".bin";
	}
}

//-----------------------------------------------------------------------------
///////////////		RSC container					///////////////
//-----------------------------------------------------------------------------

enumError ScanWWRSC (wwrsc_entry_t **entries, uint *n_entries, u8 unknowns[32],
	const u8 *data, uint size)
{
	if (!data || size < 0x20)
		return ERR_INVALID_DATA;
	if (unknowns)
		memcpy (unknowns, data, 0x20);
	// walk first to count
	uint n = 0, off = 0x20;
	while (1)
	{
		if ((u64)off + 32 > size)
			return ERR_INVALID_DATA;
		const uint type = ww_be32 (data + off);
		const uint sz = ww_be32 (data + off + 4);
		const uint next = ww_be32 (data + off + 8);
		if (type > 32 || (u64)off + 32 + sz > size)
			return ERR_INVALID_DATA;
		n++;
		if (n > 100000)
			return ERR_INVALID_DATA;
		if (!next)
			break;
		if (next <= off || next >= size || (next & 31))
			return ERR_INVALID_DATA;
		off = next;
	}
	if (!entries)
	{
		// counting/validation only (used by the format detector)
		if (n_entries)
			*n_entries = n;
		return ERR_OK;
	}
	if (!n_entries)
		return ERR_INVALID_DATA;
	wwrsc_entry_t *out = CALLOC (n ? n : 1, sizeof (*out));
	if (!out)
		return ERR_OUT_OF_MEMORY;
	off = 0x20;
	for (uint i = 0; i < n; i++)
	{
		const uint type = ww_be32 (data + off);
		const uint sz = ww_be32 (data + off + 4);
		const uint next = ww_be32 (data + off + 8);
		out[i].type = (ww_rtype_t)type;
		if (type == 1 || type == 10 || type == 14 || (type >= 2 && type <= 6) || type == 8
			|| type == 9 || type == 11)
			snprintf (out[i].name, sizeof (out[i].name), "Root_File%u%s", i,
				WWRExtension ((ww_rtype_t)type));
		else
			snprintf (out[i].name, sizeof (out[i].name), "Root_File%u_%u.bin", i, type);
		out[i].data = data + off + 32;
		out[i].size = sz;
		if (!next)
			break;
		off = next;
	}
	*n_entries = n;
	*entries = out;
	return ERR_OK;
}

enumError CreateWWRSC (u8 **dest, uint *dest_size, const u8 unknowns[32],
	const wwrsc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;
	uint total = 0x20;
	for (uint i = 0; i < n_entries; i++)
		total += 32 + ((entries[i].size + 31) & ~31u);
	u8 *buf = CALLOC (1, total ? total : 1);
	if (!buf)
		return ERR_OUT_OF_MEMORY;
	if (unknowns)
		memcpy (buf, unknowns, 0x20);
	uint off = 0x20;
	for (uint i = 0; i < n_entries; i++)
	{
		ww_wr32 (buf + off, (u32)entries[i].type);
		ww_wr32 (buf + off + 4, entries[i].size);
		const uint npos = off + 32 + ((entries[i].size + 31) & ~31u);
		ww_wr32 (buf + off + 8, i + 1 < n_entries ? npos : 0);
		if (entries[i].size && entries[i].data)
			memcpy (buf + off + 32, entries[i].data, entries[i].size);
		off = npos;
	}
	*dest = buf;
	*dest_size = total;
	return ERR_OK;
}

void FreeWWRSC (wwrsc_entry_t *entries, uint n_entries)
{
	(void)n_entries;
	FREE (entries);
}

//-----------------------------------------------------------------------------
///////////////		WW model						///////////////
//-----------------------------------------------------------------------------

#define WW_MAX_SECT 4096
#define WW_MAX_VERTS (8u << 20)

typedef struct
{
	u32 pos, nrm, col, uv, matcol, tex;
	u32 draw_n, draw_off, shape_n, shape_off, pack_n, pack_off;
} ww_hdr_t;

static bool ww_read_hdr (const u8 *data, uint size, ww_hdr_t *h)
{
	if (!data || size < 48)
		return false;
	memset (h, 0, sizeof (*h));
	h->pos = ww_be32 (data) * 4;
	h->nrm = ww_be32 (data + 4) * 4;
	h->col = ww_be32 (data + 8) * 4;
	h->uv = ww_be32 (data + 12) * 4;
	h->matcol = ww_be32 (data + 16) * 4;
	h->tex = ww_be32 (data + 20) * 4;
	h->draw_n = ww_be32 (data + 24);
	h->draw_off = ww_be32 (data + 28) * 4;
	h->shape_n = ww_be32 (data + 32);
	h->shape_off = ww_be32 (data + 36) * 4;
	h->pack_n = ww_be32 (data + 40);
	h->pack_off = ww_be32 (data + 44) * 4;
	// A zero draw/shape/packet table is meaningless; zero pools are fine
	// only when the matching offset is also zero (absent).
	if (!h->draw_n || !h->shape_n || !h->pack_n || h->draw_n > WW_MAX_SECT || h->shape_n > WW_MAX_SECT
		|| h->pack_n > WW_MAX_SECT)
		return false;
	if ((u64)h->draw_off + (u64)h->draw_n * 8 > size
		|| (u64)h->pack_off + (u64)h->pack_n * 12 > size)
		return false;
	// shapes are read through the draw elements; validate lazily there.
	// Pools must fit when present.
	if ((h->pos && h->pos >= size) || (h->nrm && h->nrm >= size) || (h->col && h->col >= size)
		|| (h->uv && h->uv >= size) || (h->matcol && h->matcol >= size)
		|| (h->tex && h->tex >= size) || (h->shape_off >= size))
		return false;
	return true;
}

bool IsWWModel (const u8 *data, size_t size)
{
	ww_hdr_t h;
	if (size > 0xffffffffu || !ww_read_hdr (data, (uint)size, &h))
		return false;
	// draw elements must reference fitting shapes
	for (uint i = 0; i < h.draw_n; i++)
	{
		const u8 *dp = data + h.draw_off + i * 8;
		const uint ns = ww_be32 (dp);
		const uint so = ww_be32 (dp + 4) * 4;
		if (!ns || ns > 256 || (u64)so + (u64)ns * 8 > size)
			return false;
		for (uint j = 0; j < ns; j++)
		{
			const u8 *sp = data + so + j * 8;
			const uint np = ww_be16 (sp + 2);
			const uint po = ww_be32 (sp + 4) * 4;
			if (!np || np > 256)
				return false;
			for (uint k = 0; k < np; k++)
			{
				if ((u64)po + (u64)k * 12 + 12 > size)
					return false;
				const u8 *pp = data + po + k * 12;
				const uint poff = ww_be32 (pp) * 4;
				const uint psz = ww_be32 (pp + 4);
				const uint f2 = pp[9]; // Offset,Size,Flags1,Flags2,...
				if (f2 == 16 || f2 == 17 || f2 == 19)
					return false; // shifted opcodes: no spec
				if (!psz || psz > (1u << 24) || (u64)poff + psz > size)
					return false;
			}
		}
	}
	return true;
}

typedef struct
{
	int pos, nrm, col, uv;
} ww_corner_t;

typedef struct
{
	ww_corner_t *v;
	size_t num, cap;
} ww_soup_t;

static bool ww_push (ww_soup_t *s, ww_corner_t c)
{
	if (s->num >= s->cap)
	{
		const size_t nc = s->cap ? s->cap * 2 : 256;
		ww_corner_t *nn = REALLOC (s->v, nc * sizeof (*nn));
		if (!nn)
			return false;
		s->v = nn;
		s->cap = nc;
	}
	s->v[s->num++] = c;
	return true;
}

static bool ww_tris (ww_soup_t *out, u8 op, const ww_corner_t *v, uint n)
{
	if (op == 0x90)
	{
		for (uint i = 0; i + 2 < n; i += 3)
			if (!ww_push (out, v[i]) || !ww_push (out, v[i + 1]) || !ww_push (out, v[i + 2]))
				return false;
		return true;
	}
	if (n < 3)
		return true;
	if (op == 0xa0)
	{
		for (uint i = 0; i < 3 && i < n; i++)
			if (!ww_push (out, v[i]))
				return false;
		for (uint i = 3; i < n; i++)
		{
			const ww_corner_t a = v[0], b = v[i - 1], d = v[i];
			if ((a.pos != b.pos || a.uv != b.uv) && (b.pos != d.pos || b.uv != d.uv)
				&& (d.pos != a.pos || d.uv != a.uv))
				if (!ww_push (out, b) || !ww_push (out, d) || !ww_push (out, a))
					return false;
		}
		return true;
	}
	for (uint i = 2; i < n; i++)
	{
		ww_corner_t v0 = (i % 2) == 0 ? v[i - 2] : v[i - 1];
		ww_corner_t v1 = (i % 2) == 0 ? v[i] : v[i - 2];
		ww_corner_t v2 = (i % 2) == 0 ? v[i - 1] : v[i];
		if ((v0.pos != v1.pos || v0.uv != v1.uv) && (v1.pos != v2.pos || v1.uv != v2.uv)
			&& (v2.pos != v0.pos || v2.uv != v0.uv))
			if (!ww_push (out, v1) || !ww_push (out, v2) || !ww_push (out, v0))
				return false;
	}
	return true;
}

// Walk one packet. Returns false on corrupt data.
static bool ww_walk_packet (const u8 *data, uint size, u32 off, u32 len, uint flags2,
	bool has_nrm, bool has_col, bool has_uv, ww_soup_t *out)
{
	if ((u64)off + len > size)
		return false;
	const bool has_pn = flags2 != 3 && flags2 != 15 && flags2 != 16 && flags2 != 14 && flags2 != 0;
	const uint ntxm = flags2 == 19 ? 3 : flags2 == 18 ? 1 : 0;
	const bool has_nbt = flags2 >= 16 || flags2 == 14 || flags2 == 15;
	const u8 *p = data + off, *end = p + len;
	while (p < end)
	{
		const u8 opc = *p++;
		if (opc == 0)
			continue;
		if (opc != 0x90 && opc != 0x98 && opc != 0xa0)
			return false;
		if (p + 2 > end)
			return false;
		const uint n = (uint)p[0] << 8 | p[1];
		p += 2;
		if (!n || n > 65536)
			return false;
		ww_corner_t *v = MALLOC (n * sizeof (*v));
		if (!v)
			return false;
		bool ok = true;
		for (uint i = 0; i < n && ok; i++)
		{
			uint need = 2 + (has_nrm ? 2 : 0) + (has_col ? 2 : 0) + (has_uv ? 2 : 0);
			if (has_pn)
				need += 1;
			need += ntxm;
			if (has_nbt)
				need += 4;
			if ((size_t)(end - p) < need)
			{
				ok = false;
				break;
			}
			if (has_pn)
				p++;
			p += (uint)ntxm;
			v[i].pos = ww_be16s (p);
			p += 2;
			v[i].nrm = -1;
			v[i].col = -1;
			v[i].uv = -1;
			if (has_nrm)
			{
				v[i].nrm = ww_be16s (p);
				p += 2;
				if (has_nbt)
					p += 4;
			}
			if (has_col)
			{
				v[i].col = ww_be16s (p);
				p += 2;
			}
			if (has_uv)
			{
				v[i].uv = ww_be16s (p);
				p += 2;
			}
		}
		if (ok)
			ok = ww_tris (out, opc, v, n);
		FREE (v);
		if (!ok)
			return false;
	}
	return true;
}

model_t *ParseWWModel (const u8 *data, size_t size)
{
	ww_hdr_t h;
	if (!IsWWModel (data, size) || !ww_read_hdr (data, (uint)size, &h))
		return 0;

	model_t *model = CALLOC (1, sizeof (*model));
	if (!model)
		return 0;

	// one joint (identity): static geometry is already model-space
	model->num_joints = 1;
	model->joints = CALLOC (1, sizeof (*model->joints));
	if (!model->joints)
	{
		FreeModel (model);
		return 0;
	}
	snprintf (model->joints[0].name, sizeof (model->joints[0].name), "Node0");
	model->joints[0].parent_idx = -1;
	model->joints[0].scale.x = model->joints[0].scale.y = model->joints[0].scale.z = 1.0f;
	model->joints[0].bind[0] = model->joints[0].bind[5] = model->joints[0].bind[10] = 1.0f;

	// count meshes = total shapes across draw elements
	uint nmeshes = 0;
	for (uint i = 0; i < h.draw_n; i++)
		nmeshes += ww_be32 (data + h.draw_off + i * 8);
	if (!nmeshes || nmeshes > WW_MAX_SECT)
	{
		FreeModel (model);
		return 0;
	}
	model->num_meshes = nmeshes;
	model->meshes = CALLOC (nmeshes ? nmeshes : 1, sizeof (*model->meshes));
	model->num_materials = nmeshes;
	model->materials = CALLOC (nmeshes ? nmeshes : 1, sizeof (*model->materials));
	if (!model->meshes || !model->materials)
	{
		FreeModel (model);
		return 0;
	}

	const bool has_nrm = h.nrm != 0, has_col = h.col != 0, has_uv = h.uv != 0;
	size_t mi = 0;
	for (uint i = 0; i < h.draw_n && mi < nmeshes; i++)
	{
		const u8 *dp = data + h.draw_off + i * 8;
		const uint ns = ww_be32 (dp);
		const uint so = ww_be32 (dp + 4) * 4;
		for (uint j = 0; j < ns && mi < nmeshes; j++)
		{
			const u8 *sp = data + so + j * 8;
			const uint np = ww_be16 (sp + 2);
			const uint po = ww_be32 (sp + 4) * 4;
			mesh_t *mesh = model->meshes + mi;
			material_t *mat = model->materials + mi;
			snprintf (mesh->name, sizeof (mesh->name), "Mesh%u", (uint)mi);
			snprintf (mat->name, sizeof (mat->name), "Material%u", (uint)mi);
			mat->diffuse[0] = mat->diffuse[1] = mat->diffuse[2] = 1.0f;
			mat->diffuse[3] = 1.0f;
			mesh->material_idx = (int)mi;
			ww_soup_t soup = { 0 };
			bool ok = true;
			for (uint k = 0; k < np && ok; k++)
			{
				const u8 *pp = data + po + k * 12;
				const uint poff = ww_be32 (pp) * 4;
				const uint psz = ww_be32 (pp + 4);
				const uint f2 = pp[9]; // Offset,Size,Flags1,Flags2,...
				const int tidx = (int8_t)pp[11];
				const int cidx = (int8_t)pp[10];
				if (tidx >= 0)
				{
					if (mat->num_textures < 8)
					{
						const int kk = mat->num_textures++;
						snprintf (mat->textures[kk], sizeof (mat->textures[kk]), "Texture%d",
							tidx);
						mat->wrap_s[kk] = mat->wrap_t[kk] = 1;
						mat->min_filter[kk] = mat->mag_filter[kk] = 1;
					}
				}
				if (cidx >= 0 && h.matcol && (u64)h.matcol + (uint)cidx * 4 + 4 <= size)
				{
					const u8 *cp = data + h.matcol + (uint)cidx * 4;
					mat->diffuse[0] = cp[0] / 255.0f;
					mat->diffuse[1] = cp[1] / 255.0f;
					mat->diffuse[2] = cp[2] / 255.0f;
					mat->diffuse[3] = cp[3] / 255.0f;
				}
				ok = ww_walk_packet (data, (uint)size, poff, psz, f2, has_nrm, has_col,
					has_uv, &soup);
			}
			if (ok)
				for (size_t c = 0; c < soup.num && ok; c++)
				{
					const ww_corner_t *cn = soup.v + c;
					if (cn->pos < 0
						|| (has_nrm && cn->nrm < 0)
						|| (has_col && cn->col < 0)
						|| (has_uv && cn->uv < 0))
						ok = false;
				}
			if (!ok || !soup.num || soup.num % 3)
			{
				FREE (soup.v);
				FreeModel (model);
				return 0;
			}
			mesh->positions = MALLOC (soup.num * sizeof (*mesh->positions));
			mesh->position_node = MALLOC (soup.num * sizeof (*mesh->position_node));
			mesh->normals = has_nrm ? MALLOC (soup.num * sizeof (*mesh->normals)) : 0;
			mesh->colors[0] = has_col ? MALLOC (soup.num * sizeof (*mesh->colors[0])) : 0;
			mesh->texcoords = has_uv ? MALLOC (soup.num * sizeof (*mesh->texcoords)) : 0;
			mesh->vertices = MALLOC (soup.num * sizeof (*mesh->vertices));
			if (!mesh->positions || !mesh->position_node || !mesh->vertices
				|| (has_nrm && !mesh->normals) || (has_col && !mesh->colors[0])
				|| (has_uv && !mesh->texcoords))
			{
				FREE (soup.v);
				FreeModel (model);
				return 0;
			}
			size_t npp = 0, nn2 = 0, nc2 = 0, nu2 = 0;
			int *vpos = MALLOC (soup.num * sizeof (*vpos));
			int *vnrm = MALLOC (soup.num * sizeof (*vnrm));
			int *vcol = MALLOC (soup.num * sizeof (*vcol));
			int *vuv = MALLOC (soup.num * sizeof (*vuv));
			if (!vpos || !vnrm || !vcol || !vuv)
			{
				FREE (vpos);
				FREE (vnrm);
				FREE (vcol);
				FREE (vuv);
				FREE (soup.v);
				FreeModel (model);
				return 0;
			}
			for (size_t c = 0; c < soup.num; c++)
			{
				const u64 po2 = (u64)h.pos + (u64)(uint)soup.v[c].pos * 6;
				if (po2 + 6 > size)
				{
					FREE (vpos);
					FREE (vnrm);
					FREE (vcol);
					FREE (vuv);
					FREE (soup.v);
					FreeModel (model);
					return 0;
				}
				float x = (float)ww_be16s (data + po2), y = (float)ww_be16s (data + po2 + 2),
					  z = (float)ww_be16s (data + po2 + 4);
				size_t f = npp;
				for (size_t k = 0; k < npp; k++)
					if (mesh->positions[k].x == x && mesh->positions[k].y == y
						&& mesh->positions[k].z == z)
					{
						f = k;
						break;
					}
				if (f == npp)
				{
					mesh->positions[npp].x = x;
					mesh->positions[npp].y = y;
					mesh->positions[npp].z = z;
					mesh->position_node[npp] = 0;
					npp++;
				}
				vpos[c] = (int)f;
				if (has_nrm)
				{
					const u64 no2 = (u64)h.nrm + (u64)(uint)soup.v[c].nrm * 6;
					if (no2 + 6 > size)
					{
						FREE (vpos);
						FREE (vnrm);
						FREE (vcol);
						FREE (vuv);
						FREE (soup.v);
						FreeModel (model);
						return 0;
					}
					float nx = (float)ww_be16s (data + no2) / 32767.0f;
					float ny = (float)ww_be16s (data + no2 + 2) / 32767.0f;
					float nz = (float)ww_be16s (data + no2 + 4) / 32767.0f;
					size_t ff = nn2;
					for (size_t k = 0; k < nn2; k++)
						if (mesh->normals[k].x == nx && mesh->normals[k].y == ny
							&& mesh->normals[k].z == nz)
						{
							ff = k;
							break;
						}
					if (ff == nn2)
					{
						mesh->normals[nn2].x = nx;
						mesh->normals[nn2].y = ny;
						mesh->normals[nn2].z = nz;
						nn2++;
					}
					vnrm[c] = (int)ff;
				}
				else
					vnrm[c] = -1;
				if (has_uv)
				{
					const u64 uo = (u64)h.uv + (u64)(uint)soup.v[c].uv * 4;
					if (uo + 4 > size)
					{
						FREE (vpos);
						FREE (vnrm);
						FREE (vcol);
						FREE (vuv);
						FREE (soup.v);
						FreeModel (model);
						return 0;
					}
					float uu = (float)ww_be16s (data + uo) / 1024.0f;
					float vv = (float)ww_be16s (data + uo + 2) / 1024.0f;
					size_t ff = nu2;
					for (size_t k = 0; k < nu2; k++)
						if (mesh->texcoords[k].u == uu && mesh->texcoords[k].v == vv)
						{
							ff = k;
							break;
						}
					if (ff == nu2)
					{
						mesh->texcoords[nu2].u = uu;
						mesh->texcoords[nu2].v = vv;
						nu2++;
					}
					vuv[c] = (int)ff;
				}
				else
					vuv[c] = -1;
				if (has_col)
				{
					const u64 co = (u64)h.col + (u64)(uint)soup.v[c].col * 2;
					if (co + 2 > size)
					{
						FREE (vpos);
						FREE (vnrm);
						FREE (vcol);
						FREE (vuv);
						FREE (soup.v);
						FreeModel (model);
						return 0;
					}
					// GX RGBA4: 4 bits each, R high nibble first
					const uint c4 = ww_be16 (data + co);
					color4_t cc = { ((c4 >> 12) & 15) / 15.0f, ((c4 >> 8) & 15) / 15.0f,
						((c4 >> 4) & 15) / 15.0f, (c4 & 15) / 15.0f };
					size_t ff = nc2;
					for (size_t k = 0; k < nc2; k++)
						if (mesh->colors[0][k].r == cc.r && mesh->colors[0][k].g == cc.g
							&& mesh->colors[0][k].b == cc.b && mesh->colors[0][k].a == cc.a)
						{
							ff = k;
							break;
						}
					if (ff == nc2)
						mesh->colors[0][nc2++] = cc;
					vcol[c] = (int)ff;
				}
				else
					vcol[c] = -1;
			}
			mesh->num_positions = npp;
			mesh->num_normals = nn2;
			if (!has_nrm)
			{
				FREE (mesh->normals);
				mesh->normals = 0;
			}
			mesh->num_colors[0] = nc2;
			if (!has_col)
			{
				FREE (mesh->colors[0]);
				mesh->colors[0] = 0;
			}
			mesh->num_texcoords = nu2;
			if (!has_uv)
			{
				FREE (mesh->texcoords);
				mesh->texcoords = 0;
			}
			mesh->num_vertices = soup.num;
			for (size_t c = 0; c < soup.num; c++)
			{
				vertex_t *vv = mesh->vertices + c;
				vv->position_idx = vpos[c];
				vv->normal_idx = vnrm[c];
				vv->tangent_idx = -1;
				vv->texcoord_idx = vuv[c];
				vv->matrix_idx = -1;
				vv->color_idx[0] = vcol[c];
				vv->color_idx[1] = -1;
				for (int k = 0; k < 7; k++)
					vv->extra_texcoord_idx[k] = -1;
			}
			FREE (vpos);
			FREE (vnrm);
			FREE (vcol);
			FREE (vuv);
			FREE (soup.v);
			mi++;
		}
	}
	return model;
}

enumError DecodeWWModel (const u8 *data, uint size, ccp out_path)
{
	if (!IsWWModel (data, size))
		return ERR_NOTHING_TO_DO;
	model_t *model = ParseWWModel (data, size);
	if (!model)
		return ERR_NOTHING_TO_DO;

	// NOTE: the embedded TPL texture container is not decoded to PNGs in
	// v1 (its table is validated structurally on the way in, and texture
	// references survive by name); material texture names keep working
	// against a TPL extracted separately.
	const int rc = ExportModelToGLB (model, out_path);
	FreeModel (model);
	return rc == 0 ? ERR_OK : ERR_CANT_CREATE;
}

// ---- encoder: canonical Flags2=3 static model ----

enumError EncodeWWModel (const model_t *model, u8 **out, uint *out_size)
{
	if (!out || !out_size || !model || !model->num_meshes)
		return ERR_INVALID_DATA;
	const size_t nm = model->num_meshes;
	if (nm > WW_MAX_SECT)
		return ERR_INVALID_DATA;

	// pools (model space; positions rounded to s16 like retail)
	typedef struct
	{
		int x, y, z;
	} ww_i3_t;
	ww_i3_t *pospool = 0;
	size_t npos = 0, cpos = 0;
	float (*nrmpool)[3] = 0;
	size_t nnrm = 0, cnrm = 0;
	u8 (*colpool)[4] = 0;
	size_t ncol = 0, ccol = 0;
	float (*uvpool)[2] = 0;
	size_t nuv = 0, cuv = 0;

	// file-level attribute presence: every packet shares one vertex layout
	// (the decoder keys field presence off pool presence).
	bool g_hn = false, g_hc = false, g_ht = false;
	for (size_t m0 = 0; m0 < nm; m0++)
	{
		if (model->meshes[m0].normals && model->meshes[m0].num_normals > 0)
			g_hn = true;
		if (model->meshes[m0].colors[0] && model->meshes[m0].num_colors[0] > 0)
			g_hc = true;
		if (model->meshes[m0].texcoords && model->meshes[m0].num_texcoords > 0)
			g_ht = true;
	}

	typedef struct
	{
		u8 *blob;
		uint len;
		uint mat;
	} ww_pack_t;
	ww_pack_t *packs = CALLOC (nm, sizeof (*packs));
	if (!packs)
		return ERR_OUT_OF_MEMORY;
	const size_t nmat = model->num_materials ? model->num_materials : 1;

	for (size_t m = 0; m < nm; m++)
	{
		const mesh_t *mesh = model->meshes + m;
		if (!mesh->num_vertices || mesh->num_vertices % 3)
		{
			FREE (pospool);
			FREE (nrmpool);
			FREE (colpool);
			FREE (uvpool);
			for (size_t k = 0; k < m; k++)
				FREE (packs[k].blob);
			FREE (packs);
			return ERR_INVALID_DATA;
		}
		packs[m].mat = mesh->material_idx >= 0 && (size_t)mesh->material_idx < nmat
			? (uint)mesh->material_idx
			: 0;
		const bool hn = g_hn, hc = g_hc, hu = g_ht;
		u8 *blob = MALLOC (3 + mesh->num_vertices * 9);
		if (!blob)
		{
			FREE (pospool);
			FREE (nrmpool);
			FREE (colpool);
			FREE (uvpool);
			for (size_t k = 0; k < m; k++)
				FREE (packs[k].blob);
			FREE (packs);
			return ERR_OUT_OF_MEMORY;
		}
		u8 *bp = blob;
		*bp++ = 0x90;
		ww_wr16 (bp, (u16)mesh->num_vertices);
		bp += 2;
		for (size_t c = 0; c < mesh->num_vertices; c++)
		{
			const vertex_t *vv = mesh->vertices + c;
			const int pi = vv->position_idx;
			float x = mesh->positions[pi].x, y = mesh->positions[pi].y, z = mesh->positions[pi].z;
			if (x > 32767)
				x = 32767;
			if (x < -32768)
				x = -32768;
			if (y > 32767)
				y = 32767;
			if (y < -32768)
				y = -32768;
			if (z > 32767)
				z = 32767;
			if (z < -32768)
				z = -32768;
			ww_i3_t P = { (int)lroundf (x), (int)lroundf (y), (int)lroundf (z) };
			size_t f = npos;
			for (size_t k = 0; k < npos; k++)
				if (pospool[k].x == P.x && pospool[k].y == P.y && pospool[k].z == P.z)
				{
					f = k;
					break;
				}
			if (f == npos)
			{
				if (npos >= cpos)
				{
					const size_t nc = cpos ? cpos * 2 : 1024;
					ww_i3_t *nn = REALLOC (pospool, nc * sizeof (*nn));
					if (!nn)
					{
						FREE (blob);
						FREE (pospool);
						FREE (nrmpool);
						FREE (colpool);
						FREE (uvpool);
						for (size_t k = 0; k < m; k++)
							FREE (packs[k].blob);
						FREE (packs);
						return ERR_OUT_OF_MEMORY;
					}
					pospool = nn;
					cpos = nc;
				}
				pospool[npos++] = P;
			}
			float nx = 0, ny = 0, nz = 0;
			if (hn && mesh->normals && vv->normal_idx >= 0
				&& (size_t)vv->normal_idx < mesh->num_normals)
			{
				nx = mesh->normals[vv->normal_idx].x;
				ny = mesh->normals[vv->normal_idx].y;
				nz = mesh->normals[vv->normal_idx].z;
			}
			size_t fn = 0;
			if (g_hn)
			{
				fn = nnrm;
				for (size_t k = 0; k < nnrm; k++)
					if (nrmpool[k][0] == nx && nrmpool[k][1] == ny && nrmpool[k][2] == nz)
					{
						fn = k;
						break;
					}
				if (fn == nnrm)
				{
					if (nnrm >= cnrm)
					{
						const size_t nc = cnrm ? cnrm * 2 : 1024;
						float(*nn)[3] = REALLOC (nrmpool, nc * sizeof (*nn));
						if (!nn)
						{
							FREE (blob);
							FREE (pospool);
							FREE (nrmpool);
							FREE (colpool);
							FREE (uvpool);
							for (size_t k = 0; k < m; k++)
								FREE (packs[k].blob);
							FREE (packs);
							return ERR_OUT_OF_MEMORY;
						}
						nrmpool = nn;
						cnrm = nc;
					}
					nrmpool[nnrm][0] = nx;
					nrmpool[nnrm][1] = ny;
					nrmpool[nnrm][2] = nz;
					nnrm++;
				}
			}
			float uu = 0, vv2 = 0;
			if (hu && mesh->texcoords && vv->texcoord_idx >= 0
				&& (size_t)vv->texcoord_idx < mesh->num_texcoords)
			{
				uu = mesh->texcoords[vv->texcoord_idx].u;
				vv2 = mesh->texcoords[vv->texcoord_idx].v;
			}
			size_t ft = 0;
			if (g_ht)
			{
				ft = nuv;
				for (size_t k = 0; k < nuv; k++)
					if (uvpool[k][0] == uu && uvpool[k][1] == vv2)
					{
						ft = k;
						break;
					}
				if (ft == nuv)
				{
					if (nuv >= cuv)
					{
						const size_t nc = cuv ? cuv * 2 : 1024;
						float(*nn)[2] = REALLOC (uvpool, nc * sizeof (*nn));
						if (!nn)
						{
							FREE (blob);
							FREE (pospool);
							FREE (nrmpool);
							FREE (colpool);
							FREE (uvpool);
							for (size_t k = 0; k < m; k++)
								FREE (packs[k].blob);
							FREE (packs);
							return ERR_OUT_OF_MEMORY;
						}
						uvpool = nn;
						cuv = nc;
					}
					uvpool[nuv][0] = uu;
					uvpool[nuv][1] = vv2;
					nuv++;
				}
			}
			u8 cc4[4] = { 255, 255, 255, 255 };
			if (hc && mesh->colors[0] && vv->color_idx[0] >= 0
				&& (size_t)vv->color_idx[0] < mesh->num_colors[0])
			{
				const color4_t *cc = mesh->colors[0] + vv->color_idx[0];
				cc4[0] = (u8)(cc->r * 255.0f);
				cc4[1] = (u8)(cc->g * 255.0f);
				cc4[2] = (u8)(cc->b * 255.0f);
				cc4[3] = (u8)(cc->a * 255.0f);
			}
			size_t fc = 0;
			if (g_hc)
			{
				fc = ncol;
				for (size_t k = 0; k < ncol; k++)
					if (!memcmp (colpool[k], cc4, 4))
					{
						fc = k;
						break;
					}
				if (fc == ncol)
				{
					if (ncol >= ccol)
					{
						const size_t nc = ccol ? ccol * 2 : 256;
						u8(*nn)[4] = REALLOC (colpool, nc * sizeof (*nn));
						if (!nn)
						{
							FREE (blob);
							FREE (pospool);
							FREE (nrmpool);
							FREE (colpool);
							FREE (uvpool);
							for (size_t k = 0; k < m; k++)
								FREE (packs[k].blob);
							FREE (packs);
							return ERR_OUT_OF_MEMORY;
						}
						colpool = nn;
						ccol = nc;
					}
					memcpy (colpool[ncol++], cc4, 4);
				}
			}
			ww_wr16 (bp, (u16)f);
			bp += 2;
			if (g_hn)
			{
				ww_wr16 (bp, (u16)fn);
				bp += 2;
			}
			if (g_hc)
			{
				ww_wr16 (bp, (u16)fc);
				bp += 2;
			}
			if (g_ht)
			{
				ww_wr16 (bp, (u16)ft);
				bp += 2;
			}
		}
		packs[m].blob = blob;
		packs[m].len = (uint)(bp - blob);
	}
	if (npos > 65535 || nnrm > 65535 || ncol > 65535 || nuv > 65535)
	{
		FREE (pospool);
		FREE (nrmpool);
		FREE (colpool);
		FREE (uvpool);
		for (size_t k = 0; k < nm; k++)
			FREE (packs[k].blob);
		FREE (packs);
		return ERR_INVALID_DATA;
	}

	// layout: header(48) + draw(8nm) + shapes(8nm) + packets(12nm) +
	// blobs + matcols(4nmat) + pools. Every section base is 4-aligned
	// because offsets are stored divided by 4.
	const uint draw_off = 48;
	const uint shape_off = draw_off + (uint)nm * 8;
	const uint pack_off = shape_off + (uint)nm * 8;
	uint cur = pack_off + (uint)nm * 12;
	uint *blob_off = MALLOC (nm * sizeof (*blob_off));
	if (!blob_off)
	{
		FREE (pospool);
		FREE (nrmpool);
		FREE (colpool);
		FREE (uvpool);
		for (size_t k = 0; k < nm; k++)
			FREE (packs[k].blob);
		FREE (packs);
		return ERR_OUT_OF_MEMORY;
	}
	for (size_t m = 0; m < nm; m++)
	{
		cur = (cur + 3) & ~3u;
		blob_off[m] = cur;
		cur += packs[m].len;
	}
	cur = (cur + 3) & ~3u;
	const uint matcol_off = cur;
	cur += (uint)nmat * 4;
	cur = (cur + 3) & ~3u;
	const uint pos_off = cur;
	cur += (uint)npos * 6;
	cur = (cur + 3) & ~3u;
	const uint nrm_off = cur;
	cur += (uint)nnrm * 6;
	cur = (cur + 3) & ~3u;
	const uint col_off = cur;
	cur += (uint)ncol * 2;
	cur = (cur + 3) & ~3u;
	const uint uv_off = cur;
	cur += (uint)nuv * 4;

	u8 *buf = CALLOC (1, cur ? cur : 1);
	if (!buf)
	{
		FREE (blob_off);
		FREE (pospool);
		FREE (nrmpool);
		FREE (colpool);
		FREE (uvpool);
		for (size_t k = 0; k < nm; k++)
			FREE (packs[k].blob);
		FREE (packs);
		return ERR_OUT_OF_MEMORY;
	}
	ww_wr32 (buf, pos_off / 4);
	ww_wr32 (buf + 4, nnrm ? nrm_off / 4 : 0);
	ww_wr32 (buf + 8, ncol ? col_off / 4 : 0);
	ww_wr32 (buf + 12, nuv ? uv_off / 4 : 0);
	ww_wr32 (buf + 16, matcol_off / 4);
	ww_wr32 (buf + 20, 0); // no TPL container rebuilt
	ww_wr32 (buf + 24, (uint)nm);
	ww_wr32 (buf + 28, draw_off / 4);
	ww_wr32 (buf + 32, (uint)nm);
	ww_wr32 (buf + 36, shape_off / 4);
	ww_wr32 (buf + 40, (uint)nm);
	ww_wr32 (buf + 44, pack_off / 4);

	for (size_t m = 0; m < nm; m++)
	{
		u8 *dp = buf + draw_off + m * 8;
		ww_wr32 (dp, 1);
		ww_wr32 (dp + 4, (shape_off + (uint)m * 8) / 4);
		u8 *sp = buf + shape_off + m * 8;
		ww_wr16 (sp, 0);
		ww_wr16 (sp + 2, 1);
		ww_wr32 (sp + 4, (pack_off + (uint)m * 12) / 4);
		u8 *pp = buf + pack_off + m * 12;
		ww_wr32 (pp, blob_off[m] / 4);
		ww_wr32 (pp + 4, packs[m].len);
		pp[8] = 0;
		pp[9] = 3; // Flags2=3: no matrix slots, colours+uvs kept
		pp[10] = packs[m].mat <= 127 ? (u8)packs[m].mat : 0;
		// preserve the referenced texture slot: materials carry
		// "TextureN" names (no pixels are rebuilt, see header note).
		pp[11] = 0xff;
		{
			const material_t *mt = model->num_materials > packs[m].mat
				? model->materials + packs[m].mat
				: 0;
			if (mt && mt->num_textures > 0 && mt->textures[0][0])
			{
				uint tn = 0;
				if (sscanf (mt->textures[0], "Texture%u", &tn) == 1 && tn <= 127)
					pp[11] = (u8)tn;
			}
		}
		memcpy (buf + blob_off[m], packs[m].blob, packs[m].len);
		u8 *cp = buf + matcol_off + packs[m].mat * 4;
		const material_t *mt = model->num_materials > packs[m].mat
			? model->materials + packs[m].mat
			: 0;
		if (mt)
		{
			cp[0] = (u8)(mt->diffuse[0] * 255.0f);
			cp[1] = (u8)(mt->diffuse[1] * 255.0f);
			cp[2] = (u8)(mt->diffuse[2] * 255.0f);
			cp[3] = (u8)(mt->diffuse[3] * 255.0f);
		}
		else
		{
			cp[0] = cp[1] = cp[2] = cp[3] = 255;
		}
	}
	for (size_t i = 0; i < npos; i++)
	{
		u8 *pp2 = buf + pos_off + i * 6;
		ww_wr16 (pp2, (u16)(pospool[i].x & 0xffff));
		ww_wr16 (pp2 + 2, (u16)(pospool[i].y & 0xffff));
		ww_wr16 (pp2 + 4, (u16)(pospool[i].z & 0xffff));
	}
	for (size_t i = 0; i < nnrm; i++)
	{
		float nx = nrmpool[i][0], ny = nrmpool[i][1], nz = nrmpool[i][2];
		if (nx > 1)
			nx = 1;
		if (nx < -1)
			nx = -1;
		if (ny > 1)
			ny = 1;
		if (ny < -1)
			ny = -1;
		if (nz > 1)
			nz = 1;
		if (nz < -1)
			nz = -1;
		u8 *pp2 = buf + nrm_off + i * 6;
		ww_wr16 (pp2, (u16)((int)lroundf (nx * 32767.0f) & 0xffff));
		ww_wr16 (pp2 + 2, (u16)((int)lroundf (ny * 32767.0f) & 0xffff));
		ww_wr16 (pp2 + 4, (u16)((int)lroundf (nz * 32767.0f) & 0xffff));
	}
	for (size_t i = 0; i < ncol; i++)
	{
		// GX RGBA4 pack (matches the decoder's unpack order)
		const uint c4 = ((uint)colpool[i][0] >> 4) << 12 | ((uint)colpool[i][1] >> 4) << 8
			| ((uint)colpool[i][2] >> 4) << 4 | ((uint)colpool[i][3] >> 4);
		ww_wr16 (buf + col_off + i * 2, (u16)c4);
	}
	for (size_t i = 0; i < nuv; i++)
	{
		u8 *pp2 = buf + uv_off + i * 4;
		ww_wr16 (pp2, (u16)((int)lroundf (uvpool[i][0] * 1024.0f) & 0xffff));
		ww_wr16 (pp2 + 2, (u16)((int)lroundf (uvpool[i][1] * 1024.0f) & 0xffff));
	}

	FREE (blob_off);
	FREE (pospool);
	FREE (nrmpool);
	FREE (colpool);
	FREE (uvpool);
	for (size_t k = 0; k < nm; k++)
		FREE (packs[k].blob);
	FREE (packs);

	*out = buf;
	*out_size = cur;
	return ERR_OK;
}

enumError EncodeModelToWWModel (const model_t *model, ccp out_path)
{
	u8 *buf = 0;
	uint size = 0;
	enumError err = EncodeWWModel (model, &buf, &size);
	if (err || !buf)
	{
		FREE (buf);
		return err ? err : ERR_INVALID_DATA;
	}
	File_t F;
	err = CreateFileOpt (&F, true, out_path, false, out_path);
	if (!err && F.f && fwrite (buf, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing WW model failed: %s\n", out_path);
	ResetFile (&F, opt_preserve);
	FREE (buf);
	return err;
}

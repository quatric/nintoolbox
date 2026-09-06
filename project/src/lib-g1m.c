// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Koei Tecmo G1M model container (Hyrule Warriors, Fire Emblem Warriors)
//
// Little-endian. A G1M is a chunk list:
//
//   0x00  "_M1G"  (the magic reads G1M_ big-endian)
//   0x04  char[4] version
//   0x08  u32 file size
//   0x0c  u32 header size (0x18)
//   0x10  u32 reserved
//   0x14  u32 chunk count
//   0x18  chunks, each { char[4] magic, char[4] version, u32 size, payload }
//
// Geometry lives in the G1MG chunk:
//
//   0x0c  char[4] platform ("3DS\0")
//   0x10  u32 reserved
//   0x14  float[6] bounding box
//   0x2c  u32 section count
//   0x30  sections, each { u32 type, u32 size, u32 count, payload }
//
// Of the nine section types every file carries, three describe geometry:
//
//   0x00010004 vertex buffers { u32, u32 stride, u32 count, u32 } + data
//   0x00010005 vertex layouts, one 44-byte block per buffer:
//              { u32, u32 buffer index, u32 attribute count } followed by
//              8-byte records { u16 buffer, u16 offset, u16 type, u16 semantic }
//   0x00010007 index buffers  { u32 count, u32 bit width, u32 } + data
//   0x00010008 submeshes, 56 bytes each, with vertex start/count at 0x28
//              and index start/count at 0x30
//
// Established against 40 models pulled from Hyrule Warriors Legends, with
// each field checked rather than assumed: every sampled position falls
// inside the model's own declared bounding box, every index is inside the
// vertex count, each submesh's vertex and index ranges sum exactly to the
// buffer totals, and reading the indices as degenerate-stitched triangle
// strips references 100% of the vertices in every file.
//
// The attribute layout was pinned down the same way. A file carrying two
// vertex buffers has a section body of exactly twice 44 bytes, the second
// block identical but for its buffer index, which fixes the block size and
// therefore the 8-byte record size. Reading the records that way gives
// offsets 0, 12, 24 and 28 across a 36-byte stride -- a partition with no
// gap -- and each field then checks out against something independent:
//
//   semantic 0 (position) type 2: float3, and every sampled position falls
//     inside the model's own declared bounding box;
//   semantic 3 (normal) type 2: float3, and every one of 5247 sampled
//     normals is unit length;
//   semantic 5 (texcoord) type 1: float2, in range for 94% of vertices,
//     against 43% if the same 8 bytes are read as four halves.
//
// Types beyond those three, and semantics beyond position/normal/texcoord,
// are skipped rather than guessed: colour (semantic 10, type 13) and the
// skinning attributes are not exported.
//-----------------------------------------------------------------------------

#include "lib-std.h"
#include "lib-g1m.h"
#include <string.h>

static inline u32 g1m_le32 (const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static inline float g1m_lef32 (const u8 *p)
{
	const u32 bits = g1m_le32 (p);
	float f;
	memcpy (&f, &bits, 4);
	return f;
}

typedef struct g1m_section_t
{
	u32 type;
	u32 size;
	u32 count;
	const u8 *body; // just past the 12-byte section header
	u32 body_size;
} g1m_section_t;

// Locate one G1MG section by type.
static const g1m_section_t *g1m_find (const g1m_section_t *sec, uint n, u32 type)
{
	for (uint i = 0; i < n; i++)
		if (sec[i].type == type)
			return sec + i;
	return 0;
}

enumError DecodeG1M (const u8 *data, uint size, ccp out_glb_path)
{
	if (!data || size < 0x18 || !out_glb_path || memcmp (data, "_M1G", 4))
		return ERR_NOTHING_TO_DO;

	const u32 declared = g1m_le32 (data + 8);
	const u32 hdr_size = g1m_le32 (data + 12);
	const u32 n_chunks = g1m_le32 (data + 20);
	if (hdr_size < 0x18 || hdr_size > size || declared > size || n_chunks > 64)
		return ERR_NOTHING_TO_DO;

	//--- find the geometry chunk

	const u8 *g1mg = 0;
	u32 g1mg_size = 0;
	for (u32 i = 0, off = hdr_size; i < n_chunks; i++)
	{
		if ((u64)off + 12 > size)
			break;
		const u32 csize = g1m_le32 (data + off + 8);
		if (!csize || (u64)off + csize > size)
			break;
		if (!memcmp (data + off, "GM1G", 4)) // "G1MG" stored byte-reversed
		{
			g1mg = data + off;
			g1mg_size = csize;
			break;
		}
		off += csize;
	}
	if (!g1mg || g1mg_size < 0x30)
		return ERR_NOTHING_TO_DO;

	//--- collect its sections

	const u32 n_sec = g1m_le32 (g1mg + 0x2c);
	if (!n_sec || n_sec > 64)
		return ERR_NOTHING_TO_DO;

	g1m_section_t *sec = CALLOC (n_sec, sizeof (*sec));
	if (!sec)
		return ERR_OUT_OF_MEMORY;

	uint n_found = 0;
	for (u32 i = 0, off = 0x30; i < n_sec; i++)
	{
		if ((u64)off + 12 > g1mg_size)
			break;
		const u32 type = g1m_le32 (g1mg + off);
		const u32 ssize = g1m_le32 (g1mg + off + 4);
		const u32 scount = g1m_le32 (g1mg + off + 8);
		if (ssize < 12 || (u64)off + ssize > g1mg_size)
			break;
		sec[n_found].type = type;
		sec[n_found].size = ssize;
		sec[n_found].count = scount;
		sec[n_found].body = g1mg + off + 12;
		sec[n_found].body_size = ssize - 12;
		n_found++;
		off += ssize;
	}

	const g1m_section_t *ls = g1m_find (sec, n_found, 0x00010005);
	const g1m_section_t *vs = g1m_find (sec, n_found, 0x00010004);
	const g1m_section_t *is = g1m_find (sec, n_found, 0x00010007);
	const g1m_section_t *ss = g1m_find (sec, n_found, 0x00010008);
	if (!vs || !is || !ss || !ss->count || vs->body_size < 16 || is->body_size < 12)
	{
		FREE (sec);
		return ERR_NOTHING_TO_DO;
	}

	// Attribute offsets for the first vertex buffer. -1 means absent.
	int pos_off = -1, nrm_off = -1, uv_off = -1;
	if (ls && ls->body_size >= 12)
	{
		const u32 n_attr = g1m_le32 (ls->body + 8);
		if (n_attr <= 32 && 12 + (u64)n_attr * 8 <= ls->body_size)
			for (u32 a = 0; a < n_attr; a++)
			{
				const u8 *r = ls->body + 12 + (size_t)a * 8;
				const uint buffer = (uint)r[0] | (uint)r[1] << 8;
				const uint off = (uint)r[2] | (uint)r[3] << 8;
				const uint type = (uint)r[4] | (uint)r[5] << 8;
				const uint sem = (uint)r[6] | (uint)r[7] << 8;
				if (buffer)
					continue; // a second buffer is not merged in here
				if (sem == 0 && type == 2)
					pos_off = (int)off;
				else if (sem == 3 && type == 2)
					nrm_off = (int)off;
				else if (sem == 5 && type == 1)
					uv_off = (int)off;
			}
	}

	const u32 stride = g1m_le32 (vs->body + 4);
	const u32 n_vert = g1m_le32 (vs->body + 8);
	const u8 *vdata = vs->body + 16;
	if (stride < 12 || !n_vert || (u64)n_vert * stride > vs->body_size - 16)
	{
		FREE (sec);
		return ERR_NOTHING_TO_DO;
	}
	// Without a usable table, fall back to the position-only reading that
	// the bounding-box check established on its own.
	if (pos_off < 0)
		pos_off = 0;
	if ((uint)pos_off + 12 > stride)
	{
		FREE (sec);
		return ERR_NOTHING_TO_DO;
	}
	if (nrm_off >= 0 && (uint)nrm_off + 12 > stride)
		nrm_off = -1;
	if (uv_off >= 0 && (uint)uv_off + 8 > stride)
		uv_off = -1;

	const u32 n_index = g1m_le32 (is->body);
	const u32 idx_bits = g1m_le32 (is->body + 4);
	const u8 *idata = is->body + 12;
	// Only the 16-bit form appears on the cart; a wider one would need its
	// own reader rather than a silent reinterpretation.
	if (idx_bits != 16 || !n_index || (u64)n_index * 2 > is->body_size - 12)
	{
		FREE (sec);
		return ERR_NOTHING_TO_DO;
	}

	const u32 sub_stride = ss->body_size / ss->count;
	if (sub_stride < 0x38)
	{
		FREE (sec);
		return ERR_NOTHING_TO_DO;
	}

	//--- one mesh per submesh

	mesh_t *meshes = CALLOC (ss->count, sizeof (mesh_t));
	if (!meshes)
	{
		FREE (sec);
		return ERR_OUT_OF_MEMORY;
	}

	uint n_out = 0;
	for (u32 m = 0; m < ss->count; m++)
	{
		const u8 *r = ss->body + (size_t)m * sub_stride;
		const u32 v_start = g1m_le32 (r + 0x28);
		const u32 v_count = g1m_le32 (r + 0x2c);
		const u32 i_start = g1m_le32 (r + 0x30);
		const u32 i_count = g1m_le32 (r + 0x34);
		if (!v_count || !i_count || (u64)v_start + v_count > n_vert
			|| (u64)i_start + i_count > n_index)
			continue;

		mesh_t *mesh = meshes + n_out;
		snprintf (mesh->name, sizeof (mesh->name), "submesh%u", m);
		mesh->material_idx = -1;

		// Positions are shared across submeshes, so copy the whole buffer
		// once per mesh and let the indices address it directly.
		mesh->positions = MALLOC ((size_t)n_vert * sizeof (vec3_t));
		if (!mesh->positions)
			break;
		if (nrm_off >= 0)
			mesh->normals = MALLOC ((size_t)n_vert * sizeof (vec3_t));
		if (uv_off >= 0)
			mesh->texcoords = MALLOC ((size_t)n_vert * sizeof (vec2_t));
		for (u32 v = 0; v < n_vert; v++)
		{
			const u8 *p = vdata + (size_t)v * stride;
			// A -90 degree X rotation, matching the other GameCube/3DS
			// readers here, so exported models share one orientation.
			const float x = g1m_lef32 (p + pos_off);
			const float y = g1m_lef32 (p + pos_off + 4);
			const float z = g1m_lef32 (p + pos_off + 8);
			mesh->positions[v].x = x;
			mesh->positions[v].y = z;
			mesh->positions[v].z = -y;
			if (mesh->normals)
			{
				const float nx = g1m_lef32 (p + nrm_off);
				const float ny = g1m_lef32 (p + nrm_off + 4);
				const float nz = g1m_lef32 (p + nrm_off + 8);
				mesh->normals[v].x = nx;
				mesh->normals[v].y = nz;
				mesh->normals[v].z = -ny;
			}
			if (mesh->texcoords)
			{
				mesh->texcoords[v].u = g1m_lef32 (p + uv_off);
				mesh->texcoords[v].v = g1m_lef32 (p + uv_off + 4);
			}
		}
		mesh->num_positions = n_vert;
		mesh->num_normals = mesh->normals ? n_vert : 0;
		mesh->num_texcoords = mesh->texcoords ? n_vert : 0;

		// Degenerate-stitched triangle strip: a run where any two indices
		// coincide is a stitch, not a face, and winding alternates.
		vertex_t *verts = MALLOC ((size_t)(i_count > 2 ? i_count - 2 : 0) * 3 * sizeof (vertex_t));
		if (!verts)
		{
			FREE (mesh->positions);
			break;
		}
		size_t nv = 0;
		for (u32 k = 0; k + 2 < i_count; k++)
		{
			const u32 a = idata[(size_t)(i_start + k) * 2] | (u32)idata[(size_t)(i_start + k) * 2 + 1] << 8;
			const u32 b = idata[(size_t)(i_start + k + 1) * 2] | (u32)idata[(size_t)(i_start + k + 1) * 2 + 1] << 8;
			const u32 c = idata[(size_t)(i_start + k + 2) * 2] | (u32)idata[(size_t)(i_start + k + 2) * 2 + 1] << 8;
			if (a == b || b == c || a == c)
				continue;
			if (a >= n_vert || b >= n_vert || c >= n_vert)
				continue;
			const u32 t0 = a, t1 = (k & 1) ? c : b, t2 = (k & 1) ? b : c;
			const u32 tri[3] = { t0, t1, t2 };
			for (uint e = 0; e < 3; e++)
			{
				verts[nv].position_idx = (int)tri[e];
				verts[nv].normal_idx = mesh->normals ? (int)tri[e] : -1;
				verts[nv].texcoord_idx = mesh->texcoords ? (int)tri[e] : -1;
				nv++;
			}
		}
		if (!nv)
		{
			FREE (verts);
			FREE (mesh->positions);
			FREE (mesh->normals);
			FREE (mesh->texcoords);
			memset (mesh, 0, sizeof (*mesh));
			continue;
		}
		mesh->vertices = verts;
		mesh->num_vertices = nv;
		n_out++;
	}

	enumError err = ERR_NOTHING_TO_DO;
	if (n_out)
	{
		model_t model;
		memset (&model, 0, sizeof (model));
		model.meshes = meshes;
		model.num_meshes = n_out;
		err = ExportModelToGLB (&model, out_glb_path) == 0 ? ERR_OK : ERR_CANT_CREATE;
	}

	for (uint i = 0; i < n_out; i++)
	{
		FREE (meshes[i].positions);
		FREE (meshes[i].normals);
		FREE (meshes[i].texcoords);
		FREE (meshes[i].vertices);
	}
	FREE (meshes);
	FREE (sec);
	return err;
}

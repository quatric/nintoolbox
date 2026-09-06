// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Next Level Games GLG model format -- see lib-glg.h for the chunk layout.
//
// Confirmed against the retail asset corpus, the GameCube game's own loader
// decompiled in Ghidra (FUN_801bfdf0/FUN_801c1e5c in Super Mario Strikers'
// main.dol), and cross-checked against KillzXGaming's independent
// StrikersRLG.cs reader (Switch-Toolbox, MIT), which also covers the Wii
// sequel's renamed .rlg variant. The vertex-attribute record layout below
// (offset/type/stride) follows that reader, which corrected an earlier,
// wrong guess in this file (attribute role guessed from byte stride alone).
//-----------------------------------------------------------------------------

#include "lib-std.h"
#include <math.h>
#include "lib-glg.h"
#include "lib-nintendo-archives.h"
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>

static inline u16 glg_be16 (const u8 *p)
{
	return (u16) p[0] << 8 | p[1];
}

static inline u32 glg_be32 (const u8 *p)
{
	return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3];
}

static inline s16 glg_be16s (const u8 *p)
{
	return (s16) glg_be16 (p);
}

static inline float glg_bef32 (const u8 *p)
{
	u32 bits = glg_be32 (p);
	float f;
	memcpy (&f, &bits, 4);
	return f;
}

static inline void glg_put16 (u8 *p, u16 v)
{
	p[0] = v >> 8;
	p[1] = (u8) v;
}

static inline void glg_put32 (u8 *p, u32 v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = (u8) v;
}

//-----------------------------------------------------------------------------

typedef struct glg_chunk_t
{
	const u8 *data; // chunk payload (after the 8-byte tag+length header)
	u32 size;
} glg_chunk_t;

// Find the first chunk with the given 24-bit key (e.g. 0x1b004 for the mesh
// table) among the inner chunks of a single-model GLG file. Returns a zeroed
// chunk (data=NULL) when not found.
static glg_chunk_t glg_find_chunk (const u8 *data, u32 size, u32 key, bool is_wii)
{
	glg_chunk_t none = { 0, 0 };
	if (size < 8)
		return none;

	const u32 outer_len = glg_be32 (data + 4);
	u32 off = 8;
	const u32 end = 8 + outer_len <= size ? 8 + outer_len : size;

	while (off + 8 <= end)
	{
		const u32 tag = glg_be32 (data + off) & 0x00ffffff;
		const u32 len = glg_be32 (data + off + 4);
		if (off + 8 + (u64) len > end)
			break;
		if (tag == key)
		{
			glg_chunk_t c = { data + off + 8, len };
			return c;
		}
		off += 8 + len;
		// Wii .rlg pads every section to a 4-byte boundary; GameCube .glg
		// does not (StrikersRLG.cs: `if (!IsGamecube) reader.Align(4)`).
		if (is_wii)
			off = (off + 3) & ~3u;
	}
	return none;
}

// True when walking the inner chunks with (or without) Wii 4-byte section
// padding consumes the container exactly, which is what distinguishes the
// two layouts.
static bool glg_walk_ends_clean (const u8 *data, u32 size, bool is_wii)
{
	if (size < 8)
		return false;

	const u32 outer_len = glg_be32 (data + 4);
	const u32 end = 8 + outer_len <= size ? 8 + outer_len : size;
	u32 off = 8;
	uint seen = 0;

	while (off + 8 <= end)
	{
		const u32 len = glg_be32 (data + off + 4);
		if (off + 8 + (u64) len > end)
			return false;
		off += 8 + len;
		if (is_wii)
			off = (off + 3) & ~3u;
		seen++;
	}
	return seen > 0 && off == end;
}

//-----------------------------------------------------------------------------
// Mesh table entry, 0x4a (74) bytes on GameCube. Field offsets confirmed
// against FUN_801bfdf0/FUN_801c1e5c and StrikersRLG.cs; fields past
// vapd_off are unattributed (StrikersRLG.cs doesn't name them either) and
// are zero-filled on encode.

enum
{
	GLG_MESH_SIZE = 0x4a, // GameCube .glg
	RLG_MESH_SIZE = 48,	  // Wii .rlg
	GLG_VAPD_SIZE = 6,	  // GameCube .glg
	RLG_VAPD_SIZE = 8,	  // Wii .rlg (trailing u16 pad)

	// Vertex-attribute "type" byte (StrikersRLG.cs pointer.Type).
	GLG_ATTR_POSITION = 0,
	GLG_ATTR_NORMAL = 1,
	GLG_ATTR_TEXCOORD0 = 3,
};

typedef struct glg_mesh_entry_t
{
	u16 face_format;
	u32 face_off;	// byte offset into the 0x1b007 index chunk
	u16 face_count; // number of indices
	u8 face_type;	// GX primitive selector; only 0 confirmed so far
	u8 attr_count;
	u32 vapd_off;	  // byte offset into the 0x1b005 chunk (GameCube only)
	u16 vertex_count; // stored directly on Wii; derived from strides on GameCube
	// PTLG texture hash: GLG binds its textures by the same 32-bit key the
	// sibling .glt/.rlt container names its entries with (which is why the
	// PTLG extractor writes "<hash>.tpl"). Verified against Super Mario
	// Strikers: over the 360 meshes whose model has a populated sibling
	// container, every non-sentinel hash resolves to a texture in it.
	// Retail files put it at either +36 or +40 and never both, so both are
	// read and the caller keeps whichever actually names a texture in the
	// container -- a hash that matches nothing binds nothing.
	u32 tex_hash[2];
} glg_mesh_entry_t;

// GameCube layout (0x4a bytes): u16 pad, u16 index format, u32 index offset,
// u16 index count, u8 face type, u8 attr count, u32 vapd offset, ...
// Wii layout (48 bytes): u32 index offset, u16 index format, u16 index count,
// u16 vertex count, u8 unknown, u8 attr count, ... (StrikersRLG.cs MeshData)
static void glg_read_mesh_entry (
	const u8 *e, uint entry_size, glg_mesh_entry_t *out, bool is_wii)
{
	memset (out, 0, sizeof (*out));
	if (is_wii)
	{
		out->face_off = glg_be32 (e);
		out->face_format = glg_be16 (e + 4);
		out->face_count = glg_be16 (e + 6);
		out->vertex_count = glg_be16 (e + 8);
		out->face_type = 0;
		out->attr_count = e[11];
		if (entry_size >= 44)
		{
			out->tex_hash[0] = glg_be32 (e + 40);
			out->tex_hash[1] = glg_be32 (e + 36);
		}
	}
	else
	{
		out->face_format = glg_be16 (e + 2);
		out->face_off = glg_be32 (e + 4);
		out->face_count = glg_be16 (e + 8);
		out->face_type = e[10];
		out->attr_count = e[11];
		out->vapd_off = glg_be32 (e + 12);
		if (entry_size >= 44)
		{
			out->tex_hash[0] = glg_be32 (e + 40);
			out->tex_hash[1] = glg_be32 (e + 36);
		}
	}
}

typedef struct glg_vapd_t
{
	u32 offset; // byte offset into the 0x1b006 vertex chunk
	u8 type;
	u8 stride; // bytes per vertex, stored directly in the file (not derived)
} glg_vapd_t;

static void glg_read_vapd (const u8 *rec, glg_vapd_t *out)
{
	out->offset = glg_be32 (rec);
	out->type = rec[4];
	out->stride = rec[5];
}

//-----------------------------------------------------------------------------

// Textures staged beside the model while the GLB is written, tracked so
// cleanup removes exactly what was created and never a pre-existing file.
typedef struct glg_staged_tex_t
{
	u32 *hashes;
	char **paths;
	uint used, size;

} glg_staged_tex_t;

static bool glg_staged_has (const glg_staged_tex_t *st, u32 hash)
{
	if (hash == 0 || hash == 0xffffffff)
		return false;
	for (uint i = 0; i < st->used; i++)
		if (st->hashes[i] == hash)
			return true;
	return false;
}

static void glg_staged_cleanup (glg_staged_tex_t *st)
{
	for (uint i = 0; i < st->used; i++)
	{
		unlink (st->paths[i]);
		FREE (st->paths[i]);
	}
	FREE (st->paths);
	FREE (st->hashes);
	memset (st, 0, sizeof (*st));
}

// Decode SRC_PATH's sibling .glt/.rlt into "<hash>.png" files beside
// OUT_GLB_PATH and record what was written.
static void glg_stage_ptlg_textures (ccp src_path, ccp out_glb_path, glg_staged_tex_t *st)
{
	ccp dot = strrchr (src_path, '.');
	if (!dot)
		return;

	char ptlg[PATH_MAX];
	static const char *const ext[] = { ".glt", ".rlt", 0 };
	const u8 *raw = 0;
	u8 *loaded = 0;
	size_t raw_size = 0;
	for (uint i = 0; ext[i]; i++)
	{
		snprintf (ptlg, sizeof (ptlg), "%.*s%s", (int)(dot - src_path), src_path, ext[i]);
		if (!LoadFileAlloc (ptlg, 0, 0, &loaded, &raw_size, 0, 0, 0, false) && loaded)
		{
			raw = loaded;
			break;
		}
	}
	if (!raw)
		return;

	char dir[PATH_MAX];
	ccp slash = strrchr (out_glb_path, '/');
	if (slash)
		snprintf (dir, sizeof (dir), "%.*s", (int)(slash - out_glb_path), out_glb_path);
	else
		snprintf (dir, sizeof (dir), ".");

	// Note which "<hash>.png" files already exist: those belong to the user
	// (or to a previous extraction) and must survive this export untouched.
	u32 n_tex = raw_size >= 8 ? ((u32)raw[4] << 24 | (u32)raw[5] << 16 | (u32)raw[6] << 8 | raw[7]) : 0;
	bool *pre_existing = n_tex && n_tex <= 0x10000 ? CALLOC (n_tex, sizeof (bool)) : 0;
	const u32 tab_off = raw_size > 0x14 && !(raw[0x10] | raw[0x11] | raw[0x12] | raw[0x13]) ? 0x20 : 0x10;
	if (pre_existing)
		for (u32 i = 0; i < n_tex; i++)
		{
			const size_t eo = (size_t)tab_off + (size_t)i * 16;
			if (eo + 4 > raw_size)
				break;
			const u32 h = (u32)raw[eo] << 24 | (u32)raw[eo + 1] << 16 | (u32)raw[eo + 2] << 8
				| raw[eo + 3];
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%08x.png", dir, h);
			struct stat sb;
			pre_existing[i] = !stat (path, &sb);
		}

	uint written = 0;
	DecodePTLGToPNGDir (raw, (uint)raw_size, dir, &written);

	if (written && pre_existing)
	{
		st->size = n_tex;
		st->hashes = CALLOC (n_tex, sizeof (*st->hashes));
		st->paths = CALLOC (n_tex, sizeof (*st->paths));
		if (st->hashes && st->paths)
			for (u32 i = 0; i < n_tex; i++)
			{
				const size_t eo = (size_t)tab_off + (size_t)i * 16;
				if (eo + 4 > raw_size)
					break;
				const u32 h = (u32)raw[eo] << 24 | (u32)raw[eo + 1] << 16
					| (u32)raw[eo + 2] << 8 | raw[eo + 3];
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%08x.png", dir, h);
				struct stat sb;
				if (stat (path, &sb))
					continue;
				st->hashes[st->used] = h;
				// A file that was already there is still usable as a
				// texture, but it is not ours to delete afterwards.
				st->paths[st->used] = pre_existing[i] ? 0 : STRDUP (path);
				st->used++;
			}
		else
		{
			FREE (st->hashes);
			FREE (st->paths);
			memset (st, 0, sizeof (*st));
		}
	}

	FREE (pre_existing);
	FREE (loaded);
}

// Materials are keyed by texture hash: meshes sharing a texture share one.
static int glg_find_or_add_material (material_t *mats, uint *n, u32 hash)
{
	char name[64];
	snprintf (name, sizeof (name), "%08x", hash);
	for (uint i = 0; i < *n; i++)
		if (!strcmp (mats[i].textures[0], name))
			return (int)i;
	material_t *m = mats + *n;
	snprintf (m->name, sizeof (m->name), "mat_%08x", hash);
	snprintf (m->textures[0], sizeof (m->textures[0]), "%s", name);
	m->num_textures = 1;
	m->diffuse[0] = m->diffuse[1] = m->diffuse[2] = m->diffuse[3] = 1.0f;
	return (int)(*n)++;
}

enumError DecodeGLG (const u8 *data, uint size, ccp out_glb_path)
{
	return DecodeGLG2 (data, size, 0, out_glb_path);
}

enumError DecodeGLG2 (const u8 *data, uint size, ccp src_path, ccp out_glb_path)
{
	if (!data || size < 16 || !out_glb_path)
		return ERR_NOTHING_TO_DO;

	// Stage files wrap the model container in an outer 0x8001b100 block; the
	// game's loader descends into it the same way (FUN_801bfdf0 advances the
	// cursor by one header when the tag matches). Every stage in Super Mario
	// Strikers holds exactly one child, so step into it and carry on.
	u32 outer_tag = glg_be32 (data) & 0x80ffffff;
	if (outer_tag == 0x8001b100)
	{
		const u32 outer_len = glg_be32 (data + 4);
		if ((u64) 8 + outer_len > size || outer_len < 8)
			return ERR_NOTHING_TO_DO;
		data += 8;
		size = outer_len;
		outer_tag = glg_be32 (data) & 0x80ffffff;
	}
	if (outer_tag != 0x8001b000 && outer_tag != 0x8001b001)
		return ERR_NOTHING_TO_DO;

	// GameCube and Wii differ in whether sections are padded to a 4-byte
	// boundary, in the model entry size (16 vs 12), and in the vertex
	// attribute record size (6 vs 8). No single one of those is a reliable
	// discriminator on its own -- plenty of Wii files are naturally
	// 4-aligned, so the unpadded walk "succeeds" for them too -- so score
	// both layouts on whether they are internally consistent end to end and
	// take the one that holds together.
	//
	// The mesh entry size is itself not a constant: it is the mesh chunk
	// divided by the mesh count summed over the model table, exactly as the
	// game and StrikersRLG.cs compute it. Super Mario Strikers' props use
	// 74-byte entries while its character models use 66, so assuming either
	// value drops a large part of the retail corpus.
	bool is_wii = false;
	u32 mesh_entry_size = 0;
	glg_chunk_t mesh_chunk = { 0, 0 };
	for (uint pass = 0; pass < 2; pass++)
	{
		const bool wii = pass != 0;
		if (!glg_walk_ends_clean (data, size, wii))
			continue;

		const glg_chunk_t mc = glg_find_chunk (data, size, 0x1b004, wii);
		const glg_chunk_t pc = glg_find_chunk (data, size, 0x1b003, wii);
		if (!mc.data || !mc.size || !pc.data)
			continue;

		const u32 model_entry_size = wii ? 12 : 16;
		u32 total_meshes = 0;
		for (u32 m = 0; m + model_entry_size <= pc.size; m += model_entry_size)
			total_meshes += wii ? glg_be32 (pc.data + m + 4) : glg_be32 (pc.data + m);
		if (!total_meshes || mc.size % total_meshes)
			continue;

		const u32 entry = mc.size / total_meshes;
		if (entry < 16 || entry > 256)
			continue;

		// Cross-check against the attribute table: the per-mesh attribute
		// counts must tile the VAPD chunk exactly at this layout's record
		// size. This is what actually separates the two platforms.
		const glg_chunk_t vc = glg_find_chunk (data, size, 0x1b005, wii);
		if (!vc.data)
			continue;
		u32 attr_total = 0;
		for (u32 i = 0; i < total_meshes; i++)
			attr_total += mc.data[i * entry + 11];
		if (attr_total * (wii ? RLG_VAPD_SIZE : GLG_VAPD_SIZE) != vc.size)
			continue;

		is_wii = wii;
		mesh_entry_size = entry;
		mesh_chunk = mc;
		break;
	}
	if (!mesh_chunk.data)
		return ERR_NOTHING_TO_DO;

	const u32 vapd_entry_size = is_wii ? RLG_VAPD_SIZE : GLG_VAPD_SIZE;
	glg_chunk_t vapd_chunk = glg_find_chunk (data, size, 0x1b005, is_wii);
	glg_chunk_t vert_chunk = glg_find_chunk (data, size, 0x1b006, is_wii);
	glg_chunk_t idx_chunk = glg_find_chunk (data, size, 0x1b007, is_wii);
	if (!mesh_chunk.data || !vapd_chunk.data || !vert_chunk.data || !idx_chunk.data)
		return ERR_NOTHING_TO_DO;
	if (mesh_chunk.size % mesh_entry_size)
		return ERR_NOTHING_TO_DO;

	const uint mesh_count = mesh_chunk.size / mesh_entry_size;
	if (!mesh_count)
		return ERR_NOTHING_TO_DO;

	glg_mesh_entry_t *entries = MALLOC (mesh_count * sizeof (*entries));
	for (uint i = 0; i < mesh_count; i++)
		glg_read_mesh_entry (
			mesh_chunk.data + i * mesh_entry_size, mesh_entry_size, entries + i, is_wii);

	mesh_t *meshes = CALLOC (mesh_count, sizeof (mesh_t));
	// Not every mesh-table entry emits a mesh, so remember which entry each
	// output mesh came from -- the texture hash lives in that entry.
	u32 *mesh_src_entry = CALLOC (mesh_count, sizeof (*mesh_src_entry));
	uint num_out_meshes = 0;
	uint wii_vapd_off = 0;

	for (uint i = 0; i < mesh_count; i++)
	{
		const glg_mesh_entry_t *m = entries + i;
		const u32 vapd_mesh_off = is_wii ? wii_vapd_off : m->vapd_off;
		if (is_wii)
			wii_vapd_off += m->attr_count * RLG_VAPD_SIZE;

		const uint bytes_per_idx = (m->face_format == 0) ? 2 : 1;
		if ((u64) m->face_off + (u64) m->face_count * bytes_per_idx > idx_chunk.size || m->face_count < 3)
			continue;
		if ((u64) vapd_mesh_off + (u64) m->attr_count * vapd_entry_size > vapd_chunk.size || !m->attr_count)
			continue;

		glg_vapd_t attrs[64];
		const uint num_attrs = m->attr_count < 64 ? m->attr_count : 64;
		for (uint a = 0; a < num_attrs; a++)
			glg_read_vapd (vapd_chunk.data + vapd_mesh_off + a * vapd_entry_size, attrs + a);

		const glg_vapd_t *pos_attr = NULL, *uv_attr = NULL, *nrm_attr = NULL;
		for (uint a = 0; a < num_attrs; a++)
		{
			if ((attrs[a].type == GLG_ATTR_POSITION || attrs[a].type == 0x67) && !pos_attr)
				pos_attr = attrs + a;
			else if ((attrs[a].type == GLG_ATTR_TEXCOORD0 || attrs[a].type == 0x26 || attrs[a].type == 0xcc) && !uv_attr)
				uv_attr = attrs + a;
			// Only float3 normals are decoded. The packed 3-byte variant the
			// GameCube build uses is left alone: decoding it as 3x s8 (either
			// /127 or /255, renormalized) renders visibly wrong, with
			// backface-style black patches, so its encoding is still unknown
			// and emitting a guess is worse than emitting none.
			else if ((attrs[a].type == GLG_ATTR_NORMAL || attrs[a].type == 0xfe) && !nrm_attr
				&& attrs[a].stride == 12)
				nrm_attr = attrs + a;
		}
		if (!pos_attr || !pos_attr->stride || (pos_attr->stride != 6 && pos_attr->stride != 12))
			continue;

		u32 vertex_count = 0;
		if (is_wii)
		{
			vertex_count = m->vertex_count;
		}
		else
		{
			// Vertex count for this mesh's arrays: the byte gap to the next
			// attribute (in file order, matching StrikersRLG.cs) divided by the
			// stored stride, or to the vertex-chunk end for the last attribute.
			const glg_vapd_t *next = NULL;
			for (uint a = 0; a < num_attrs; a++)
				if (attrs + a != pos_attr && attrs[a].offset > pos_attr->offset
					&& (!next || attrs[a].offset < next->offset))
					next = attrs + a;
			const u32 region_end = next ? next->offset : vert_chunk.size;
			if (region_end <= pos_attr->offset)
				continue;
			vertex_count = (region_end - pos_attr->offset) / pos_attr->stride;
		}
		if (!vertex_count || (u64) pos_attr->offset + (u64) vertex_count * pos_attr->stride > vert_chunk.size)
			continue;

		vec3_t *positions = MALLOC (vertex_count * sizeof (vec3_t));
		for (u32 v = 0; v < vertex_count; v++)
		{
			const u8 *p = vert_chunk.data + pos_attr->offset + (u64) v * pos_attr->stride;
			float x, y, z;
			if (pos_attr->stride == 12)
			{
				x = glg_bef32 (p);
				y = glg_bef32 (p + 4);
				z = glg_bef32 (p + 8);
			}
			else // 6: 3x s16, 10.6 fixed point (matches StrikersRLG.cs: /1024)
			{
				x = glg_be16s (p) / 1024.0f;
				y = glg_be16s (p + 2) / 1024.0f;
				z = glg_be16s (p + 4) / 1024.0f;
			}
			// Bring the model upright for glTF's Y-up convention: a -90 deg
			// rotation about X, as StrikersRLG.cs applies. Being an axis
			// swap with a sign flip it is exact in binary floating point,
			// so EncodeGLG's inverse below round-trips without drift.
			positions[v].x = x;
			positions[v].y = z;
			positions[v].z = -y;
		}

		vec2_t *texcoords = NULL;
		if (uv_attr && uv_attr->stride == 4
			&& (u64) uv_attr->offset + (u64) vertex_count * uv_attr->stride <= vert_chunk.size)
		{
			texcoords = MALLOC (vertex_count * sizeof (vec2_t));
			for (u32 v = 0; v < vertex_count; v++)
			{
				const u8 *p = vert_chunk.data + uv_attr->offset + (u64) v * uv_attr->stride;
				texcoords[v].u = glg_be16 (p) / 1024.0f;
				texcoords[v].v = glg_be16 (p + 2) / 1024.0f;
			}
		}

		vec3_t *normals = NULL;
		if (nrm_attr
			&& (u64) nrm_attr->offset + (u64) vertex_count * nrm_attr->stride <= vert_chunk.size)
		{
			normals = MALLOC (vertex_count * sizeof (vec3_t));
			for (u32 v = 0; v < vertex_count; v++)
			{
				const u8 *p = vert_chunk.data + nrm_attr->offset + (u64) v * nrm_attr->stride;
				// Same -90 deg X rotation the positions get, so normals stay
				// consistent with the geometry they belong to.
				const float x = glg_bef32 (p);
				const float y = glg_bef32 (p + 4);
				const float z = glg_bef32 (p + 8);
				normals[v].x = x;
				normals[v].y = z;
				normals[v].z = -y;
			}
		}

		mesh_t *mesh = meshes + num_out_meshes;
		snprintf (mesh->name, sizeof (mesh->name), "mesh%u", i);
		mesh->material_idx = -1;

		vertex_t *verts = MALLOC ((size_t) (m->face_count - 2) * 3 * sizeof (vertex_t));
		size_t num_verts = 0;

		// face_type selects the primitive: 0 is a plain triangle list, 1 a
		// triangle strip. Reading a list as a strip still yields a roughly
		// right-looking shape -- which is how the two were confused here at
		// first -- but invents a triangle per index rather than per three:
		// ball.glg's 360 indices are 120 clean list triangles with not one
		// degenerate group, against 305 fabricated strip triangles.
		const bool is_strip = m->face_type != 0;
		const u8 *idx_data = idx_chunk.data + m->face_off;
		const uint step = is_strip ? 1 : 3;
		for (u32 t = 0; t + 2 < m->face_count; t += step)
		{
			u16 i0 = (m->face_format == 0) ? glg_be16 (idx_data + t * 2) : idx_data[t];
			u16 i1 = (m->face_format == 0) ? glg_be16 (idx_data + (t + 1) * 2) : idx_data[t + 1];
			u16 i2 = (m->face_format == 0) ? glg_be16 (idx_data + (t + 2) * 2) : idx_data[t + 2];
			if (is_strip && (t & 1))
			{
				const u16 tmp = i1;
				i1 = i2;
				i2 = tmp;
			}
			if (i0 == i1 || i1 == i2 || i0 == i2)
				continue; // degenerate tristrip restart
			if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count)
				continue;

			const u16 tri_idx[3] = { i0, i1, i2 };
			vertex_t *v = verts + num_verts;
			for (uint k = 0; k < 3; k++)
			{
				v[k].position_idx = tri_idx[k];
				v[k].normal_idx = normals ? (int) tri_idx[k] : -1;
				v[k].texcoord_idx = texcoords ? (int) tri_idx[k] : -1;
				v[k].tangent_idx = v[k].matrix_idx = -1;
				v[k].color_idx[0] = v[k].color_idx[1] = -1;
				for (uint j = 0; j < 7; j++)
					v[k].extra_texcoord_idx[j] = -1;
			}
			num_verts += 3;
		}

		if (!num_verts)
		{
			FREE (verts);
			FREE (positions);
			FREE (texcoords);
			FREE (normals);
			continue;
		}

		mesh->positions = positions;
		mesh->num_positions = vertex_count;
		mesh->texcoords = texcoords;
		mesh->normals = normals;
		mesh->num_normals = normals ? vertex_count : 0;
		mesh->num_texcoords = texcoords ? vertex_count : 0;
		mesh->vertices = verts;
		mesh->num_vertices = num_verts;
		mesh_src_entry[num_out_meshes] = i;
		num_out_meshes++;
	}

	if (!num_out_meshes)
	{
		FREE (entries);
		FREE (meshes);
		FREE (mesh_src_entry);
		return ERR_NOTHING_TO_DO;
	}

	// Bind textures from the sibling PTLG container, when there is one.
	//
	// A .glg names no textures at all: each mesh entry carries the 32-bit
	// PTLG hash of the texture it uses, and the images live in the .glt
	// (GameCube) or .rlt (Wii) file beside it. Decode that container's
	// textures to "<hash>.png" next to the GLB being written, so the GLB
	// writer's own bare-name lookup finds them and embeds them; then remove
	// the staged files, since the images are inside the GLB by then.
	//
	// A hash is only bound when the container really holds it, so a model
	// whose textures live elsewhere stays untextured rather than picking up
	// something wrong.
	glg_staged_tex_t staged = { 0, 0, 0 };
	material_t *materials = 0;
	uint num_materials = 0;
	if (src_path)
	{
		glg_stage_ptlg_textures (src_path, out_glb_path, &staged);
		if (staged.used)
		{
			materials = CALLOC (num_out_meshes, sizeof (*materials));
			if (materials)
				for (uint i = 0; i < num_out_meshes; i++)
				{
					const glg_mesh_entry_t *m = entries + mesh_src_entry[i];
					for (uint h = 0; h < 2; h++)
					{
						const u32 hash = m->tex_hash[h];
						if (!glg_staged_has (&staged, hash))
							continue;
						const int idx
							= glg_find_or_add_material (materials, &num_materials, hash);
						meshes[i].material_idx = idx;
						break;
					}
				}
		}
	}

	model_t model;
	memset (&model, 0, sizeof (model));
	model.meshes = meshes;
	model.num_meshes = num_out_meshes;
	model.materials = materials;
	model.num_materials = num_materials;

	const int rc = ExportModelToGLB (&model, out_glb_path);
	glg_staged_cleanup (&staged);
	FREE (materials);
	FREE (entries);

	for (uint i = 0; i < num_out_meshes; i++)
	{
		FREE (meshes[i].positions);
		FREE (meshes[i].texcoords);
		FREE (meshes[i].normals);
		FREE (meshes[i].vertices);
	}
	FREE (meshes);
	FREE (mesh_src_entry);

	return rc == 0 ? ERR_OK : ERR_CANT_CREATE;
}

//-----------------------------------------------------------------------------
// Encoder: inverse of DecodeGLG. Not a byte-exact reproduction of a retail
// file (several mesh-table fields have no recovered meaning, and the
// original per-mesh vertex/hash metadata isn't representable in a GLB), but
// deterministic and self-consistent: decode(encode(m)) reconstructs the same
// triangles decode(original) would have, so encode -> GLB -> encode is a
// true canonical fixed point, the same standard this project's other
// decode/encode pairs (MSH, MOD, HSD, ...) are held to.
//
// Geometry is written with the format's own triangle-list primitive
// (face_type 0), which DecodeGLG reads back one-for-one.

typedef struct glg_out_buf_t
{
	u8 *data;
	u32 size, cap;
} glg_out_buf_t;

static void glg_buf_reserve (glg_out_buf_t *b, u32 extra)
{
	if (b->size + extra <= b->cap)
		return;
	b->cap = (b->size + extra) * 2 + 64;
	b->data = REALLOC (b->data, b->cap);
}

static void glg_buf_put (glg_out_buf_t *b, const void *src, u32 n)
{
	glg_buf_reserve (b, n);
	memcpy (b->data + b->size, src, n);
	b->size += n;
}

static void glg_buf_put32 (glg_out_buf_t *b, u32 v)
{
	u8 tmp[4];
	glg_put32 (tmp, v);
	glg_buf_put (b, tmp, 4);
}

static void glg_buf_put16 (glg_out_buf_t *b, u16 v)
{
	u8 tmp[2];
	glg_put16 (tmp, v);
	glg_buf_put (b, tmp, 2);
}

static void glg_buf_zero (glg_out_buf_t *b, u32 n)
{
	glg_buf_reserve (b, n);
	memset (b->data + b->size, 0, n);
	b->size += n;
}

enumError EncodeGLG (const model_t *model, ccp out_path)
{
	if (!model || !model->num_meshes || !out_path)
		return ERR_NOTHING_TO_DO;

	glg_out_buf_t mesh_buf = { 0 }, vapd_buf = { 0 }, vert_buf = { 0 }, idx_buf = { 0 };

	for (uint mi = 0; mi < model->num_meshes; mi++)
	{
		const mesh_t *mesh = model->meshes + mi;
		if (!mesh->num_vertices || mesh->num_vertices % 3)
			continue;

		// Weld (position,texcoord) pairs into a fresh, mesh-local, densely
		// numbered vertex set -- GLG shares one index across every attribute
		// array, unlike the model_t vertex_t's independent per-attribute
		// indices.
		const size_t num_corners = mesh->num_vertices;
		u32 *welded = MALLOC (num_corners * sizeof (u32));
		vec3_t *out_pos = MALLOC (num_corners * sizeof (vec3_t));
		vec2_t *out_uv = mesh->texcoords ? MALLOC (num_corners * sizeof (vec2_t)) : NULL;
		u32 num_welded = 0;

		for (size_t c = 0; c < num_corners; c++)
		{
			const vertex_t *v = mesh->vertices + c;
			const int uv_idx = mesh->texcoords ? v->texcoord_idx : -1;
			u32 found = num_welded;
			for (u32 w = 0; w < num_welded; w++)
			{
				// welded[w] holds the corner index it was created from;
				// compare against that corner's own indices.
				const vertex_t *wv = mesh->vertices + welded[w];
				if (wv->position_idx == v->position_idx
					&& (uv_idx < 0 || wv->texcoord_idx == uv_idx))
				{
					found = w;
					break;
				}
			}
			if (found == num_welded)
			{
				welded[num_welded] = (u32) c;
				out_pos[num_welded] = mesh->positions[v->position_idx];
				if (out_uv)
					out_uv[num_welded] = mesh->texcoords[uv_idx < 0 ? 0 : uv_idx];
				num_welded++;
			}
			// Reuse welded[] as the corner's assigned combined index by
			// overwriting a scratch array indexed by corner -- see below.
		}

		// Second pass: build the per-corner combined-index list now that
		// welding is resolved (kept separate from the loop above so the
		// O(num_welded) welded[] search there isn't disturbed by reuse).
		u32 *combined = MALLOC (num_corners * sizeof (u32));
		for (size_t c = 0; c < num_corners; c++)
		{
			const vertex_t *v = mesh->vertices + c;
			const int uv_idx = mesh->texcoords ? v->texcoord_idx : -1;
			for (u32 w = 0; w < num_welded; w++)
			{
				const vertex_t *wv = mesh->vertices + welded[w];
				if (wv->position_idx == v->position_idx
					&& (uv_idx < 0 || wv->texcoord_idx == uv_idx))
				{
					combined[c] = w;
					break;
				}
			}
		}
		FREE (welded);

		if (num_welded < 3 || num_welded > 0xfffe)
		{
			FREE (combined);
			FREE (out_pos);
			FREE (out_uv);
			continue;
		}

		// Mesh table entry (written now, patched with offsets once known).
		const u32 mesh_entry_off = mesh_buf.size;
		glg_buf_zero (&mesh_buf, GLG_MESH_SIZE);
		u8 *entry = mesh_buf.data + mesh_entry_off;
		glg_put16 (entry + 2, 0); // face_format
		// entry+4 (face_off) and entry+8 (face_count) are patched below,
		// once the index bytes for this mesh have actually been written.
		entry[10] = 0;					   // face_type
		entry[11] = (u8) (out_uv ? 2 : 1);		   // attr_count
		glg_put32 (entry + 12, vapd_buf.size);		   // vapd_off

		// VAPD records for this mesh.
		u8 vapd_pos[6];
		glg_put32 (vapd_pos, vert_buf.size);
		vapd_pos[4] = GLG_ATTR_POSITION;
		vapd_pos[5] = 6;
		glg_buf_put (&vapd_buf, vapd_pos, 6);

		for (u32 v = 0; v < num_welded; v++)
		{
			// Inverse of DecodeGLG's -90 deg X rotation: (x,y,z) -> (x,-z,y).
			const float x = out_pos[v].x;
			const float y = -out_pos[v].z;
			const float z = out_pos[v].y;
			u8 tmp[6];
			glg_put16 (tmp, (s16) lroundf (x * 1024.0f));
			glg_put16 (tmp + 2, (s16) lroundf (y * 1024.0f));
			glg_put16 (tmp + 4, (s16) lroundf (z * 1024.0f));
			glg_buf_put (&vert_buf, tmp, 6);
		}

		if (out_uv)
		{
			u8 vapd_uv[6];
			glg_put32 (vapd_uv, vert_buf.size);
			vapd_uv[4] = GLG_ATTR_TEXCOORD0;
			vapd_uv[5] = 4;
			glg_buf_put (&vapd_buf, vapd_uv, 6);

			for (u32 v = 0; v < num_welded; v++)
			{
				u8 tmp[4];
				glg_put16 (tmp, (u16) lroundf (out_uv[v].u * 1024.0f));
				glg_put16 (tmp + 2, (u16) lroundf (out_uv[v].v * 1024.0f));
				glg_buf_put (&vert_buf, tmp, 4);
			}
		}
		FREE (out_pos);
		FREE (out_uv);

		// Index stream: a plain triangle list, matching the face_type 0 the
		// mesh entry declares. The format's own list primitive is a direct
		// inverse of the decoder, so no strip bridging or winding
		// compensation is needed.
		const u32 face_off = idx_buf.size;
		for (size_t c = 0; c < num_corners; c++)
			glg_buf_put16 (&idx_buf, (u16) combined[c]);
		FREE (combined);

		const u32 face_count = (idx_buf.size - face_off) / 2;
		glg_put32 (entry + 4, face_off);
		glg_put16 (entry + 8, (u16) face_count);
	}

	if (!vert_buf.size || !idx_buf.size)
	{
		FREE (mesh_buf.data);
		FREE (vapd_buf.data);
		FREE (vert_buf.data);
		FREE (idx_buf.data);
		return ERR_NOTHING_TO_DO;
	}

	// Model table: one entry spanning every emitted mesh.
	glg_out_buf_t model_buf = { 0 };
	glg_buf_put32 (&model_buf, mesh_buf.size / GLG_MESH_SIZE);
	glg_buf_put32 (&model_buf, 0); // hash
	glg_buf_zero (&model_buf, 8);

	// Root transform: identity 4x4.
	static const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	glg_out_buf_t matrix_buf = { 0 };
	for (uint i = 0; i < 16; i++)
	{
		u32 bits;
		memcpy (&bits, identity + i, 4);
		glg_buf_put32 (&matrix_buf, bits);
	}

	glg_out_buf_t out = { 0 };
	glg_buf_put32 (&out, 0x8001b000);
	glg_buf_put32 (&out, 0); // total length, patched below

	struct
	{
		u32 tag;
		glg_out_buf_t *buf;
	} chunks[] = {
		{ 0x0001b003, &model_buf },
		{ 0x0001b004, &mesh_buf },
		{ 0x0001b002, &matrix_buf },
		{ 0x0001b005, &vapd_buf },
		{ 0x0001b006, &vert_buf },
		{ 0x0001b007, &idx_buf },
	};
	for (uint i = 0; i < sizeof (chunks) / sizeof (*chunks); i++)
	{
		glg_buf_put32 (&out, chunks[i].tag);
		glg_buf_put32 (&out, chunks[i].buf->size);
		glg_buf_put (&out, chunks[i].buf->data, chunks[i].buf->size);
	}
	glg_put32 (out.data + 4, out.size - 8);

	FREE (model_buf.data);
	FREE (matrix_buf.data);
	FREE (mesh_buf.data);
	FREE (vapd_buf.data);
	FREE (vert_buf.data);
	FREE (idx_buf.data);

	const enumError rc = SaveFILE (out_path, 0, true, out.data, out.size, 0);
	FREE (out.data);
	return rc;
}

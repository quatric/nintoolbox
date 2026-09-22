// SPDX-License-Identifier: GPL-2.0+
#include "lib-std.h"
#include "lib-zmb.h"

//-----------------------------------------------------------------------------
///////////////		Konami ZMB model decode				///////////////
//-----------------------------------------------------------------------------
//
// Layout recovered by decompiling the ZMB loader in the disc's sys/main.dol
// (function at 0x800cbab4 in the DDR: Winx Club (Europe) build, found via
// its cross-references to the literal bone-name strings "trans", "ZDRAW"
// and a 22-entry table of known biped bone names), not guessed from data.
// Cross-checked against several retail DATA/files/character/*.bin and
// DATA/files/accessory/*.bin samples.
//
// A file has exactly ONE "WII\0" wrapper at its start, around one or two
// "ZMB GC\0\0" chunks placed back to back (a character file is body+head:
// two chunks; a prop file is one chunk) -- the second chunk, if present,
// has no wrapper of its own. Wrapper header (offsets from file start):
//   +0x00  "WII\0"
//   +0x04  f32 1.0
//   +0x08  u32 2 (unknown constant)
//   +0x0c  u32 0
//   +0x10  u32 chunk 0 offset (0x20 in every sample seen)
//   +0x14  u32 chunk 0 end offset (== chunk 1 offset, when present)
//   +0x18  u32 chunk 1 offset (0 if there is no second chunk)
//   +0x1c  u32 chunk 1 size (runs to EOF)
//
// Chunk header (offsets from the chunk's own "ZMB GC" start):
//   +0x00  "ZMB GC\0\0"
//   +0x14  f32 1.0
//   +0x18  u32 offset to the texture-name table
//   +0x1c  u32 offset to the material table
//   +0x20  u32 offset to the bone table
// All three offsets are chunk-relative until the loader relocates them
// (tracked by a "relocated" flag the game stores back into the file's own
// in-memory copy at chunk+0x24 -- irrelevant for a read-only decoder, every
// offset below is simply chunk-relative).
//
// Texture-name table: u32 count, then 'count' fixed 0x20-byte NUL-padded
// ASCII '.tga' names.
//
// Material table: u32 count, f32 LOD-switch threshold, u32 offset to the
// record array. Records are NOT fixed size: the loader compares the record
// array's own second field (a float) against a compiled-in threshold and
// picks a per-table record stride of 0x38 or 0x50 (a whole-table
// high/low-detail toggle) -- since the threshold is a constant baked into
// main.dol this decoder never reads, it instead trusts 0x50 (both retail
// samples' 0x50-strided records decode to clean, identical-looking colour
// data across every material, while 0x38 produces garbage past the first
// record on the multi-material sample). A 0x50-byte record's first 8
// bytes are two RGBA8 colours (both `0x959595ff` on every sample so far --
// plausibly a neutral default, since real material variation likely comes
// from its bound texture rather than this colour); a field 12 bytes later
// looks like a texture-layer count, but which texture(s) a material binds
// is not resolved. This decoder does not attempt to pick apart individual
// material fields yet -- it only walks the table far enough to report the
// count.
//
// Bone table: u32 count, f32 2.0 (unknown constant), u32 offset to the
// record array. Records are a fixed 0xa0 bytes:
//   +0x00  name[0x20]        ASCII or Shift-JIS, e.g. bone 0 is always
//                             "\x83\x56\x81\x5b\x83\x93 \x83\x8b\x81\x5b\x83\x67"
//                             (Shift-JIS "Scene Root")
//   +0x2c  u32 type           2 = placeholder (no geometry); 4 = single
//                             named anchor point (+0x70/+0x74/+0x78 is one
//                             {x,y,z}, not a bounding box); anything else
//                             carries a mesh and a bind-pose transform
//   +0x30..+0x6c  3x4 float bind-pose transform (three rows of 4 floats)
//   +0x70,+0x74,+0x78  f32[3] anchor point (type 4 only)
//   +0x94  u32 -1 sentinel when type == 2
//   +0x9a  u16 submesh count
//   +0x9c  u32 offset to the submesh record array
//
// Submesh record: fixed 0x40 bytes, owning four parallel attribute pools
// (all chunk-relative offsets; a null offset means that attribute is absent):
//   +0x00  u32 material index into the chunk's material table (verified:
//          CHR040.bin's "wb_04_body" bone has 3 submeshes using indices
//          2, 3, 4 out of that chunk's 5 materials, one index each, no
//          repeats or gaps -- a clean fit for "which material this
//          submesh's faces are drawn with")
//   +0x04  u16 LOD/format flag (nonzero selects a richer 0x20-byte vertex-
//          block layout instead of 0x14, with 3 more optional index arrays
//          this decoder does not read)
//   +0x0a  u16 vertex-block count
//   +0x0c  u32 position-pool entry count
//   +0x14  u32 normal-pool entry count
//   +0x18  u32 UV-pool flag (nonzero -- the actual count is implied by the
//          index arrays, not stored separately)
//   +0x1c  u32 colour-pool entry count (unreliable -- see below)
//   +0x20  u32 offset to the vertex-block array
//   +0x24  u32 offset to the position pool: tightly packed f32[3], 12-byte
//          stride (also confirmed independently by bbox cross-checking
//          against bone anchor points on single-submesh samples)
//   +0x2c  u32 offset to the normal pool: f32[3], 12-byte stride
//   +0x30  u32 offset to the UV pool: f32[2], 8-byte stride
//   +0x34  u32 offset to the colour pool: RGBA8, 4-byte stride (the loader's
//          default-fill path writes {0,0,0,0xff} per entry when this is
//          absent, which is how the 4-byte stride was inferred; the count
//          field above was observed smaller than the highest index actually
//          used against it on a real sample, so trust the index values, not
//          this count, when sizing the pool)
//
// Vertex-block record: fixed 0x14 bytes (0x20 with the LOD flag above):
//   +0x00  u16 unknown (1 in every sample seen)
//   +0x02  u16 N, the polygon's corner count (observed 3..7 -- arbitrary
//          N-gons, not just triangles/quads)
//   +0x04  u32 offset to a u32[N] index array into the position pool
//   +0x08  u32 offset to a u32[N] index array into the normal pool (0 if
//          the polygon has no normals)
//   +0x0c  u32 offset to a u32[N] index array into the UV pool (0 if none)
//   +0x10  u32 offset to a u32[N] index array into the colour pool (0 if
//          none)
// All four index arrays, when present, are parallel: index array entry i
// names the attribute for the polygon's i-th corner. Verified end to end on
// two retail samples (mii_head_acc08.bin, 2 quads; mii_head_acc07.bin, 14
// N-gon faces): position indices decode to sane, bone-anchor-bounded
// coordinates, and the UV pool decodes to an exact unit-square {0,1}/{0,0}/
// {1,0}/{1,1} on the first sample -- not a coincidence four floats would
// produce by chance.
//
// Bone hierarchy, for composing a submesh's local-pool geometry into a
// single posed model: each bone record also carries
//   +0x30..+0x5c  3x3 float rotation, row-major, 0x10-byte row stride
//                 (the 4th float of each 0x10 row, printed above as part of
//                 a naively-assumed 3x4 matrix, is unused padding)
//   +0x60,+0x64,+0x68  f32[3] translation, relative to the parent bone
//   +0x94  s32 parent bone index, or -1 for the root
// (found by noticing +0x94 -- previously read only as a -1 "no mesh"
// sentinel for type 2 -- is in fact a complete, valid parent-index array:
// e.g. on CHR040.bin, bone 9 "RightLeg" has +0x94 = 8 = bone 8
// "RightUpLeg", bone 16 "LeftCollar" has +0x94 = 14 = bone 14 "Spine1", and
// so on for the entire 54-bone rig). A bone's world transform is its
// parent's world transform composed with its own local rotation+
// translation; a submesh's position pool is in its owning bone's local
// space (translation magnitudes match plausible limb lengths, e.g.
// "LeftLeg"'s +0x60 x-translation of 3.485 is the thigh length), so
// world_vertex = world_transform(owning_bone) * local_vertex.
//
// Not decoded here: per-vertex bone-index skinning (assigned by the game at
// load time via a closest-bone match against submesh+0x10/+0x28, not stored
// as blend weights -- this decoder instead attaches each submesh rigidly to
// its one owning bone), the material<->texture binding, and the trailing
// subsection (presumed embedded GX texture pixels).

static inline u32 zmb_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static inline float zmb_bef32 (const u8 *p)
{
	u32 bits = zmb_be32 (p);
	float f;
	memcpy (&f, &bits, 4);
	return f;
}

static inline u16 zmb_be16 (const u8 *p)
{
	return (u16)p[0] << 8 | p[1];
}

static bool zmb_is_chunk (const u8 *data, uint size, u32 off)
{
	return off + 8 <= size && !memcmp (data + off, "ZMB GC\0\0", 8);
}

static bool zmb_in_bounds ( uint size, u32 off, u32 len )
{
	return off <= size && len <= size - off;
}

bool IsZMB (const u8 *data, uint size)
{
	if ( size < 0x24 || memcmp (data, "WII\0", 4) )
		return false;
	const u32 chunk_off = zmb_be32 (data + 0x10);
	return zmb_is_chunk (data, size, chunk_off);
}

//-----------------------------------------------------------------------------

static void zmb_dump_name ( FILE *f, ccp key, const u8 *name, uint max_len )
{
	uint len = 0;
	while ( len < max_len && name[len] )
		len++;
	fprintf (f, "%s: \"", key);
	for ( uint i = 0; i < len; i++ )
	{
		const u8 c = name[i];
		if ( c == '"' || c == '\\' )
			fputc ('\\', f);
		if ( c >= 0x20 && c < 0x7f )
			fputc (c, f);
		else
			fprintf (f, "\\x%02x", c);
	}
	fprintf (f, "\"\n");
}

static void zmb_dump_chunk ( FILE *f, const u8 *data, uint size, u32 chunk_off, ccp label )
{
	const u8 *chunk = data + chunk_off;
	fprintf (f, "- chunk: %s\n", label);
	fprintf (f, "  offset: 0x%x\n", chunk_off);

	//--- textures: {count, 0, name-array offset (chunk-relative)}, then
	// 'count' fixed 0x20-byte NUL-padded names at that offset.
	const u32 tex_hdr = chunk_off + zmb_be32 (chunk + 0x18);
	if ( tex_hdr + 0xc <= size )
	{
		const u32 tex_count = zmb_be32 (data + tex_hdr);
		const u32 tex_names = chunk_off + zmb_be32 (data + tex_hdr + 8);
		fprintf (f, "  textures:\n");
		for ( u32 i = 0; i < tex_count && tex_names + (i + 1) * 0x20 <= size; i++ )
		{
			fprintf (f, "    - ");
			zmb_dump_name (f, "name", data + tex_names + i * 0x20, 0x20);
		}
	}

	//--- materials (count + stride only; per-record fields not decoded)
	const u32 mat_off = chunk_off + zmb_be32 (chunk + 0x1c);
	u32 mat_count = 0;
	if ( mat_off + 4 <= size )
	{
		mat_count = zmb_be32 (data + mat_off);
		fprintf (f, "  material_count: %u\n", mat_count);
	}

	//--- bones
	const u32 bone_off = chunk_off + zmb_be32 (chunk + 0x20);
	if ( bone_off + 0xc > size )
		return;
	const u32 bone_count = zmb_be32 (data + bone_off);
	// Relocated relative to the CHUNK start, not the bone table itself
	// (matches the decompiled loader: "local_50[2] = iVar54 + local_50[2]").
	const u32 bone_rec = chunk_off + zmb_be32 (data + bone_off + 8);
	fprintf (f, "  bone_count: %u\n", bone_count);
	fprintf (f, "  bones:\n");

	enum { BONE_STRIDE = 0xa0, SUBMESH_STRIDE = 0x40 };

	for ( u32 i = 0; i < bone_count; i++ )
	{
		const u32 b = bone_rec + i * BONE_STRIDE;
		if ( b + BONE_STRIDE > size )
			break;

		fprintf (f, "    - ");
		zmb_dump_name (f, "name", data + b, 0x20);

		const u32 type = zmb_be32 (data + b + 0x2c);
		fprintf (f, "      type: %u\n", type);

		if ( type == 4 )
		{
			fprintf (f, "      anchor: [ %g, %g, %g ]\n",
				zmb_bef32 (data + b + 0x70),
				zmb_bef32 (data + b + 0x74),
				zmb_bef32 (data + b + 0x78) );
		}
		else if ( type != 2 )
		{
			fprintf (f, "      bind_pose:\n");
			for ( uint row = 0; row < 3; row++ )
				fprintf (f, "        - [ %g, %g, %g, %g ]\n",
					zmb_bef32 (data + b + 0x30 + row * 0x10),
					zmb_bef32 (data + b + 0x34 + row * 0x10),
					zmb_bef32 (data + b + 0x38 + row * 0x10),
					zmb_bef32 (data + b + 0x3c + row * 0x10) );
		}

		const u16 submesh_count = zmb_be16 (data + b + 0x9a);
		if ( !submesh_count )
			continue;

		// Chunk-relative on disk, like the texture/material/bone table
		// offsets above (the game relocates it in its own RAM copy only).
		const u32 sub0 = chunk_off + zmb_be32 (data + b + 0x9c);

		fprintf (f, "      submesh_count: %u\n", submesh_count);
		fprintf (f, "      submeshes:\n");
		for ( u16 s = 0; s < submesh_count; s++ )
		{
			const u32 sm = sub0 + s * SUBMESH_STRIDE;
			if ( sm + SUBMESH_STRIDE > size )
				break;
			const u16 vblock_count = zmb_be16 (data + sm + 0xa);
			const u32 material_idx = zmb_be32 (data + sm);
			fprintf (f, "        - material_index: %u\n", material_idx);
			fprintf (f, "          vertex_block_count: %u\n", vblock_count);
		}
	}
}

//-----------------------------------------------------------------------------
///////////////		bone world-transform composition		///////////////
//-----------------------------------------------------------------------------

typedef struct zmb_mat_t
{
	float r[9]; // 3x3 rotation, row-major: r[row*3+col]
	float t[3]; // translation
}
zmb_mat_t;

static const zmb_mat_t zmb_identity = { { 1,0,0, 0,1,0, 0,0,1 }, { 0, 0, 0 } };

static void zmb_mat_apply ( const zmb_mat_t *m, const float in[3], float out[3], bool translate )
{
	for ( uint row = 0; row < 3; row++ )
		out[row] = m->r[row * 3] * in[0] + m->r[row * 3 + 1] * in[1] + m->r[row * 3 + 2] * in[2]
			+ ( translate ? m->t[row] : 0 );
}

// world = parent_world composed with this bone's own local rotation+translation.
static void zmb_mat_compose ( const zmb_mat_t *parent, const zmb_mat_t *local, zmb_mat_t *out )
{
	for ( uint row = 0; row < 3; row++ )
		for ( uint col = 0; col < 3; col++ )
			out->r[row * 3 + col] = parent->r[row * 3] * local->r[col]
				+ parent->r[row * 3 + 1] * local->r[3 + col]
				+ parent->r[row * 3 + 2] * local->r[6 + col];
	zmb_mat_apply (parent, local->t, out->t, true);
}

// One zmb_mat_t per bone record in table order, or NULL on allocation
// failure (callers must then fall back to unposed/identity geometry).
// 'base' re-roots the whole chunk under an outside transform (used to
// attach a character's head chunk to the body chunk's mii_head bone); pass
// &zmb_identity for a chunk's own, unattached hierarchy.
static zmb_mat_t * zmb_compute_world_mats ( const u8 *data, uint size, u32 bone_rec, u32 bone_count,
	const zmb_mat_t *base )
{
	zmb_mat_t *world = MALLOC (bone_count * sizeof(*world));
	if (!world)
		return 0;

	for ( u32 i = 0; i < bone_count; i++ )
	{
		const u32 b = bone_rec + i * 0xa0;
		zmb_mat_t local;
		// The stored 3 rows of 4 floats (0x10-byte stride) are columns of
		// the rotation matrix, not rows -- confirmed empirically: reading
		// them as rows composes a leg chain that runs UP from the hip;
		// transposed, it runs down to a foot near y=0 and a toe that
		// extends forward, exactly as a bind-pose leg should.
		for ( uint row = 0; row < 3; row++ )
			for ( uint col = 0; col < 3; col++ )
				local.r[row * 3 + col] = zmb_bef32 (data + b + 0x30 + col * 0x10 + row * 4);
		local.t[0] = zmb_bef32 (data + b + 0x60);
		local.t[1] = zmb_bef32 (data + b + 0x64);
		local.t[2] = zmb_bef32 (data + b + 0x68);

		const s32 parent = (s32)zmb_be32 (data + b + 0x94);
		if ( parent < 0 ) // root: re-rooted under 'base' instead of left at identity
			zmb_mat_compose (base, &local, &world[i]);
		else if ( (u32)parent >= i ) // not yet computed (corrupt/out-of-order data): best effort
			world[i] = local;
		else
			zmb_mat_compose (&world[parent], &local, &world[i]);
	}
	return world;
}

// Find a bone by exact name within a chunk's own table (used to locate the
// body chunk's "mii_head" attachment point); returns bone index or -1.
static s32 zmb_find_bone ( const u8 *data, uint size, u32 bone_rec, u32 bone_count, ccp name )
{
	const size_t len = strlen (name);
	if ( len >= 0x20 )
		return -1;
	for ( u32 i = 0; i < bone_count; i++ )
	{
		const u32 b = bone_rec + i * 0xa0;
		if ( !zmb_in_bounds (size, b, 0x20) )
			break;
		if ( !memcmp (data + b, name, len) && ( len == 0x20 || !data[b + len] ) )
			return (s32)i;
	}
	return -1;
}

//-----------------------------------------------------------------------------
///////////////		mesh export (Wavefront .obj)			///////////////
//-----------------------------------------------------------------------------

typedef struct zmb_obj_ctx_t
{
	FILE	*f;
	u32	next_pos;  // 1-based running OBJ vertex-index base
	u32	next_norm;
	u32	next_uv;
}
zmb_obj_ctx_t;

// Write one submesh's position/normal/UV pools as 'v'/'vn'/'vt' lines, then
// one fan-triangulated 'f' line per vertex block. Silently skips whatever
// doesn't fit in 'size' (best-effort against truncated/corrupt input).
static void zmb_write_submesh_obj ( zmb_obj_ctx_t *ctx, const u8 *data, uint size,
	u32 chunk_off, u32 sm, ccp group_name, const zmb_mat_t *world, ccp mtl_name )
{
	const u32 pos_off  = chunk_off + zmb_be32 (data + sm + 0x24);
	const u32 norm_off = chunk_off + zmb_be32 (data + sm + 0x2c);
	const u32 uv_off   = chunk_off + zmb_be32 (data + sm + 0x30);
	const u32 pos_cnt  = zmb_be32 (data + sm + 0xc);
	const u32 norm_cnt = zmb_be32 (data + sm + 0x14);
	const u16 vblock_count = zmb_be16 (data + sm + 0xa);
	const u16 lod_flag = zmb_be16 (data + sm + 4);
	const u32 vb_stride = lod_flag ? 0x20 : 0x14;
	const u32 vb_arr = chunk_off + zmb_be32 (data + sm + 0x20);

	if ( !vblock_count || !zmb_in_bounds (size, vb_arr, (u32)vblock_count * vb_stride) )
		return;

	// A submesh's UV pool has no reliable stored count (see the comment
	// above), so size it from the highest index any of its vertex blocks
	// actually uses.
	u32 uv_cnt = 0;
	for ( u16 s = 0; s < vblock_count; s++ )
	{
		const u32 vb = vb_arr + s * vb_stride;
		const u16 n = zmb_be16 (data + vb + 2);
		const u32 uv_idx = zmb_be32 (data + vb + 0xc);
		if ( !uv_idx || !n )
			continue;
		const u32 idx_arr = chunk_off + uv_idx;
		if ( !zmb_in_bounds (size, idx_arr, (u32)n * 4) )
			continue;
		for ( u16 c = 0; c < n; c++ )
		{
			const u32 idx = zmb_be32 (data + idx_arr + c * 4);
			if ( idx + 1 > uv_cnt )
				uv_cnt = idx + 1;
		}
	}

	const bool have_pos  = pos_cnt  && zmb_in_bounds (size, pos_off,  pos_cnt  * 12);
	const bool have_norm = norm_cnt && zmb_in_bounds (size, norm_off, norm_cnt * 12);
	const bool have_uv   = uv_cnt   && zmb_in_bounds (size, uv_off,   uv_cnt   * 8);
	if ( !have_pos )
		return;

	fprintf (ctx->f, "g %s\nusemtl %s\n", group_name, mtl_name);
	for ( u32 i = 0; i < pos_cnt; i++ )
	{
		float in[3] = {
			zmb_bef32 (data + pos_off + i * 12),
			zmb_bef32 (data + pos_off + i * 12 + 4),
			zmb_bef32 (data + pos_off + i * 12 + 8) };
		float out[3];
		zmb_mat_apply (world, in, out, true);
		fprintf (ctx->f, "v %g %g %g\n", out[0], out[1], out[2]);
	}
	if ( have_norm )
		for ( u32 i = 0; i < norm_cnt; i++ )
		{
			float in[3] = {
				zmb_bef32 (data + norm_off + i * 12),
				zmb_bef32 (data + norm_off + i * 12 + 4),
				zmb_bef32 (data + norm_off + i * 12 + 8) };
			float out[3];
			zmb_mat_apply (world, in, out, false);
			fprintf (ctx->f, "vn %g %g %g\n", out[0], out[1], out[2]);
		}
	if ( have_uv )
		for ( u32 i = 0; i < uv_cnt; i++ )
			fprintf (ctx->f, "vt %g %g\n",
				zmb_bef32 (data + uv_off + i * 8),
				zmb_bef32 (data + uv_off + i * 8 + 4) );

	for ( u16 s = 0; s < vblock_count; s++ )
	{
		const u32 vb = vb_arr + s * vb_stride;
		const u16 n = zmb_be16 (data + vb + 2);
		if ( n < 3 || n > 256 )
			continue;

		const u32 pos_idx_off  = chunk_off + zmb_be32 (data + vb + 4);
		const u32 norm_idx_off = zmb_be32 (data + vb + 8) ? chunk_off + zmb_be32 (data + vb + 8) : 0;
		const u32 uv_idx_off   = zmb_be32 (data + vb + 0xc) ? chunk_off + zmb_be32 (data + vb + 0xc) : 0;
		if ( !zmb_in_bounds (size, pos_idx_off, (u32)n * 4) )
			continue;
		if ( norm_idx_off && ( !have_norm || !zmb_in_bounds (size, norm_idx_off, (u32)n * 4) ) )
			continue;
		if ( uv_idx_off && ( !have_uv || !zmb_in_bounds (size, uv_idx_off, (u32)n * 4) ) )
			continue;

		u32 corner[256];
		for ( u16 c = 0; c < n; c++ )
			corner[c] = zmb_be32 (data + pos_idx_off + c * 4);

		// Fan-triangulate the N-gon: (0,c,c+1) for c in [1,N-2].
		for ( u16 c = 1; c + 1 < n; c++ )
		{
			fprintf (ctx->f, "f");
			const u16 tri[3] = { 0, c, (u16)(c + 1) };
			for ( uint k = 0; k < 3; k++ )
			{
				const u32 p = ctx->next_pos + corner[tri[k]];
				if ( uv_idx_off )
				{
					const u32 t = ctx->next_uv + zmb_be32 (data + uv_idx_off + tri[k] * 4);
					if ( norm_idx_off )
						fprintf (ctx->f, " %u/%u/%u", p, t,
							ctx->next_norm + zmb_be32 (data + norm_idx_off + tri[k] * 4));
					else
						fprintf (ctx->f, " %u/%u", p, t);
				}
				else if ( norm_idx_off )
					fprintf (ctx->f, " %u//%u", p,
						ctx->next_norm + zmb_be32 (data + norm_idx_off + tri[k] * 4));
				else
					fprintf (ctx->f, " %u", p);
			}
			fprintf (ctx->f, "\n");
		}
	}

	ctx->next_pos  += pos_cnt;
	ctx->next_norm += have_norm ? norm_cnt : 0;
	ctx->next_uv   += have_uv ? uv_cnt : 0;
}

// Locates a chunk's bone table; returns false (and leaves *bone_count/
// *bone_rec untouched) if the chunk header doesn't check out.
static bool zmb_chunk_bones ( const u8 *data, uint size, u32 chunk_off, u32 *bone_count, u32 *bone_rec )
{
	const u32 bone_off = chunk_off + zmb_be32 (data + chunk_off + 0x20);
	if ( !zmb_in_bounds (size, bone_off, 0xc) )
		return false;
	const u32 count = zmb_be32 (data + bone_off);
	const u32 rec = chunk_off + zmb_be32 (data + bone_off + 8);
	if ( !zmb_in_bounds (size, rec, count * 0xa0) )
		return false;
	*bone_count = count;
	*bone_rec = rec;
	return true;
}

static void zmb_write_chunk_obj ( zmb_obj_ctx_t *ctx, const u8 *data, uint size,
	u32 chunk_off, uint chunk_idx, const zmb_mat_t *base )
{
	u32 bone_count, bone_rec;
	if ( !zmb_chunk_bones (data, size, chunk_off, &bone_count, &bone_rec) )
		return;

	zmb_mat_t *world = zmb_compute_world_mats (data, size, bone_rec, bone_count, base);

	enum { BONE_STRIDE = 0xa0, SUBMESH_STRIDE = 0x40 };
	for ( u32 i = 0; i < bone_count; i++ )
	{
		const u32 b = bone_rec + i * BONE_STRIDE;
		if ( !zmb_in_bounds (size, b, BONE_STRIDE) )
			break;

		const u16 submesh_count = zmb_be16 (data + b + 0x9a);
		if ( !submesh_count )
			continue;
		const u32 sub0 = chunk_off + zmb_be32 (data + b + 0x9c);
		const zmb_mat_t *bone_world = world ? &world[i] : &zmb_identity;

		char bone_name[0x21];
		uint len = 0;
		while ( len < 0x20 && data[b + len] )
			len++;
		memcpy (bone_name, data + b, len);
		bone_name[len] = 0;
		for ( uint k = 0; k < len; k++ )
			if ( (u8)bone_name[k] < 0x20 || (u8)bone_name[k] >= 0x7f || bone_name[k] == ' ' )
				bone_name[k] = '_';

		for ( u16 s = 0; s < submesh_count; s++ )
		{
			const u32 sm = sub0 + s * SUBMESH_STRIDE;
			if ( !zmb_in_bounds (size, sm, SUBMESH_STRIDE) )
				break;
			char group[80];
			snprintf (group, sizeof(group), "chunk%u_%s%s_sm%u",
				chunk_idx, len ? bone_name : "bone", len ? "" : "0", s);
			char mtl_name[40];
			snprintf (mtl_name, sizeof(mtl_name), "chunk%u_mat%u", chunk_idx, zmb_be32 (data + sm));
			zmb_write_submesh_obj (ctx, data, size, chunk_off, sm, group, bone_world, mtl_name);
		}
	}

	if (world)
		FREE (world);
}

static void zmb_replace_ext ( char *dest, uint dest_size, ccp src, ccp new_ext )
{
	ccp dot = strrchr (src, '.');
	ccp slash = strrchr (src, '/');
	const uint base_len = dot && ( !slash || dot > slash ) ? (uint)(dot - src) : (uint)strlen (src);
	snprintf (dest, dest_size, "%.*s%s", base_len, src, new_ext);
}

// Placeholder material per index in the chunk's material table -- a plain
// grey (matches the one colour value seen so far in retail records; which
// texture, if any, a material actually binds is not decoded, see lib-zmb.c
// above) -- so faces at least group correctly by material for later editing.
static void zmb_write_mtl_chunk ( FILE *f, const u8 *data, uint size, u32 chunk_off, uint chunk_idx )
{
	const u32 mat_off = chunk_off + zmb_be32 (data + chunk_off + 0x1c);
	if ( !zmb_in_bounds (size, mat_off, 4) )
		return;
	const u32 mat_count = zmb_be32 (data + mat_off);
	for ( u32 i = 0; i < mat_count; i++ )
		fprintf (f, "newmtl chunk%u_mat%u\nKd 0.584 0.584 0.584\n", chunk_idx, i);
}

//-----------------------------------------------------------------------------

enumError DecodeZMB ( const u8 *data, uint size, ccp out_path )
{
	if (!IsZMB (data, size))
		return ERR_NOTHING_TO_DO;

	FILE *f = fopen (out_path, "wb");
	if (!f)
		return ERROR0 (ERR_CANT_CREATE, "Can't create file: %s\n", out_path);

	fprintf (f, "%%YAML 1.1\n---\nformat: ZMB\nchunks:\n");

	const u32 chunk0_off = zmb_be32 (data + 0x10);
	zmb_dump_chunk (f, data, size, chunk0_off, "0");

	const u32 chunk1_off = zmb_be32 (data + 0x18);
	const bool have_chunk1 = chunk1_off && zmb_is_chunk (data, size, chunk1_off);
	if ( have_chunk1 )
		zmb_dump_chunk (f, data, size, chunk1_off, "1");

	fclose (f);

	char obj_path[PATH_MAX], mtl_path[PATH_MAX];
	zmb_replace_ext (obj_path, sizeof(obj_path), out_path, ".obj");
	zmb_replace_ext (mtl_path, sizeof(mtl_path), out_path, ".mtl");

	FILE *mtl = fopen (mtl_path, "wb");
	if (mtl)
	{
		zmb_write_mtl_chunk (mtl, data, size, chunk0_off, 0);
		if ( have_chunk1 )
			zmb_write_mtl_chunk (mtl, data, size, chunk1_off, 1);
		fclose (mtl);
	}

	FILE *obj = fopen (obj_path, "wb");
	if (obj)
	{
		ccp mtl_base = strrchr (mtl_path, '/');
		mtl_base = mtl_base ? mtl_base + 1 : mtl_path;
		fprintf (obj, "# Konami ZMB model, decoded by nintoolbox\n"
			"# Rigid per-bone attachment, no per-vertex skin blending -- see lib-zmb.c\n"
			"mtllib %s\n", mtl_base);
		zmb_obj_ctx_t ctx = { obj, 1, 1, 1 };
		zmb_write_chunk_obj (&ctx, data, size, chunk0_off, 0, &zmb_identity);

		if ( have_chunk1 )
		{
			// A character's second chunk (head/hair) is a standalone model
			// attached at chunk 0's "mii_head" bone; re-root its own
			// hierarchy under that bone's world transform so the two
			// chunks pose as one figure instead of two independent ones.
			zmb_mat_t head_base = zmb_identity;
			u32 bc0, br0;
			if ( zmb_chunk_bones (data, size, chunk0_off, &bc0, &br0) )
			{
				zmb_mat_t *world0 = zmb_compute_world_mats (data, size, br0, bc0, &zmb_identity);
				if (world0)
				{
					const s32 head_bone = zmb_find_bone (data, size, br0, bc0, "mii_head");
					if ( head_bone >= 0 )
						head_base = world0[head_bone];
					FREE (world0);
				}
			}
			zmb_write_chunk_obj (&ctx, data, size, chunk1_off, 1, &head_base);
		}
		fclose (obj);
	}

	return ERR_OK;
}

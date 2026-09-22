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
// high/low-detail toggle). This decoder does not attempt to pick apart
// individual material fields yet -- it only walks the table far enough to
// report the count and stride.
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
// Not decoded here: per-vertex bone-index skinning (assigned by the game at
// load time via a closest-bone match against submesh+0x10/+0x28, not stored
// as blend weights), the material<->texture binding, composing a submesh's
// geometry through its owning bone's (and ancestors') bind-pose transform,
// and the trailing subsection (presumed embedded GX texture pixels).

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
			fprintf (f, "        - vertex_block_count: %u\n", vblock_count);
		}
	}
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

static bool zmb_in_bounds ( uint size, u32 off, u32 len )
{
	return off <= size && len <= size - off;
}

// Write one submesh's position/normal/UV pools as 'v'/'vn'/'vt' lines, then
// one fan-triangulated 'f' line per vertex block. Silently skips whatever
// doesn't fit in 'size' (best-effort against truncated/corrupt input).
static void zmb_write_submesh_obj ( zmb_obj_ctx_t *ctx, const u8 *data, uint size,
	u32 chunk_off, u32 sm, ccp group_name )
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

	fprintf (ctx->f, "g %s\n", group_name);
	for ( u32 i = 0; i < pos_cnt; i++ )
		fprintf (ctx->f, "v %g %g %g\n",
			zmb_bef32 (data + pos_off + i * 12),
			zmb_bef32 (data + pos_off + i * 12 + 4),
			zmb_bef32 (data + pos_off + i * 12 + 8) );
	if ( have_norm )
		for ( u32 i = 0; i < norm_cnt; i++ )
			fprintf (ctx->f, "vn %g %g %g\n",
				zmb_bef32 (data + norm_off + i * 12),
				zmb_bef32 (data + norm_off + i * 12 + 4),
				zmb_bef32 (data + norm_off + i * 12 + 8) );
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

static void zmb_write_chunk_obj ( zmb_obj_ctx_t *ctx, const u8 *data, uint size,
	u32 chunk_off, uint chunk_idx )
{
	const u32 bone_off = chunk_off + zmb_be32 (data + chunk_off + 0x20);
	if ( !zmb_in_bounds (size, bone_off, 0xc) )
		return;
	const u32 bone_count = zmb_be32 (data + bone_off);
	const u32 bone_rec = chunk_off + zmb_be32 (data + bone_off + 8);

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
			zmb_write_submesh_obj (ctx, data, size, chunk_off, sm, group);
		}
	}
}

static void zmb_replace_ext ( char *dest, uint dest_size, ccp src, ccp new_ext )
{
	ccp dot = strrchr (src, '.');
	ccp slash = strrchr (src, '/');
	const uint base_len = dot && ( !slash || dot > slash ) ? (uint)(dot - src) : (uint)strlen (src);
	snprintf (dest, dest_size, "%.*s%s", base_len, src, new_ext);
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

	char obj_path[PATH_MAX];
	zmb_replace_ext (obj_path, sizeof(obj_path), out_path, ".obj");
	FILE *obj = fopen (obj_path, "wb");
	if (obj)
	{
		fprintf (obj, "# Konami ZMB model, decoded by nintoolbox\n"
			"# Geometry is in raw file pool coordinates, not posed through\n"
			"# the bone hierarchy -- see lib-zmb.c\n");
		zmb_obj_ctx_t ctx = { obj, 1, 1, 1 };
		zmb_write_chunk_obj (&ctx, data, size, chunk0_off, 0);
		if ( have_chunk1 )
			zmb_write_chunk_obj (&ctx, data, size, chunk1_off, 1);
		fclose (obj);
	}

	return ERR_OK;
}

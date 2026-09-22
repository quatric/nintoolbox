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
// Submesh record: fixed 0x40 bytes.
//   +0x04  u16 LOD/format flag (nonzero selects a richer vertex-block
//          layout and extra relocations this decoder does not decode)
//   +0x0a  u16 vertex-block count
//   +0x20  u32 offset to the vertex-block array
// Each vertex block holds a u16 index count and a relocated array of
// (12-byte-stride) indices into one shared f32[3] position pool for the
// whole chunk; the loader computes a min/max bounding box over each
// block's referenced positions on load, for runtime culling, which is
// what this decoder reports per submesh instead of raw geometry (the
// index-to-position addressing needed to emit real triangles is not
// fully traced yet -- see FORMATS.md).
//
// Not decoded here: UV coordinates, per-vertex bone-index skinning
// (assigned by the game at load time via a closest-bone match, not stored
// as blend weights), the material<->texture binding, and the trailing
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
	if ( chunk1_off && zmb_is_chunk (data, size, chunk1_off) )
		zmb_dump_chunk (f, data, size, chunk1_off, "1");

	fclose (f);
	return ERR_OK;
}

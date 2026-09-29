static void decompress_mpbin_lzss (u8 *out, uint out_size, const u8 *src, uint src_size)
{
	u8 window[1024] = { 0 };
	uint win_off = 958;
	uint dest_off = 0;
	uint src_off = 0;
	int code_word = 0;
	while (dest_off < out_size && src_off < src_size)
	{
		if ((code_word & 0x100) == 0)
		{
			if (src_off >= src_size)
				break;
			code_word = src[src_off++] | 0xFF00;
		}
		if (code_word & 1)
		{
			if (src_off >= src_size)
				break;
			u8 val = src[src_off++];
			out[dest_off++] = val;
			window[win_off] = val;
			win_off = (win_off + 1) % 1024;
		}
		else
		{
			if (src_off + 1 >= src_size)
				break;
			u8 b1 = src[src_off++];
			u8 b2 = src[src_off++];
			uint offset = ((b2 & 0xC0) << 2) | b1;
			uint copy_len = (b2 & 0x3F) + 3;
			for (uint i = 0; i < copy_len && dest_off < out_size; i++)
			{
				u8 val = window[(offset + i) % 1024];
				window[win_off] = val;
				win_off = (win_off + 1) % 1024;
				out[dest_off++] = val;
			}
		}
		code_word >>= 1;
	}
}

static void decompress_mpbin_slide (u8 *out, uint out_size, const u8 *src, uint src_size)
{
	uint dest_off = 0;
	uint src_off = 0;
	u32 code_word = 0;
	int bits_left = 0;
	while (dest_off < out_size && src_off < src_size)
	{
		if (bits_left == 0)
		{
			if (src_off + 4 > src_size)
				break;
			code_word = ((u32)src[src_off] << 24) | ((u32)src[src_off + 1] << 16)
				| ((u32)src[src_off + 2] << 8) | src[src_off + 3];
			src_off += 4;
			bits_left = 32;
		}
		if (code_word & 0x80000000)
		{
			if (src_off >= src_size)
				break;
			out[dest_off++] = src[src_off++];
		}
		else
		{
			if (src_off + 1 >= src_size)
				break;
			u8 b1 = src[src_off++];
			u8 b2 = src[src_off++];
			uint dist_back = (((b1 & 0x0F) << 8) | b2) + 1;
			uint copy_len = ((b1 & 0xF0) >> 4) + 2;
			if (copy_len == 2)
			{
				if (src_off >= src_size)
					break;
				copy_len = src[src_off++] + 18;
			}
			for (uint i = 0; i < copy_len && dest_off < out_size; i++)
			{
				u8 val = (dist_back > dest_off) ? 0 : out[dest_off - dist_back];
				out[dest_off++] = val;
			}
		}
		code_word <<= 1;
		bits_left--;
	}
}

static void decompress_mpbin_rle (u8 *out, uint out_size, const u8 *src, uint src_size)
{
	uint dest_off = 0;
	uint src_off = 0;
	while (dest_off < out_size && src_off < src_size)
	{
		u8 code_byte = src[src_off++];
		uint rep_len = code_byte & 0x7F;
		if (code_byte & 0x80)
		{
			for (uint i = 0; i < rep_len && dest_off < out_size && src_off < src_size; i++)
				out[dest_off++] = src[src_off++];
		}
		else
		{
			if (src_off >= src_size)
				break;
			u8 rep_byte = src[src_off++];
			for (uint i = 0; i < rep_len && dest_off < out_size; i++)
				out[dest_off++] = rep_byte;
		}
	}
}

static void decompress_mpbin_inflate (u8 *out, uint out_size, const u8 *src, uint comp_size)
{
	z_stream stream;
	memset (&stream, 0, sizeof (stream));
	stream.avail_in = comp_size;
	stream.next_in = (Bytef *)src;
	stream.avail_out = out_size;
	stream.next_out = (Bytef *)out;
	if (inflateInit2 (&stream, 15 + 32) == Z_OK)
	{
		inflate (&stream, Z_FINISH);
		inflateEnd (&stream);
	}
}

// Export the structural half of a Switch ("NX") BFRES container as XML.
// Wii U BFRES (ParseBFRES() in lib-bfres.c) is big-endian, version 3.x, and
// self-relative offsets; Switch reuses the "FRES" magic but is little-endian
// and every offset is *absolute* from the start of the file -- a completely
// different, undocumented-in-tree layout.
//
// This decode was originally reverse engineered by hand against exactly one
// sample (~/Downloads/Male.bfres, BFRES version 9) and shipped with known,
// documented gaps. Testing it against real Nintendo Switch retail data --
// Super Mario Odyssey's RomFS, ~3400 .bfres files inside ObjectData/*.szs --
// showed it was in fact broken on that data: every Odyssey .bfres is BFRES
// **version 8**, and the FMDL/FSHP/FMAT field layout genuinely differs
// between version 8 and version 9+ (not just the "is_v8" FSHP-array-vs-dict
// guess the old code made) -- names, shape names and material names all
// silently came back empty on real data.
//
// This decode was rewritten by porting the verified, from-source struct
// layouts of KillzXGaming's BfresLibrary (MIT, the library Switch Toolbox
// itself uses: https://github.com/KillzXGaming/BfresLibrary), specifically
// ModelParser.cs/ShapeParser.cs/MaterialParser.cs/VertexBufferParser.cs and
// ResFileParser.cs, rather than continuing to guess-and-check against raw
// bytes. Verified against 10 real files: ~/Downloads/Male.bfres (v9, the
// original sample -- name "TopL", 2 shapes, 2 materials, still correct) and
// 9 different Odyssey ObjectData/*.szs samples pulled from very different
// parts of the game (AirBubble, CityMan, CityWorldUnderground..., Fukuwarai...,
// LakeWorldHomeWater001, MarioKing2D, NokonokoNpc, SandWorldHomeMeganeStep002,
// SkyDark, Special2WorldLavaKeyMoveParts001 -- all v8): FMDL/FSHP/FMAT names
// now resolve correctly on every one of them (e.g. AirBubble.bfres ->
// FMDL "AirBubble", shape "AirBubble__LifeBubble_Mat", material
// "LifeBubble_Mat" -- previously all three came back empty).
//
// Root cause of the old bug: BfresLibrary's loader reads a *version*-gated
// header prologue before the FMDL/FSHP/FMAT/FVTX name field -- a plain
// 4-byte flags word for version>=9, but a 12-byte "header block" (offset
// u32 + size s64, both unused/legacy) for version<9. The old code's offsets
// were tuned to the 4-byte (v9) case, so on real v8 data every subsequent
// field -- name, shape/FVTX pointers, material pointers, attribute array --
// was read 8 bytes short of where it actually is. This rewrite computes the
// FMDL/FSHP/FMAT/FVTX field offsets from the file's own version field
// (main header +0x08, major = bits 16..31) instead of hardcoding one
// version's layout, so both the v8 (Odyssey/2017-era) and v9/v10 (later
// titles) shapes are handled the same way. See bfres_switch_hdr_extra()
// and the field-offset math in extract_bfres_switch_manifest() below for
// the exact per-version byte layout, which mirrors BfresLibrary's
// ModelParser.Read()/ShapeParser.Read()/MaterialParser.Load() sequential
// field order 1:1.
//
// Buffer pool location: BfresLibrary's ResFileParser reads a `BufferInfo`
// offset near the end of the main header, and that struct's own +0x08
// field holds the absolute file offset of the shared pool all per-shape
// index/vertex data lives in. The version>=9 header inserts a 32-byte
// reserved block ahead of it, so the pointer sits at +0xB0 on v9+ files
// (v8 keeps it at +0x90); Male.bfres (v9) has 0 at +0x90 and a valid
// {unk=36, size=40960, pool=122880} triple at +0xB0, with header+0xA8 ==
// pool+size as an independent cross-check. ParseBFRESSwitch() in
// lib-bfres.c therefore reads the pointer version-gated (+0x90 for v8,
// +0xB0 for v9+, either slot accepted when its struct validates) and
// decodes full geometry from the pool -- verified on Male.bfres (both
// shapes: human-scale bboxes, index max = vcount-1, full vertex coverage;
// wmdlt emits a 2-mesh GLB) with no synthetic-v8 round-trip regression,
// and since on 2284 Tomodachi Life v10 files (2043/2043 with geometry
// decode; skeleton + skinning via the FSKL/_i0 work in lib-bfres.c).
// Unverified: exotic component formats; mesh LODs beyond LOD0 are still
// skipped. See PLAN.md.
static const char *rel_bfres_switch_string (const u8 *d, size_t size, s64 off)
{
	if (off < 2 || (size_t)off + 2 > size)
		return NULL;
	const uint len = le16 (d + off);
	if ((size_t)off + 2 + len > size)
		return NULL;
	return (const char *)(d + off + 2);
}

// Bytes consumed by the version-gated prologue that precedes the first
// LoadString()'d name field in FMDL/FSHP/FMAT/FVTX sections: a plain u32
// flags word for version>=9, or a legacy 12-byte "header block" (u32
// offset + s64 size, both unused) for version<9. See ModelParser.Read()
// et al in BfresLibrary -- every one of these section readers starts with
// this same version check before the first LoadString().
static inline uint bfres_switch_hdr_extra (uint vmajor)
{
	return vmajor >= 9 ? 4 : 12;
}

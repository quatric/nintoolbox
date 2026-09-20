// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Asobo BigFile scanner; see lib-asobo.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-asobo.h"
#include <string.h>
#include "lib-excite.h"

#define ASOBO_MAX_BLOCKS 0x10000
#define ASOBO_MAX_RESOURCES 0x1000000
#define ASOBO_MAX_SIZE (0x40000000u)

static u32 as_rd32 (const u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static u32 as_rd32le (const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }

static u32 as_table[256];
static bool as_table_ready;

static void as_init (void)
{
	for (uint i = 0; i < 256; i++)
	{
		u32 c = i << 24;
		for (uint k = 0; k < 8; k++)
			c = c & 0x80000000 ? c << 1 ^ 0x04c11db7 : c << 1;
		as_table[i] = c;
	}
	as_table_ready = true;
}

static u32 as_hash (ccp s)
{
	if (!as_table_ready)
		as_init ();
	u32 h = 0;
	for (; *s; s++)
		h = h >> 8 ^ as_table[((*s >= 'A' && *s <= 'Z' ? *s + 32 : *s) ^ h) & 0xff];
	return h;
}

static ccp as_class_name (u32 hash)
{
	static const char *const names[] = { "Animation_Z", "Bitmap_Z", "CameraZone_Z", "Camera_Z",
		"CollisionVol_Z", "Fonts_Z", "GameObj_Z", "GenWorld_Z", "GwRoad_Z", "Light_Z", "LodData_Z",
		"Lod_Z", "MaterialAnim_Z", "MaterialObj_Z", "Material_Z", "MeshData_Z", "Mesh_Z", "Node_Z",
		"Omni_Z", "ParticlesData_Z", "Particles_Z", "RotShapeData_Z", "RotShape_Z", "Skel_Z", "Skin_Z",
		"SoundBank_Z", "Sound_Z", "SplineGraph_Z", "Spline_Z", "SurfaceDatas_Z", "Surface_Z", "Text_Z",
		"UserDefine_Z", "Warp_Z", "World_Z", "Rtc_Z", "Binary_Z", "Zone_Z", "Path_Z", "Sprite_Z",
		"Cinematic_Z", "ShapeMorph_Z", "MaterialArray_Z", "SurfaceDatasArray_Z", "Movie_Z", 0 };
	for (uint i = 0; names[i]; i++)
		if (as_hash (names[i]) == hash)
			return names[i];
	return 0;
}

// Decode an LZRS stream. Returns false when the data is malformed.
static bool as_lzrs (const u8 *src, size_t src_size, u8 *dst, u32 dst_size)
{
	size_t s = 0;
	u32 d = 0;
	while (d < dst_size)
	{
		if (s + 4 > src_size)
			return false;
		u32 flags = as_rd32 (src + s);
		s += 4;
		const uint shift_n = flags & 3, shift = 14 - shift_n, mask = (0x4000u >> shift_n) - 1;
		for (uint i = 0; i < 30; i++)
		{
			if (flags & 0x80000000u)
			{
				if (s + 2 > src_size)
					return false;
				const uint t = src[s] << 8 | src[s + 1];
				s += 2;
				const uint back = (t & mask) + 1, len = (t >> shift) + 3;
				if (back > d || d + len > dst_size)
					return false;
				for (uint k = 0; k < len; k++, d++)
					dst[d] = dst[d - back];
			}
			else
			{
				if (s >= src_size || d >= dst_size)
					return false;
				dst[d++] = src[s++];
			}
			if (d >= dst_size)
				break;
			flags <<= 1;
		}
	}
	return true;
}

enumError ScanAsoboDrv (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size)
{
	if (!entries || !n_entries || !data || size < 0x800 || data[0] != 'v' || memcmp (data + 1, "1.", 2)
		|| !memmem (data, 0x40, "Asobo", 5))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u32 nb = as_rd32 (data + 0x104);
	if (!nb || nb > ASOBO_MAX_BLOCKS || 0x120 + 24ull * nb > 0x800)
		return EINVAL;
	uint total = 0;
	for (uint b = 0; b < nb; b++)
		total += as_rd32 (data + 0x120 + 24ull * b);
	if (total > ASOBO_MAX_RESOURCES)
		return EINVAL;

	nintendo_sarc_entry_t *out = CALLOC (total * 2 + 1, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;
	uint n = 0;
	u64 pos = 0x800;
	for (uint b = 0; b < nb; b++)
	{
		const u8 *bd = data + 0x120 + 24ull * b;
		const u32 rc = as_rd32 (bd), padded = as_rd32 (bd + 4);
		u64 q = pos;
		for (uint r = 0; r < rc; r++)
		{
			if (q + 24 > size)
				goto fail;
			const u8 *h = data + q;
			const u32 lh = as_rd32 (h + 4), dsz = as_rd32 (h + 8), csz = as_rd32 (h + 12);
			const u32 cls = as_rd32 (h + 16), nm = as_rd32 (h + 20);
			const u64 bp = q + 24 + lh;
			const u64 stored = csz ? csz : dsz;
			if (dsz > ASOBO_MAX_SIZE || bp + stored > size)
				goto fail;

			u8 *plain = 0;
			const u8 *body = data + bp;
			if (csz)
			{
				if (csz < 8 || as_rd32le (data + bp) != dsz || as_rd32le (data + bp + 4) != csz)
					goto fail;
				plain = dsz ? MALLOC (dsz) : CALLOC (1, 1);
				if (!plain || !as_lzrs (data + bp + 8, csz - 8, plain, dsz))
				{
					FREE (plain);
					goto fail;
				}
				body = plain;
			}

			char path[96];
			ccp cn = as_class_name (cls);
			if (cn)
				snprintf (path, sizeof (path), "%08x.%s", nm, cn);
			else
				snprintf (path, sizeof (path), "%08x.%08x", nm, cls);
			bool ok = OwnedEntryAdd (out, n, path, body, dsz);
			FREE (plain);
			if (!ok)
				goto fail;
			n++;
			if (lh)
			{
				char lpath[112];
				snprintf (lpath, sizeof (lpath), "%s.lnk", path);
				if (!OwnedEntryAdd (out, n, lpath, data + q + 24, lh))
					goto fail;
				n++;
			}
			q = bp + stored;
		}
		pos += padded;
	}
	if (!n)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;

fail:
	ResetOwnedEntries (out, n);
	return EINVAL;
}

static bool as_bitmap_gx (u8 format, uint *gx, uint *bits)
{
	switch (format)
	{
	case 7: *gx = 4; *bits = 16; return true; // RGB565
	case 12: *gx = 6; *bits = 32; return true; // RGBA8
	case 14: *gx = 14; *bits = 4; return true; // CMPR
	}
	return false;
}

bool IsAsoboBitmap (const u8 *d, size_t size)
{
	uint gx, bits;
	if (size < 0x14 || !as_bitmap_gx (d[12], &gx, &bits))
		return false;
	const u32 w = as_rd32 (d), h = as_rd32 (d + 4);
	if (!w || !h || w > 4096 || h > 4096 || (w & 3) || (h & 3))
		return false;
	// The first level must fit; smaller-than-tile sizes are padded to 8x8 (CMPR).
	const u64 need = (u64)w * h * bits / 8;
	return size - 0x14 >= need;
}

enumError DecodeAsoboBitmap (u8 **rgba, uint *width, uint *height, const u8 *d, size_t size)
{
	if (!IsAsoboBitmap (d, size))
		return ERR_NOTHING_TO_DO;
	uint gx, bits;
	as_bitmap_gx (d[12], &gx, &bits);
	const uint w = as_rd32 (d), h = as_rd32 (d + 4);
	const enumError err = DecodeGXTexture_RGBA (rgba, w, h, gx, d + 0x14, size - 0x14, 0, 0, 0);
	if (!err)
	{
		*width = w;
		*height = h;
	}
	return err;
}

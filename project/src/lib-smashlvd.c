// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 stage/level data (.lvd, magic "LVD1").
//
// Reference: KillzXGaming/Smash-Forge, "Smash Forge/Filetypes/LVD.cs"
// (MIT licensed; clean-room C port of the binary layout).
//
// Big-endian, packed, no alignment. File header (10 bytes):
//   u32 0x00000001, u8 0x0A, u8 0x01, char[4] "LVD1".
// Then 19 counted lists in fixed order, each `u8 0x01 + s32 count +
// entries`. Lists 1-6 hold collisions, spawns, respawns, camera bounds,
// blast zones and enemy generators; lists 7-11 must be empty; lists
// 12-15 hold damage shapes, item spawners, general shapes and general
// points; lists 16-19 must be empty.
// Every entry starts with a 12-byte magic identifying its family plus a
// 223-byte shared base (fixed-size NUL-padded name/subname/bone strings,
// start position, misc fields), each field prefixed by a 0x01 tag byte,
// followed by the type-specific payload.

#include "lib-smashlvd.h"
#include "lib-std.h"
#include <string.h>

static float smashlvd_rd_f32 (const u8 *p)
{
	u32 v = rd_be32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

typedef struct smashlvd_cur_t
{
	const u8 *data;
	size_t size, pos;
	bool bad;
} smashlvd_cur_t;

static u8 smashlvd_u8 (smashlvd_cur_t *c)
{
	if (c->pos + 1 > c->size)
	{
		c->bad = true;
		return 0;
	}
	return c->data[c->pos++];
}

static s32 smashlvd_s32 (smashlvd_cur_t *c)
{
	if (c->pos + 4 > c->size)
	{
		c->bad = true;
		return 0;
	}
	s32 v = (s32)rd_be32 (c->data + c->pos);
	c->pos += 4;
	return v;
}

static float smashlvd_f32 (smashlvd_cur_t *c)
{
	if (c->pos + 4 > c->size)
	{
		c->bad = true;
		return 0;
	}
	float f = smashlvd_rd_f32 (c->data + c->pos);
	c->pos += 4;
	return f;
}

// Expect a 0x01 tag byte; the reference reader skips it unchecked, but
// requiring it rejects non-LVD data early.
static bool smashlvd_tag (smashlvd_cur_t *c)
{
	return smashlvd_u8 (c) == 0x01;
}

static void smashlvd_fixed_str (char *dst, size_t dstsz, smashlvd_cur_t *c, size_t len)
{
	size_t n = 0;
	for (size_t i = 0; i < len; i++)
	{
		u8 ch = smashlvd_u8 (c);
		if (ch && n + 1 < dstsz)
			dst[n++] = (char)ch;
	}
	dst[n] = 0;
}

static const u8 smashlvd_magic_coll[12] =
	{ 0x03, 0x04, 0x01, 0x01, 0x77, 0x35, 0xbb, 0x75, 0x00, 0x00, 0x00, 0x02 };
static const u8 smashlvd_magic_spawn[12] =
	{ 0x02, 0x04, 0x01, 0x01, 0x77, 0x35, 0xbb, 0x75, 0x00, 0x00, 0x00, 0x02 };
static const u8 smashlvd_magic_item[12] =
	{ 0x01, 0x04, 0x01, 0x01, 0x77, 0x35, 0xbb, 0x75, 0x00, 0x00, 0x00, 0x02 };

static bool smashlvd_magic_ok (smashlvd_cur_t *c, const u8 *magic)
{
	if (c->pos + 12 > c->size)
	{
		c->bad = true;
		return false;
	}
	bool ok = !memcmp (c->data + c->pos, magic, 12);
	c->pos += 12;
	return ok;
}

// Shared 223-byte entry base; prints the identity block when `out` != 0.
static bool smashlvd_base (smashlvd_cur_t *c, const u8 *magic, FILE *out, const char *kind, uint idx)
{
	if (!smashlvd_magic_ok (c, magic) || c->bad)
		return false;
	char name[0x39], sub[0x41], bone[0x41];
	float sx, sy, sz, u0, u1, u2;
	s32 unk1, unk3;
	u8 use_sp;
	if (!smashlvd_tag (c))
		return false;
	smashlvd_fixed_str (name, sizeof (name), c, 0x38);
	if (!smashlvd_tag (c))
		return false;
	smashlvd_fixed_str (sub, sizeof (sub), c, 0x40);
	if (!smashlvd_tag (c))
		return false;
	sx = smashlvd_f32 (c);
	sy = smashlvd_f32 (c);
	sz = smashlvd_f32 (c);
	use_sp = smashlvd_u8 (c);
	if (!smashlvd_tag (c))
		return false;
	unk1 = smashlvd_s32 (c);
	if (!smashlvd_tag (c))
		return false;
	u0 = smashlvd_f32 (c);
	u1 = smashlvd_f32 (c);
	u2 = smashlvd_f32 (c);
	unk3 = smashlvd_s32 (c);
	if (!smashlvd_tag (c))
		return false;
	smashlvd_fixed_str (bone, sizeof (bone), c, 0x40);
	if (c->bad)
		return false;
	if (out)
		fprintf (out, "[%s %u]\nname = %s\nsubname = %s\nbone = %s\n"
			"start = %.6g %.6g %.6g (used=%u)\nunk1 = %d\nunk2 = %.6g %.6g %.6g\nunk3 = %d\n",
			kind, idx, name, sub, bone, sx, sy, sz, use_sp,
			unk1, u0, u1, u2, unk3);
	return true;
}

static bool smashlvd_shape (smashlvd_cur_t *c, FILE *out, const char *prefix)
{
	smashlvd_u8 (c); // type tag (0x03 on save, unchecked on load)
	s32 type = smashlvd_s32 (c);
	float x1 = smashlvd_f32 (c), y1 = smashlvd_f32 (c);
	float x2 = smashlvd_f32 (c), y2 = smashlvd_f32 (c);
	if (!smashlvd_tag (c) || !smashlvd_tag (c))
		return false;
	s32 npts = smashlvd_s32 (c);
	if (npts < 0 || npts > 100000 || c->bad)
		return false;
	if (out)
		fprintf (out, "%s shape_type = %d rect = %.6g %.6g %.6g %.6g points = %d\n",
			prefix, type, x1, y1, x2, y2, npts);
	for (s32 i = 0; i < npts; i++)
	{
		if (!smashlvd_tag (c))
			return false;
		float x = smashlvd_f32 (c), y = smashlvd_f32 (c);
		if (out)
			fprintf (out, "%s point%d = %.6g %.6g\n", prefix, i, x, y);
		if (c->bad)
			return false;
	}
	return !c->bad;
}

static bool smashlvd_cliff (smashlvd_cur_t *c, FILE *out, uint idx)
{
	char label[64];
	snprintf (label, sizeof (label), "cliff%u", idx);
	if (!smashlvd_base (c, smashlvd_magic_coll, out, "cliff", idx))
		return false;
	if (!smashlvd_tag (c))
		return false;
	float x = smashlvd_f32 (c), y = smashlvd_f32 (c);
	float ang = smashlvd_f32 (c);
	s32 line = smashlvd_s32 (c);
	if (out)
		fprintf (out, "pos = %.6g %.6g\nangle = %.6g\nline = %d\n", x, y, ang, line);
	(void)label;
	return !c->bad;
}

static bool smashlvd_collision (smashlvd_cur_t *c, FILE *out, uint idx)
{
	if (!smashlvd_base (c, smashlvd_magic_coll, out, "collision", idx))
		return false;
	u8 f1 = smashlvd_u8 (c), f2 = smashlvd_u8 (c);
	u8 f3 = smashlvd_u8 (c), f4 = smashlvd_u8 (c);
	if (out)
		fprintf (out, "flags = %u %u %u %u\n", f1, f2, f3, f4);
	if (!smashlvd_tag (c))
		return false;
	s32 nverts = smashlvd_s32 (c);
	if (nverts < 0 || nverts > 100000)
		return false;
	if (out)
		fprintf (out, "verts = %d\n", nverts);
	for (s32 i = 0; i < nverts; i++)
	{
		if (!smashlvd_tag (c))
			return false;
		float x = smashlvd_f32 (c), y = smashlvd_f32 (c);
		if (out)
			fprintf (out, "v%d = %.6g %.6g\n", i, x, y);
	}
	if (!smashlvd_tag (c))
		return false;
	s32 nnorm = smashlvd_s32 (c);
	if (nnorm < 0 || nnorm > 100000)
		return false;
	if (out)
		fprintf (out, "normals = %d\n", nnorm);
	for (s32 i = 0; i < nnorm; i++)
	{
		if (!smashlvd_tag (c))
			return false;
		float x = smashlvd_f32 (c), y = smashlvd_f32 (c);
		if (out)
			fprintf (out, "n%d = %.6g %.6g\n", i, x, y);
	}
	if (!smashlvd_tag (c))
		return false;
	s32 ncliff = smashlvd_s32 (c);
	if (ncliff < 0 || ncliff > 10000)
		return false;
	if (out)
		fprintf (out, "cliffs = %d\n", ncliff);
	for (s32 i = 0; i < ncliff; i++)
		if (!smashlvd_cliff (c, out, (uint)i))
			return false;
	if (!smashlvd_tag (c))
		return false;
	s32 nmat = smashlvd_s32 (c);
	if (nmat < 0 || nmat > 100000)
		return false;
	if (out)
		fprintf (out, "materials = %d\n", nmat);
	for (s32 i = 0; i < nmat; i++)
	{
		if (!smashlvd_tag (c))
			return false;
		if (c->pos + 12 > c->size)
		{
			c->bad = true;
			return false;
		}
		if (out)
			fprintf (out, "mat%d = physics 0x%02x flags 0x%02x raw %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
				i, c->data[c->pos + 3], c->data[c->pos + 10],
				c->data[c->pos], c->data[c->pos + 1], c->data[c->pos + 2],
				c->data[c->pos + 3], c->data[c->pos + 4], c->data[c->pos + 5],
				c->data[c->pos + 6], c->data[c->pos + 7], c->data[c->pos + 8],
				c->data[c->pos + 9], c->data[c->pos + 10], c->data[c->pos + 11]);
		c->pos += 12;
	}
	return !c->bad;
}

static bool smashlvd_spawn (smashlvd_cur_t *c, FILE *out, const char *kind, uint idx)
{
	if (!smashlvd_base (c, smashlvd_magic_spawn, out, kind, idx))
		return false;
	if (!smashlvd_tag (c))
		return false;
	float x = smashlvd_f32 (c), y = smashlvd_f32 (c);
	if (out)
		fprintf (out, "pos = %.6g %.6g\n", x, y);
	return !c->bad;
}

static bool smashlvd_bounds (smashlvd_cur_t *c, FILE *out, const char *kind, uint idx)
{
	if (!smashlvd_base (c, smashlvd_magic_spawn, out, kind, idx))
		return false;
	if (!smashlvd_tag (c))
		return false;
	float l = smashlvd_f32 (c), r = smashlvd_f32 (c);
	float t = smashlvd_f32 (c), b = smashlvd_f32 (c);
	if (out)
		fprintf (out, "left = %.6g right = %.6g top = %.6g bottom = %.6g\n", l, r, t, b);
	return !c->bad;
}

static bool smashlvd_shape_list (smashlvd_cur_t *c, FILE *out, const char *prefix)
{
	if (!smashlvd_tag (c) || !smashlvd_tag (c))
		return false;
	s32 n = smashlvd_s32 (c);
	if (n < 0 || n > 10000)
		return false;
	if (out)
		fprintf (out, "%s_sections = %d\n", prefix, n);
	for (s32 i = 0; i < n; i++)
	{
		char label[64];
		snprintf (label, sizeof (label), "%s%u", prefix, i);
		if (!smashlvd_tag (c))
			return false;
		if (!smashlvd_shape (c, out, label))
			return false;
	}
	return !c->bad;
}

static bool smashlvd_enemy (smashlvd_cur_t *c, FILE *out, uint idx)
{
	if (!smashlvd_base (c, smashlvd_magic_coll, out, "enemy", idx))
		return false;
	if (!smashlvd_shape_list (c, out, "sec1_"))
		return false;
	if (!smashlvd_shape_list (c, out, "sec2_"))
		return false;
	if (!smashlvd_tag (c) || !smashlvd_tag (c))
		return false;
	if (smashlvd_s32 (c) != 0) // always-empty count
		return false;
	if (!smashlvd_tag (c))
		return false;
	s32 id = smashlvd_s32 (c);
	if (!smashlvd_tag (c))
		return false;
	s32 nid = smashlvd_s32 (c);
	if (nid < 0 || nid > 100000)
		return false;
	if (out)
		fprintf (out, "id = %d sub_ids = %d\n", id, nid);
	for (s32 i = 0; i < nid; i++)
	{
		if (!smashlvd_tag (c))
			return false;
		s32 v = smashlvd_s32 (c);
		if (out)
			fprintf (out, "sub_id%d = %d\n", i, v);
	}
	if (!smashlvd_tag (c))
		return false;
	if (smashlvd_s32 (c) != 0)
		return false;
	if (!smashlvd_tag (c))
		return false;
	s32 npad = smashlvd_s32 (c);
	if (npad < 0 || npad > 100000)
		return false;
	for (s32 i = 0; i < npad; i++)
	{
		if (c->pos + 5 > c->size)
		{
			c->bad = true;
			return false;
		}
		c->pos += 5;
	}
	if (out)
		fprintf (out, "pad_blocks = %d\n", npad);
	return !c->bad;
}

static bool smashlvd_damage (smashlvd_cur_t *c, FILE *out, uint idx)
{
	if (!smashlvd_base (c, smashlvd_magic_item, out, "damage", idx))
		return false;
	if (!smashlvd_tag (c))
		return false;
	s32 type = smashlvd_s32 (c);
	float x = smashlvd_f32 (c), y = smashlvd_f32 (c), z = smashlvd_f32 (c);
	if (out)
		fprintf (out, "type = %d pos = %.6g %.6g %.6g\n", type, x, y, z);
	if (type == 2) // sphere
	{
		float r = smashlvd_f32 (c);
		float dx = smashlvd_f32 (c), dy = smashlvd_f32 (c), dz = smashlvd_f32 (c);
		if (out)
			fprintf (out, "radius = %.6g dir = %.6g %.6g %.6g\n", r, dx, dy, dz);
	}
	else if (type == 3) // capsule
	{
		float dx = smashlvd_f32 (c), dy = smashlvd_f32 (c), dz = smashlvd_f32 (c);
		float r = smashlvd_f32 (c);
		if (out)
			fprintf (out, "dir = %.6g %.6g %.6g radius = %.6g\n", dx, dy, dz, r);
	}
	else
		return false;
	u8 u1 = smashlvd_u8 (c);
	s32 u2 = smashlvd_s32 (c);
	if (out)
		fprintf (out, "unk = %u %d\n", u1, u2);
	return !c->bad;
}

static bool smashlvd_item (smashlvd_cur_t *c, FILE *out, uint idx)
{
	if (!smashlvd_base (c, smashlvd_magic_item, out, "itemspawner", idx))
		return false;
	if (!smashlvd_tag (c))
		return false;
	s32 id = smashlvd_s32 (c);
	if (out)
		fprintf (out, "id = 0x%08x\n", (u32)id);
	return smashlvd_shape_list (c, out, "section_");
}

static bool smashlvd_general_shape (smashlvd_cur_t *c, FILE *out, uint idx)
{
	if (!smashlvd_base (c, smashlvd_magic_item, out, "generalshape", idx))
		return false;
	if (!smashlvd_tag (c))
		return false;
	s32 id = smashlvd_s32 (c);
	if (out)
		fprintf (out, "id = %d\n", id);
	return smashlvd_shape (c, out, "shape");
}

static bool smashlvd_general_point (smashlvd_cur_t *c, FILE *out, uint idx)
{
	if (!smashlvd_base (c, smashlvd_magic_item, out, "generalpoint", idx))
		return false;
	if (!smashlvd_tag (c))
		return false;
	s32 id = smashlvd_s32 (c);
	if (!smashlvd_tag (c))
		return false;
	s32 type = smashlvd_s32 (c);
	float x = smashlvd_f32 (c), y = smashlvd_f32 (c), z = smashlvd_f32 (c);
	if (c->pos + 16 > c->size)
	{
		c->bad = true;
		return false;
	}
	c->pos += 16;
	if (out)
		fprintf (out, "id = %d type = %d pos = %.6g %.6g %.6g\n", id, type, x, y, z);
	return !c->bad;
}

// Walk one of the 19 file-level lists; `parse` selects the entry reader
// (0 = collision, 1 = spawn, 2 = bounds, 3 = enemy, 4 = damage,
// 5 = item, 6 = general shape, 7 = general point, -1 = must be empty).
static bool smashlvd_list (smashlvd_cur_t *c, FILE *out,
	const char *lname, int parse, const char *kind)
{
	if (!smashlvd_tag (c))
		return false;
	s32 n = smashlvd_s32 (c);
	if (n < 0 || n > 100000)
		return false;
	if (out)
		fprintf (out, "\n[%s: %d]\n", lname, n);
	if (parse < 0)
		return n == 0;
	for (s32 i = 0; i < n; i++)
	{
		bool ok = false;
		switch (parse)
		{
			case 0: ok = smashlvd_collision (c, out, (uint)i); break;
			case 1: ok = smashlvd_spawn (c, out, kind, (uint)i); break;
			case 2: ok = smashlvd_bounds (c, out, kind, (uint)i); break;
			case 3: ok = smashlvd_enemy (c, out, (uint)i); break;
			case 4: ok = smashlvd_damage (c, out, (uint)i); break;
			case 5: ok = smashlvd_item (c, out, (uint)i); break;
			case 6: ok = smashlvd_general_shape (c, out, (uint)i); break;
			case 7: ok = smashlvd_general_point (c, out, (uint)i); break;
			default: return false;
		}
		if (!ok || c->bad)
			return false;
		if (out)
			fprintf (out, "\n");
	}
	return !c->bad;
}

static bool smashlvd_walk (const u8 *data, size_t size, FILE *out)
{
	if (!data || size < 10 || rd_be32 (data) != 1 || data[4] != 0x0A
		|| data[5] != 0x01 || memcmp (data + 6, "LVD1", 4))
		return false;
	smashlvd_cur_t cur = { data, size, 10, false };
	smashlvd_cur_t *c = &cur;
	if (out)
		fprintf (out, "#LVD\n# Super Smash Bros. 4 stage data\n");
	bool ok = smashlvd_list (c, out, "collisions", 0, 0)
		&& smashlvd_list (c, out, "spawns", 1, "spawn")
		&& smashlvd_list (c, out, "respawns", 1, "respawn")
		&& smashlvd_list (c, out, "camera_bounds", 2, "camera")
		&& smashlvd_list (c, out, "blast_zones", 2, "blast")
		&& smashlvd_list (c, out, "enemies", 3, 0)
		&& smashlvd_list (c, out, "reserved6", -1, 0)
		&& smashlvd_list (c, out, "reserved7", -1, 0)
		&& smashlvd_list (c, out, "reserved8", -1, 0)
		&& smashlvd_list (c, out, "reserved9", -1, 0)
		&& smashlvd_list (c, out, "reserved10", -1, 0)
		&& smashlvd_list (c, out, "damage_shapes", 4, 0)
		&& smashlvd_list (c, out, "item_spawners", 5, 0)
		&& smashlvd_list (c, out, "general_shapes", 6, 0)
		&& smashlvd_list (c, out, "general_points", 7, 0)
		&& smashlvd_list (c, out, "reserved15", -1, 0)
		&& smashlvd_list (c, out, "reserved16", -1, 0)
		&& smashlvd_list (c, out, "reserved17", -1, 0)
		&& smashlvd_list (c, out, "reserved18", -1, 0);
	return ok && !c->bad && c->pos == size;
}

bool IsSmashLVD (const u8 *data, size_t size)
{
	return smashlvd_walk (data, size, 0);
}

enumError DecodeSmashLVD_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !smashlvd_walk (data, size, out))
		return ERR_INVALID_DATA;
	return ERR_OK;
}

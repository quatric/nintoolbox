// SPDX-License-Identifier: GPL-2.0+
// XLink2 effect / sound link databases (XLNK) to YAML; see lib-xlnk.h.

#include "lib-xlnk.h"
#include "lib-sbuf.h"
#include <zlib.h>

typedef struct xl_param_t
{
	u64 name; // offset into the param define string pool
	u32 type; // 0 int, 1 float, 2 bool, 3 enum, 4 string, 5 custom
} xl_param_t;

typedef struct xl_hash_t
{
	u32 crc;
	u32 off;
} xl_hash_t;

typedef struct xl_t
{
	const u8 *d;
	size_t n;
	bool be, bad, p64, slink;
	uint fam; // layout family 0..3, see lib-xlnk.h
	uint version;

	u64 name_pos, name_end, cond_pos, ex_pos, tow_pos, asset_param_pos;
	u64 lp_pos, dv_pos, rnd_pos, curve_pos, cpt_pos;
	u32 n_lp, n_le, n_dv, n_rnd, n_curve, n_cpt;

	u64 pdt_str_pos, pdt_str_end;
	u32 n_user_param, n_asset_param, n_custom_asset, n_trig_param;
	xl_param_t *user_p, *asset_p, *trig_p;

	xl_hash_t *hashes;
	uint n_hashes;

	// current user
	u64 act_pos, cont_pos;
	u32 n_act;

	sbuf_t b;
} xl_t;

//-----------------------------------------------------------------------------
// reading (absolute offsets, bounds checked; a failed read sets bad and yields 0)

static bool xl_ok (xl_t *x, u64 o, u64 len)
{
	if (o > x->n || len > x->n - o)
	{
		x->bad = true;
		return false;
	}
	return true;
}

static u8 r8 (xl_t *x, u64 o)
{
	return xl_ok (x, o, 1) ? x->d[o] : 0;
}

static u16 r16 (xl_t *x, u64 o)
{
	if (!xl_ok (x, o, 2))
		return 0;
	const u8 *p = x->d + o;
	return x->be ? (u16)(p[0] << 8 | p[1]) : (u16)(p[1] << 8 | p[0]);
}

static u32 r32 (xl_t *x, u64 o)
{
	if (!xl_ok (x, o, 4))
		return 0;
	const u8 *p = x->d + o;
	return x->be ? (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]
				 : (u32)p[3] << 24 | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0];
}

static u64 r64 (xl_t *x, u64 o)
{
	const u64 a = r32 (x, o), b = r32 (x, o + 4);
	return x->be ? a << 32 | b : b << 32 | a;
}

static float rf32 (xl_t *x, u64 o)
{
	const u32 v = r32 (x, o);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static u64 rptr (xl_t *x, u64 o)
{
	return x->p64 ? r64 (x, o) : r32 (x, o);
}

static uint ptr_size (const xl_t *x)
{
	return x->p64 ? 8 : 4;
}

static u64 align_ptr (const xl_t *x, u64 o)
{
	const u64 a = ptr_size (x);
	return (o + a - 1) & ~(a - 1);
}

// String at BASE + OFF that must end before END.
static ccp xl_str_in (xl_t *x, u64 base, u64 end, u64 off)
{
	if (off >= end - base || base + off >= x->n)
	{
		x->bad = true;
		return "";
	}
	const u8 *s = x->d + base + off;
	if (!memchr (s, 0, end - base - off))
	{
		x->bad = true;
		return "";
	}
	return (ccp)s;
}

static ccp xl_name (xl_t *x, u64 off)
{
	return xl_str_in (x, x->name_pos, x->name_end, off);
}

static ccp xl_local_prop (xl_t *x, s64 idx)
{
	if (idx < 0 || idx >= x->n_lp)
	{
		x->bad = true;
		return "";
	}
	return xl_name (x, rptr (x, x->lp_pos + (u64)idx * ptr_size (x)));
}

static ccp xl_local_enum (xl_t *x, s64 idx)
{
	if (idx < 0 || idx >= x->n_le)
	{
		x->bad = true;
		return "";
	}
	return xl_name (x, rptr (x, x->lp_pos + ((u64)x->n_lp + (u64)idx) * ptr_size (x)));
}

//-----------------------------------------------------------------------------
// record sizes per layout family

static uint sz_pdt (const xl_t *x)
{
	return x->p64 ? 0x18 : 0x0c;
}
static uint sz_act (const xl_t *x)
{
	return x->p64 ? 0x30 : 0x20;
}
static uint sz_slot (const xl_t *x)
{
	return x->p64 ? 0x10 : 0x08;
}
static uint sz_action (const xl_t *x)
{
	return x->p64 ? 0x10 : 0x0c;
}
static uint sz_action_trig (const xl_t *x)
{
	return x->p64 ? 0x28 : 0x18;
}
static uint sz_prop (const xl_t *x)
{
	return x->p64 ? 0x18 : 0x10;
}
static uint sz_prop_trig (const xl_t *x)
{
	return x->p64 ? 0x20 : 0x14;
}
static uint sz_curve (const xl_t *x)
{
	return x->p64 ? 0x18 : 0x14;
}

//-----------------------------------------------------------------------------
// output helpers

static void ind (xl_t *x, uint n)
{
	static const char sp[] = "                                ";
	sb_write (&x->b, sp, n < sizeof (sp) - 1 ? n : sizeof (sp) - 1);
}

static void put_f (xl_t *x, float f)
{
	if (f != f)
		sb_puts (&x->b, ".nan");
	else if (f == (float)(s32)f && f > -1e9f && f < 1e9f)
		sb_printf (&x->b, "%d.0", (s32)f);
	else
		sb_printf (&x->b, "%.9g", f);
}

static void put_str (xl_t *x, ccp s)
{
	sb_quotez (&x->b, s);
}

static ccp param_type_name (u32 t)
{
	static ccp const tab[] = { "int", "float", "bool", "enum", "string", "custom" };
	return t < 6 ? tab[t] : "?";
}

//-----------------------------------------------------------------------------
// params

static void put_random (xl_t *x, uint ref, u32 idx)
{
	if (idx >= x->n_rnd)
	{
		x->bad = true;
		return;
	}
	const u64 o = x->rnd_pos + 8ull * idx;
	ccp kind = "linear";
	double power = 0;
	if (ref >= 6 && ref <= 0x11)
	{
		static const double pw[4] = { 2, 3, 4, 1.5 };
		kind = ref < 0x0a ? "inflected" : ref < 0x0e ? "increasing" : "decreasing";
		power = pw[(ref - 6) % 4];
	}
	sb_printf (&x->b, "{random: %s, ", kind);
	if (power)
		sb_printf (&x->b, "power: %g, ", power);
	sb_puts (&x->b, "min: ");
	put_f (x, rf32 (x, o));
	sb_puts (&x->b, ", max: ");
	put_f (x, rf32 (x, o + 4));
	sb_puts (&x->b, "}");
}

static void put_curve (xl_t *x, u32 idx)
{
	if (idx >= x->n_curve)
	{
		x->bad = true;
		return;
	}
	const u64 o = x->curve_pos + (u64)sz_curve (x) * idx;
	const uint start = r16 (x, o), count = r16 (x, o + 2), type = r16 (x, o + 4),
			   global = r16 (x, o + 6);
	const uint po = ptr_size (x);
	ccp prop = xl_name (x, rptr (x, o + 8));
	const s32 unk = (s32)r32 (x, o + 8 + po);
	const s16 upd = (s16)r16 (x, o + 14 + po);
	if ((u64)start + count > x->n_cpt)
	{
		x->bad = true;
		return;
	}
	sb_printf (&x->b, "{curve: %s, property: ",
		type == 0		? "standard"
			: type == 1 ? "constant"
						: "unknown");
	put_str (x, prop);
	sb_printf (&x->b, ", global: %s", global == 1 ? "true" : "false");
	if (unk)
		sb_printf (&x->b, ", unknown: %d", unk);
	if (upd)
		sb_printf (&x->b, ", update: %s", upd == 1 ? "no-update" : "?");
	sb_puts (&x->b, ", points: [");
	for (uint i = 0; i < count; i++)
	{
		const u64 p = x->cpt_pos + 8ull * (start + i);
		sb_puts (&x->b, i ? ", [" : "[");
		put_f (x, rf32 (x, p));
		sb_puts (&x->b, ", ");
		put_f (x, rf32 (x, p + 4));
		sb_puts (&x->b, "]");
	}
	sb_puts (&x->b, "]}");
}

static void put_arrange (xl_t *x, u32 off)
{
	static ccp const limits[] = { "none", "priority-then-oldest", "priority-then-newest",
		"oldest-then-priority", "newest-then-priority", "spatial-priority-then-oldest",
		"spatial-priority-then-newest", "oldest-then-spatial-priority",
		"newest-then-spatial-priority" };
	const u64 o = x->ex_pos + off;
	const s32 count = (s32)r32 (x, o);
	sb_puts (&x->b, "{arrange: [");
	const uint gs = x->p64 ? 0x10 : 0x08;
	for (s32 i = 0; i < count && i < 0x10000 && !x->bad; i++)
	{
		const u64 g = o + 4 + (u64)gs * i;
		const uint lim = r8 (x, g + ptr_size (x));
		sb_puts (&x->b, i ? ", {name: " : "{name: ");
		put_str (x, xl_name (x, rptr (x, g)));
		if (lim < 9)
			sb_printf (&x->b, ", limit: %s", limits[lim]);
		else
			sb_printf (&x->b, ", limit: %u", lim);
		sb_printf (&x->b, ", threshold: %d, include-fading: %s}", (s8)r8 (x, g + ptr_size (x) + 1),
			r8 (x, g + ptr_size (x) + 2) ? "true" : "false");
	}
	sb_puts (&x->b, "]}");
}

static void put_param_value (xl_t *x, u32 packed, u32 type)
{
	const uint ref = packed >> 24;
	const u32 v = packed & 0xffffff;
	switch (ref)
	{
		case 0: // direct value
		{
			if (v >= x->n_dv)
			{
				x->bad = true;
				return;
			}
			const u64 o = x->dv_pos + 4ull * v;
			if (type == 1)
				put_f (x, rf32 (x, o));
			else if (type == 2)
				sb_puts (&x->b, r32 (x, o) ? "true" : "false");
			else if (type == 0)
				sb_printf (&x->b, "%d", (s32)r32 (x, o));
			else
				sb_printf (&x->b, "%u", r32 (x, o));
			return;
		}
		case 1:
			put_str (x, xl_name (x, v));
			return;
		case 2:
			put_curve (x, v);
			return;
		case 4:
			put_arrange (x, v);
			return;
		case 5:
			if (v >= x->n_dv)
			{
				x->bad = true;
				return;
			}
			sb_printf (&x->b, "{bitfield: 0x%x}", r32 (x, x->dv_pos + 4ull * v));
			return;
		default:
			if (ref == 3 || (ref >= 6 && ref <= 0x11))
				put_random (x, ref, v);
			else
				x->bad = true;
	}
}

static ccp pdt_name (xl_t *x, const xl_param_t *p)
{
	return xl_str_in (x, x->pdt_str_pos, x->pdt_str_end, p->name);
}

static void put_param (xl_t *x, uint indent, const xl_param_t *def, u32 packed)
{
	ind (x, indent);
	put_str (x, pdt_name (x, def));
	sb_puts (&x->b, ": ");
	put_param_value (x, packed, def->type);
	sb_puts (&x->b, "\n");
}

// Mask-selected params at O (mask is u64 for assets, u32 for trigger overwrites).
static void put_masked_params (
	xl_t *x, uint indent, ccp key, u64 o, bool wide, const xl_param_t *defs, u32 n_defs)
{
	const u64 mask = wide ? r64 (x, o) : r32 (x, o);
	u64 p = o + (wide ? 8 : 4);
	if (!mask)
		return;
	ind (x, indent);
	sb_printf (&x->b, "%s:\n", key);
	for (u32 i = 0; i < n_defs && i < 64 && !x->bad; i++)
		if (mask >> i & 1)
		{
			put_param (x, indent + 2, defs + i, r32 (x, p));
			p += 4;
		}
	if (n_defs < 64 && mask >> n_defs)
		x->bad = true;
}

static void put_overwrite (xl_t *x, uint indent, u64 off)
{
	if ((off & 0xffffffff) == 0xffffffff)
		return;
	put_masked_params (
		x, indent, "overwrite-params", x->tow_pos + off, false, x->trig_p, x->n_trig_param);
}

//-----------------------------------------------------------------------------
// conditions

static ccp compare_name (uint c)
{
	static ccp const tab[] = { "==", ">", ">=", "<", "<=", "!=" };
	return c < 6 ? tab[c] : "?";
}

static void put_condition (xl_t *x, uint indent, u64 off)
{
	if ((off & 0xffffffff) == 0xffffffff)
		return;
	const u64 o = x->cond_pos + off;
	const u32 ctype = r32 (x, o);
	ind (x, indent);
	sb_puts (&x->b, "condition: ");
	switch (ctype)
	{
		case 1:
		case 2:
			sb_puts (&x->b, "{weight: ");
			put_f (x, rf32 (x, o + 4));
			sb_puts (&x->b, "}\n");
			return;
		case 3:
			sb_puts (&x->b, "{min: ");
			put_f (x, rf32 (x, o + 4));
			sb_puts (&x->b, ", max: ");
			put_f (x, rf32 (x, o + 8));
			sb_printf (&x->b, ", min-op: %u, max-op: %u}\n", r8 (x, o + 12), r8 (x, o + 13));
			return;
		case 4:
			sb_printf (&x->b, "{force-continue: %u}\n", r32 (x, o + 4));
			return;
		case 0:
			break;
		default:
			x->bad = true;
			sb_puts (&x->b, "?\n");
			return;
	}

	uint ptype, cmp;
	bool global;
	if (x->fam == 0)
	{
		ptype = r32 (x, o + 4);
		cmp = r32 (x, o + 8);
		global = r8 (x, o + 0x13) != 0;
	}
	else
	{
		ptype = r8 (x, o + 4);
		cmp = r8 (x, o + 5);
		global = r8 (x, o + 7) != 0;
	}
	if (ptype > 5 || cmp > 5)
		x->bad = true;
	sb_printf (&x->b, "{compare: \"%s\", ", compare_name (cmp));
	if (global)
		sb_puts (&x->b, "global: true, ");
	sb_puts (&x->b, "value: ");
	// value location: fam 0: +0x0c (enum index u16 at +0x10); fam 1: +0x08 / local enum +0x0c;
	// fam 2/3: index or action hash +0x08, value +0x0c, enum name pointer +0x10
	const u64 vo = x->fam == 0 ? o + 0x0c : x->fam == 1 ? o + 0x08 : o + 0x0c;
	switch (ptype)
	{
		case 0: // enum
			if (x->fam >= 2)
			{
				const u64 nameoff = rptr (x, o + 0x10);
				put_str (x, xl_name (x, nameoff));
				const u32 key = r32 (x, o + 8);
				if (!global)
				{
					// local enum: index naming the same string; action slot: CRC32 of the action
					// name
					ccp nm = xl_name (x, nameoff);
					const bool by_index = key < x->n_le && !strcmp (xl_local_enum (x, key), nm);
					if (!by_index && (u32)crc32 (0, (const Bytef *)nm, (uInt)strlen (nm)) != key)
						x->bad = true;
				}
			}
			else if (global)
				put_str (x, xl_name (x, r32 (x, vo)));
			else
				put_str (x, xl_local_enum (x, x->fam == 0 ? r16 (x, o + 0x10) : r32 (x, o + 0x0c)));
			break;
		case 1:
		case 4:
			sb_printf (&x->b, "%d", (s32)r32 (x, vo));
			break;
		case 2:
		case 5:
			put_f (x, rf32 (x, vo));
			break;
		case 3:
			sb_puts (&x->b, r32 (x, vo) ? "true" : "false");
			break;
	}
	sb_puts (&x->b, "}\n");
}

//-----------------------------------------------------------------------------
// asset call tables

static ccp container_name (uint t, bool blend_by)
{
	static ccp const tab[]
		= { "switch", "random", "random-no-repeat", "blend", "sequence", "grid", "jump" };
	if (t == 3 && blend_by)
		return "blend-by";
	return t < 7 ? tab[t] : "?";
}

static void put_grid_values (xl_t *x, u64 o, uint count, bool global)
{
	sb_puts (&x->b, "[");
	for (uint i = 0; i < count; i++)
	{
		if (i)
			sb_puts (&x->b, ", ");
		const u32 v = r32 (x, o + 4ull * i);
		put_str (x, global ? xl_name (x, v) : xl_local_enum (x, v));
	}
	sb_puts (&x->b, "]");
}

static void put_container (xl_t *x, uint indent, u64 o)
{
	uint type;
	bool blend_by = false;
	if (x->fam >= 2)
	{
		type = r8 (x, o);
		blend_by = r8 (x, o + 1) != 0;
	}
	else
		type = r32 (x, o);
	const s32 cs = (s32)r32 (x, o + 4), ce = (s32)r32 (x, o + 8);
	const u64 p = o + (x->p64 ? 0x10 : 0x0c);
	if (type > 6 || (type >= 5 && x->fam < 2) || (type == 6 && x->fam < 3))
		x->bad = true;
	ind (x, indent);
	sb_printf (&x->b, "container: %s\n", container_name (type, blend_by));
	if (cs >= 0)
	{
		if (ce < cs || (u32)ce >= x->n_act)
			x->bad = true;
		ind (x, indent);
		sb_printf (&x->b, "children: [%d, %d]\n", cs, ce);
	}
	else if (type != 6)
		x->bad = true;

	if (type == 0 || (type == 3 && blend_by))
	{
		ccp var = xl_name (x, rptr (x, p));
		const u64 q = p + ptr_size (x);
		const s32 unk = (s32)r32 (x, q);
		const s16 pidx = (s16)r16 (x, q + 4);
		const bool global = r8 (x, q + 6) != 0, action = r8 (x, q + 7) != 0;
		ind (x, indent);
		sb_puts (&x->b, type == 0 ? "switch-on: " : "blend-property: ");
		put_str (x, var);
		sb_puts (&x->b, "\n");
		ind (x, indent);
		ccp scope = action ? "action-slot" : global ? "global" : pidx >= 0 ? "local" : "none";
		sb_printf (&x->b, "scope: %s\n", scope);
		if (!action && !global && pidx >= 0 && strcmp (xl_local_prop (x, pidx), var))
			x->bad = true;
		if (unk)
		{
			ind (x, indent);
			sb_printf (&x->b, "unknown: %d\n", unk);
		}
	}
	else if (type == 5)
	{
		const uint ps = ptr_size (x);
		ccp p1 = xl_name (x, rptr (x, p)), p2 = xl_name (x, rptr (x, p + ps));
		const u64 q = p + 2 * ps;
		const uint flags = r16 (x, q + 4), c1 = r8 (x, q + 6), c2 = r8 (x, q + 7);
		const bool g1 = flags & 1, g2 = flags >> 1 & 1;
		const u64 v1 = q + 8, v2 = v1 + 4ull * c1, idx = v2 + 4ull * c2;
		ind (x, indent);
		sb_puts (&x->b, "property-1: ");
		put_str (x, p1);
		sb_printf (&x->b, "\n");
		ind (x, indent);
		sb_printf (&x->b, "property-1-scope: %s\n", g1 ? "global" : "local");
		ind (x, indent);
		sb_puts (&x->b, "property-1-values: ");
		put_grid_values (x, v1, c1, g1);
		sb_puts (&x->b, "\n");
		ind (x, indent);
		sb_puts (&x->b, "property-2: ");
		put_str (x, p2);
		sb_printf (&x->b, "\n");
		ind (x, indent);
		sb_printf (&x->b, "property-2-scope: %s\n", g2 ? "global" : "local");
		ind (x, indent);
		sb_puts (&x->b, "property-2-values: ");
		put_grid_values (x, v2, c2, g2);
		sb_puts (&x->b, "\n");
		ind (x, indent);
		sb_puts (&x->b, "grid: [");
		for (uint i = 0; i < c1; i++)
		{
			sb_puts (&x->b, i ? ", [" : "[");
			for (uint j = 0; j < c2; j++)
				sb_printf (&x->b, j ? ", %d" : "%d", (s32)r32 (x, idx + 4ull * (i * c2 + j)));
			sb_puts (&x->b, "]");
		}
		sb_puts (&x->b, "]\n");
	}
}

// Container type of call table I (-1 for an asset), with blend-by reported as 7.
static int act_container_type (xl_t *x, s32 i)
{
	if (i < 0 || (u32)i >= x->n_act)
		return -1;
	const u64 o = x->act_pos + (u64)sz_act (x) * i;
	const uint ps = ptr_size (x);
	if (!(r16 (x, o + ps + 2) & 1))
		return -1;
	const u64 c = x->cont_pos + rptr (x, o + (x->p64 ? 0x20 : 0x18));
	if (x->fam < 2)
		return (int)r32 (x, c);
	const int t = r8 (x, c);
	return t == 3 && r8 (x, c + 1) ? 7 : t;
}

static void put_call_table (xl_t *x, uint i)
{
	const u64 o = x->act_pos + (u64)sz_act (x) * i;
	const uint ps = ptr_size (x);
	ccp key = xl_name (x, rptr (x, o));
	const s16 asset_idx = (s16)r16 (x, o + ps);
	const uint flags = r16 (x, o + ps + 2);
	const s32 emit = (s32)r32 (x, o + ps + 4), parent = (s32)r32 (x, o + ps + 8);
	const u32 guid = r32 (x, o + ps + 12), keyhash = r32 (x, o + ps + 16);
	const u64 q = o + (x->p64 ? 0x20 : 0x18);
	const u64 param_off = rptr (x, q), cond_off = rptr (x, q + ps);
	(void)keyhash;

	sb_printf (&x->b, "      - index: %u\n        key: ", i);
	put_str (x, key);
	sb_printf (&x->b, "\n        guid: 0x%08x\n", guid);
	if (parent >= 0)
	{
		if ((u32)parent >= x->n_act)
			x->bad = true;
		sb_printf (&x->b, "        parent: %d\n", parent);
	}
	if (asset_idx >= 0)
		sb_printf (&x->b, "        asset-index: %d\n", asset_idx);
	if (emit != -1)
		sb_printf (&x->b, "        emit-count: %d\n", emit);
	if (flags & 0x08)
		sb_puts (&x->b, "        one-shot: true\n");
	if (flags & 0x10)
		sb_puts (&x->b, "        no-pause: true\n");
	if (flags & 0xe6)
		sb_printf (&x->b, "        flags: 0x%x\n", flags & 0xe6);
	if (flags >> 8)
		sb_printf (&x->b, "        user-flags: 0x%02x\n", flags >> 8);
	// only switch, random, blend-by and sequence children carry a condition (sequence: type 4 only)
	const int pt = act_container_type (x, parent);
	if (pt == 0 || pt == 1 || pt == 2 || pt == 7
		|| (pt == 4 && (cond_off & 0xffffffff) != 0xffffffff
			&& r32 (x, x->cond_pos + cond_off) == 4))
		put_condition (x, 8, cond_off);
	if (flags & 1)
		put_container (x, 8, x->cont_pos + param_off);
	else
		put_masked_params (
			x, 8, "params", x->asset_param_pos + param_off, true, x->asset_p, x->n_asset_param);
}

// Trigger's call table reference (offset from the call table array) as an index.
static void put_act_ref (xl_t *x, uint indent, u64 off)
{
	const uint s = sz_act (x);
	if (off % s || off / s >= x->n_act)
	{
		x->bad = true;
		return;
	}
	ind (x, indent);
	sb_printf (&x->b, "call-table: %u\n", (uint)(off / s));
}

//-----------------------------------------------------------------------------
// users

static ccp user_name (xl_t *x, u32 crc)
{
	uint lo = 0, hi = x->n_hashes;
	while (lo < hi)
	{
		const uint mid = (lo + hi) / 2;
		if (x->hashes[mid].crc < crc)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo < x->n_hashes && x->hashes[lo].crc == crc
		? (ccp)x->d + x->name_pos + x->hashes[lo].off
		: 0;
}

static void put_user (xl_t *x, u32 hash, u64 base)
{
	const uint ps = ptr_size (x);
	uint n_local;
	s32 hdr_unk = 0;
	if (x->fam == 3)
	{
		n_local = r16 (x, base + 4);
		hdr_unk = (s16)r16 (x, base + 6);
	}
	else
		n_local = r32 (x, base + 4);
	u32 c[9];
	for (uint i = 0; i < 9; i++)
		c[i] = r32 (x, base + 8 + 4 * i);
	const u32 n_act = c[0], n_slot = c[3], n_action = c[4], n_atrig = c[5], n_prop = c[6],
			  n_ptrig = c[7], n_always = c[8];
	const u64 trig_rel = rptr (x, base + (x->p64 ? 0x30 : 0x2c));
	if (n_local > 0x10000 || n_act > 0x100000 || x->bad)
	{
		x->bad = true;
		return;
	}

	sb_puts (&x->b, "  - name: ");
	ccp nm = user_name (x, hash);
	if (nm)
		put_str (x, nm);
	else
		sb_puts (&x->b, "~");
	sb_printf (&x->b, "\n    hash: 0x%08x\n", hash);
	if (hdr_unk)
		sb_printf (&x->b, "    unknown: %d\n", hdr_unk);

	u64 p = base + (x->p64 ? 0x38 : 0x30);
	if (n_local)
	{
		sb_puts (&x->b, "    local-properties: [");
		for (uint i = 0; i < n_local; i++)
		{
			if (i)
				sb_puts (&x->b, ", ");
			put_str (x, xl_name (x, rptr (x, p)));
			p += ps;
		}
		sb_puts (&x->b, "]\n");
	}
	if (x->n_user_param)
	{
		sb_puts (&x->b, "    params:\n");
		for (u32 i = 0; i < x->n_user_param; i++, p += 4)
			put_param (x, 6, x->user_p + i, r32 (x, p));
	}
	p += 2ull * n_act; // sorted asset index list
	p = (p + 3) & ~3ull;
	x->act_pos = p;
	x->n_act = n_act;
	x->cont_pos = p + (u64)sz_act (x) * n_act;

	if (n_act)
	{
		sb_puts (&x->b, "    call-tables:\n");
		for (u32 i = 0; i < n_act && !x->bad; i++)
			put_call_table (x, i);
	}

	const u64 slot_pos = base + trig_rel;
	const u64 action_pos = slot_pos + (u64)sz_slot (x) * n_slot;
	const u64 atrig_pos = action_pos + (u64)sz_action (x) * n_action;
	const u64 prop_pos = atrig_pos + (u64)sz_action_trig (x) * n_atrig;
	const u64 ptrig_pos = prop_pos + (u64)sz_prop (x) * n_prop;
	const u64 always_pos = ptrig_pos + (u64)sz_prop_trig (x) * n_ptrig;
	if (!xl_ok (x, slot_pos, 0) || !xl_ok (x, always_pos, 0))
		return;

	if (n_slot)
		sb_puts (&x->b, "    action-slots:\n");
	for (u32 s = 0; s < n_slot && !x->bad; s++)
	{
		const u64 so = slot_pos + (u64)sz_slot (x) * s;
		const s16 a0 = (s16)r16 (x, so + ps), a1 = (s16)r16 (x, so + ps + 2);
		sb_puts (&x->b, "      - name: ");
		put_str (x, xl_name (x, rptr (x, so)));
		sb_puts (&x->b, "\n");
		if (a0 < 0)
			continue;
		if (a1 < a0 || (u32)a1 >= n_action)
		{
			x->bad = true;
			return;
		}
		sb_puts (&x->b, "        actions:\n");
		for (s32 a = a0; a <= a1 && !x->bad; a++)
		{
			const u64 ao = action_pos + (u64)sz_action (x) * a;
			s32 t0, t1;
			bool prefix = false;
			if (x->fam >= 2)
			{
				t0 = (s16)r16 (x, ao + 8);
				prefix = r8 (x, ao + 10) != 0;
				t1 = (s32)r32 (x, ao + 12);
			}
			else
			{
				t0 = (s32)r32 (x, ao + 4);
				t1 = (s32)r32 (x, ao + 8);
			}
			sb_puts (&x->b, "          - name: ");
			put_str (x, xl_name (x, rptr (x, ao)));
			sb_puts (&x->b, "\n");
			if (prefix)
				sb_puts (&x->b, "            prefix: true\n");
			if (t1 < t0)
				continue;
			if (t0 < 0 || (u32)t1 >= n_atrig)
			{
				x->bad = true;
				return;
			}
			sb_puts (&x->b, "            triggers:\n");
			for (s32 t = t0; t <= t1 && !x->bad; t++)
			{
				const u64 to = atrig_pos + (u64)sz_action_trig (x) * t;
				const u32 guid = r32 (x, to);
				u64 q = to + 4;
				s32 unk1 = 0;
				if (x->fam >= 2)
				{
					unk1 = (s32)r32 (x, q);
					q += 4;
				}
				const u64 act = rptr (x, q);
				q += ps;
				const u64 prev = rptr (x, q);
				q += ps;
				const s32 end = (s32)r32 (x, q);
				const uint fl = r16 (x, q + 4), unk2 = r16 (x, q + 6);
				const u64 ow = rptr (x, q + 8);
				sb_printf (&x->b, "              - guid: 0x%08x\n", guid);
				put_act_ref (x, 16, act);
				if (fl & 0x04)
					sb_puts (&x->b, "                type: always\n");
				else if (fl & 0x08)
					sb_puts (&x->b, "                type: on-leave\n");
				else if (fl & 0x10)
				{
					sb_puts (
						&x->b, "                type: previous\n                previous-action: ");
					put_str (x, xl_name (x, prev));
					sb_puts (&x->b, "\n");
				}
				else
					sb_printf (&x->b,
						"                type: frames\n                start-frame: %d\n",
						(s32)(u32)prev);
				if (!(fl & 0x14))
					sb_printf (&x->b, "                end-frame: %d\n", end);
				if (fl & 1)
					sb_puts (&x->b, "                one-shot: true\n");
				if (fl & ~0x1du)
					sb_printf (&x->b, "                flags: 0x%x\n", fl);
				if (unk1)
					sb_printf (&x->b, "                unknown-1: %d\n", unk1);
				if (unk2)
					sb_printf (&x->b, "                unknown-2: %u\n", unk2);
				put_overwrite (x, 16, ow);
			}
		}
	}

	if (n_prop)
		sb_puts (&x->b, "    properties:\n");
	for (u32 i = 0; i < n_prop && !x->bad; i++)
	{
		const u64 po = prop_pos + (u64)sz_prop (x) * i;
		const u32 global = r32 (x, po + ps);
		const s32 t0 = (s32)r32 (x, po + ps + 4), t1 = (s32)r32 (x, po + ps + 8);
		sb_puts (&x->b, "      - name: ");
		put_str (x, xl_name (x, rptr (x, po)));
		sb_printf (&x->b, "\n        scope: %s\n", global == 1 ? "global" : "local");
		if (t1 < t0)
			continue;
		if (t0 < 0 || (u32)t1 >= n_ptrig)
		{
			x->bad = true;
			return;
		}
		sb_puts (&x->b, "        triggers:\n");
		for (s32 t = t0; t <= t1 && !x->bad; t++)
		{
			const u64 to = ptrig_pos + (u64)sz_prop_trig (x) * t;
			const u32 guid = r32 (x, to);
			uint fl, unk;
			u64 act, cond, ow;
			if (x->fam >= 2)
			{
				fl = r16 (x, to + 4);
				unk = r16 (x, to + 6);
				act = rptr (x, to + 8);
				cond = rptr (x, to + 16);
				ow = rptr (x, to + 24);
			}
			else
			{
				act = rptr (x, to + 4);
				cond = rptr (x, to + 8);
				fl = r16 (x, to + 12);
				unk = r16 (x, to + 14);
				ow = rptr (x, to + 16);
			}
			sb_printf (&x->b, "          - guid: 0x%08x\n", guid);
			put_act_ref (x, 12, act);
			if (fl & 1)
				sb_puts (&x->b, "            lazy: true\n");
			if (fl & ~1u)
				sb_printf (&x->b, "            flags: 0x%x\n", fl);
			if (unk)
				sb_printf (&x->b, "            unknown: %u\n", unk);
			if ((cond & 0xffffffff) == 0xffffffff)
				x->bad = true;
			put_condition (x, 12, cond);
			put_overwrite (x, 12, ow);
		}
	}

	if (n_always)
		sb_puts (&x->b, "    always-triggers:\n");
	for (u32 i = 0; i < n_always && !x->bad; i++)
	{
		const u64 to = always_pos + (u64)(x->p64 ? 0x18 : 0x10) * i;
		const u32 guid = r32 (x, to);
		uint fl, unk;
		u64 act, ow;
		if (x->fam >= 2)
		{
			fl = r16 (x, to + 4);
			unk = r16 (x, to + 6);
			act = rptr (x, to + 8);
			ow = rptr (x, to + 16);
		}
		else
		{
			act = rptr (x, to + 4);
			fl = r16 (x, to + 8);
			unk = r16 (x, to + 10);
			ow = rptr (x, to + 12);
		}
		sb_printf (&x->b, "      - guid: 0x%08x\n", guid);
		put_act_ref (x, 8, act);
		if (fl)
			sb_printf (&x->b, "        flags: 0x%x\n", fl);
		if (unk)
			sb_printf (&x->b, "        unknown: %u\n", unk);
		put_overwrite (x, 8, ow);
	}
}

//-----------------------------------------------------------------------------
// database

static int cmp_hash (const void *a, const void *b)
{
	const u32 x = ((const xl_hash_t *)a)->crc, y = ((const xl_hash_t *)b)->crc;
	return x < y ? -1 : x > y;
}

static void build_name_hashes (xl_t *x)
{
	uint cap = 0;
	for (u64 o = x->name_pos; o < x->name_end;)
	{
		const u8 *s = x->d + o, *z = memchr (s, 0, x->name_end - o);
		if (!z)
			break;
		if (x->n_hashes == cap)
		{
			cap = cap ? cap * 2 : 1024;
			xl_hash_t *nh = REALLOC (x->hashes, cap * sizeof (*nh));
			if (!nh)
				return;
			x->hashes = nh;
		}
		x->hashes[x->n_hashes].crc = (u32)crc32 (0, s, (uInt)(z - s));
		x->hashes[x->n_hashes++].off = (u32)(o - x->name_pos);
		o += (u64)(z - s) + 1;
	}
	if (x->n_hashes)
		qsort (x->hashes, x->n_hashes, sizeof (*x->hashes), cmp_hash);
}

static xl_param_t *load_pdt_params (xl_t *x, u64 *p, u32 count)
{
	xl_param_t *v = CALLOC (count ? count : 1, sizeof (*v));
	if (!v)
	{
		x->bad = true;
		return 0;
	}
	for (u32 i = 0; i < count; i++, *p += sz_pdt (x))
	{
		v[i].name = rptr (x, *p);
		v[i].type = r32 (x, *p + ptr_size (x));
		if (v[i].type > 5)
			x->bad = true;
	}
	return v;
}

static void put_pdt_section (
	xl_t *x, ccp key, const xl_param_t *v, u32 count, u64 first, u32 n_system)
{
	if (!count)
		return;
	sb_printf (&x->b, "  %s:\n", key);
	for (u32 i = 0; i < count && !x->bad; i++)
	{
		const u64 o = first + (u64)sz_pdt (x) * i;
		const u64 vo = align_ptr (x, o + ptr_size (x) + 4);
		sb_puts (&x->b, "    - name: ");
		put_str (x, pdt_name (x, v + i));
		sb_printf (&x->b, "\n      type: %s\n      default: ", param_type_name (v[i].type));
		switch (v[i].type)
		{
			case 0:
				sb_printf (&x->b, "%lld",
					x->p64 ? (long long)(s64)r64 (x, vo) : (long long)(s32)r32 (x, vo));
				break;
			case 1:
				if (x->p64)
				{
					const u64 bits = r64 (x, vo);
					double dv;
					memcpy (&dv, &bits, 8);
					sb_printf (&x->b, "%.9g", dv);
				}
				else
					put_f (x, rf32 (x, vo));
				break;
			case 2:
				sb_puts (&x->b, rptr (x, vo) ? "true" : "false");
				break;
			case 3:
				sb_printf (&x->b, "%llu", (unsigned long long)rptr (x, vo));
				break;
			case 4:
				put_str (x, xl_str_in (x, x->pdt_str_pos, x->pdt_str_end, rptr (x, vo)));
				break;
			default:
				sb_puts (&x->b, "~");
		}
		sb_puts (&x->b, "\n");
		if (i < n_system)
			sb_puts (&x->b, "      system: true\n");
	}
}

static enumError xl_convert (xl_t *x)
{
	x->p64 = x->fam >= 2;
	const uint ps = ptr_size (x);
	const u32 data_size = r32 (x, 4);
	if (data_size > x->n || data_size < 0x48)
		return ERR_INVALID_DATA;
	x->n = data_size;

	u64 h = 0x0c;
	const u32 n_res_param = r32 (x, h), n_res_asset = r32 (x, h + 4), n_res_tow = r32 (x, h + 8);
	(void)n_res_param;
	(void)n_res_asset;
	(void)n_res_tow;
	h = align_ptr (x, h + 12);
	x->tow_pos = rptr (x, h);
	x->lp_pos = rptr (x, h + ps);
	h += 2 * ps;
	x->n_lp = r32 (x, h);
	x->n_le = r32 (x, h + 4);
	x->n_dv = r32 (x, h + 8);
	x->n_rnd = r32 (x, h + 12);
	x->n_curve = r32 (x, h + 16);
	x->n_cpt = r32 (x, h + 20);
	h = align_ptr (x, h + 24);
	x->ex_pos = rptr (x, h);
	h += ps;
	const u32 n_user = r32 (x, h);
	h = align_ptr (x, h + 4);
	x->cond_pos = rptr (x, h);
	x->name_pos = rptr (x, h + ps);
	h += 2 * ps;
	x->name_end = data_size;
	if (x->bad || x->name_pos >= data_size || x->cond_pos > x->name_pos || x->ex_pos > data_size
		|| x->tow_pos > data_size || x->lp_pos > data_size || n_user > 0x100000
		|| x->n_lp > 0x100000 || x->n_le > 0x100000)
		return ERR_INVALID_DATA;
	if (data_size && x->d[data_size - 1] != 0)
		return ERR_INVALID_DATA;

	const u64 hash_pos = h;
	const u64 uoff_pos = align_ptr (x, hash_pos + 4ull * n_user);
	u64 p = align_ptr (x, uoff_pos + (u64)ps * n_user);

	// param define table
	const u64 pdt_base = p;
	const u32 pdt_size = r32 (x, p);
	x->n_user_param = r32 (x, p + 4);
	x->n_asset_param = r32 (x, p + 8);
	x->n_custom_asset = r32 (x, p + 12);
	x->n_trig_param = r32 (x, p + 16);
	const u32 n_system = x->slink ? 8 : 0;
	if (x->bad || x->n_user_param > 64 || x->n_asset_param > 64 || x->n_trig_param > 32
		|| x->n_custom_asset > x->n_asset_param || x->n_user_param < n_system
		|| pdt_base + pdt_size > data_size)
		return ERR_INVALID_DATA;
	x->asset_param_pos = align_ptr (x, pdt_base + pdt_size);
	p = align_ptr (x, p + 0x14);
	const u64 user_first = p;
	x->user_p = load_pdt_params (x, &p, x->n_user_param);
	const u64 asset_first = p;
	x->asset_p = load_pdt_params (x, &p, x->n_asset_param);
	const u64 trig_first = p;
	x->trig_p = load_pdt_params (x, &p, x->n_trig_param);
	x->pdt_str_pos = p;
	x->pdt_str_end = pdt_base + pdt_size;
	if (x->bad || x->pdt_str_pos > x->pdt_str_end)
		return ERR_INVALID_DATA;

	x->dv_pos = x->lp_pos + (u64)ps * (x->n_lp + x->n_le);
	x->rnd_pos = x->dv_pos + 4ull * x->n_dv;
	x->curve_pos = x->rnd_pos + 8ull * x->n_rnd;
	x->cpt_pos = x->curve_pos + (u64)sz_curve (x) * x->n_curve;
	if (x->cpt_pos + 8ull * x->n_cpt > data_size)
		return ERR_INVALID_DATA;

	build_name_hashes (x);

	sb_printf (&x->b, "# XLink2 %s database (XLNK version 0x%x, %s, %s)\n",
		x->slink ? "sound link" : "effect link", x->version, x->p64 ? "64-bit" : "32-bit",
		x->be ? "big endian" : "little endian");
	sb_printf (&x->b, "module: %s\nversion: 0x%x\nlayout: %u\n", x->slink ? "SLink" : "ELink",
		x->version, x->fam);
	sb_puts (&x->b, "param-defines:\n");
	put_pdt_section (x, "user", x->user_p, x->n_user_param, user_first, n_system);
	put_pdt_section (x, "asset", x->asset_p, x->n_asset_param, asset_first,
		x->n_asset_param - x->n_custom_asset);
	put_pdt_section (x, "trigger", x->trig_p, x->n_trig_param, trig_first, 0);
	if (x->bad)
		return ERR_INVALID_DATA;
	// asset params past the system ones are game-defined
	if (x->n_custom_asset)
		sb_printf (&x->b, "custom-asset-params: %u\n", x->n_custom_asset);

	sb_printf (&x->b, "users: # %u\n", n_user);
	for (u32 i = 0; i < n_user && !x->bad; i++)
		put_user (x, r32 (x, hash_pos + 4ull * i), rptr (x, uoff_pos + (u64)ps * i));
	return x->bad || x->b.fail ? ERR_INVALID_DATA : ERR_OK;
}

static void xl_reset (xl_t *x)
{
	FREE (x->user_p);
	FREE (x->asset_p);
	FREE (x->trig_p);
	FREE (x->hashes);
	FREE (x->b.d);
	x->user_p = x->asset_p = x->trig_p = 0;
	x->hashes = 0;
	x->n_hashes = 0;
	memset (&x->b, 0, sizeof (x->b));
	x->bad = false;
}

bool IsXlnk (const u8 *data, size_t size)
{
	if (!data || size < 0x48 || memcmp (data, "XLNK", 4))
		return false;
	const u32 le = data[8] | data[9] << 8 | (u32)data[10] << 16 | (u32)data[11] << 24;
	const u32 be = (u32)data[8] << 24 | (u32)data[9] << 16 | data[10] << 8 | data[11];
	return (le >= 0x1c && le < 0x40) || (be >= 0x1c && be < 0x40);
}

enumError ConvertXlnkToYaml (const u8 *data, size_t size, ccp hint, char **text, size_t *text_size)
{
	*text = 0;
	if (text_size)
		*text_size = 0;
	if (!IsXlnk (data, size))
		return ERR_INVALID_DATA;
	const u32 le = data[8] | data[9] << 8 | (u32)data[10] << 16 | (u32)data[11] << 24;
	const bool be = !(le >= 0x1c && le < 0x40);
	const u32 ver = be ? (u32)data[8] << 24 | (u32)data[9] << 16 | data[10] << 8 | data[11] : le;

	// candidate {family, slink} pairs for this version
	struct
	{
		uint fam;
		bool slink;
	} cand[4];
	uint nc = 0;
#define ADD(f, s)                                                                                  \
	do                                                                                             \
	{                                                                                              \
		cand[nc].fam = (f);                                                                        \
		cand[nc++].slink = (s);                                                                    \
	} while (0)
	switch (ver)
	{
		case 0x1c:
			ADD (0, true);
			break;
		case 0x1e:
			ADD (0, false);
			break;
		case 0x1d:
			ADD (1, true);
			break;
		case 0x1f:
			ADD (2, true);
			ADD (1, false);
			break;
		case 0x22:
			ADD (3, true);
			ADD (2, false);
			break;
		case 0x21:
			ADD (3, true);
			break;
		case 0x24:
			ADD (3, false);
			break;
		default: // newer: assume the latest layout
			ADD (3, false);
			ADD (3, true);
			break;
	}
#undef ADD
	const bool want_slink = hint && (strstr (hint, ".bslnk") || strstr (hint, ".BSLNK"));
	const bool want_elink = hint && (strstr (hint, ".belnk") || strstr (hint, ".BELNK"));
	if (nc == 2 && ((want_slink && !cand[0].slink) || (want_elink && cand[0].slink)))
	{
		const uint f = cand[0].fam;
		const bool s = cand[0].slink;
		cand[0] = cand[1];
		cand[1].fam = f;
		cand[1].slink = s;
	}

	for (uint i = 0; i < nc; i++)
	{
		xl_t x = { 0 };
		x.d = data;
		x.n = size;
		x.be = be;
		x.version = ver;
		x.fam = cand[i].fam;
		x.slink = cand[i].slink;
		const enumError err = xl_convert (&x);
		if (!err)
		{
			FREE (x.user_p);
			FREE (x.asset_p);
			FREE (x.trig_p);
			FREE (x.hashes);
			return sb_finish (&x.b, text, text_size);
		}
		xl_reset (&x);
	}
	return ERR_INVALID_DATA;
}

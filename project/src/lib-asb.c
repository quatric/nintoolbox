// SPDX-License-Identifier: GPL-2.0+
// Animation sequence binaries (ASB 0x41B) to YAML; see lib-asb.h.

#include "lib-asb.h"
#include "lib-sbuf.h"

typedef struct asb_t
{
	const u8 *d;
	size_t n;
	bool bad;
	u32 pool, pool_size, n_elem, elem_pos, bb;
	sbuf_t b;
} asb_t;

static u32 a32 (asb_t *a, u64 o)
{
	if (o > a->n || a->n - o < 4)
	{
		a->bad = true;
		return 0;
	}
	const u8 *p = a->d + o;
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static u16 a16 (asb_t *a, u64 o)
{
	if (o > a->n || a->n - o < 2)
	{
		a->bad = true;
		return 0;
	}
	return (u16)(a->d[o] | a->d[o + 1] << 8);
}

static u8 a8 (asb_t *a, u64 o)
{
	if (o >= a->n)
	{
		a->bad = true;
		return 0;
	}
	return a->d[o];
}

static float af (asb_t *a, u64 o)
{
	const u32 v = a32 (a, o);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static ccp astr (asb_t *a, u32 off)
{
	const u64 o = (u64)a->pool + off;
	if (off >= a->pool_size || o >= a->n || !memchr (a->d + o, 0, a->n - o))
	{
		a->bad = true;
		return "";
	}
	return (ccp)a->d + o;
}

static void aquote (asb_t *a, u32 off)
{
	sb_quotez (&a->b, astr (a, off));
}

static void afloat (asb_t *a, float f)
{
	if (f != f)
		sb_puts (&a->b, ".nan");
	else if (f == (float)(s32)f && f > -1e9f && f < 1e9f)
		sb_printf (&a->b, "%d.0", (s32)f);
	else
		sb_printf (&a->b, "%.9g", f);
}

static void aguid (asb_t *a, u64 o)
{
	if (o + 16 > a->n)
	{
		a->bad = true;
		return;
	}
	const u8 *g = a->d + o;
	sb_printf (&a->b, "\"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x\"", a32 (a, o),
		a16 (a, o + 4), a16 (a, o + 6), g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
}

static void ind (asb_t *a, uint n)
{
	static const char sp[] = "                                ";
	sb_write (&a->b, sp, n < 32 ? n : 32);
}

// Tag array pointer: AsbArray<AsbString>, 0 or 0xffffffff = none. Written as a flow list.
static void atags (asb_t *a, u32 ptr)
{
	sb_puts (&a->b, "[");
	if (ptr && ptr != 0xffffffff)
	{
		const u32 n = a32 (a, ptr);
		for (u32 i = 0; i < n && i < 0x10000 && !a->bad; i++)
		{
			if (i)
				sb_puts (&a->b, ", ");
			aquote (a, a32 (a, ptr + 4 + 4ull * i));
		}
	}
	sb_puts (&a->b, "]");
}

//-----------------------------------------------------------------------------
// blackboard: 7 type headers {u16 count, u16 base, u16 default offset, u16}, then u32 entries
// {name : 24, extra info index : 7, is extra : 1}, then the default values.

static ccp const bb_type_name[7]
	= { "string", "int", "uint", "float", "bool", "vector3f", "pointer" };

static ccp bb_entry_name (asb_t *a, uint type, u32 index)
{
	if (!a->bb || type >= 7)
		return 0;
	const u16 count = a16 (a, a->bb + 8 * type), base = a16 (a, a->bb + 8 * type + 2);
	if (index >= count)
		return 0;
	return astr (a, a32 (a, a->bb + 0x38 + 4ull * (base + index)) & 0xffffff);
}

// Property kinds
enum
{
	P_FLOAT,
	P_BOOL,
	P_INT,
	P_STRING,
	P_UINT,
};

static void aprop (asb_t *a, u64 o, uint kind)
{
	const u32 flags = a32 (a, o);
	const u64 v = o + 4;
	if (!(flags >> 31))
	{
		switch (kind)
		{
			case P_FLOAT:
				afloat (a, af (a, v));
				break;
			case P_BOOL:
				sb_puts (&a->b, a32 (a, v) ? "true" : "false");
				break;
			case P_INT:
				sb_printf (&a->b, "%d", (s32)a32 (a, v));
				break;
			case P_STRING:
				aquote (a, a32 (a, v));
				break;
			default:
				sb_printf (&a->b, "%u", a32 (a, v));
		}
		if (flags)
			sb_printf (&a->b, " # flags 0x%x", flags);
		return;
	}
	const u32 idx = flags & 0xffff;
	if (flags >> 24 & 1)
	{
		sb_printf (&a->b, "{exb: %u, input: ", idx);
		kind == P_FLOAT ? afloat (a, af (a, v)) : (void)sb_printf (&a->b, "%d", (s32)a32 (a, v));
		sb_puts (&a->b, "}");
	}
	else if (kind == P_FLOAT && flags >> 30 & 1)
		sb_printf (&a->b, "{calc-cache: %u}", idx);
	else if ((kind == P_FLOAT || kind == P_STRING) && flags >> 25 & 1)
	{
		static ccp const cd[]
			= { "normalized-frame", "random", "random", "string", "fade-in-frame", "5", "float" };
		sb_printf (&a->b, "{command-data: %s}", idx < 7 ? cd[idx] : "?");
	}
	else
	{
		uint type = kind == P_FLOAT ? 3
			: kind == P_BOOL		? 4
			: kind == P_STRING		? 0
			: kind == P_UINT		? 2
									: 1;
		const uint comp = kind == P_FLOAT ? flags >> 26 & 3 : 0;
		if (comp)
			type = 5;
		ccp nm = bb_entry_name (a, type, idx);
		sb_printf (&a->b, "{blackboard: %u", idx);
		if (nm)
		{
			sb_puts (&a->b, ", name: ");
			sb_quotez (&a->b, nm);
		}
		if (comp)
			sb_printf (&a->b, ", component: %c", "xyz"[comp - 1]);
		sb_puts (&a->b, "}");
	}
}

//-----------------------------------------------------------------------------
// elements

typedef struct field_t
{
	u8 off;
	u8 kind; // P_* or 'T' tags, 'U' raw u32
	ccp name;
} field_t;

typedef struct etype_t
{
	ccp name;
	int plug; // plug table offset in the params, -1 = unknown
	u8 child_size; // child plug entry size; the child id is its last u32
	u8 child_kind; // selector value kind of a child plug (0xff = none)
	const field_t *f;
	uint nf;
} etype_t;

#define F(o, k, n) { o, k, n }
static const field_t f_selector_f[] = { F (0, P_FLOAT, "select-value"), F (8, P_BOOL, "slot-sync"),
	F (0x10, 'U', "select-timing") };
static const field_t f_selector_i[]
	= { F (0, P_INT, "select-value"), F (8, P_BOOL, "slot-sync"), F (0x10, 'U', "select-timing") };
static const field_t f_selector_b[]
	= { F (0, P_BOOL, "select-value"), F (8, P_BOOL, "slot-sync"), F (0x10, 'U', "select-timing") };
static const field_t f_skel[] = { F (0, P_STRING, "animation"), F (8, 'U', "unknown-08"),
	F (0x0c, 'U', "unknown-0c"), F (0x10, P_BOOL, "mirror"), F (0x18, P_FLOAT, "mirror-offset") };
static const field_t f_blend1[] = { F (0, P_FLOAT, "weight"), F (8, 'U', "easing") };
static const field_t f_seq[] = { F (0, P_BOOL, "looping"), F (8, P_INT, "loop-count"),
	F (0x10, P_BOOL, "select-in-next-frame") };
static const field_t f_simul[] = { F (0, 'U', "state-end-type") };
static const field_t f_event[] = { F (0, 'U', "unknown-00") };
static const field_t f_mat[] = { F (0, 'U', "material-blend"), F (4, P_STRING, "animation"),
	F (0x0c, P_BOOL, "reset-on-state-end") };
static const field_t f_frame[] = { F (0, P_FLOAT, "play-rate"), F (8, P_FLOAT, "start-frame"),
	F (0x10, P_FLOAT, "frame-count"), F (0x18, 'U', "loop-mode"), F (0x1c, P_BOOL, "kill-loop"),
	F (0x24, P_BOOL, "play-reverse"), F (0x2c, P_INT, "loop-count"),
	F (0x34, P_INT, "loop-count-random-range"), F (0x3c, P_BOOL, "slot-sync-random"),
	F (0x44, P_FLOAT, "seek-played-percent"), F (0x4c, P_FLOAT, "seek-frame"),
	F (0x54, P_FLOAT, "end-frame"), F (0x5c, 'U', "end-frame-mode"),
	F (0x60, P_FLOAT, "unknown-60"), F (0x68, P_BOOL, "unknown-68"),
	F (0x70, P_BOOL, "seek-debug") };
static const field_t f_dummy[] = { F (0, P_FLOAT, "frame-count"), F (8, P_BOOL, "looping") };
static const field_t f_random[] = { F (0, 'U', "judge-timing"), F (4, P_BOOL, "slot-sync"),
	F (0x0c, P_INT, "forbid-same-child-history"), F (0x14, P_INT, "unknown-14"),
	F (0x1c, 'U', "select-timing") };
static const field_t f_prevtag[] = { F (0, 'U', "unknown-00") };
static const field_t f_bonepos[] = { F (0, P_STRING, "left-bone") };
static const field_t f_bonevis[]
	= { F (0, P_STRING, "animation"), F (8, P_BOOL, "reset-on-state-end"),
		  F (0x10, P_BOOL, "mirror"), F (0x18, P_FLOAT, "mirror-offset") };
static const field_t f_initial[] = { F (0, 'U', "initial-frame-type"), F (4, 'T', "tags"),
	F (8, P_BOOL, "share-by-same-anim-key"), F (0x10, P_STRING, "left-bone"),
	F (0x18, P_STRING, "right-bone"), F (0x20, 'U', "component"), F (0x24, P_BOOL, "random"),
	F (0x2c, P_BOOL, "slot-sync-random") };
static const field_t f_boneblend[] = { F (0, P_STRING, "bone-blend"), F (8, 'U', "sync-child"),
	F (0x0c, P_FLOAT, "bone-blend-mode-value"), F (0x14, 'U', "bone-blend-mode"),
	F (0x18, 'U', "state-end-mode") };
static const field_t f_alert[] = { F (0, P_STRING, "alert") };
static const field_t f_shape[] = { F (0, P_STRING, "animation") };
static const field_t f_blend2[] = { F (0, P_FLOAT, "weight-0"), F (8, P_FLOAT, "weight-1"),
	F (0x10, 'U', "key-frames-offset"), F (0x14, 'U', "key-data-offset") };
#undef F
#define NF(x) x, sizeof (x) / sizeof (*x)

static const etype_t etypes[28] = {
	[1] = { "FloatSelector", 0x14, 0x14, P_FLOAT, NF (f_selector_f) },
	[2] = { "StringSelector", 0x14, 0x0c, P_STRING, NF (f_selector_f) },
	[3] = { "SkeletalAnimation", 0x20, 4, 0xff, NF (f_skel) },
	[4] = { "State", 0, 4, 0xff, 0, 0 },
	[5] = { "StateEnd", -1, 4, 0xff, 0, 0 },
	[6] = { "OneDimensionalBlender", 0x0c, 0x14, P_FLOAT, NF (f_blend1) },
	[7] = { "Sequential", 0x18, 4, 0xff, NF (f_seq) },
	[8] = { "IntSelector", 0x14, 0x0c, P_INT, NF (f_selector_i) },
	[9] = { "Simultaneous", 4, 4, 0xff, NF (f_simul) },
	[10] = { "Event", 4, 4, 0xff, NF (f_event) },
	[11] = { "MaterialAnimation", 0x14, 4, 0xff, NF (f_mat) },
	[12] = { "FrameController", 0x78, 4, 0xff, NF (f_frame) },
	[13] = { "DummyAnimation", 0x10, 4, 0xff, NF (f_dummy) },
	[14] = { "RandomSelector", 0x20, 0x0c, P_FLOAT, NF (f_random) },
	[15] = { "StateEnd", -1, 4, 0xff, 0, 0 },
	[16] = { "PreviousTagSelector", 4, 8, 'T', NF (f_prevtag) },
	[17] = { "BonePositionSelector", 0x1c, 0x14, P_FLOAT, NF (f_bonepos) },
	[18] = { "BoneVisibilityAnimation", 0x20, 4, 0xff, NF (f_bonevis) },
	[19] = { "InitialFrame", 0x34, 4, 0xff, NF (f_initial) },
	[20] = { "BoneBlender", 0x1c, 4, 0xff, NF (f_boneblend) },
	[21] = { "BoolSelector", 0x14, 4, 0xff, NF (f_selector_b) },
	[22] = { "Alert", 8, 4, 0xff, NF (f_alert) },
	[23] = { "SubtractAnimation", 0, 4, 0xff, 0, 0 },
	[24] = { "ShapeAnimation", 8, 4, 0xff, NF (f_shape) },
	[25] = { 0, -1, 4, 0xff, 0, 0 },
	[26] = { "TwoDimensionalBlender", 0x18, 4, 0xff, NF (f_blend2) },
	[27] = { 0, -1, 4, 0xff, 0, 0 },
};
#undef NF

static void afield (asb_t *a, u64 base, const field_t *f, uint indent)
{
	ind (a, indent);
	sb_printf (&a->b, "%s: ", f->name);
	if (f->kind == 'U')
	{
		const u32 v = a32 (a, base + f->off);
		sb_printf (&a->b, v > 0xffff ? "0x%x" : "%u", v);
	}
	else if (f->kind == 'T')
		atags (a, a32 (a, base + f->off));
	else
		aprop (a, base + f->off, f->kind);
	sb_puts (&a->b, "\n");
}

static void ahex (asb_t *a, u64 o, u64 len)
{
	sb_puts (&a->b, "\"");
	for (u64 i = 0; i < len && i < 0x400; i++)
		sb_printf (&a->b, "%02x", a8 (a, o + i));
	if (len > 0x400)
		sb_puts (&a->b, "...");
	sb_puts (&a->b, "\"");
}

static void aelement (asb_t *a, u32 i, u32 param_end)
{
	const u64 e = a->elem_pos + 0x24ull * i;
	const uint type = a16 (a, e);
	const uint n_att = a8 (a, e + 2), no_end = a8 (a, e + 3);
	const u32 tags = a32 (a, e + 4), par = a32 (a, e + 8);
	const uint fx_flags = a16 (a, e + 0x0c), fx_count = a16 (a, e + 0x0e),
			   att_base = a16 (a, e + 0x10), ext = a16 (a, e + 0x12);
	const etype_t *t = type < 28 ? etypes + type : 0;
	sb_printf (
		&a->b, "  - index: %u\n    type: %s\n    guid: ", i, t && t->name ? t->name : "unknown");
	aguid (a, e + 0x14);
	sb_puts (&a->b, "\n");
	if (!t || !t->name)
		sb_printf (&a->b, "    type-id: %u\n", type);
	if (tags && tags != 0xffffffff)
	{
		sb_puts (&a->b, "    tags: ");
		atags (a, tags);
		sb_puts (&a->b, "\n");
	}
	if (n_att)
		sb_printf (&a->b, "    attachments: {count: %u, base: %u}\n", n_att, att_base);
	if (no_end)
		sb_puts (&a->b, "    no-state-end: true\n");
	if (ext != 0xffff && ext)
		sb_printf (&a->b, "    external-action: %u\n", ext);
	if (fx_count)
		sb_printf (&a->b, "    float-property-ex: {flags: 0x%x, count: %u}\n", fx_flags, fx_count);
	if (!par)
		return;
	if (!t || t->plug < 0)
	{
		sb_puts (&a->b, "    params-raw: ");
		ahex (a, par, param_end > par ? param_end - par : 0);
		sb_puts (&a->b, "\n");
		return;
	}
	for (uint k = 0; k < t->nf && !a->bad; k++)
		afield (a, par, t->f + k, 4);

	// plug table
	const u64 p = par + (u64)t->plug;
	u8 c[12];
	for (uint k = 0; k < 12; k++)
		c[k] = a8 (a, p + k);
	uint total = 0;
	for (uint k = 0; k < 6; k++)
		total += c[2 * k];
	static ccp const groups[6]
		= { "states", "unknown-plugs", "children", "exb-plugs", "events", "frame-controllers" };
	for (uint g = 0; g < 6 && !a->bad; g++)
	{
		const uint cnt = c[2 * g], base = c[2 * g + 1];
		if (!cnt)
			continue;
		if (base + cnt > total)
		{
			a->bad = true;
			return;
		}
		sb_printf (&a->b, "    %s:\n", groups[g]);
		for (uint k = base; k < base + cnt && !a->bad; k++)
		{
			const u64 q = a32 (a, p + 12 + 4ull * k);
			const u64 next = k + 1 < total ? a32 (a, p + 12 + 4ull * (k + 1)) : 0;
			if (g == 2)
			{
				const uint cs = t->child_size;
				const u32 child = a32 (a, q + cs - 4);
				if (child >= a->n_elem)
					a->bad = true;
				sb_printf (&a->b, "      - {element: %u", child);
				if (t->child_kind == 'T')
				{
					sb_puts (&a->b, ", tags: ");
					atags (a, a32 (a, q));
				}
				else if (t->child_kind != 0xff)
				{
					sb_puts (&a->b, cs == 0x14 ? ", min: " : ", value: ");
					aprop (a, q, t->child_kind);
					if (cs == 0x14)
					{
						sb_puts (&a->b, ", max: ");
						aprop (a, q + 8, t->child_kind);
					}
				}
				sb_puts (&a->b, "}\n");
			}
			else if (g >= 4)
			{
				const u32 el = a32 (a, q);
				if (el >= a->n_elem)
					a->bad = true;
				sb_printf (&a->b, "      - %u\n", el);
			}
			else
			{
				sb_puts (&a->b, "      - ");
				ahex (a, q, next > q ? next - q : 4);
				sb_puts (&a->b, "\n");
			}
		}
	}
}

//-----------------------------------------------------------------------------

bool IsAsb (const u8 *d, size_t size)
{
	return d && size >= 0x80 && !memcmp (d, "ASB ", 4)
		&& (d[4] | d[5] << 8 | (u32)d[6] << 16 | (u32)d[7] << 24) == 0x41b;
}

static int cmp_u32 (const void *x, const void *y)
{
	const u32 a = *(const u32 *)x, b = *(const u32 *)y;
	return a < b ? -1 : a > b;
}

static bool asb_init (asb_t *a, const u8 *d, size_t size)
{
	memset (a, 0, sizeof (*a));
	a->d = d;
	a->n = size;
	if (!IsAsb (d, size))
		return false;
	a->pool = a32 (a, 0x3c);
	a->pool_size = a32 (a, 0x60);
	a->n_elem = a32 (a, 0x14);
	a->elem_pos = a32 (a, 0x18);
	a->bb = a32 (a, 0x38);
	return !a->bad && a->pool < size && a->pool_size <= size - a->pool
		&& (u64)a->elem_pos + 0x24ull * a->n_elem <= size;
}

size_t CollectAsbGuidStrings (const u8 *d, size_t size, u8 **out)
{
	*out = 0;
	asb_t a;
	if (!asb_init (&a, d, size))
		return 0;
	sbuf_t b = { 0 };
	for (u32 i = 0; i < a.n_elem; i++)
	{
		const u8 *g = d + a.elem_pos + 0x24ull * i + 0x14;
		sb_printf (&b, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
			g[0] | g[1] << 8 | g[2] << 16 | (u32)g[3] << 24, g[4] | g[5] << 8, g[6] | g[7] << 8,
			g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
		sb_write (&b, "", 1);
	}
	size_t n = 0;
	char *t = 0;
	if (sb_finish (&b, &t, &n))
		return 0;
	*out = (u8 *)t;
	return n;
}

enumError ConvertAsbToYaml (const u8 *d, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	if (text_size)
		*text_size = 0;
	asb_t a;
	if (!asb_init (&a, d, size))
		return ERR_INVALID_DATA;

	sb_puts (&a.b, "# Animation sequence binary (ASB 0x41b)\nname: ");
	aquote (&a, a32 (&a, 8));
	sb_puts (&a.b, "\n");

	// commands
	const u32 n_cmd = a32 (&a, 0x0c), cmd = a32 (&a, 0x10);
	if (n_cmd > 0x10000 || (u64)cmd + 0x30ull * n_cmd > size)
		a.bad = true;
	if (n_cmd)
		sb_printf (&a.b, "commands: # %u\n", n_cmd);
	for (u32 i = 0; i < n_cmd && !a.bad; i++)
	{
		const u64 c = cmd + 0x30ull * i;
		sb_puts (&a.b, "  - name: ");
		aquote (&a, a32 (&a, c));
		const u32 tg = a32 (&a, c + 4);
		if (tg && tg != 0xffffffff)
		{
			sb_puts (&a.b, "\n    tags: ");
			atags (&a, tg);
		}
		sb_puts (&a.b, "\n    frame-count: ");
		aprop (&a, c + 8, P_FLOAT);
		const u32 dbg = a32 (&a, c + 0x10);
		if (dbg)
			sb_printf (&a.b, "\n    debug: %u", dbg);
		sb_puts (&a.b, "\n    interpolation: ");
		aprop (&a, c + 0x14, P_UINT);
		sb_puts (&a.b, "\n    guid: ");
		aguid (&a, c + 0x1c);
		const uint root = a16 (&a, c + 0x2c);
		if (root >= a.n_elem)
			a.bad = true;
		sb_printf (&a.b, "\n    element: %u\n", root);
		const uint u2e = a16 (&a, c + 0x2e);
		if (u2e)
			sb_printf (&a.b, "    unknown-2e: %u\n", u2e);
	}

	// elements, with each param region bounded by the next one
	u32 *ends = CALLOC (a.n_elem + 1, sizeof (u32));
	if (!ends)
		return ERR_OUT_OF_MEMORY;
	for (u32 i = 0; i < a.n_elem; i++)
		ends[i] = a32 (&a, a.elem_pos + 0x24ull * i + 8);
	qsort (ends, a.n_elem, sizeof (u32), cmp_u32);
	if (a.n_elem)
		sb_printf (&a.b, "elements: # %u\n", a.n_elem);
	for (u32 i = 0; i < a.n_elem && !a.bad; i++)
	{
		const u32 par = a32 (&a, a.elem_pos + 0x24ull * i + 8);
		u32 end = a32 (&a, 0x34) > par ? a32 (&a, 0x34) : (u32)size;
		for (u32 k = 0; k < a.n_elem; k++)
			if (ends[k] > par)
			{
				end = ends[k];
				break;
			}
		aelement (&a, i, end);
	}
	FREE (ends);

	// blackboard
	if (a.bb && !a.bad)
	{
		uint total = 0;
		for (uint t = 0; t < 7; t++)
			total += a16 (&a, a.bb + 8 * t);
		if (total)
		{
			const u64 defaults = a.bb + 0x38 + 4ull * total;
			static const u8 vsize[7] = { 4, 4, 4, 4, 4, 12, 0 };
			sb_puts (&a.b, "blackboard:\n");
			for (uint t = 0; t < 7 && !a.bad; t++)
			{
				const uint cnt = a16 (&a, a.bb + 8 * t), base = a16 (&a, a.bb + 8 * t + 2),
						   doff = a16 (&a, a.bb + 8 * t + 4);
				if (!cnt)
					continue;
				sb_printf (&a.b, "  %s:\n", bb_type_name[t]);
				for (uint k = 0; k < cnt && !a.bad; k++)
				{
					const u32 ent = a32 (&a, a.bb + 0x38 + 4ull * (base + k));
					sb_puts (&a.b, "    - {name: ");
					aquote (&a, ent & 0xffffff);
					if (ent >> 31)
						sb_printf (&a.b, ", extra-info: %u", ent >> 24 & 0x7f);
					if (vsize[t])
					{
						const u64 v = defaults + doff + (u64)vsize[t] * k;
						sb_puts (&a.b, ", default: ");
						switch (t)
						{
							case 0:
								aquote (&a, a32 (&a, v));
								break;
							case 1:
								sb_printf (&a.b, "%d", (s32)a32 (&a, v));
								break;
							case 2:
								sb_printf (&a.b, "%u", a32 (&a, v));
								break;
							case 3:
								afloat (&a, af (&a, v));
								break;
							case 4:
								sb_puts (&a.b, a32 (&a, v) ? "true" : "false");
								break;
							case 5:
								sb_puts (&a.b, "[");
								afloat (&a, af (&a, v));
								sb_puts (&a.b, ", ");
								afloat (&a, af (&a, v + 4));
								sb_puts (&a.b, ", ");
								afloat (&a, af (&a, v + 8));
								sb_puts (&a.b, "]");
								break;
						}
					}
					sb_puts (&a.b, "}\n");
				}
			}
		}
	}

	// partials
	const u32 n_part = a32 (&a, 0x24), part = a32 (&a, 0x28);
	if (n_part && !a.bad)
	{
		sb_puts (&a.b, "partials:\n");
		for (u32 i = 0; i < n_part && i < 0x10000 && !a.bad; i++)
		{
			const u64 p = part + 0x10ull * i;
			sb_puts (&a.b, "  - {name: ");
			aquote (&a, a32 (&a, p));
			sb_printf (&a.b, ", partial-skeleton: %u, unknown-08: ", a32 (&a, p + 4));
			aquote (&a, a32 (&a, p + 8));
			sb_printf (&a.b, ", unknown-0c: %u}\n", a32 (&a, p + 12));
		}
	}

	// partial skeletons
	const u32 n_ps = a32 (&a, 0x58), ps = a32 (&a, 0x5c);
	if (n_ps && !a.bad)
	{
		sb_puts (&a.b, "partial-skeletons:\n");
		for (u32 i = 0; i < n_ps && i < 0x10000 && !a.bad; i++)
		{
			const u64 p = ps + 0x0cull * i;
			const u32 bones = a32 (&a, p), nb = a32 (&a, p + 8);
			sb_puts (&a.b, "  - name: ");
			aquote (&a, a32 (&a, p + 4));
			sb_puts (&a.b, "\n    bones:\n");
			for (u32 k = 0; k < nb && k < 0x10000 && !a.bad; k++)
			{
				const u64 bo = bones + 8ull * k;
				sb_puts (&a.b, "      - {name: ");
				aquote (&a, a32 (&a, bo));
				sb_printf (&a.b, ", bind-flags: %d, recursive: %s}\n", (s16)a16 (&a, bo + 4),
					a8 (&a, bo + 6) ? "true" : "false");
			}
		}
	}

	// float property ex
	const u32 fx = a32 (&a, 0x48), n_fx = a32 (&a, 0x4c); // pointer first, unlike the other pairs
	if (n_fx && !a.bad)
	{
		sb_puts (&a.b, "float-property-ex:\n");
		for (u32 i = 0; i < n_fx && i < 0x100000 && !a.bad; i++)
		{
			const u64 p = fx + 0x20ull * i;
			sb_puts (&a.b, "  - {value: ");
			aprop (&a, p, P_FLOAT);
			sb_printf (&a.b, ", curve-type: %u, curve: [", a32 (&a, p + 8));
			for (uint k = 0; k < 3; k++)
			{
				if (k)
					sb_puts (&a.b, ", ");
				afloat (&a, af (&a, p + 12 + 4 * k));
			}
			sb_puts (&a.b, "], min: ");
			afloat (&a, af (&a, p + 0x18));
			sb_puts (&a.b, ", max: ");
			afloat (&a, af (&a, p + 0x1c));
			sb_puts (&a.b, "}\n");
		}
	}

	// tags, external actions, material blends, enum relocations
	const u32 tg = a32 (&a, 0x6c);
	if (tg && a32 (&a, tg) && !a.bad)
	{
		sb_puts (&a.b, "tags: ");
		atags (&a, tg);
		sb_puts (&a.b, "\n");
	}
	const u32 xa = a32 (&a, 0x70);
	if (xa && !a.bad)
	{
		const u32 n = a32 (&a, xa);
		if (n)
			sb_puts (&a.b, "external-actions:\n");
		for (u32 i = 0; i < n && i < 0x10000 && !a.bad; i++)
		{
			const u64 p = xa + 4 + 12ull * i;
			sb_puts (&a.b, "  - {type: ");
			aquote (&a, a32 (&a, p));
			sb_puts (&a.b, ", slot: ");
			aquote (&a, a32 (&a, p + 4));
			sb_puts (&a.b, ", action: ");
			aquote (&a, a32 (&a, p + 8));
			sb_puts (&a.b, "}\n");
		}
	}
	const u32 mb = a32 (&a, 0x7c);
	if (mb && !a.bad)
	{
		const u32 n = a32 (&a, mb);
		if (n)
			sb_puts (&a.b, "material-blends:\n");
		for (u32 i = 0; i < n && i < 0x10000 && !a.bad; i++)
		{
			const u64 p = mb + 4 + 8ull * i;
			sb_puts (&a.b, "  - {name: ");
			aquote (&a, a32 (&a, p));
			sb_puts (&a.b, ", weight: ");
			afloat (&a, af (&a, p + 4));
			sb_puts (&a.b, "}\n");
		}
	}
	const u32 er = a32 (&a, 0x40);
	if (er && !a.bad)
	{
		const u32 n = a32 (&a, er);
		if (n)
			sb_puts (&a.b, "enum-relocations:\n");
		for (u32 i = 0; i < n && i < 0x10000 && !a.bad; i++)
		{
			const u64 p = er + 4 + 12ull * i;
			sb_printf (&a.b, "  - {at: 0x%x, class: ", a32 (&a, p));
			aquote (&a, a32 (&a, p + 4));
			sb_puts (&a.b, ", value: ");
			aquote (&a, a32 (&a, p + 8));
			sb_puts (&a.b, "}\n");
		}
	}

	// sections whose records are not decoded
	const u32 st = a32 (&a, 0x44);
	sb_printf (&a.b,
		"# not decoded: %u states, %u bone blends, %u attachments, transition group count %u\n",
		st ? a32 (&a, st) : 0, a32 (&a, 0x50), a32 (&a, 0x2c), a32 (&a, 0x78));

	if (a.bad)
	{
		FREE (a.b.d);
		return ERR_INVALID_DATA;
	}
	return sb_finish (&a.b, text, text_size);
}

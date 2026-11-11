// SPDX-License-Identifier: GPL-2.0+
// AI node graphs (.ainb) as YAML. See lib-ainb.h.

#include "lib-ainb.h"
#include "lib-sbuf.h"

typedef struct ainb_t
{
	const u8 *d;
	size_t size, pool;
	uint version;
	sbuf_t *b;
} ainb_t;

static u32 n_le32 (const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static u16 n_le16 (const u8 *p) { return (u16)(p[0] | p[1] << 8); }

static u32 ai_u32 (const ainb_t *a, size_t o) { return o + 4 <= a->size ? n_le32 (a->d + o) : 0; }
static u16 ai_u16 (const ainb_t *a, size_t o) { return o + 2 <= a->size ? n_le16 (a->d + o) : 0; }

static float ai_f32 (const ainb_t *a, size_t o)
{
	const u32 v = ai_u32 (a, o);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static void ai_float (const ainb_t *a, float f)
{
	if (f != f)
		sb_puts (a->b, ".nan");
	else if (f > 3.4e38f || f < -3.4e38f)
		sb_puts (a->b, f > 0 ? ".inf" : "-.inf");
	else
		sb_printf (a->b, "%.9g", f);
}

// Quoted pool string.
static void ai_str (const ainb_t *a, u32 off)
{
	const size_t at = a->pool + off;
	if (at >= a->size)
	{
		sb_puts (a->b, "\"\"");
		return;
	}
	sb_quote (a->b, (const char *)a->d + at, strnlen ((const char *)a->d + at, a->size - at));
}

static void ai_guid (const ainb_t *a, size_t o)
{
	const u8 *g = o + 16 <= a->size ? a->d + o : (const u8 *)"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
	sb_printf (a->b, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", n_le32 (g), n_le16 (g + 4), n_le16 (g + 6), g[8],
		g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
}

static void ai_indent (const ainb_t *a, uint n)
{
	for (uint i = 0; i < n; i++)
		sb_puts (a->b, "  ");
}

bool IsAINB (const u8 *d, size_t size)
{
	if (!d || size < 0x74 || memcmp (d, "AIB ", 4))
		return false;
	const u32 v = n_le32 (d + 4);
	return v >= 0x404 && v <= 0x408;
}

static ccp const ai_types[6] = { "Int", "Bool", "Float", "String", "Vec3f", "Pointer" };
static const uint ai_prop_size[6] = { 0x0c, 0x0c, 0x0c, 0x0c, 0x14, 0x0c };
static const uint ai_input_size[6] = { 0x10, 0x10, 0x10, 0x10, 0x18, 0x14 };

static ccp ai_node_type (uint t)
{
	switch (t)
	{
	case 0: return "UserDefined";
	case 1: return "S32Selector";
	case 2: return "Sequential";
	case 3: return "Simultaneous";
	case 4: return "F32Selector";
	case 5: return "StringSelector";
	case 6: return "RandomSelector";
	case 7: return "BoolSelector";
	case 8: return "Fork";
	case 9: return "Join";
	case 10: return "Alert";
	case 20: return "Expression";
	case 100: return "ModuleIF_Input_S32";
	case 101: return "ModuleIF_Input_F32";
	case 102: return "ModuleIF_Input_Vec3f";
	case 103: return "ModuleIF_Input_String";
	case 104: return "ModuleIF_Input_Bool";
	case 105: return "ModuleIF_Input_Ptr";
	case 200: return "ModuleIF_Output_S32";
	case 201: return "ModuleIF_Output_F32";
	case 202: return "ModuleIF_Output_Vec3f";
	case 203: return "ModuleIF_Output_String";
	case 204: return "ModuleIF_Output_Bool";
	case 205: return "ModuleIF_Output_Ptr";
	case 300: return "ModuleIF_Child";
	case 400: return "StateEnd";
	case 500: return "SplitTiming";
	}
	return 0;
}

// Writes a typed value at O (int, bool, float, string, vec3f; pointers have none).
static void ai_value (const ainb_t *a, uint type, size_t o)
{
	switch (type)
	{
	case 0: sb_printf (a->b, "%d", (int)ai_u32 (a, o)); break;
	case 1: sb_puts (a->b, ai_u32 (a, o) ? "true" : "false"); break;
	case 2: ai_float (a, ai_f32 (a, o)); break;
	case 3: ai_str (a, ai_u32 (a, o)); break;
	case 4:
		sb_puts (a->b, "[");
		for (uint i = 0; i < 3; i++)
		{
			sb_puts (a->b, i ? ", " : "");
			ai_float (a, ai_f32 (a, o + 4 * i));
		}
		sb_puts (a->b, "]");
		break;
	default: sb_puts (a->b, "null");
	}
}

// Property entry at O: name, [class name for pointers], flags, value.
static void ai_property (const ainb_t *a, uint type, size_t o)
{
	sb_printf (a->b, "{type: %s, name: ", ai_types[type]);
	ai_str (a, ai_u32 (a, o));
	if (type == 5)
	{
		sb_puts (a->b, ", class: ");
		ai_str (a, ai_u32 (a, o + 4));
		sb_printf (a->b, ", flags: 0x%x}", ai_u32 (a, o + 8));
		return;
	}
	sb_printf (a->b, ", flags: 0x%x, value: ", ai_u32 (a, o + 4));
	ai_value (a, type, o + 8);
	sb_puts (a->b, "}");
}

static void ai_source (const ainb_t *a, size_t o)
{
	const int node = (s16)ai_u16 (a, o), out = (s16)ai_u16 (a, o + 2);
	const u32 flags = ai_u32 (a, o + 4);
	if (out < 0)
		sb_printf (a->b, "{blackboard: %d", out & 0x7fff);
	else
		sb_printf (a->b, "{node: %d, output: %d", node, out);
	if (flags)
		sb_printf (a->b, ", flags: 0x%x", flags);
	sb_puts (a->b, "}");
}

typedef struct ai_tables_t
{
	size_t prop[6], prop_end[6], in[6], out[6], out_end[6], multi, multi_end;
} ai_tables_t;

// Input entry at O: name, [class name], source {s16 node, s16 output, u32 flags}, default value.
static void ai_input (const ainb_t *a, const ai_tables_t *t, uint type, size_t o)
{
	sb_printf (a->b, "{type: %s, name: ", ai_types[type]);
	ai_str (a, ai_u32 (a, o));
	size_t p = o + 4;
	if (type == 5)
	{
		sb_puts (a->b, ", class: ");
		ai_str (a, ai_u32 (a, p));
		p += 4;
	}
	const int node = (s16)ai_u16 (a, p);
	if (node <= -100)
	{
		const uint first = (uint)(-100 - node), n = ai_u16 (a, p + 2);
		sb_puts (a->b, ", sources: [");
		for (uint i = 0; i < n && t->multi + 8 * (first + i + 1) <= t->multi_end; i++)
		{
			sb_puts (a->b, i ? ", " : "");
			ai_source (a, t->multi + 8 * (first + i));
		}
		sb_puts (a->b, "]");
	}
	else if (node >= 0 || (s16)ai_u16 (a, p + 2) < 0 || ai_u32 (a, p + 4))
	{
		sb_puts (a->b, ", source: ");
		ai_source (a, p);
	}
	if (type != 5)
	{
		sb_puts (a->b, ", value: ");
		ai_value (a, type, p + 8);
	}
	sb_puts (a->b, "}");
}

static void ai_output (const ainb_t *a, uint type, size_t o)
{
	const u32 f = ai_u32 (a, o);
	sb_printf (a->b, "{type: %s, name: ", ai_types[type]);
	ai_str (a, f & 0x3fffffff);
	if (type == 5)
	{
		sb_puts (a->b, ", class: ");
		ai_str (a, ai_u32 (a, o + 4));
	}
	if (f >> 31)
		sb_puts (a->b, ", linked: true");
	sb_puts (a->b, "}");
}

// Property slices {base, count} x 6 at O.
static void ai_props (const ainb_t *a, const ai_tables_t *t, size_t o, uint depth)
{
	bool any = false;
	for (uint type = 0; type < 6; type++)
	{
		const u32 base = ai_u32 (a, o + 8 * type), n = ai_u32 (a, o + 8 * type + 4);
		for (u32 i = 0; i < n; i++)
		{
			const size_t e = t->prop[type] + (size_t)ai_prop_size[type] * (base + i);
			if (e + ai_prop_size[type] > t->prop_end[type])
				break;
			if (!any)
			{
				ai_indent (a, depth);
				sb_puts (a->b, "properties:\n");
				any = true;
			}
			ai_indent (a, depth + 1);
			sb_puts (a->b, "- ");
			ai_property (a, type, e);
			sb_puts (a->b, "\n");
		}
	}
}

static void ai_blackboard (const ainb_t *a, size_t o)
{
	static ccp const bb07[6] = { "String", "S32", "F32", "Bool", "Vec3f", "VoidPtr" };
	static ccp const bb08[7] = { "String", "S32", "U32", "F32", "Bool", "Vec3f", "VoidPtr" };
	static const uint vt07[6] = { 3, 0, 2, 1, 4, 5 }, vt08[7] = { 3, 0, 0, 2, 1, 4, 5 };
	static const uint vs07[6] = { 4, 4, 4, 4, 12, 0 }, vs08[7] = { 4, 4, 4, 4, 4, 12, 0 };
	const bool v8 = a->version >= 0x408;
	const uint nt = v8 ? 7 : 6;
	uint count[7], off[7], total = 0;
	for (uint i = 0; i < nt; i++)
	{
		count[i] = ai_u16 (a, o + 8 * i);
		off[i] = ai_u16 (a, o + 8 * i + 4);
		total += count[i];
	}
	if (!total)
		return;
	const size_t info = o + 8 * nt, base = info + 8 * (size_t)total;
	sb_puts (a->b, "blackboard:\n");
	size_t inf = info;
	for (uint i = 0; i < nt; i++)
		for (uint k = 0; k < count[i]; k++, inf += 8)
		{
			const u32 f = ai_u32 (a, inf);
			sb_printf (a->b, "  - {type: %s, name: ", (v8 ? bb08 : bb07)[i]);
			ai_str (a, f & 0x3fffff);
			const u32 notes = ai_u32 (a, inf + 4);
			if (a->pool + notes < a->size && a->d[a->pool + notes])
			{
				sb_puts (a->b, ", notes: ");
				ai_str (a, notes);
			}
			const uint vs = (v8 ? vs08 : vs07)[i];
			if (vs)
			{
				sb_puts (a->b, ", value: ");
				const uint vt = (v8 ? vt08 : vt07)[i];
				if (v8 && i == 2)
					sb_printf (a->b, "%u", ai_u32 (a, base + off[i] + (size_t)vs * k));
				else
					ai_value (a, vt, base + off[i] + (size_t)vs * k);
			}
			if (f >> 31)
				sb_printf (a->b, ", file_ref: %u", f >> 24 & 0x7f);
			sb_puts (a->b, "}\n");
		}
}

enumError ConvertAINBToYaml (const u8 *d, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	*text_size = 0;
	if (!IsAINB (d, size))
		return ERR_INVALID_DATA;
	sbuf_t b = { 0 };
	ainb_t A = { d, size, n_le32 (d + 0x24), n_le32 (d + 4), &b }, *a = &A;
	if (a->pool >= size)
		return ERR_INVALID_DATA;

	const u32 n_cmd = ai_u32 (a, 0x0c), n_node = ai_u32 (a, 0x10), n_att = ai_u32 (a, 0x18);
	const size_t bb = ai_u32 (a, 0x20), props = ai_u32 (a, 0x2c), trans = ai_u32 (a, 0x30), io = ai_u32 (a, 0x34),
				 multi = ai_u32 (a, 0x38), att = ai_u32 (a, 0x3c), att_idx = ai_u32 (a, 0x40), modules = ai_u32 (a, 0x5c),
				 actions = ai_u32 (a, 0x68);

	ai_tables_t t;
	for (uint i = 0; i < 6; i++)
	{
		t.prop[i] = ai_u32 (a, props + 4 * i);
		t.in[i] = ai_u32 (a, io + 8 * i);
		t.out[i] = ai_u32 (a, io + 8 * i + 4);
	}
	for (uint i = 0; i < 6; i++)
	{
		t.prop_end[i] = i < 5 ? t.prop[i + 1] : io;
		t.out_end[i] = i < 5 ? t.in[i + 1] : multi;
	}
	t.multi = multi;
	t.multi_end = trans;

	const size_t node_size = a->version >= 0x407 ? 0x3c : 0x38, cmd_at = 0x74, node_at = cmd_at + 0x18 * (size_t)n_cmd;
	if (n_cmd > 0x10000 || n_node > 0x100000 || node_at + node_size * n_node > size)
		return ERR_INVALID_DATA;

	sb_printf (&b, "# AI node graph (AINB 0x%x)\nfile: ", a->version);
	ai_str (a, ai_u32 (a, 0x08));
	sb_puts (&b, "\ncategory: ");
	ai_str (a, ai_u32 (a, 0x60));
	sb_printf (&b, "\nblackboard_id: 0x%08x\n", ai_u32 (a, ai_u32 (a, 0x70)));

	if (n_cmd)
	{
		sb_puts (&b, "commands:\n");
		for (u32 i = 0; i < n_cmd; i++)
		{
			const size_t c = cmd_at + 0x18 * i;
			sb_puts (&b, "  - {name: ");
			ai_str (a, ai_u32 (a, c));
			sb_puts (&b, ", guid: ");
			ai_guid (a, c + 4);
			sb_printf (&b, ", root: %u", ai_u16 (a, c + 0x14));
			if (ai_u16 (a, c + 0x16))
				sb_printf (&b, ", secondary_root: %u", ai_u16 (a, c + 0x16) - 1);
			sb_puts (&b, "}\n");
		}
	}

	if (bb && bb < size)
		ai_blackboard (a, bb);

	if (n_node)
		sb_puts (&b, "nodes:\n");
	for (u32 i = 0; i < n_node && !b.fail; i++)
	{
		const size_t n = node_at + node_size * i;
		const uint type = ai_u16 (a, n), flags = d[n + 6];
		size_t p = n + 8;
		const u32 name = ai_u32 (a, p);
		p += a->version >= 0x407 ? 8 : 4; // name, [hash], unknown
		p += 4;
		const size_t body = ai_u32 (a, p);
		p += 4 + 8; // expression counts, multi count, padding
		const u32 att_base = ai_u32 (a, p), n_attach = ai_u16 (a, n + 4);
		const ccp tn = ai_node_type (type);

		sb_printf (&b, "  - index: %d\n    type: ", (s16)ai_u16 (a, n + 2));
		if (tn)
			sb_puts (&b, tn);
		else
			sb_printf (&b, "%u", type);
		sb_puts (&b, "\n    name: ");
		ai_str (a, name);
		sb_puts (&b, "\n    guid: ");
		ai_guid (a, n + node_size - 16);
		sb_puts (&b, "\n");
		if (flags)
			sb_printf (&b, "    flags: [%s%s%s%s]\n", flags & 1 ? "query" : "", flags & 2 ? &", module"[!(flags & 1) * 2] : "",
				flags & 4 ? &", root"[!(flags & 3) * 2] : "", flags & 8 ? &", multi_param_type2"[!(flags & 7) * 2] : "");
		if (n_attach && att_idx && att)
		{
			sb_puts (&b, "    attachments:\n");
			for (u32 k = 0; k < n_attach; k++)
			{
				const size_t ix = att_idx + 4 * ((size_t)att_base + k);
				if (ix + 4 > att)
					break;
				const u32 ai = ai_u32 (a, ix);
				if (ai >= n_att)
					continue;
				const size_t e = att + (a->version >= 0x407 ? 16 : 12) * (size_t)ai;
				sb_puts (&b, "      - name: ");
				ai_str (a, ai_u32 (a, e));
				sb_puts (&b, "\n");
				const size_t ab = ai_u32 (a, e + 4);
				if (ab && ab + 0x34 <= size)
					ai_props (a, &t, ab + 4, 4);
			}
		}
		if (!body || body + 0x90 + 10 > size)
			continue;

		ai_props (a, &t, body, 2);
		bool any = false;
		for (uint type_i = 0; type_i < 6; type_i++)
		{
			const size_t s = body + 0x30 + 16 * type_i;
			const u32 base = ai_u32 (a, s), cnt = ai_u32 (a, s + 4);
			for (u32 k = 0; k < cnt; k++)
			{
				const size_t e = t.in[type_i] + (size_t)ai_input_size[type_i] * (base + k);
				if (e + ai_input_size[type_i] > t.out[type_i])
					break;
				if (!any)
					sb_puts (&b, "    inputs:\n"), any = true;
				sb_puts (&b, "      - ");
				ai_input (a, &t, type_i, e);
				sb_puts (&b, "\n");
			}
		}
		any = false;
		for (uint type_i = 0; type_i < 6; type_i++)
		{
			const size_t s = body + 0x30 + 16 * type_i + 8;
			const u32 base = ai_u32 (a, s), cnt = ai_u32 (a, s + 4);
			const uint es = type_i == 5 ? 8 : 4;
			for (u32 k = 0; k < cnt; k++)
			{
				const size_t e = t.out[type_i] + (size_t)es * (base + k);
				if (e + es > t.out_end[type_i])
					break;
				if (!any)
					sb_puts (&b, "    outputs:\n"), any = true;
				sb_puts (&b, "      - ");
				ai_output (a, type_i, e);
				sb_puts (&b, "\n");
			}
		}

		// Plugs: 10 x {u8 count, u8 base index}, then the plug offset table.
		static ccp const plug_names[10] = { "generic", "plug_1", "child", "transition", "string", "int", "plug_6", "plug_7",
			"plug_8", "plug_9" };
		const size_t pinfo = body + 0x90, ptab = pinfo + 20;
		any = false;
		for (uint pt = 0; pt < 10; pt++)
		{
			const uint pcnt = d[pinfo + 2 * pt], pbase = d[pinfo + 2 * pt + 1];
			for (uint k = 0; k < pcnt; k++)
			{
				const size_t po = ai_u32 (a, ptab + 4 * ((size_t)pbase + k));
				if (!po || po + 8 > size)
					continue;
				if (!any)
					sb_puts (&b, "    plugs:\n"), any = true;
				sb_printf (&b, "      - {kind: %s, node: %d", plug_names[pt], (int)ai_u32 (a, po));
				if (pt != 3)
				{
					sb_puts (&b, ", name: ");
					ai_str (a, ai_u32 (a, po + 4));
				}
				sb_puts (&b, "}\n");
			}
		}
	}

	if (actions && actions + 4 <= size)
	{
		const u32 n = ai_u32 (a, actions);
		if (n && n < 0x100000)
		{
			sb_puts (&b, "xlink_actions:\n");
			for (u32 i = 0; i < n && actions + 4 + 12 * (i + 1) <= size; i++)
			{
				const size_t e = actions + 4 + 12 * i;
				sb_printf (&b, "  - {node: %d, slot: ", (int)ai_u32 (a, e));
				ai_str (a, ai_u32 (a, e + 4));
				sb_puts (&b, ", action: ");
				ai_str (a, ai_u32 (a, e + 8));
				sb_puts (&b, "}\n");
			}
		}
	}

	if (modules && modules + 4 <= size)
	{
		const u32 n = ai_u32 (a, modules);
		if (n && n < 0x100000)
		{
			sb_puts (&b, "modules:\n");
			for (u32 i = 0; i < n && modules + 4 + 12 * (i + 1) <= size; i++)
			{
				const size_t e = modules + 4 + 12 * i;
				sb_puts (&b, "  - {path: ");
				ai_str (a, ai_u32 (a, e));
				sb_puts (&b, ", category: ");
				ai_str (a, ai_u32 (a, e + 4));
				sb_printf (&b, ", instances: %u}\n", ai_u32 (a, e + 8));
			}
		}
	}
	return sb_finish (&b, text, text_size);
}

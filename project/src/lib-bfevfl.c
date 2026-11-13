// SPDX-License-Identifier: GPL-2.0+
// Nintendo event flow binaries (BFEVFL) to YAML; see lib-bfevfl.h.

#include "lib-bfevfl.h"
#include "lib-sbuf.h"

typedef struct ef_t
{
	const u8 *d;
	size_t n;
	bool bad;
	uint version, ev_size;
	sbuf_t b;
} ef_t;

static u32 ef32 (ef_t *e, u64 o)
{
	if (o > e->n || e->n - o < 4)
	{
		e->bad = true;
		return 0;
	}
	const u8 *p = e->d + o;
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static u32 ef16 (ef_t *e, u64 o)
{
	if (o > e->n || e->n - o < 2)
	{
		e->bad = true;
		return 0;
	}
	return e->d[o] | e->d[o + 1] << 8;
}

static u64 ef64 (ef_t *e, u64 o)
{
	return ef32 (e, o) | (u64)ef32 (e, o + 4) << 32;
}

static void sb_putc (sbuf_t *b, char c)
{
	sb_write (b, &c, 1);
}

// A string reference: pointer to a pascal string, 0 = none.
static void ef_str (ef_t *e, u64 ptr, sbuf_t *out)
{
	if (!ptr || ptr > e->n || e->n - ptr < 3)
	{
		sb_puts (out, "\"\"");
		return;
	}
	const uint len = ef16 (e, ptr);
	if (ptr + 2 + len > e->n)
	{
		e->bad = true;
		sb_puts (out, "\"\"");
		return;
	}
	sb_putc (out, '"');
	for (uint i = 0; i < len; i++)
	{
		const u8 c = e->d[ptr + 2 + i];
		if (c == '"' || c == '\\')
			sb_printf (out, "\\%c", c);
		else if (c < 0x20)
			sb_printf (out, "\\x%02x", c);
		else
			sb_putc (out, (char)c);
	}
	sb_putc (out, '"');
}

static void ef_s (ef_t *e, u64 ptr)
{
	ef_str (e, ptr, &e->b);
}

static void ef_float (sbuf_t *b, u32 bits)
{
	float f;
	memcpy (&f, &bits, 4);
	if (f == (float)(s32)f && f > -1e9f && f < 1e9f)
		sb_printf (b, "%d.0", (s32)f);
	else
		sb_printf (b, "%.9g", f);
}

// BFRES style dictionary at DIC: key i (0 based) is the key of node i + 1.
static uint ef_dic_count (ef_t *e, u64 dic)
{
	if (!dic || dic > e->n || e->n - dic < 0x18 || memcmp (e->d + dic, "DIC ", 4))
		return 0;
	const uint n = ef32 (e, dic + 4);
	return dic + 8 + (u64)(n + 1) * 16 <= e->n ? n : 0;
}

static u64 ef_dic_key (ef_t *e, u64 dic, uint i)
{
	return ef64 (e, dic + 8 + (u64)(i + 1) * 16 + 8);
}

// event index -> name of that event (or a bare number)
static void ef_event_ref (ef_t *e, u64 events, uint n_events, uint idx)
{
	if (idx == 0xffff)
		sb_puts (&e->b, "null");
	else if (idx < n_events)
		ef_s (e, ef64 (e, events + (u64)idx * e->ev_size));
	else
		sb_printf (&e->b, "%u", idx);
}

static void ef_container (ef_t *e, u64 c, uint indent)
{
	char pad[32];
	snprintf (pad, sizeof (pad), "%*s", indent, "");
	if (!c || c > e->n || e->n - c < 0x10)
	{
		sb_puts (&e->b, "{}\n");
		return;
	}
	const uint n = ef16 (e, c + 2), dic = ef_dic_count (e, ef64 (e, c + 8));
	if (!n || n != dic || c + 0x10 + (u64)n * 8 > e->n)
	{
		sb_puts (&e->b, "{}\n");
		return;
	}
	sb_putc (&e->b, '\n');
	for (uint i = 0; i < n && !e->bad; i++)
	{
		sb_puts (&e->b, pad);
		ef_str (e, ef_dic_key (e, ef64 (e, c + 8), i), &e->b);
		sb_puts (&e->b, ": ");
		const u64 it = ef64 (e, c + 0x10 + (u64)i * 8);
		if (!it || it > e->n || e->n - it < 0x10)
		{
			sb_puts (&e->b, "null\n");
			continue;
		}
		const uint tag = e->d[it], cnt = ef16 (e, it + 2);
		const u64 pl = it + 0x10;
		switch (tag)
		{
		case 2: sb_printf (&e->b, "%d\n", (s32)ef32 (e, pl)); break;
		case 3: sb_puts (&e->b, ef32 (e, pl) ? "true\n" : "false\n"); break;
		case 4: ef_float (&e->b, ef32 (e, pl)); sb_putc (&e->b, '\n'); break;
		case 0:
		case 5:
		case 6:
			if (tag == 0)
				sb_puts (&e->b, "!argument ");
			ef_s (e, ef64 (e, pl));
			sb_putc (&e->b, '\n');
			break;
		case 7: case 8: case 9: case 10:
			sb_putc (&e->b, '[');
			for (uint k = 0; k < cnt && k < 65536 && !e->bad; k++)
			{
				if (k)
					sb_puts (&e->b, ", ");
				if (tag == 7)
					sb_printf (&e->b, "%d", (s32)ef32 (e, pl + 4 * (u64)k));
				else if (tag == 8)
					sb_puts (&e->b, ef32 (e, pl + 4 * (u64)k) ? "true" : "false");
				else if (tag == 9)
					ef_float (&e->b, ef32 (e, pl + 4 * (u64)k));
				else
					ef_s (e, ef64 (e, pl + 8 * (u64)k));
			}
			sb_puts (&e->b, "]\n");
			break;
		case 12: case 13:
			sb_printf (&e->b, "%s[", tag == 13 ? "!custom_key " : "!actor ");
			ef_s (e, ef64 (e, pl));
			sb_puts (&e->b, ", ");
			ef_s (e, ef64 (e, pl + 8));
			sb_puts (&e->b, "]\n");
			break;
		default:
			sb_printf (&e->b, "!unknown_%u null\n", tag);
		}
	}
}

bool IsBfevfl (const u8 *data, size_t size)
{
	if (!data || size < 0x48 || memcmp (data, "BFEVFL\0\0", 8))
		return false;
	const uint ver = data[8] | data[9] << 8;
	return (ver == 0x0300 || ver == 0x0400) && data[0x0c] == 0xff && data[0x0d] == 0xfe;
}

enumError ConvertBfevflToYaml (const u8 *data, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	*text_size = 0;
	if (!IsBfevfl (data, size))
		return ERR_INVALID_DATA;

	ef_t e = { .d = data, .n = size };
	e.version = data[8] | data[9] << 8;
	e.ev_size = e.version >= 0x0400 ? 0x30 : 0x28;
	sbuf_t *b = &e.b;

	sb_printf (b, "# Nintendo event flow (BFEVFL version 0x%04x)\n", e.version);
	// The header's name offset points at the characters (NUL terminated), not at a pascal string.
	{
		const u32 no = ef32 (&e, 0x10);
		sb_puts (b, "name: \"");
		for (u64 i = no; no && i < size && data[i]; i++)
			if (data[i] >= 0x20 && data[i] != '"' && data[i] != '\\')
				sb_putc (b, (char)data[i]);
		sb_puts (b, "\"\n");
	}

	const uint nfc = ef16 (&e, 0x20), ntl = ef16 (&e, 0x22);
	if (ntl)
		sb_puts (b, "timeline: present  # timelines are not decoded\n");
	if (nfc == 1)
	{
		const u64 fc = ef64 (&e, ef64 (&e, 0x28));
		if (fc > size || size - fc < 0x40 || memcmp (data + fc, "EVFL", 4))
			e.bad = true;
		else
		{
			const uint na = ef16 (&e, fc + 0x10), nev = ef16 (&e, fc + 0x16), nep = ef16 (&e, fc + 0x18);
			const u64 actors = ef64 (&e, fc + 0x28), events = ef64 (&e, fc + 0x30), epdic = ef64 (&e, fc + 0x38);
			sb_puts (b, "flowchart:\n  name: ");
			ef_s (&e, ef64 (&e, fc + 0x20));
			sb_putc (b, '\n');

			sb_puts (b, "  actors:\n");
			for (uint i = 0; i < na && !e.bad; i++)
			{
				const u64 a = actors + (u64)i * 0x38;
				sb_puts (b, "    - name: ");
				ef_s (&e, ef64 (&e, a));
				sb_puts (b, "\n      secondary_name: ");
				ef_s (&e, ef64 (&e, a + 8));
				sb_puts (b, "\n      argument_name: ");
				ef_s (&e, ef64 (&e, a + 0x10));
				sb_putc (b, '\n');
				const uint nac = ef16 (&e, a + 0x30), nq = ef16 (&e, a + 0x32), aep = ef16 (&e, a + 0x34);
				sb_printf (b, "      argument_entry_point: %d\n      concurrent_clips: %u\n", aep == 0xffff ? -1 : (int)aep,
					ef16 (&e, a + 0x36));
				const u64 params = ef64 (&e, a + 0x28);
				if (params)
				{
					sb_puts (b, "      params:");
					ef_container (&e, params, 8);
				}
				sb_puts (b, "      actions: [");
				for (uint k = 0; k < nac && !e.bad; k++)
				{
					if (k)
						sb_puts (b, ", ");
					ef_s (&e, ef64 (&e, ef64 (&e, a + 0x18) + 8 * (u64)k));
				}
				sb_puts (b, "]\n      queries: [");
				for (uint k = 0; k < nq && !e.bad; k++)
				{
					if (k)
						sb_puts (b, ", ");
					ef_s (&e, ef64 (&e, ef64 (&e, a + 0x20) + 8 * (u64)k));
				}
				sb_puts (b, "]\n");
			}

			sb_puts (b, "  events:\n");
			for (uint i = 0; i < nev && !e.bad; i++)
			{
				const u64 v = events + (u64)i * e.ev_size, d = v + 10;
				const uint type = data[v + 8 > size ? 0 : v + 8];
				static const char *const tn[] = { "action", "switch", "fork", "join", "sub_flow", "sub_flow_switch", "return" };
				sb_puts (b, "    - name: ");
				ef_s (&e, ef64 (&e, v));
				sb_printf (b, "\n      type: %s\n", type < 7 ? tn[type] : "unknown");
				if (type >= 7)
					sb_printf (b, "      type_id: %u\n", type);
				u64 params = 0;
				switch (type)
				{
				case 0:
					sb_puts (b, "      next: ");
					ef_event_ref (&e, events, nev, ef16 (&e, d));
					sb_printf (b, "\n      actor: %u\n      action: %u\n", ef16 (&e, d + 2), ef16 (&e, d + 4));
					params = ef64 (&e, d + 6);
					break;
				case 1:
				{
					const uint nc = ef16 (&e, d), act = ef16 (&e, d + 2), q = ef16 (&e, d + 4);
					sb_printf (b, "      actor: %u\n      query: %u\n", act, q);
					params = ef64 (&e, d + 6);
					const u64 cases = ef64 (&e, d + 14);
					sb_puts (b, "      cases:\n");
					for (uint k = 0; k < nc && !e.bad; k++)
					{
						sb_printf (b, "        %d: ", (s32)ef32 (&e, cases + 8 * (u64)k));
						ef_event_ref (&e, events, nev, ef16 (&e, cases + 8 * (u64)k + 4));
						sb_putc (b, '\n');
					}
					break;
				}
				case 2:
				{
					const uint nf = ef16 (&e, d);
					sb_puts (b, "      join: ");
					ef_event_ref (&e, events, nev, ef16 (&e, d + 2));
					sb_puts (b, "\n      forks: [");
					const u64 forks = ef64 (&e, d + 6);
					for (uint k = 0; k < nf && !e.bad; k++)
					{
						if (k)
							sb_puts (b, ", ");
						ef_event_ref (&e, events, nev, ef16 (&e, forks + 2 * (u64)k));
					}
					sb_puts (b, "]\n");
					break;
				}
				case 3:
					sb_puts (b, "      next: ");
					ef_event_ref (&e, events, nev, ef16 (&e, d));
					sb_putc (b, '\n');
					break;
				case 4:
					sb_puts (b, "      next: ");
					ef_event_ref (&e, events, nev, ef16 (&e, d));
					sb_puts (b, "\n      flowchart: ");
					ef_s (&e, ef64 (&e, d + 14));
					sb_puts (b, "\n      entry_point: ");
					ef_s (&e, ef64 (&e, d + 22));
					sb_putc (b, '\n');
					params = ef64 (&e, d + 6);
					break;
				case 5:
				{
					const uint nc = ef16 (&e, d);
					params = ef64 (&e, d + 6);
					const u64 cases = ef64 (&e, d + 14);
					sb_puts (b, "      flowchart: ");
					ef_s (&e, ef64 (&e, d + 22));
					sb_puts (b, "\n      entry_point: ");
					ef_s (&e, ef64 (&e, d + 30));
					sb_puts (b, "\n      cases:\n");
					for (uint k = 0; k < nc && !e.bad; k++)
					{
						sb_printf (b, "        %d: ", (s32)ef32 (&e, cases + 8 * (u64)k));
						ef_event_ref (&e, events, nev, ef16 (&e, cases + 8 * (u64)k + 4));
						sb_putc (b, '\n');
					}
					break;
				}
				case 6:
					sb_printf (b, "      value: %d\n", (s32)ef32 (&e, d + 2));
					break;
				default:
					break;
				}
				if (params)
				{
					sb_puts (b, "      params:");
					ef_container (&e, params, 8);
				}
			}

			sb_puts (b, "  entry_points:\n");
			const uint ndic = ef_dic_count (&e, epdic);
			for (uint i = 0; i < nep && i < ndic && !e.bad; i++)
			{
				const u64 p = ef64 (&e, fc + 0x40) + (u64)i * 0x20;
				sb_puts (b, "    - name: ");
				ef_s (&e, ef_dic_key (&e, epdic, i));
				sb_puts (b, "\n      main_event: ");
				ef_event_ref (&e, events, nev, ef16 (&e, p + 0x1c));
				sb_puts (b, "\n      sub_flow_events: [");
				const uint nsub = ef16 (&e, p + 0x18);
				for (uint k = 0; k < nsub && !e.bad; k++)
				{
					if (k)
						sb_puts (b, ", ");
					ef_event_ref (&e, events, nev, ef16 (&e, ef64 (&e, p) + 2 * (u64)k));
				}
				sb_puts (b, "]\n");
				const uint nvar = ef16 (&e, p + 0x1a), vdic = ef_dic_count (&e, ef64 (&e, p + 8));
				if (nvar && nvar == vdic)
				{
					sb_puts (b, "      variables:\n");
					for (uint k = 0; k < nvar && !e.bad; k++)
					{
						const u64 vd = ef64 (&e, p + 0x10) + (u64)k * 0x10;
						sb_puts (b, "        ");
						ef_str (&e, ef_dic_key (&e, ef64 (&e, p + 8), k), b);
						sb_puts (b, ": ");
						const uint ty = e.d[vd + 0x0a > size ? 0 : vd + 0x0a], cnt = ef16 (&e, vd + 8);
						if (ty == 2)
							sb_printf (b, "%d\n", (s32)ef32 (&e, vd));
						else if (ty == 3)
							sb_puts (b, ef32 (&e, vd) ? "true\n" : "false\n");
						else if (ty == 4)
						{
							ef_float (b, ef32 (&e, vd));
							sb_putc (b, '\n');
						}
						else
							sb_printf (b, "!type_%u [%u values]\n", ty, cnt);
					}
				}
			}
		}
	}
	if (e.bad && !e.b.n)
	{
		FREE (e.b.d);
		return ERR_INVALID_DATA;
	}
	if (e.bad)
		sb_puts (b, "# (truncated: malformed or out of range data)\n");
	return sb_finish (b, text, text_size);
}

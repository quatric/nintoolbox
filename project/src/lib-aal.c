// SPDX-License-Identifier: GPL-2.0+
// AAL audio data files (AMTA, AATN, AROC, AACL, AGST) and acoustic probe grids (DAPG) as text. See lib-aal.h.

#include "lib-aal.h"
#include "lib-byml.h"
#include "lib-sbuf.h"

// open_memstream() buffers come from the libc allocator
#undef free
#undef malloc

#ifdef __MINGW32__
// MinGW has no open_memstream(): spool to a temp file and slurp it on close.
static FILE *aal_memstream_open (char **buf, size_t *len)
{
	*buf = 0;
	*len = 0;
	return tmpfile ();
}
static void aal_memstream_close (FILE *f, char **buf, size_t *len)
{
	fflush (f);
	long n = ftell (f);
	char *b = n >= 0 ? malloc ((size_t)n + 1) : 0;
	if (b)
	{
		rewind (f);
		size_t got = fread (b, 1, (size_t)n, f);
		b[got] = 0;
		*len = got;
	}
	*buf = b;
	fclose (f);
}
#define open_memstream aal_memstream_open
#define AAL_MEMSTREAM_CLOSE(f, buf, len) aal_memstream_close (f, &(buf), &(len))
#else
#define AAL_MEMSTREAM_CLOSE(f, buf, len) fclose (f)
#endif

static u32 a_le32 (const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static u16 a_le16 (const u8 *p) { return (u16)(p[0] | p[1] << 8); }

static float a_lef32 (const u8 *p)
{
	const u32 v = a_le32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static bool aal_header (const u8 *d, size_t size, ccp magic, size_t min_size)
{
	return d && size >= min_size && !memcmp (d, magic, 4) && d[4] == 0xff && d[5] == 0xfe;
}

// Quoted string at absolute offset AT ("" if out of range).
static void aal_str (sbuf_t *b, const u8 *d, size_t size, size_t at)
{
	if (at >= size)
	{
		sb_puts (b, "\"\"");
		return;
	}
	sb_quote (b, (const char *)d + at, strnlen ((const char *)d + at, size - at));
}

// Quoted string whose offset is stored at FIELD, relative to FIELD.
static void aal_rel_str (sbuf_t *b, const u8 *d, size_t size, size_t field)
{
	aal_str (b, d, size, field + 4 <= size ? field + a_le32 (d + field) : size);
}

static void aal_float (sbuf_t *b, float f)
{
	if (f != f)
		sb_puts (b, ".nan");
	else if (f > 3.4e38f || f < -3.4e38f)
		sb_puts (b, f > 0 ? ".inf" : "-.inf");
	else
		sb_printf (b, "%.9g", f);
}

//-----------------------------------------------------------------------------
// AMTA 5.0

static void amta_minf (sbuf_t *b, const u8 *d, size_t end, size_t m)
{
	if (m + 0x2c > end || memcmp (d + m, "MINF", 4))
	{
		sb_printf (b, "music_info: {offset: 0x%zx, error: not a MINF block}\n", m);
		return;
	}
	const u32 msize = a_le32 (d + m + 8);
	sb_printf (b, "music_info:\n  version: \"%u.%u\"\n  size: %u\n  sample_rate: %u\n  tempo: ", d[m + 7], d[m + 6], msize,
		a_le32 (d + m + 0x14));
	aal_float (b, a_lef32 (d + m + 0x24));
	sb_printf (b, "\n  time_signature: [%u, %u]\n  values: [%u, %u, %u, %u, %u]\n", a_le16 (d + m + 0x28),
		a_le16 (d + m + 0x2a), a_le32 (d + m + 0x0c), a_le32 (d + m + 0x10), a_le32 (d + m + 0x18), a_le32 (d + m + 0x1c),
		a_le32 (d + m + 0x20));
	if (msize > 0x2c)
		sb_printf (b, "  # %u more bytes (bar and section tables) are not decoded\n", msize - 0x2c);
}

enumError ConvertAMTAToYaml (const u8 *d, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	*text_size = 0;
	if (!aal_header (d, size, "AMTA", 0x34) || d[7] != 5)
		return ERR_INVALID_DATA;
	size_t end = a_le32 (d + 8);
	if (end < 0x34 || end > size)
		end = size;

	sbuf_t b = { 0 };
	sb_printf (&b, "# AAL audio metadata (AMTA %u.%u)\nname: ", d[7], d[6]);
	aal_rel_str (&b, d, end, 0x24);
	sb_printf (&b, "\nname_crc32: 0x%08x\nflags: 0x%x\ntracks: %u\nchannels: %u\n", a_le32 (d + 0x28), a_le32 (d + 0x2c),
		d[0x30], d[0x31]);
	if (d[0x31])
	{
		sb_puts (&b, "channel_map: [");
		for (uint i = 0; i < d[0x31] && 0x33 + 2 * i < end; i++)
			sb_printf (&b, "%s[%u, %u]", i ? ", " : "", d[0x32 + 2 * i], d[0x33 + 2 * i]);
		sb_puts (&b, "]  # [channel, speaker]\n");
	}

	const size_t data = a_le32 (d + 0x10);
	if (data >= 0x34 && data + 4 <= end)
	{
		const u32 mask = a_le32 (d + data);
		size_t p = data + 4;
		static ccp const names[] = { "peak", "rms", "level_a_db", "level_b_db" };
		sb_printf (&b, "data_mask: 0x%x\n", mask);
		for (uint bit = 0; bit < 4; bit++)
			if (mask & (1u << bit) && p + 4 <= end)
			{
				sb_printf (&b, "%s: ", names[bit]);
				aal_float (&b, a_lef32 (d + p));
				sb_puts (&b, "\n");
				p += 4;
			}
		bool ok = !(mask & 0x10);
		if (ok && mask & 0x20 && p + 4 <= end)
		{
			const uint n = a_le16 (d + p);
			sb_printf (&b, "envelope_kind: %u\nenvelope:  # [sample, level]\n", a_le16 (d + p + 2));
			p += 4;
			for (uint i = 0; i < n && p + 8 <= end; i++, p += 8)
			{
				sb_printf (&b, "  - [%u, ", a_le32 (d + p + 4));
				aal_float (&b, a_lef32 (d + p));
				sb_puts (&b, "]\n");
			}
		}
		if (ok && mask & 0x40 && p + 4 <= end)
		{
			sb_puts (&b, "value_6: ");
			aal_float (&b, a_lef32 (d + p));
			sb_puts (&b, "\n");
		}
		if (!ok || mask & ~0x6fu)
			sb_printf (&b, "# data mask bits 0x%x are not decoded\n", mask & ~0x6fu);
	}

	const size_t mark = a_le32 (d + 0x14);
	if (mark && mark + 4 <= end)
	{
		const u32 n = a_le32 (d + mark);
		sb_puts (&b, "markers:\n");
		for (u32 i = 0; i < n && mark + 4 + 16 * (i + 1) <= end; i++)
		{
			const size_t e = mark + 4 + 16 * i;
			sb_printf (&b, "  - {id: %u, name: ", a_le32 (d + e));
			aal_rel_str (&b, d, end, e + 4);
			sb_printf (&b, ", start: %u, length: %u}\n", a_le32 (d + e + 8), a_le32 (d + e + 12));
		}
	}

	const size_t minf = a_le32 (d + 0x18);
	if (minf)
		amta_minf (&b, d, end, minf);

	const size_t tags = a_le32 (d + 0x1c);
	if (tags && tags + 4 <= end)
	{
		const u32 n = a_le32 (d + tags);
		sb_puts (&b, "tags: [");
		for (u32 i = 0; i < n && tags + 4 + 4 * (i + 1) <= end; i++)
		{
			sb_puts (&b, i ? ", " : "");
			aal_rel_str (&b, d, end, tags + 4 + 4 * i);
		}
		sb_puts (&b, "]\n");
	}

	const size_t prm = a_le32 (d + 0x20);
	if (prm && prm + 4 <= end)
	{
		const u32 n = a_le32 (d + prm);
		sb_puts (&b, "params:\n");
		for (u32 i = 0; i < n && prm + 4 + 8 * (i + 1) <= end; i++)
		{
			const size_t e = prm + 4 + 8 * i;
			sb_puts (&b, "  - {name: ");
			aal_rel_str (&b, d, end, e);
			sb_printf (&b, ", value: %u}\n", a_le32 (d + e + 4));
		}
	}
	return sb_finish (&b, text, text_size);
}

//-----------------------------------------------------------------------------
// AATN / AROC / AACL

enumError ConvertAATNToYaml (const u8 *d, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	*text_size = 0;
	if (!aal_header (d, size, "AATN", 0x44))
		return ERR_INVALID_DATA;
	const size_t strings = a_le32 (d + 8);
	if (strings < 0x44 || strings > size)
		return ERR_INVALID_DATA;

	static ccp const targets[] = { "volume", "env_fx", "filter", "spread", "priority" };
	static ccp const types[] = { "RollOff", "Custom", "UnitDistance" };
	sbuf_t b = { 0 };
	sb_printf (&b, "# AAL attenuator (AATN %u)\ncurves:  # RollOff = .baroc, Custom = .bactc, UnitDistance = .baudc\n",
		a_le16 (d + 6));
	for (uint i = 0; i < 5; i++)
	{
		const u32 type = a_le32 (d + 0x10 + 8 * i);
		sb_printf (&b, "  %s: {name: ", targets[i]);
		aal_str (&b, d, size, strings + a_le32 (d + 0x0c + 8 * i));
		if (type < 3)
			sb_printf (&b, ", type: %s}\n", types[type]);
		else
			sb_printf (&b, ", type: %u}\n", type);
	}
	sb_puts (&b, "directivity: ");
	aal_str (&b, d, size, strings + a_le32 (d + 0x34));
	sb_puts (&b, "\nculling: ");
	aal_str (&b, d, size, strings + a_le32 (d + 0x38));
	sb_printf (&b, "\nlistener: %s\nocclusion: %s\n", a_le32 (d + 0x3c) ? "true" : "false", a_le32 (d + 0x40) ? "true" : "false");
	return sb_finish (&b, text, text_size);
}

enumError ConvertAROCToYaml (const u8 *d, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	*text_size = 0;
	if (!aal_header (d, size, "AROC", 0x20))
		return ERR_INVALID_DATA;
	const uint version = a_le16 (d + 6);
	sbuf_t b = { 0 };
	sb_printf (&b, "# AAL roll-off curve (AROC %u)\nmodel: %u\nref_distance: ", version, a_le32 (d + 8));
	aal_float (&b, a_lef32 (d + 0x0c));
	sb_puts (&b, "\nmax_distance: ");
	aal_float (&b, a_lef32 (d + 0x10));
	sb_puts (&b, "\nrolloff_factor: ");
	aal_float (&b, a_lef32 (d + 0x14));
	sb_puts (&b, "\nstart_value: ");
	aal_float (&b, a_lef32 (d + 0x18));
	sb_printf (&b, "\nincrease_mode: %s\n", a_le32 (d + 0x1c) ? "true" : "false");
	if (version >= 2 && size >= 0x24)
	{
		sb_puts (&b, "culling_start_distance: ");
		aal_float (&b, a_lef32 (d + 0x20));
		sb_puts (&b, "\n");
	}
	return sb_finish (&b, text, text_size);
}

enumError ConvertAACLToYaml (const u8 *d, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	*text_size = 0;
	if (!aal_header (d, size, "AACL", 0x10))
		return ERR_INVALID_DATA;
	const uint version = a_le16 (d + 6);
	sbuf_t b = { 0 };
	sb_printf (&b, "# AAL attenuation culling (AACL %u)\nculling_distance: ", version);
	aal_float (&b, a_lef32 (d + 8));
	sb_puts (&b, "\nculling_margin: ");
	aal_float (&b, a_lef32 (d + 0x0c));
	sb_puts (&b, "\n");
	if (version >= 2 && size >= 0x11)
		sb_printf (&b, "priority_down: %s\n", d[0x10] ? "true" : "false");
	return sb_finish (&b, text, text_size);
}

//-----------------------------------------------------------------------------
// AADR

enumError ConvertAADRToYaml (const u8 *d, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	*text_size = 0;
	if (!aal_header (d, size, "AADR", 0x18))
		return ERR_INVALID_DATA;
	sbuf_t b = { 0 };
	sb_printf (&b, "# AAL attenuation directivity (AADR %u)\ninner_angle: ", a_le16 (d + 6));
	aal_float (&b, a_lef32 (d + 8));
	sb_puts (&b, "\nouter_angle: ");
	aal_float (&b, a_lef32 (d + 0x0c));
	sb_puts (&b, "\nouter_volume: ");
	aal_float (&b, a_lef32 (d + 0x10));
	sb_puts (&b, "\nunknown_14: ");
	aal_float (&b, a_lef32 (d + 0x14));
	sb_puts (&b, "\n");
	return sb_finish (&b, text, text_size);
}

//-----------------------------------------------------------------------------
// AGST

typedef struct agst_t
{
	const u8 *d;
	size_t size, strings, grp;
	u32 n_groups;
} agst_t;

static void agst_group_name (sbuf_t *b, const agst_t *a, u32 index)
{
	if (a->grp && index < a->n_groups && a->grp + 0x10 + 4 * index <= a->size)
	{
		const size_t g = a_le32 (a->d + a->grp + 0x0c + 4 * index);
		if (g + 4 <= a->size && a->strings)
		{
			aal_str (b, a->d, a->size, a->strings + a_le32 (a->d + g));
			return;
		}
	}
	sb_printf (b, "%u", index);
}

static void agst_node (sbuf_t *b, const agst_t *a, size_t node, size_t limit, uint depth)
{
	for (uint i = 0; i < depth; i++)
		sb_puts (b, "  ");
	if (depth > 32 || node + 12 > limit)
	{
		sb_puts (b, "- {error: bad node}\n");
		return;
	}
	const u32 nsize = a_le32 (a->d + node), n = a_le32 (a->d + node + 8);
	sb_puts (b, "- group: ");
	agst_group_name (b, a, a_le32 (a->d + node + 4));
	sb_puts (b, "\n");
	if (!n)
		return;
	for (uint i = 0; i < depth + 1; i++)
		sb_puts (b, "  ");
	sb_puts (b, "children:\n");
	const size_t node_end = nsize >= 12 && node + nsize <= limit ? node + nsize : limit;
	size_t c = node + 12;
	for (u32 i = 0; i < n && c + 12 <= node_end; i++)
	{
		const u32 csize = a_le32 (a->d + c);
		agst_node (b, a, c, node_end, depth + 1);
		if (csize < 12)
			break;
		c += csize;
	}
}

// Decodes a BYML document to YAML and appends it indented by two spaces.
static void agst_byml (sbuf_t *b, const u8 *data, size_t size)
{
	char *buf = 0;
	size_t len = 0;
	FILE *f = open_memstream (&buf, &len);
	if (!f)
		return;
	const enumError err = DecodeBYML_YAML (f, data, size);
	AAL_MEMSTREAM_CLOSE (f, buf, len);
	if (err || !buf)
		sb_puts (b, "  # BYML could not be decoded\n");
	else
	{
		size_t i = 0;
		if (len >= 4 && !memcmp (buf, "---\n", 4))
			i = 4;
		while (i < len)
		{
			const char *nl = memchr (buf + i, '\n', len - i);
			const size_t ll = nl ? (size_t)(nl - (buf + i)) : len - i;
			if (ll)
			{
				sb_puts (b, "  ");
				sb_write (b, buf + i, ll);
			}
			sb_puts (b, "\n");
			i += ll + 1;
		}
	}
	free (buf);
}

enumError ConvertAGSTToYaml (const u8 *d, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	*text_size = 0;
	if (!aal_header (d, size, "AGST", 0x18))
		return ERR_INVALID_DATA;

	// Section offsets follow the header up to the first section.
	size_t sec[16];
	uint n_sec = 0;
	size_t first = size;
	for (size_t p = 0x0c; p + 4 <= first && n_sec < 16; p += 4)
	{
		const size_t o = a_le32 (d + p);
		if (o < 0x10 || o + 8 > size)
			break;
		sec[n_sec++] = o;
		if (o < first)
			first = o;
	}
	agst_t a = { d, size, 0, 0, 0 };
	for (uint i = 0; i < n_sec; i++)
	{
		if (!memcmp (d + sec[i], "STRG", 4))
			a.strings = sec[i] + 8;
		else if (!memcmp (d + sec[i], "GRP ", 4) && sec[i] + 12 <= size)
		{
			a.grp = sec[i];
			a.n_groups = a_le32 (d + sec[i] + 8);
			if (a.n_groups > (size - sec[i] - 12) / 4)
				a.n_groups = (u32)((size - sec[i] - 12) / 4);
		}
	}

	sbuf_t b = { 0 };
	sb_printf (&b, "# AAL group settings (AGST %u)\n", a_le16 (d + 6));
	for (uint i = 0; i < n_sec; i++)
	{
		const size_t s = sec[i];
		size_t end = s + a_le32 (d + s + 4);
		if (end > size || end < s + 8)
			end = size;
		char magic[5];
		for (uint k = 0; k < 4; k++)
			magic[k] = d[s + k] >= 0x20 && d[s + k] < 0x7f ? (char)d[s + k] : '?';
		magic[4] = 0;
		while (magic[0] && magic[strlen (magic) - 1] == ' ')
			magic[strlen (magic) - 1] = 0;

		if (!strcmp (magic, "TREE") && s + 0x18 <= end)
		{
			sb_puts (&b, "tree:\n  default_group: ");
			aal_str (&b, d, size, a.strings ? a.strings + a_le32 (d + s + 8) : size);
			sb_puts (&b, "\n  root:\n");
			agst_node (&b, &a, s + 0x0c, end, 1);
		}
		else if (!strcmp (magic, "GRP"))
		{
			sb_puts (&b, "groups:\n");
			for (u32 g = 0; g < a.n_groups; g++)
			{
				const size_t at = a_le32 (d + s + 0x0c + 4 * g);
				size_t g_end = g + 1 < a.n_groups ? a_le32 (d + s + 0x10 + 4 * g) : end;
				if (at + 4 > end || g_end > end || g_end < at)
					break;
				sb_puts (&b, "  - name: ");
				agst_group_name (&b, &a, g);
				sb_puts (&b, "\n    words: [");
				for (size_t p = at + 4; p + 4 <= g_end; p += 4)
					sb_printf (&b, "%s0x%08x", p > at + 4 ? ", " : "", a_le32 (d + p));
				sb_puts (&b, "]\n");
			}
		}
		else if (!strcmp (magic, "STRG"))
		{
			sb_puts (&b, "strings:\n");
			for (size_t p = s + 8; p < end;)
			{
				const size_t len = strnlen ((const char *)d + p, end - p);
				if (len)
				{
					sb_puts (&b, "  - ");
					sb_quote (&b, (const char *)d + p, len);
					sb_puts (&b, "\n");
				}
				p += len + 1;
			}
		}
		else if (end - s >= 0x18 && (!memcmp (d + s + 8, "BY", 2) || !memcmp (d + s + 8, "YB", 2)))
		{
			sb_printf (&b, "%s:  # BYML\n", magic);
			agst_byml (&b, d + s + 8, end - s - 8);
		}
		else
			sb_printf (&b, "%s: {offset: 0x%zx, size: %zu}\n", magic, s, end - s);
	}
	return sb_finish (&b, text, text_size);
}

//-----------------------------------------------------------------------------
// DAPG

enumError ConvertDAPGToCsv (const u8 *d, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	*text_size = 0;
	if (!d || size < 8)
		return ERR_INVALID_DATA;
	const u32 n = a_le32 (d);
	if (!n || n > 1024)
		return ERR_INVALID_DATA;
	const size_t cells = (size_t)n * n, rec_base = 4 + 2 * cells;
	if (rec_base > size || (size - rec_base) % 0x7c)
		return ERR_INVALID_DATA;
	const size_t n_rec = (size - rec_base) / 0x7c;

	sbuf_t b = { 0 };
	sb_puts (&b, "x,z,probe,f0,f1,u0,u1,f2,f3,f4,f5");
	for (uint r = 0; r < 12; r++)
		sb_printf (&b, ",ray%u,tag%u", r, r);
	sb_puts (&b, "\n");
	for (size_t i = 0; i < cells && !b.fail; i++)
	{
		const uint idx = a_le16 (d + 4 + 2 * i);
		sb_printf (&b, "%zu,%zu,%u", i % n, i / n, idx);
		if (idx && idx <= n_rec)
		{
			const u8 *r = d + rec_base + (size_t)(idx - 1) * 0x7c;
			sb_printf (&b, ",%.9g,%.9g,%u,%u", a_lef32 (r), a_lef32 (r + 4), a_le16 (r + 8), a_le16 (r + 10));
			for (uint k = 0; k < 4; k++)
				sb_printf (&b, ",%.9g", a_lef32 (r + 12 + 4 * k));
			for (uint k = 0; k < 12; k++)
				sb_printf (&b, ",%.9g,0x%08x", a_lef32 (r + 0x1c + 8 * k), a_le32 (r + 0x20 + 8 * k));
		}
		sb_puts (&b, "\n");
	}
	return sb_finish (&b, text, text_size);
}

//-----------------------------------------------------------------------------
// DLPG

enumError ConvertDLPGToCsv (const u8 *d, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	*text_size = 0;
	if (!d || size < 8 + 656 || memcmp (d, "DLPG", 4) || (size - 8) % 656)
		return ERR_INVALID_DATA;
	const size_t n_col = (size - 8) / 656;
	sbuf_t b = { 0 };
	sb_puts (&b, "column,probe,x,y,z");
	for (uint i = 0; i < 24; i++)
		sb_printf (&b, ",c%u", i);
	sb_puts (&b, ",a0,a1,a2,a3,b0,b1,b2,b3,u,packed,column_base_y,column_spacing\n");
	for (size_t c = 0; c < n_col && !b.fail; c++)
	{
		const u8 *col = d + 8 + 656 * c, *info = col + 640;
		for (uint k = 0; k < 4; k++)
		{
			const u8 *r = col + 160 * k;
			sb_printf (&b, "%zu,%u,%.9g,%.9g,%.9g", c, k, a_lef32 (r + 128), a_lef32 (r + 132), a_lef32 (r + 136));
			for (uint i = 0; i < 32; i++)
				sb_printf (&b, ",%.9g", a_lef32 (r + 4 * i));
			sb_printf (&b, ",%u,", a_le32 (r + 140));
			for (uint i = 0; i < 16; i++)
				sb_printf (&b, "%02x", r[144 + i]);
			sb_printf (&b, ",%.9g,%.9g\n", a_lef32 (info), a_lef32 (info + 4));
		}
	}
	return sb_finish (&b, text, text_size);
}

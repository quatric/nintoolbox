// SPDX-License-Identifier: GPL-2.0+
// Paper Mario (Switch) lighting data: probe headers and render params.
//
// Native C port of KillzXGaming/Paper-Mario-Tools' LightConverter:
//   ProbeHeader.cs <-> ScanPMProbe/EncodePMProbe (+ JSON)
//   RenderSettings/RenderParams.cs <-> ScanPMRender/EncodePMRender (+ JSON)
//   RenderSettings/Hashing.cs + crc32.cs <-> pm_hash_strings table + zlib CRC32
//
// Reference behavior notes (verified against the C# sources, not guessed):
// - ProbeHeader binary is little-endian; first u32 is 100, second is the
//   axis-parameter count N; total size is exactly 156 + 4*N.
// - RenderParams .data starts with a UTF-8 BOM, then LF lines: section
//   count, "hash:size" table, then per-section bodies whose first line is
//   the section hash and whose remaining lines are "phash:pvalue". Lines
//   without exactly one ':' are skipped on load (including that hash line).
// - Scalar floats are "0x"-prefixed hex bit patterns ("0x0" for zero);
//   vectors are comma separated %.6f-style decimals; integers are decimal;
//   anything else is a raw string. Value dispatch order is hex, comma,
//   decimal-int, string.
// - Name hashing is CRC32-IEEE; ToHash() prefers a plain hex parse and
//   falls back to CRC32. Reverse lookup is first-hash-wins over the
//   upstream hash_strings.txt table; unknown hashes print as unpadded
//   uppercase hex ("X" format).
// - .NET Core hex parsing tolerates an optional 0x/0X prefix and
//   surrounding whitespace; replicated here for keys and hex floats.

#include "lib-papermario.h"
#include "lib-std.h"
#include "dclib-debug.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <zlib.h>

//-----------------------------------------------------------------------------
// little-endian scalar helpers
//-----------------------------------------------------------------------------

static float pm_rd_float (const u8 *p)
{
	u32 u = le32 (p);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

static void pm_wr32 (u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

static void pm_wr_float (u8 *p, float f)
{
	u32 u;
	memcpy (&u, &f, 4);
	pm_wr32 (p, u);
}

//-----------------------------------------------------------------------------
// string builder
//-----------------------------------------------------------------------------

typedef struct pm_sb_t
{
	char *buf;
	size_t len;
	size_t cap;
} pm_sb_t;

static void pm_sb_init (pm_sb_t *sb)
{
	sb->cap = 1024;
	sb->len = 0;
	sb->buf = MALLOC (sb->cap);
	if (sb->buf)
		sb->buf[0] = '\0';
}

static void pm_sb_free (pm_sb_t *sb)
{
	FREE (sb->buf);
	sb->buf = 0;
	sb->len = sb->cap = 0;
}

static bool pm_sb_reserve (pm_sb_t *sb, size_t extra)
{
	if (!sb->buf)
		return false;
	if (sb->len + extra + 1 > sb->cap)
	{
		size_t ncap = sb->cap * 2 + extra + 64;
		char *nb = REALLOC (sb->buf, ncap);
		if (!nb)
			return false;
		sb->buf = nb;
		sb->cap = ncap;
	}
	return true;
}

static void pm_sb_putc (pm_sb_t *sb, char c)
{
	if (!pm_sb_reserve (sb, 1))
		return;
	sb->buf[sb->len++] = c;
	sb->buf[sb->len] = '\0';
}

static void pm_sb_putn (pm_sb_t *sb, const char *s, size_t n)
{
	if (!s || !n)
		return;
	if (!pm_sb_reserve (sb, n))
		return;
	memcpy (sb->buf + sb->len, s, n);
	sb->len += n;
	sb->buf[sb->len] = '\0';
}

static void pm_sb_puts (pm_sb_t *sb, const char *s)
{
	if (s)
		pm_sb_putn (sb, s, strlen (s));
}

static void pm_sb_printf (pm_sb_t *sb, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	char tmp[128];
	vsnprintf (tmp, sizeof (tmp), fmt, ap);
	va_end (ap);
	pm_sb_puts (sb, tmp);
}

//-----------------------------------------------------------------------------
// minimal JSON DOM + parser (shared by both formats)
//-----------------------------------------------------------------------------

typedef enum pj_type_t
{
	PJ_NULL = 0,
	PJ_BOOL,
	PJ_NUM,
	PJ_STR,
	PJ_ARR,
	PJ_OBJ,
} pj_type_t;

typedef struct pj_node_t
{
	pj_type_t type;
	char *str; // PJ_STR (unescaped UTF-8)
	int is_int; // PJ_NUM: integral token
	int64_t ival; // PJ_NUM integral value
	double dval; // PJ_NUM numeric value
	int boolean; // PJ_BOOL
	struct pj_node_t **items; // PJ_ARR
	size_t n_items;
	char **keys; // PJ_OBJ
	struct pj_node_t **vals;
	size_t n_pairs;
} pj_node_t;

static void pj_free (pj_node_t *n)
{
	if (!n)
		return;
	size_t i;
	FREE (n->str);
	for (i = 0; i < n->n_items; i++)
		pj_free (n->items[i]);
	FREE (n->items);
	for (i = 0; i < n->n_pairs; i++)
	{
		FREE (n->keys[i]);
		pj_free (n->vals[i]);
	}
	FREE (n->keys);
	FREE (n->vals);
	FREE (n);
}

typedef struct pj_parser_t
{
	const char *p;
	const char *end;
} pj_parser_t;

static void pj_skip_ws (pj_parser_t *j)
{
	while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r'))
		j->p++;
}

static pj_node_t *pj_new (pj_type_t type)
{
	pj_node_t *n = MALLOC (sizeof (*n));
	if (n)
		memset (n, 0, sizeof (*n));
	if (n)
		n->type = type;
	return n;
}

static void pj_utf8_put (pm_sb_t *sb, unsigned int cp)
{
	char tmp[4];
	int n = 0;
	if (cp < 0x80)
	{
		tmp[n++] = (char)cp;
	}
	else if (cp < 0x800)
	{
		tmp[n++] = (char)(0xc0 | (cp >> 6));
		tmp[n++] = (char)(0x80 | (cp & 0x3f));
	}
	else if (cp < 0x10000)
	{
		tmp[n++] = (char)(0xe0 | (cp >> 12));
		tmp[n++] = (char)(0x80 | ((cp >> 6) & 0x3f));
		tmp[n++] = (char)(0x80 | (cp & 0x3f));
	}
	else
	{
		tmp[n++] = (char)(0xf0 | (cp >> 18));
		tmp[n++] = (char)(0x80 | ((cp >> 12) & 0x3f));
		tmp[n++] = (char)(0x80 | ((cp >> 6) & 0x3f));
		tmp[n++] = (char)(0x80 | (cp & 0x3f));
	}
	pm_sb_putn (sb, tmp, (size_t)n);
}

static int pj_hexval (char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static pj_node_t *pj_parse_value (pj_parser_t *j);

static pj_node_t *pj_parse_string_node (pj_parser_t *j)
{
	// j->p points at the opening quote
	j->p++;
	pm_sb_t sb;
	pm_sb_init (&sb);
	if (!sb.buf)
		return 0;
	while (j->p < j->end)
	{
		char c = *j->p++;
		if (c == '"')
		{
			pj_node_t *n = pj_new (PJ_STR);
			if (!n)
			{
				pm_sb_free (&sb);
				return 0;
			}
			n->str = sb.buf; // adopt
			return n;
		}
		if (c == '\\')
		{
			if (j->p >= j->end)
				break;
			char e = *j->p++;
			switch (e)
			{
				case '"':
					pm_sb_putc (&sb, '"');
					break;
				case '\\':
					pm_sb_putc (&sb, '\\');
					break;
				case '/':
					pm_sb_putc (&sb, '/');
					break;
				case 'b':
					pm_sb_putc (&sb, '\b');
					break;
				case 'f':
					pm_sb_putc (&sb, '\f');
					break;
				case 'n':
					pm_sb_putc (&sb, '\n');
					break;
				case 'r':
					pm_sb_putc (&sb, '\r');
					break;
				case 't':
					pm_sb_putc (&sb, '\t');
					break;
				case 'u':
				{
					if (j->p + 4 > j->end)
					{
						pm_sb_free (&sb);
						return 0;
					}
					unsigned int cp = 0;
					for (int k = 0; k < 4; k++)
					{
						int hv = pj_hexval (j->p[k]);
						if (hv < 0)
						{
							pm_sb_free (&sb);
							return 0;
						}
						cp = cp * 16 + (unsigned int)hv;
					}
					j->p += 4;
					if (cp >= 0xd800 && cp <= 0xdbff && j->p + 6 <= j->end && j->p[0] == '\\'
						&& j->p[1] == 'u')
					{
						unsigned int lo = 0;
						bool ok = true;
						for (int k = 0; k < 4; k++)
						{
							int hv = pj_hexval (j->p[2 + k]);
							if (hv < 0)
								ok = false;
							else
								lo = lo * 16 + (unsigned int)hv;
						}
						if (ok && lo >= 0xdc00 && lo <= 0xdfff)
						{
							j->p += 6;
							cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
						}
					}
					pj_utf8_put (&sb, cp);
					break;
				}
				default:
					pm_sb_free (&sb);
					return 0;
			}
		}
		else if ((unsigned char)c < 0x20)
		{
			pm_sb_free (&sb);
			return 0;
		}
		else
			pm_sb_putc (&sb, c);
	}
	pm_sb_free (&sb);
	return 0;
}

static pj_node_t *pj_parse_number (pj_parser_t *j)
{
	const char *start = j->p;
	if (j->p < j->end && (*j->p == '-' || *j->p == '+'))
		j->p++;
	bool has_digits = false;
	while (j->p < j->end && isdigit ((unsigned char)*j->p))
	{
		j->p++;
		has_digits = true;
	}
	bool is_int = has_digits;
	if (j->p < j->end && *j->p == '.')
	{
		is_int = false;
		j->p++;
		while (j->p < j->end && isdigit ((unsigned char)*j->p))
			j->p++;
	}
	if (j->p < j->end && (*j->p == 'e' || *j->p == 'E'))
	{
		is_int = false;
		j->p++;
		if (j->p < j->end && (*j->p == '-' || *j->p == '+'))
			j->p++;
		if (j->p >= j->end || !isdigit ((unsigned char)*j->p))
			return 0;
		while (j->p < j->end && isdigit ((unsigned char)*j->p))
			j->p++;
	}
	if (!has_digits || j->p == start)
		return 0;
	// Reject NaN/Infinity spellings here; pj_parse_value handles them.
	pj_node_t *n = pj_new (PJ_NUM);
	if (!n)
		return 0;
	char *tmp = MALLOC ((size_t)(j->p - start) + 1);
	if (!tmp)
	{
		pj_free (n);
		return 0;
	}
	memcpy (tmp, start, (size_t)(j->p - start));
	tmp[j->p - start] = '\0';
	n->dval = strtod (tmp, 0);
	n->is_int = false;
	n->ival = 0;
	if (is_int)
	{
		char *e = 0;
		long long v = strtoll (tmp, &e, 10);
		if (e && !*e)
		{
			n->is_int = true;
			n->ival = (int64_t)v;
		}
	}
	FREE (tmp);
	return n;
}

static bool pj_lit (pj_parser_t *j, const char *lit)
{
	size_t n = strlen (lit);
	if ((size_t)(j->end - j->p) < n || memcmp (j->p, lit, n))
		return false;
	j->p += n;
	return true;
}

static pj_node_t *pj_parse_array (pj_parser_t *j)
{
	j->p++; // [
	pj_node_t *n = pj_new (PJ_ARR);
	if (!n)
		return 0;
	pj_skip_ws (j);
	if (j->p < j->end && *j->p == ']')
	{
		j->p++;
		return n;
	}
	for (;;)
	{
		pj_skip_ws (j);
		pj_node_t *it = pj_parse_value (j);
		if (!it)
		{
			pj_free (n);
			return 0;
		}
		pj_node_t **ni = REALLOC (n->items, (n->n_items + 1) * sizeof (*ni));
		if (!ni)
		{
			pj_free (it);
			pj_free (n);
			return 0;
		}
		n->items = ni;
		n->items[n->n_items++] = it;
		pj_skip_ws (j);
		if (j->p >= j->end)
		{
			pj_free (n);
			return 0;
		}
		if (*j->p == ',')
		{
			j->p++;
			continue;
		}
		if (*j->p == ']')
		{
			j->p++;
			return n;
		}
		pj_free (n);
		return 0;
	}
}

static pj_node_t *pj_parse_object (pj_parser_t *j)
{
	j->p++; // {
	pj_node_t *n = pj_new (PJ_OBJ);
	if (!n)
		return 0;
	pj_skip_ws (j);
	if (j->p < j->end && *j->p == '}')
	{
		j->p++;
		return n;
	}
	for (;;)
	{
		pj_skip_ws (j);
		if (j->p >= j->end || *j->p != '"')
		{
			pj_free (n);
			return 0;
		}
		pj_node_t *ks = pj_parse_string_node (j);
		if (!ks)
		{
			pj_free (n);
			return 0;
		}
		pj_skip_ws (j);
		if (j->p >= j->end || *j->p != ':')
		{
			pj_free (ks);
			pj_free (n);
			return 0;
		}
		j->p++;
		pj_node_t *v = pj_parse_value (j);
		if (!v)
		{
			pj_free (ks);
			pj_free (n);
			return 0;
		}
		char **nk = REALLOC (n->keys, (n->n_pairs + 1) * sizeof (*nk));
		pj_node_t **nv = REALLOC (n->vals, (n->n_pairs + 1) * sizeof (*nv));
		if (!nk || !nv)
		{
			FREE (nk);
			FREE (nv);
			pj_free (ks);
			pj_free (v);
			pj_free (n);
			return 0;
		}
		n->keys = nk;
		n->vals = nv;
		n->keys[n->n_pairs] = ks->str;
		ks->str = 0; // adopt
		n->vals[n->n_pairs] = v;
		n->n_pairs++;
		pj_free (ks);
		pj_skip_ws (j);
		if (j->p >= j->end)
		{
			pj_free (n);
			return 0;
		}
		if (*j->p == ',')
		{
			j->p++;
			continue;
		}
		if (*j->p == '}')
		{
			j->p++;
			return n;
		}
		pj_free (n);
		return 0;
	}
}

static pj_node_t *pj_parse_value (pj_parser_t *j)
{
	pj_skip_ws (j);
	if (j->p >= j->end)
		return 0;
	switch (*j->p)
	{
		case '{':
			return pj_parse_object (j);
		case '[':
			return pj_parse_array (j);
		case '"':
			return pj_parse_string_node (j);
		case 't':
		{
			pj_node_t *n;
			if (!pj_lit (j, "true"))
				return 0;
			n = pj_new (PJ_BOOL);
			if (n)
				n->boolean = 1;
			return n;
		}
		case 'f':
		{
			pj_node_t *n;
			if (!pj_lit (j, "false"))
				return 0;
			n = pj_new (PJ_BOOL);
			return n;
		}
		case 'n':
			if (!pj_lit (j, "null"))
				return 0;
			return pj_new (PJ_NULL);
		case 'N':
			if (pj_lit (j, "NaN"))
			{
				pj_node_t *n = pj_new (PJ_NUM);
				if (n)
					n->dval = strtod ("NAN", 0);
				return n;
			}
			return 0;
		case 'I':
			if (pj_lit (j, "Infinity"))
			{
				pj_node_t *n = pj_new (PJ_NUM);
				if (n)
					n->dval = strtod ("INF", 0);
				return n;
			}
			return 0;
		default:
			if (*j->p == '-' || *j->p == '+' || isdigit ((unsigned char)*j->p))
				return pj_parse_number (j);
			if (!strncmp (j->p, "-Infinity", 9))
			{
				j->p += 9;
				pj_node_t *n = pj_new (PJ_NUM);
				if (n)
					n->dval = strtod ("-INF", 0);
				return n;
			}
			return 0;
	}
}

static pj_node_t *pj_parse (const char *text, size_t len)
{
	pj_parser_t j;
	j.p = text;
	j.end = text + len;
	pj_node_t *n = pj_parse_value (&j);
	if (!n)
		return 0;
	pj_skip_ws (&j);
	if (j.p != j.end)
	{
		pj_free (n);
		return 0;
	}
	return n;
}

static const pj_node_t *pj_get (const pj_node_t *obj, const char *key)
{
	size_t i;
	if (!obj || obj->type != PJ_OBJ)
		return 0;
	for (i = 0; i < obj->n_pairs; i++)
		if (!strcmp (obj->keys[i], key))
			return obj->vals[i];
	return 0;
}

// number -> double (ints coerce); false when not numeric
static bool pj_as_double (const pj_node_t *n, double *out)
{
	if (!n || n->type != PJ_NUM)
		return false;
	*out = n->is_int ? (double)n->ival : n->dval;
	return true;
}

// number -> u32 (integral doubles coerce when exactly integral)
static bool pj_as_u32 (const pj_node_t *n, u32 *out)
{
	double d;
	if (!pj_as_double (n, &d))
		return false;
	if (d < 0 || d > 4294967295.0 || d != floor (d))
		return false;
	*out = (u32)d;
	return true;
}

//-----------------------------------------------------------------------------
// JSON writer helpers
//-----------------------------------------------------------------------------

static void pm_json_escape (pm_sb_t *sb, const char *s)
{
	pm_sb_putc (sb, '"');
	for (const unsigned char *p = (const unsigned char *)s; *p; p++)
	{
		switch (*p)
		{
			case '"':
				pm_sb_puts (sb, "\\\"");
				break;
			case '\\':
				pm_sb_puts (sb, "\\\\");
				break;
			case '\b':
				pm_sb_puts (sb, "\\b");
				break;
			case '\f':
				pm_sb_puts (sb, "\\f");
				break;
			case '\n':
				pm_sb_puts (sb, "\\n");
				break;
			case '\r':
				pm_sb_puts (sb, "\\r");
				break;
			case '\t':
				pm_sb_puts (sb, "\\t");
				break;
			default:
				if (*p < 0x20)
					pm_sb_printf (sb, "\\u%04x", *p);
				else
					pm_sb_putc (sb, (char)*p);
				break;
		}
	}
	pm_sb_putc (sb, '"');
}

// float with round-trip precision; whole numbers keep ".0" (Newtonsoft
// shape) so they still parse back as floats, not ints.
static void pm_json_float (pm_sb_t *sb, double v)
{
	if (isnan (v))
	{
		pm_sb_puts (sb, "NaN");
		return;
	}
	if (isinf (v))
	{
		pm_sb_puts (sb, v < 0 ? "-Infinity" : "Infinity");
		return;
	}
	char tmp[32];
	snprintf (tmp, sizeof (tmp), "%.9g", v);
	if (!strchr (tmp, '.') && !strchr (tmp, 'e') && !strchr (tmp, 'E'))
	{
		size_t n = strlen (tmp);
		if (n + 2 < sizeof (tmp))
		{
			tmp[n] = '.';
			tmp[n + 1] = '0';
			tmp[n + 2] = '\0';
		}
	}
	pm_sb_puts (sb, tmp);
}

static void pm_json_indent (pm_sb_t *sb, int level)
{
	for (int i = 0; i < level * 2; i++)
		pm_sb_putc (sb, ' ');
}

//-----------------------------------------------------------------------------
// CRC32 name hashing + upstream string table
//-----------------------------------------------------------------------------

#include "pm-hash-strings.inc"

typedef struct pm_hashent_t
{
	u32 hash;
	const char *str;
} pm_hashent_t;

static pm_hashent_t *pm_hashidx = 0;
static size_t pm_hashidx_n = 0;

static int pm_hashent_cmp (const void *a, const void *b)
{
	const u32 ha = ((const pm_hashent_t *)a)->hash;
	const u32 hb = ((const pm_hashent_t *)b)->hash;
	return ha < hb ? -1 : ha > hb ? 1 : 0;
}

static void pm_hash_init (void)
{
	size_t i;
	if (pm_hashidx)
		return;
	pm_hashidx = MALLOC (pm_hash_strings_count * sizeof (*pm_hashidx));
	if (!pm_hashidx)
		return;
	for (i = 0; i < pm_hash_strings_count; i++)
	{
		pm_hashidx[i].hash
			= (u32)crc32 (0L, (const Bytef *)pm_hash_strings[i], (uInt)strlen (pm_hash_strings[i]));
		pm_hashidx[i].str = pm_hash_strings[i];
	}
	qsort (pm_hashidx, pm_hash_strings_count, sizeof (*pm_hashidx), pm_hashent_cmp);
	pm_hashidx_n = pm_hash_strings_count;
}

u32 PMRender_Hash (const char *name, size_t len)
{
	return (u32)crc32 (0L, (const Bytef *)name, (uInt)len);
}

ccp PMRender_Lookup (u32 hash)
{
	pm_hashent_t key;
	pm_hashent_t *found;
	pm_hash_init ();
	if (!pm_hashidx)
		return 0;
	key.hash = hash;
	key.str = 0;
	found = bsearch (&key, pm_hashidx, pm_hashidx_n, sizeof (*pm_hashidx), pm_hashent_cmp);
	return found ? found->str : 0;
}

// "X"-format fallback for unknown hashes (unpadded uppercase hex).
static void pm_hash_name (char out[16], u32 hash)
{
	ccp s = PMRender_Lookup (hash);
	if (s)
	{
		snprintf (out, 16, "%s", s);
		return;
	}
	snprintf (out, 16, "%X", hash);
}

// ToHash(): plain hex parse wins, else CRC32 of the raw key.
static u32 pm_to_hash (const char *key)
{
	const char *p = key;
	while (*p && isspace ((unsigned char)*p))
		p++;
	const char *e = p + strlen (p);
	while (e > p && isspace ((unsigned char)e[-1]))
		e--;
	if ((e - p >= 2) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
		p += 2;
	size_t n = (size_t)(e - p);
	if (n >= 1 && n <= 8)
	{
		u32 v = 0;
		size_t i;
		for (i = 0; i < n; i++)
		{
			int hv = pj_hexval (p[i]);
			if (hv < 0)
				break;
			v = v * 16 + (u32)hv;
		}
		if (i == n)
			return v;
	}
	return PMRender_Hash (key, strlen (key));
}

// strict 1..8 hex digits, full consume (after trim); false otherwise
static bool pm_parse_hex (const char *s, size_t n, u32 *out)
{
	while (n && isspace ((unsigned char)*s))
	{
		s++;
		n--;
	}
	while (n && isspace ((unsigned char)s[n - 1]))
		n--;
	if (!n || n > 8)
		return false;
	u32 v = 0;
	size_t i;
	for (i = 0; i < n; i++)
	{
		int hv = pj_hexval (s[i]);
		if (hv < 0)
			return false;
		v = v * 16 + (u32)hv;
	}
	*out = v;
	return true;
}

// canonical 8-digit lowercase hex (table/body hashes as written upstream)
static bool pm_parse_hex8 (const char *s, size_t n, u32 *out)
{
	if (n != 8)
		return false;
	return pm_parse_hex (s, n, out);
}

//-----------------------------------------------------------------------------
// probe.header
//-----------------------------------------------------------------------------

void InitializePMProbe (pmprobe_t *probe)
{
	if (probe)
		memset (probe, 0, sizeof (*probe));
}

void ResetPMProbe (pmprobe_t *probe)
{
	if (probe)
	{
		FREE (probe->axis);
		memset (probe, 0, sizeof (*probe));
	}
}

bool IsPMProbe (const u8 *data, size_t size)
{
	u32 n;
	if (!data || size < 8)
		return false;
	if (le32 (data) != PMPROBE_MAGIC)
		return false;
	n = le32 (data + 4);
	if (n > PMPROBE_MAX_AXIS)
		return false;
	return size == 156 + (size_t)n * 4;
}

enumError ScanPMProbe (pmprobe_t *probe, const u8 *data, size_t size)
{
	u32 n, i;
	size_t pos;
	if (!probe || !data)
		return ERR_INVALID_DATA;
	ResetPMProbe (probe);
	if (!IsPMProbe (data, size))
		return ERR_INVALID_DATA;
	n = le32 (data + 4);
	if (n)
	{
		probe->axis = MALLOC ((size_t)n * sizeof (*probe->axis));
		if (!probe->axis)
			return ERR_NO_MEMORY;
		for (i = 0; i < n; i++)
			probe->axis[i] = le32 (data + 8 + (size_t)i * 4);
	}
	probe->n_axis = n;
	pos = 8 + (size_t)n * 4;
	for (i = 0; i < 3; i++)
		probe->pos[i] = pm_rd_float (data + pos + (size_t)i * 4);
	for (i = 0; i < 3; i++)
		probe->scale[i] = pm_rd_float (data + pos + 12 + (size_t)i * 4);
	for (i = 0; i < 4; i++)
		probe->unk[i] = pm_rd_float (data + pos + 24 + (size_t)i * 4);
	probe->param1 = pm_rd_float (data + pos + 40);
	probe->param2 = pm_rd_float (data + pos + 44);
	for (i = 0; i < 3; i++)
		probe->color[i] = pm_rd_float (data + pos + 48 + (size_t)i * 4);
	for (i = 0; i < 3; i++)
		probe->unk2[i] = pm_rd_float (data + pos + 60 + (size_t)i * 4);
	memcpy (probe->type, data + pos + 72, 64);
	probe->type[64] = '\0';
	// NUL-pad convention: cut at the first NUL for the JSON form
	probe->type[strnlen (probe->type, 64)] = '\0';
	probe->unkA0 = le32 (data + pos + 136);
	probe->unkA4 = pm_rd_float (data + pos + 140);
	probe->unkA8 = pm_rd_float (data + pos + 144);
	probe->unkAC = le32 (data + pos + 148);
	return ERR_OK;
}

enumError EncodePMProbe (const pmprobe_t *probe, u8 **dest, size_t *dest_size)
{
	u8 *out;
	size_t total, pos, i;
	size_t type_len;
	if (!probe || !dest || !dest_size)
		return ERR_INVALID_DATA;
	if (probe->n_axis > PMPROBE_MAX_AXIS || (probe->n_axis && !probe->axis))
		return ERR_INVALID_DATA;
	type_len = strlen (probe->type);
	if (type_len > 64)
		return ERR_INVALID_DATA;
	*dest = 0;
	*dest_size = 0;
	total = 156 + (size_t)probe->n_axis * 4;
	out = MALLOC (total ? total : 1);
	if (!out)
		return ERR_NO_MEMORY;
	pm_wr32 (out, PMPROBE_MAGIC);
	pm_wr32 (out + 4, probe->n_axis);
	for (i = 0; i < probe->n_axis; i++)
		pm_wr32 (out + 8 + i * 4, probe->axis[i]);
	pos = 8 + (size_t)probe->n_axis * 4;
	for (i = 0; i < 3; i++)
		pm_wr_float (out + pos + (size_t)i * 4, probe->pos[i]);
	for (i = 0; i < 3; i++)
		pm_wr_float (out + pos + 12 + (size_t)i * 4, probe->scale[i]);
	for (i = 0; i < 4; i++)
		pm_wr_float (out + pos + 24 + (size_t)i * 4, probe->unk[i]);
	pm_wr_float (out + pos + 40, probe->param1);
	pm_wr_float (out + pos + 44, probe->param2);
	for (i = 0; i < 3; i++)
		pm_wr_float (out + pos + 48 + (size_t)i * 4, probe->color[i]);
	for (i = 0; i < 3; i++)
		pm_wr_float (out + pos + 60 + (size_t)i * 4, probe->unk2[i]);
	memset (out + pos + 72, 0, 64);
	memcpy (out + pos + 72, probe->type, type_len);
	pm_wr32 (out + pos + 136, probe->unkA0);
	pm_wr_float (out + pos + 140, probe->unkA4);
	pm_wr_float (out + pos + 144, probe->unkA8);
	pm_wr32 (out + pos + 148, probe->unkAC);
	*dest = out;
	*dest_size = total;
	return ERR_OK;
}

static void pm_json_float_array (pm_sb_t *sb, const float *v, size_t n, int level)
{
	size_t i;
	pm_sb_putc (sb, '[');
	for (i = 0; i < n; i++)
	{
		if (i)
			pm_sb_puts (sb, ", ");
		pm_json_float (sb, v[i]);
	}
	pm_sb_putc (sb, ']');
	(void)level;
}

enumError DecodePMProbe_JSON (FILE *out, const u8 *data, size_t size)
{
	pmprobe_t probe;
	pm_sb_t sb;
	size_t i;
	enumError err = ScanPMProbe (&probe, data, size);
	if (err)
		return err;
	pm_sb_init (&sb);
	if (!sb.buf)
	{
		ResetPMProbe (&probe);
		return ERR_NO_MEMORY;
	}
	pm_sb_puts (&sb, "{\n");
	pm_sb_puts (&sb, "  \"AxisParameters\": [");
	for (i = 0; i < probe.n_axis; i++)
		pm_sb_printf (&sb, "%s%u", i ? ", " : "", probe.axis[i]);
	pm_sb_puts (&sb, "],\n");
	pm_sb_puts (&sb, "  \"ProbePosition\": ");
	pm_json_float_array (&sb, probe.pos, 3, 1);
	pm_sb_puts (&sb, ",\n");
	pm_sb_puts (&sb, "  \"BoxScale\": ");
	pm_json_float_array (&sb, probe.scale, 3, 1);
	pm_sb_puts (&sb, ",\n");
	pm_sb_puts (&sb, "  \"Unknown\": ");
	pm_json_float_array (&sb, probe.unk, 4, 1);
	pm_sb_puts (&sb, ",\n");
	pm_sb_puts (&sb, "  \"ProbeParam1\": ");
	pm_json_float (&sb, probe.param1);
	pm_sb_puts (&sb, ",\n");
	pm_sb_puts (&sb, "  \"ProbeParam2\": ");
	pm_json_float (&sb, probe.param2);
	pm_sb_puts (&sb, ",\n");
	pm_sb_puts (&sb, "  \"Color\": ");
	pm_json_float_array (&sb, probe.color, 3, 1);
	pm_sb_puts (&sb, ",\n");
	pm_sb_puts (&sb, "  \"Unknown2\": ");
	pm_json_float_array (&sb, probe.unk2, 3, 1);
	pm_sb_puts (&sb, ",\n");
	pm_sb_puts (&sb, "  \"Type\": ");
	pm_json_escape (&sb, probe.type);
	pm_sb_puts (&sb, ",\n");
	pm_sb_printf (&sb, "  \"Unknown0xA0\": %u,\n", probe.unkA0);
	pm_sb_puts (&sb, "  \"Unknown0xA4\": ");
	pm_json_float (&sb, probe.unkA4);
	pm_sb_puts (&sb, ",\n");
	pm_sb_puts (&sb, "  \"Unknown0xA8\": ");
	pm_json_float (&sb, probe.unkA8);
	pm_sb_puts (&sb, ",\n");
	pm_sb_printf (&sb, "  \"Unknown0xAC\": %u\n", probe.unkAC);
	pm_sb_puts (&sb, "}\n");
	ResetPMProbe (&probe);
	if (!sb.buf)
		return ERR_NO_MEMORY;
	fputs (sb.buf, out ? out : stdout);
	pm_sb_free (&sb);
	return ERR_OK;
}

static bool pm_json_float_array_parse (float *out, size_t want, const pj_node_t *n)
{
	size_t i;
	double d;
	if (!n || n->type != PJ_ARR || n->n_items != want)
		return false;
	for (i = 0; i < want; i++)
	{
		if (!pj_as_double (n->items[i], &d))
			return false;
		out[i] = (float)d;
	}
	return true;
}

static bool pm_json_u32_array_parse (u32 **out, u32 *n_out, const pj_node_t *n)
{
	size_t i;
	u32 v;
	u32 *arr;
	*out = 0;
	*n_out = 0;
	if (!n || n->type != PJ_ARR || n->n_items > PMPROBE_MAX_AXIS)
		return false;
	if (!n->n_items)
		return true;
	arr = MALLOC (n->n_items * sizeof (*arr));
	if (!arr)
		return false;
	for (i = 0; i < n->n_items; i++)
	{
		if (!pj_as_u32 (n->items[i], &v))
		{
			FREE (arr);
			return false;
		}
		arr[i] = v;
	}
	*out = arr;
	*n_out = (u32)n->n_items;
	return true;
}

enumError ParsePMProbe_JSON (pmprobe_t *probe, const char *text, size_t text_len)
{
	pj_node_t *root;
	const pj_node_t *n;
	double d;
	u32 u;
	enumError err = ERR_INVALID_DATA;
	if (!probe || !text)
		return ERR_INVALID_DATA;
	ResetPMProbe (probe);
	root = pj_parse (text, text_len);
	if (!root || root->type != PJ_OBJ)
	{
		pj_free (root);
		return ERR_INVALID_DATA;
	}
	// discriminator: only probe JSON carries these fields
	if (!pj_get (root, "ProbePosition") && !pj_get (root, "AxisParameters"))
	{
		pj_free (root);
		return ERR_INVALID_DATA;
	}
	n = pj_get (root, "AxisParameters");
	if (n && !pm_json_u32_array_parse (&probe->axis, &probe->n_axis, n))
		goto done;
	n = pj_get (root, "ProbePosition");
	if (n && !pm_json_float_array_parse (probe->pos, 3, n))
		goto done;
	n = pj_get (root, "BoxScale");
	if (n && !pm_json_float_array_parse (probe->scale, 3, n))
		goto done;
	n = pj_get (root, "Unknown");
	if (n && !pm_json_float_array_parse (probe->unk, 4, n))
		goto done;
	n = pj_get (root, "ProbeParam1");
	if (n)
	{
		if (!pj_as_double (n, &d))
			goto done;
		probe->param1 = (float)d;
	}
	n = pj_get (root, "ProbeParam2");
	if (n)
	{
		if (!pj_as_double (n, &d))
			goto done;
		probe->param2 = (float)d;
	}
	n = pj_get (root, "Color");
	if (n && !pm_json_float_array_parse (probe->color, 3, n))
		goto done;
	n = pj_get (root, "Unknown2");
	if (n && !pm_json_float_array_parse (probe->unk2, 3, n))
		goto done;
	n = pj_get (root, "Type");
	if (n)
	{
		if (n->type != PJ_STR || strlen (n->str) > 64)
			goto done;
		snprintf (probe->type, sizeof (probe->type), "%s", n->str);
	}
	n = pj_get (root, "Unknown0xA0");
	if (n)
	{
		if (!pj_as_u32 (n, &u))
			goto done;
		probe->unkA0 = u;
	}
	n = pj_get (root, "Unknown0xA4");
	if (n)
	{
		if (!pj_as_double (n, &d))
			goto done;
		probe->unkA4 = (float)d;
	}
	n = pj_get (root, "Unknown0xA8");
	if (n)
	{
		if (!pj_as_double (n, &d))
			goto done;
		probe->unkA8 = (float)d;
	}
	n = pj_get (root, "Unknown0xAC");
	if (n)
	{
		if (!pj_as_u32 (n, &u))
			goto done;
		probe->unkAC = u;
	}
	err = ERR_OK;
done:
	pj_free (root);
	if (err)
		ResetPMProbe (probe);
	return err;
}

enumError encode_pmprobe_file (ccp source, ccp dest)
{
	u8 *text = 0;
	size_t text_len = 0;
	pmprobe_t probe;
	u8 *bin = 0;
	size_t bin_size = 0;
	enumError err;
	InitializePMProbe (&probe);
	err = LoadFileAlloc (source, 0, 0, &text, &text_len, 64 << 20, 0, 0, false);
	if (err)
		return err;
	err = ParsePMProbe_JSON (&probe, (const char *)text, (uint)text_len);
	FREE (text);
	if (err)
	{
		ResetPMProbe (&probe);
		return err;
	}
	err = EncodePMProbe (&probe, &bin, &bin_size);
	ResetPMProbe (&probe);
	if (err)
		return err;
	if (!testmode)
	{
		File_t F;
		CreateFILE (&F, true, dest, testmode, false, true, false, false);
		if (F.f && fwrite (bin, 1, bin_size, F.f) != bin_size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing probe failed: %s\n", dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (bin);
	return err;
}

//-----------------------------------------------------------------------------
// render params ".data"
//-----------------------------------------------------------------------------

void InitializePMRender (pmrender_t *render)
{
	if (render)
		memset (render, 0, sizeof (*render));
}

static void pm_render_val_free (pmrender_val_t *v)
{
	if (v)
	{
		FREE (v->sval);
		v->sval = 0;
	}
}

void ResetPMRender (pmrender_t *render)
{
	size_t s, p;
	if (!render)
		return;
	for (s = 0; s < render->n_sections; s++)
	{
		pmrender_section_t *sec = &render->sections[s];
		for (p = 0; p < sec->n_props; p++)
		{
			FREE (sec->props[p].key);
			pm_render_val_free (&sec->props[p].val);
		}
		FREE (sec->props);
		FREE (sec->name);
	}
	FREE (render->sections);
	memset (render, 0, sizeof (*render));
}

// read one LF line from [pos,limit); strips a single trailing CR.
// returns false at end of input. *line/*len point into the buffer.
static bool pm_read_line (const u8 *data, size_t limit, size_t *pos, const char **line, size_t *len)
{
	size_t start, nl;
	if (*pos >= limit)
		return false;
	start = *pos;
	nl = start;
	while (nl < limit && data[nl] != '\n')
		nl++;
	*line = (const char *)data + start;
	*len = nl - start;
	*pos = nl < limit ? nl + 1 : nl; // skip the LF
	if (*len && (*line)[*len - 1] == '\r')
		(*len)--;
	return true;
}

// count colons in a line
static size_t pm_count_colons (const char *s, size_t n)
{
	size_t c = 0, i;
	for (i = 0; i < n; i++)
		if (s[i] == ':')
			c++;
	return c;
}

// decimal int32 with surrounding whitespace tolerated (int.TryParse shape)
static bool pm_parse_dec_int (const char *s, size_t n, int64_t *out)
{
	while (n && isspace ((unsigned char)*s))
	{
		s++;
		n--;
	}
	while (n && isspace ((unsigned char)s[n - 1]))
		n--;
	if (!n)
		return false;
	size_t i = 0;
	bool neg = false;
	if (s[0] == '+' || s[0] == '-')
	{
		neg = s[0] == '-';
		i = 1;
		(void)neg;
	}
	if (i >= n || !isdigit ((unsigned char)s[i]))
		return false;
	long long v = 0;
	for (; i < n; i++)
	{
		if (!isdigit ((unsigned char)s[i]))
			return false;
		v = v * 10 + (s[i] - '0');
		if (v > (long long)0x7fffffff + 1)
			return false;
	}
	if (s[0] == '-')
		v = -v;
	if (v < (long long)-0x80000000LL || v > (long long)0x7fffffffLL)
		return false;
	*out = (int64_t)v;
	return true;
}

// float with full consume (float.Parse shape, C locale)
static bool pm_parse_float_str (const char *s, size_t n, float *out)
{
	while (n && isspace ((unsigned char)*s))
	{
		s++;
		n--;
	}
	while (n && isspace ((unsigned char)s[n - 1]))
		n--;
	if (!n)
		return false;
	char *tmp = MALLOC (n + 1);
	char *e;
	float f;
	if (!tmp)
		return false;
	memcpy (tmp, s, n);
	tmp[n] = '\0';
	f = strtof (tmp, &e);
	bool ok = e && !*e;
	FREE (tmp);
	if (!ok)
		return false;
	*out = f;
	return true;
}

static enumError pm_parse_value (pmrender_val_t *val, const char *s, size_t n)
{
	memset (val, 0, sizeof (*val));
	if (n >= 2 && s[0] == '0' && s[1] == 'x')
	{
		// hex bit pattern -> float scalar
		u32 u;
		if (!pm_parse_hex (s + 2, n - 2, &u))
			return ERR_INVALID_DATA;
		val->kind = PMR_FLOAT;
		memcpy (&val->fval, &u, 4);
		return ERR_OK;
	}
	size_t i;
	bool comma = false;
	for (i = 0; i < n; i++)
		if (s[i] == ',')
		{
			comma = true;
			break;
		}
	if (comma)
	{
		// comma separated 2/3/4-float vector
		size_t parts = 1;
		for (i = 0; i < n; i++)
			if (s[i] == ',')
				parts++;
		if (parts < 2 || parts > 4)
			return ERR_INVALID_DATA;
		val->kind = parts == 2 ? PMR_VEC2 : parts == 3 ? PMR_VEC3 : PMR_VEC4;
		size_t start = 0, k = 0;
		for (i = 0; i <= n; i++)
		{
			if (i == n || s[i] == ',')
			{
				if (!pm_parse_float_str (s + start, i - start, &val->vec[k]))
					return ERR_INVALID_DATA;
				k++;
				start = i + 1;
			}
		}
		return ERR_OK;
	}
	int64_t iv;
	if (pm_parse_dec_int (s, n, &iv))
	{
		val->kind = PMR_INT;
		val->ival = iv;
		return ERR_OK;
	}
	val->kind = PMR_STRING;
	val->sval = MALLOC (n + 1);
	if (!val->sval)
		return ERR_NO_MEMORY;
	memcpy (val->sval, s, n);
	val->sval[n] = '\0';
	return ERR_OK;
}

// parse one section body in [start,end) into sec (props appended)
static enumError pm_parse_body (pmrender_section_t *sec, const u8 *data, size_t start, size_t end)
{
	size_t pos = start;
	const char *line;
	size_t len;
	while (pm_read_line (data, end, &pos, &line, &len))
	{
		const char *colon;
		size_t hlen, vlen;
		u32 phash;
		pmrender_prop_t *np;
		char *key;
		if (!len)
			continue;
		if (pm_count_colons (line, len) != 1)
			continue; // mirrors ReadValue's Length != 2 skip
		colon = memchr (line, ':', len);
		hlen = (size_t)(colon - line);
		vlen = len - hlen - 1;
		if (!pm_parse_hex (colon - hlen, hlen, &phash))
			return ERR_INVALID_DATA;
		(void)phash;
		key = MALLOC (hlen + 1);
		if (!key)
			return ERR_NO_MEMORY;
		memcpy (key, line, hlen);
		key[hlen] = '\0';
		// resolve key display name through the hash table
		np = REALLOC (sec->props, (sec->n_props + 1) * sizeof (*np));
		if (!np)
		{
			FREE (key);
			return ERR_NO_MEMORY;
		}
		sec->props = np;
		np = &sec->props[sec->n_props];
		memset (np, 0, sizeof (*np));
		{
			u32 kh = pm_to_hash (key);
			ccp known = PMRender_Lookup (kh);
			char tmp[16];
			if (!known)
			{
				snprintf (tmp, sizeof (tmp), "%X", kh);
				known = tmp;
			}
			np->key = MALLOC (strlen (known) + 1);
			if (!np->key)
			{
				FREE (key);
				return ERR_NO_MEMORY;
			}
			strcpy (np->key, known);
		}
		FREE (key);
		if (pm_parse_value (&np->val, colon + 1, vlen))
		{
			FREE (np->key);
			np->key = 0;
			return ERR_INVALID_DATA;
		}
		sec->n_props++;
	}
	return ERR_OK;
}

enumError ScanPMRender (pmrender_t *render, const u8 *data, size_t size)
{
	size_t pos = 0;
	const char *line;
	size_t len;
	u32 nsec, s;
	if (!render || !data)
		return ERR_INVALID_DATA;
	ResetPMRender (render);
	if (size < 4 || data[0] != 0xef || data[1] != 0xbb || data[2] != 0xbf)
		return ERR_INVALID_DATA;
	pos = 3;
	if (!pm_read_line (data, size, &pos, &line, &len))
		return ERR_INVALID_DATA;
	if (!pm_parse_hex (line, len, &nsec))
		return ERR_INVALID_DATA;
	if (nsec > 100000)
		return ERR_INVALID_DATA;
	if (nsec)
	{
		render->sections = MALLOC ((size_t)nsec * sizeof (*render->sections));
		if (!render->sections)
			return ERR_INVALID_DATA;
		memset (render->sections, 0, (size_t)nsec * sizeof (*render->sections));
	}
	render->n_sections = nsec;
	for (s = 0; s < nsec; s++)
	{
		const char *colon;
		pmrender_section_t *sec = &render->sections[s];
		u32 hash, sz;
		if (!pm_read_line (data, size, &pos, &line, &len))
		{
			ResetPMRender (render);
			return ERR_INVALID_DATA;
		}
		if (pm_count_colons (line, len) != 1)
		{
			ResetPMRender (render);
			return ERR_INVALID_DATA;
		}
		colon = memchr (line, ':', len);
		if (!pm_parse_hex (line, (size_t)(colon - line), &hash)
			|| !pm_parse_hex (colon + 1, len - (size_t)(colon - line) - 1, &sz))
		{
			ResetPMRender (render);
			return ERR_INVALID_DATA;
		}
		sec->hash = hash;
		{
			char tmp[16];
			pm_hash_name (tmp, hash);
			sec->name = MALLOC (strlen (tmp) + 1);
			if (!sec->name)
			{
				ResetPMRender (render);
				return ERR_NO_MEMORY;
			}
			strcpy (sec->name, tmp);
		}
		if (pos + sz > size)
		{
			ResetPMRender (render);
			return ERR_INVALID_DATA;
		}
		if (pm_parse_body (sec, data, pos, pos + sz))
		{
			ResetPMRender (render);
			return ERR_INVALID_DATA;
		}
		pos += sz;
	}
	if (pos != size)
	{
		ResetPMRender (render);
		return ERR_INVALID_DATA;
	}
	return ERR_OK;
}

// canonical validator: strict 8-digit table hashes + exact size fit
bool IsPMRender (const u8 *data, size_t size)
{
	size_t pos = 0;
	const char *line;
	size_t len;
	u32 nsec, s;
	if (!data || size < 4 || size > (64 << 20))
		return false;
	if (data[0] != 0xef || data[1] != 0xbb || data[2] != 0xbf)
		return false;
	pos = 3;
	if (!pm_read_line (data, size, &pos, &line, &len))
		return false;
	if (!pm_parse_hex8 (line, len, &nsec) || nsec > 100000)
		return false;
	for (s = 0; s < nsec; s++)
	{
		const char *colon;
		u32 hash, sz;
		size_t bpos, bend;
		if (!pm_read_line (data, size, &pos, &line, &len))
			return false;
		if (pm_count_colons (line, len) != 1)
			return false;
		colon = memchr (line, ':', len);
		if (!pm_parse_hex8 (line, (size_t)(colon - line), &hash))
			return false;
		if (!pm_parse_hex8 (colon + 1, len - (size_t)(colon - line) - 1, &sz))
			return false;
		if (pos + sz > size)
			return false;
		// body: optional hash line + "hash:value" lines
		bpos = pos;
		bend = pos + sz;
		bool first = true;
		while (bpos < bend)
		{
			const char *bl;
			size_t blen;
			size_t save = bpos;
			(void)save;
			if (!pm_read_line (data, bend, &bpos, &bl, &blen))
				return false;
			if (!blen)
				continue;
			size_t cc = pm_count_colons (bl, blen);
			if (cc != 1)
			{
				if (!first)
					return false; // only the leading hash line may lack ':'
				if (blen != 8)
					return false;
				u32 dummy;
				if (!pm_parse_hex8 (bl, blen, &dummy))
					return false;
				first = false;
				continue;
			}
			first = false;
			const char *bc = memchr (bl, ':', blen);
			u32 dummy;
			if (!pm_parse_hex8 (bl, (size_t)(bc - bl), &dummy))
				return false;
		}
		pos = bend;
	}
	return pos == size;
}

static void pm_write_hexfloat (pm_sb_t *sb, float f)
{
	if (f == 0.0f)
	{
		pm_sb_puts (sb, "0x0");
		return;
	}
	u32 u;
	memcpy (&u, &f, 4);
	pm_sb_printf (sb, "0x%08x", u);
}

static void pm_write_value (pm_sb_t *sb, const pmrender_val_t *v)
{
	size_t k, n;
	switch (v->kind)
	{
		case PMR_INT:
			pm_sb_printf (sb, "%lld", (long long)v->ival);
			break;
		case PMR_FLOAT:
			pm_write_hexfloat (sb, v->fval);
			break;
		case PMR_VEC2:
		case PMR_VEC3:
		case PMR_VEC4:
			n = v->kind == PMR_VEC2 ? 2 : v->kind == PMR_VEC3 ? 3 : 4;
			for (k = 0; k < n; k++)
				pm_sb_printf (sb, "%s%.6f", k ? "," : "", (double)v->vec[k]);
			break;
		case PMR_STRING:
		default:
			pm_sb_puts (sb, v->sval ? v->sval : "");
			break;
	}
}

enumError EncodePMRender (const pmrender_t *render, u8 **dest, size_t *dest_size)
{
	pm_sb_t *bodies;
	pm_sb_t out;
	size_t s, p;
	if (!render || !dest || !dest_size)
		return ERR_INVALID_DATA;
	if (render->n_sections > 100000)
		return ERR_INVALID_DATA;
	*dest = 0;
	*dest_size = 0;
	bodies = MALLOC (render->n_sections * sizeof (*bodies));
	if (render->n_sections && !bodies)
		return ERR_NO_MEMORY;
	for (s = 0; s < render->n_sections; s++)
	{
		const pmrender_section_t *sec = &render->sections[s];
		pm_sb_init (&bodies[s]);
		if (!bodies[s].buf)
		{
			while (s)
				pm_sb_free (&bodies[--s]);
			FREE (bodies);
			return ERR_NO_MEMORY;
		}
		pm_sb_printf (&bodies[s], "%08x\n", sec->hash);
		for (p = 0; p < sec->n_props; p++)
		{
			pm_sb_printf (&bodies[s], "%08x:", pm_to_hash (sec->props[p].key));
			pm_write_value (&bodies[s], &sec->props[p].val);
			pm_sb_putc (&bodies[s], '\n');
		}
		if (!bodies[s].buf)
		{
			for (p = 0; p <= s; p++)
				pm_sb_free (&bodies[p]);
			FREE (bodies);
			return ERR_NO_MEMORY;
		}
	}
	pm_sb_init (&out);
	if (!out.buf)
	{
		for (s = 0; s < render->n_sections; s++)
			pm_sb_free (&bodies[s]);
		FREE (bodies);
		return ERR_NO_MEMORY;
	}
	pm_sb_putn (&out, "\xef\xbb\xbf", 3);
	pm_sb_printf (&out, "%08x\n", (unsigned)render->n_sections);
	for (s = 0; s < render->n_sections; s++)
		pm_sb_printf (&out, "%08x:%08x\n", render->sections[s].hash, (unsigned)bodies[s].len);
	for (s = 0; s < render->n_sections; s++)
	{
		pm_sb_putn (&out, bodies[s].buf, bodies[s].len);
		pm_sb_free (&bodies[s]);
	}
	FREE (bodies);
	if (!out.buf)
		return ERR_NO_MEMORY;
	*dest = (u8 *)out.buf;
	*dest_size = out.len;
	return ERR_OK;
}

static void pm_json_value (pm_sb_t *sb, const pmrender_val_t *v)
{
	size_t k, n;
	static const char *const comps[] = { "X", "Y", "Z", "W" };
	switch (v->kind)
	{
		case PMR_INT:
			pm_sb_printf (sb, "%lld", (long long)v->ival);
			break;
		case PMR_FLOAT:
			pm_json_float (sb, v->fval);
			break;
		case PMR_VEC2:
		case PMR_VEC3:
		case PMR_VEC4:
			n = v->kind == PMR_VEC2 ? 2 : v->kind == PMR_VEC3 ? 3 : 4;
			pm_sb_puts (sb, "{ ");
			for (k = 0; k < n; k++)
			{
				pm_sb_printf (sb, "%s\"%s\": ", k ? ", " : "", comps[k]);
				pm_json_float (sb, v->vec[k]);
			}
			pm_sb_puts (sb, " }");
			break;
		case PMR_STRING:
		default:
			pm_json_escape (sb, v->sval ? v->sval : "");
			break;
	}
}

enumError DecodePMRender_JSON (FILE *out, const u8 *data, size_t size)
{
	pmrender_t render;
	pm_sb_t sb;
	size_t s, p;
	InitializePMRender (&render);
	if (ScanPMRender (&render, data, size))
		return ERR_INVALID_DATA;
	pm_sb_init (&sb);
	if (!sb.buf)
	{
		ResetPMRender (&render);
		return ERR_NO_MEMORY;
	}
	pm_sb_puts (&sb, "{\n  \"Sections\": [");
	for (s = 0; s < render.n_sections; s++)
	{
		const pmrender_section_t *sec = &render.sections[s];
		pm_sb_puts (&sb, s ? ",\n    {\n" : "\n    {\n");
		pm_sb_printf (&sb, "      \"hash_name\": { \"Hash\": %u, \"String\": ", sec->hash);
		pm_json_escape (&sb, sec->name ? sec->name : "");
		pm_sb_puts (&sb, " },\n      \"properties\": {");
		for (p = 0; p < sec->n_props; p++)
		{
			pm_sb_puts (&sb, p ? ",\n        " : "\n        ");
			pm_json_escape (&sb, sec->props[p].key);
			pm_sb_puts (&sb, ": ");
			pm_json_value (&sb, &sec->props[p].val);
		}
		pm_sb_puts (&sb, sec->n_props ? "\n      }\n    }" : " }\n    }");
	}
	pm_sb_puts (&sb, render.n_sections ? "\n  ]\n}\n" : "]\n}\n");
	ResetPMRender (&render);
	if (!sb.buf)
		return ERR_NO_MEMORY;
	fputs (sb.buf, out ? out : stdout);
	pm_sb_free (&sb);
	return ERR_OK;
}

static bool pm_json_parse_val (pmrender_val_t *val, const pj_node_t *n)
{
	double d;
	size_t k;
	static const char *const comps[] = { "X", "Y", "Z", "W" };
	memset (val, 0, sizeof (*val));
	if (!n)
		return false;
	if (n->type == PJ_NUM)
	{
		if (n->is_int)
		{
			if (n->ival < (int64_t)-0x80000000LL || n->ival > (int64_t)0x7fffffffLL)
				return false;
			val->kind = PMR_INT;
			val->ival = n->ival;
			return true;
		}
		val->kind = PMR_FLOAT;
		val->fval = (float)n->dval;
		return true;
	}
	if (n->type == PJ_STR)
	{
		val->kind = PMR_STRING;
		val->sval = MALLOC (strlen (n->str) + 1);
		if (!val->sval)
			return false;
		strcpy (val->sval, n->str);
		return true;
	}
	if (n->type == PJ_BOOL)
	{
		// upstream booleans fall through to object.ToString()
		val->kind = PMR_STRING;
		val->sval = MALLOC (6);
		if (!val->sval)
			return false;
		strcpy (val->sval, n->boolean ? "True" : "False");
		return true;
	}
	if (n->type == PJ_OBJ)
	{
		bool has_w = pj_get (n, "W") != 0;
		bool has_z = pj_get (n, "Z") != 0;
		bool has_y = pj_get (n, "Y") != 0;
		size_t want = has_w ? 4 : has_z ? 3 : has_y ? 2 : 0;
		if (!want)
			return false;
		val->kind = want == 2 ? PMR_VEC2 : want == 3 ? PMR_VEC3 : PMR_VEC4;
		for (k = 0; k < want; k++)
		{
			const pj_node_t *c = pj_get (n, comps[k]);
			if (c)
			{
				if (!pj_as_double (c, &d))
					return false;
				val->vec[k] = (float)d;
			}
			else
				val->vec[k] = 0;
		}
		return true;
	}
	return false;
}

enumError ParsePMRender_JSON (pmrender_t *render, const char *text, size_t text_len)
{
	pj_node_t *root;
	const pj_node_t *secs;
	size_t s;
	enumError err = ERR_INVALID_DATA;
	if (!render || !text)
		return ERR_INVALID_DATA;
	ResetPMRender (render);
	root = pj_parse (text, text_len);
	if (!root || root->type != PJ_OBJ)
	{
		pj_free (root);
		return ERR_INVALID_DATA;
	}
	secs = pj_get (root, "Sections");
	if (!secs || secs->type != PJ_ARR)
	{
		pj_free (root);
		return ERR_INVALID_DATA;
	}
	if (secs->n_items)
	{
		render->sections = MALLOC (secs->n_items * sizeof (*render->sections));
		if (!render->sections)
		{
			pj_free (root);
			return ERR_NO_MEMORY;
		}
		memset (render->sections, 0, secs->n_items * sizeof (*render->sections));
	}
	render->n_sections = secs->n_items;
	for (s = 0; s < secs->n_items; s++)
	{
		const pj_node_t *js = secs->items[s];
		const pj_node_t *jh, *jp;
		const pj_node_t *hash_n;
		pmrender_section_t *sec = &render->sections[s];
		u32 h;
		size_t p;
		if (!js || js->type != PJ_OBJ)
			goto done;
		jh = pj_get (js, "hash_name");
		if (!jh || jh->type != PJ_OBJ)
			goto done;
		hash_n = pj_get (jh, "Hash");
		if (!pj_as_u32 (hash_n, &h))
			goto done;
		sec->hash = h;
		{
			char tmp[16];
			pm_hash_name (tmp, h);
			sec->name = MALLOC (strlen (tmp) + 1);
			if (!sec->name)
			{
				err = ERR_NO_MEMORY;
				goto done;
			}
			strcpy (sec->name, tmp);
		}
		jp = pj_get (js, "properties");
		if (jp && jp->type != PJ_OBJ)
			goto done;
		if (jp && jp->n_pairs)
		{
			sec->props = MALLOC (jp->n_pairs * sizeof (*sec->props));
			if (!sec->props)
			{
				err = ERR_NO_MEMORY;
				goto done;
			}
			memset (sec->props, 0, jp->n_pairs * sizeof (*sec->props));
		}
		for (p = 0; jp && p < jp->n_pairs; p++)
		{
			pmrender_prop_t *prop = &sec->props[p];
			prop->key = MALLOC (strlen (jp->keys[p]) + 1);
			if (!prop->key)
			{
				err = ERR_NO_MEMORY;
				goto done;
			}
			strcpy (prop->key, jp->keys[p]);
			if (!pm_json_parse_val (&prop->val, jp->vals[p]))
				goto done;
			sec->n_props++;
		}
	}
	err = ERR_OK;
done:
	pj_free (root);
	if (err)
		ResetPMRender (render);
	return err;
}

enumError encode_pmrender_file (ccp source, ccp dest)
{
	u8 *text = 0;
	size_t text_len = 0;
	pmrender_t render;
	u8 *bin = 0;
	size_t bin_size = 0;
	enumError err;
	InitializePMRender (&render);
	err = LoadFileAlloc (source, 0, 0, &text, &text_len, 64 << 20, 0, 0, false);
	if (err)
		return err;
	err = ParsePMRender_JSON (&render, (const char *)text, (uint)text_len);
	FREE (text);
	if (err)
	{
		ResetPMRender (&render);
		return err;
	}
	err = EncodePMRender (&render, &bin, &bin_size);
	ResetPMRender (&render);
	if (err)
		return err;
	if (!testmode)
	{
		File_t F;
		CreateFILE (&F, true, dest, testmode, false, true, false, false);
		if (F.f && fwrite (bin, 1, bin_size, F.f) != bin_size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing render params failed: %s\n", dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (bin);
	return err;
}

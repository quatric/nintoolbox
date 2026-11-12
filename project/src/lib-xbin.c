// SPDX-License-Identifier: GPL-2.0+
// HAL Laboratory XData ("XBIN") files: binary YAML and FDG scene graphs -> YAML text.
// Layouts follow firubii's HEL engine notes and KirbyLib (Yaml.cs, FDG.cs, XData.cs),
// checked on Kirby and the Forgotten Land (XData 5.0, YAML 6, FDG 3).
#include "lib-xbin.h"
#include <string.h>

#define XBIN_MAX_DEPTH 128

typedef struct xbin_t
{
	const u8 *data;
	uint size;
	bool be;
	uint header_size; // 0x10, or 0x14 when an RLOC offset follows (XData major > 2)
	u32 yaml_version;
	FILE *out;
	bool bad;
} xbin_t;

static u32 xb_u32 (xbin_t *x, u64 off)
{
	if (off + 4 > x->size)
	{
		x->bad = true;
		return 0;
	}
	return x->be ? be32 (x->data + off) : le32 (x->data + off);
}

static u64 xb_u64 (xbin_t *x, u64 off)
{
	if (off + 8 > x->size)
	{
		x->bad = true;
		return 0;
	}
	return x->be ? be64 (x->data + off) : le64 (x->data + off);
}

static bool xb_header (xbin_t *x, const u8 *data, uint size)
{
	memset (x, 0, sizeof (*x));
	if (!data || size < 0x18 || memcmp (data, "XBIN", 4))
		return false;
	if (data[4] == 0x34 && data[5] == 0x12)
		x->be = false;
	else if (data[4] == 0x12 && data[5] == 0x34)
		x->be = true;
	else
		return false;
	x->data = data;
	x->size = size;
	const u8 major = data[6];
	if (!major || major > 9 || xb_u32 (x, 0xc) != 65001)
		return false;
	x->header_size = major > 2 ? 0x14 : 0x10;
	return true;
}

// Payload start of a YAML file: XData 5+/YAML 6 adds 4 bytes of zero padding first.
static uint xb_yaml_start (xbin_t *x)
{
	uint p = x->header_size;
	if (p + 8 <= x->size && !memcmp (x->data + p, "\0\0\0\0", 4))
		p += 4;
	return p;
}

xbin_kind_t GetXBINKind (const u8 *data, uint size)
{
	xbin_t x;
	if (!xb_header (&x, data, size))
		return XBIN_KIND_NONE;
	const uint y = xb_yaml_start (&x);
	if (y + 8 <= x.size && !memcmp (data + y, "YAML", 4))
		return XBIN_KIND_YAML;
	const uint f = x.header_size;
	if (f + 0x14 <= x.size && xb_u32 (&x, f) == 0x46444748) // "FDGH" in the file's byte order
		return XBIN_KIND_FDG;
	return XBIN_KIND_OTHER;
}

// A string is a u32 length and the bytes (NUL-terminated, padded) at an absolute offset.
static bool xb_string (xbin_t *x, u64 off, const char **str, u32 *len)
{
	const u32 n = xb_u32 (x, off);
	if (x->bad || off + 4 + (u64)n > x->size)
	{
		x->bad = true;
		return false;
	}
	*str = (const char *)x->data + off + 4;
	*len = n;
	return true;
}

static void xb_put_quoted (FILE *f, const char *s, u32 n)
{
	fputc ('"', f);
	for (u32 i = 0; i < n; i++)
	{
		const u8 c = (u8)s[i];
		if (c == '"' || c == '\\')
			fprintf (f, "\\%c", c);
		else if (c == '\n')
			fputs ("\\n", f);
		else if (c == '\t')
			fputs ("\\t", f);
		else if (c == '\r')
			fputs ("\\r", f);
		else if (c < 0x20 || c == 0x7f)
			fprintf (f, "\\x%02x", c);
		else
			fputc (c, f);
	}
	fputc ('"', f);
}

static void xb_put_key (FILE *f, const char *s, u32 n)
{
	bool plain = n > 0 && !(s[0] >= '0' && s[0] <= '9') && s[0] != '-';
	for (u32 i = 0; plain && i < n; i++)
	{
		const char c = s[i];
		plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
			|| c == '_' || c == '.' || c == '-';
	}
	static const char *const reserved[] = { "true", "false", "null", "yes", "no", "on", "off", "~" };
	for (uint r = 0; plain && r < sizeof (reserved) / sizeof (*reserved); r++)
		if (n == strlen (reserved[r]) && !strncasecmp (s, reserved[r], n))
			plain = false;
	if (plain)
		fwrite (s, 1, n, f);
	else
		xb_put_quoted (f, s, n);
}

static void xb_indent (FILE *f, uint depth)
{
	for (uint i = 0; i < depth; i++)
		fputs ("  ", f);
}

enum
{
	YT_INVALID,
	YT_INT,
	YT_U64,
	YT_FLOAT,
	YT_BOOL,
	YT_STRING,
	YT_HASH,
	YT_ARRAY
};

static uint xb_yaml_type (xbin_t *x, u64 p, u64 *payload)
{
	if (x->yaml_version >= 6)
	{
		*payload = p + 8;
		const u64 t = xb_u64 (x, p);
		return t <= YT_ARRAY ? (uint)t : YT_INVALID;
	}
	*payload = p + 4;
	u32 t = xb_u32 (x, p);
	if (t >= 2)
		t++; // YAML 6 inserted UInt64 at 2
	return t <= YT_ARRAY ? t : YT_INVALID;
}

// Offsets inside YAML 5+ are relative to the field holding them.
static u64 xb_yaml_ref (xbin_t *x, u64 field)
{
	return (u64)xb_u32 (x, field) + (x->yaml_version >= 5 ? field : 0);
}

static void xb_yaml_node (xbin_t *x, u64 p, uint depth, bool inline_ok);

static void xb_yaml_scalar (xbin_t *x, uint type, u64 q)
{
	FILE *f = x->out;
	switch (type)
	{
	case YT_INT:
		fprintf (f, "%d", (int)xb_u32 (x, q));
		break;
	case YT_U64:
		fprintf (f, "%llu", (unsigned long long)xb_u64 (x, q));
		break;
	case YT_FLOAT:
	{
		const u32 bits = xb_u32 (x, q);
		float v;
		memcpy (&v, &bits, 4);
		if (v != v)
			fputs (".nan", f);
		else if (v > 3.4e38f || v < -3.4e38f)
			fputs (v > 0 ? ".inf" : "-.inf", f);
		else
		{
			char buf[48];
			snprintf (buf, sizeof (buf), "%.9g", v);
			fputs (buf, f);
			if (!strpbrk (buf, ".en"))
				fputs (".0", f);
		}
		break;
	}
	case YT_BOOL:
		fputs (xb_u32 (x, q) ? "true" : "false", f);
		break;
	case YT_STRING:
	{
		const char *s;
		u32 n;
		if (xb_string (x, xb_yaml_ref (x, q), &s, &n))
			xb_put_quoted (f, s, n);
		else
			fputs ("~", f);
		break;
	}
	default:
		fputs ("~", f);
	}
}

static void xb_yaml_node (xbin_t *x, u64 p, uint depth, bool inline_ok)
{
	FILE *f = x->out;
	u64 q;
	const uint type = xb_yaml_type (x, p, &q);
	if (x->bad || depth > XBIN_MAX_DEPTH)
	{
		x->bad = true;
		fputs (" ~\n", f);
		return;
	}
	if (type != YT_HASH && type != YT_ARRAY)
	{
		fputc (' ', f);
		xb_yaml_scalar (x, type, q);
		fputc ('\n', f);
		return;
	}

	const u32 count = xb_u32 (x, q);
	const u64 list = q + 4;
	const u64 stride = type == YT_HASH ? 8 : 4;
	if (x->bad || list + (u64)count * stride > x->size)
	{
		x->bad = true;
		fputs (" ~\n", f);
		return;
	}
	if (!count)
	{
		fputs (type == YT_HASH ? " {}\n" : " []\n", f);
		return;
	}
	(void)inline_ok;
	fputc ('\n', f);
	const bool ordered = type == YT_HASH && x->yaml_version >= 4
		&& list + (u64)count * 12 <= x->size;
	for (u32 i = 0; i < count && !x->bad; i++)
	{
		xb_indent (f, depth);
		if (type == YT_ARRAY)
		{
			fputc ('-', f);
			xb_yaml_node (x, xb_yaml_ref (x, list + (u64)i * 4), depth + 1, true);
			continue;
		}
		u32 slot = ordered ? xb_u32 (x, list + (u64)count * 8 + (u64)i * 4) : i;
		if (slot >= count)
			slot = i;
		const u64 pair = list + (u64)slot * 8;
		const char *key;
		u32 klen;
		if (xb_string (x, xb_yaml_ref (x, pair), &key, &klen))
			xb_put_key (f, key, klen);
		else
			fputs ("\"?\"", f);
		fputc (':', f);
		xb_yaml_node (x, xb_yaml_ref (x, pair + 4), depth + 1, false);
	}
}

static void xb_fdg (xbin_t *x)
{
	FILE *f = x->out;
	const uint h = x->header_size;
	const u32 version = xb_u32 (x, h + 4);
	const u32 pub = xb_u32 (x, h + 8), scn = xb_u32 (x, h + 0xc), ast = xb_u32 (x, h + 0x10);
	const u32 asset_count = xb_u32 (x, ast), scene_count = xb_u32 (x, scn);
	if (x->bad || asset_count > x->size / 4 || scene_count > x->size / 12)
	{
		x->bad = true;
		return;
	}
	fprintf (f, "# HAL FDG scene graph, version %u\nversion: %u\nscenes:", version, version);
	if (!scene_count)
		fputs (" []", f);
	fputc ('\n', f);

	const u32 pub_count = xb_u32 (x, pub);
	for (u32 i = 0; i < scene_count && !x->bad; i++)
	{
		const u64 e = (u64)scn + 4 + (u64)i * 12;
		const char *s;
		u32 n;
		fputs ("  - name: ", f);
		if (xb_string (x, xb_u32 (x, e), &s, &n))
			xb_put_quoted (f, s, n);
		fputc ('\n', f);

		bool is_public = false;
		for (u32 k = 0; k < pub_count && k < scene_count && !x->bad; k++)
			if (xb_u32 (x, (u64)pub + 4 + (u64)k * 4) == i)
				is_public = true;
		if (is_public)
			fputs ("    public: true\n", f);

		const u32 deps = xb_u32 (x, e + 4), dep_count = xb_u32 (x, deps);
		if (dep_count && dep_count <= scene_count)
		{
			fputs ("    dependencies:\n", f);
			for (u32 d = 0; d < dep_count && !x->bad; d++)
			{
				const u32 idx = xb_u32 (x, (u64)deps + 4 + (u64)d * 4);
				fputs ("      - ", f);
				if (idx < scene_count && xb_string (x, xb_u32 (x, (u64)scn + 4 + (u64)idx * 12), &s, &n))
					xb_put_quoted (f, s, n);
				else
					fprintf (f, "%u", idx);
				fputc ('\n', f);
			}
		}

		const u32 assets = xb_u32 (x, e + 8), a_count = xb_u32 (x, assets);
		if (a_count && a_count <= asset_count)
		{
			fputs ("    assets:\n", f);
			for (u32 a = 0; a < a_count && !x->bad; a++)
			{
				const u32 idx = xb_u32 (x, (u64)assets + 4 + (u64)a * 4);
				fputs ("      - ", f);
				// FDG 3 lists assets as 16-byte {u64 FNV-1a hash, u32 string offset, pad} records
				const u64 sref = version > 2 ? (u64)ast + 8 + (u64)idx * 16 + 8 : (u64)ast + 4 + (u64)idx * 4;
				if (idx < asset_count && xb_string (x, xb_u32 (x, sref), &s, &n))
					xb_put_quoted (f, s, n);
				else
					fprintf (f, "%u", idx);
				fputc ('\n', f);
			}
		}
	}
}

enumError SaveTextXBIN (const u8 *data, uint size, ccp path)
{
	xbin_t x;
	if (!xb_header (&x, data, size))
		return ERR_INVALID_DATA;
	const xbin_kind_t kind = GetXBINKind (data, size);
	if (kind != XBIN_KIND_YAML && kind != XBIN_KIND_FDG)
		return ERR_NOTHING_TO_DO;

	FILE *f = fopen (path, "w");
	if (!f)
		return ERROR1 (ERR_CANT_CREATE, "Can't create file: %s\n", path);
	x.out = f;
	if (kind == XBIN_KIND_YAML)
	{
		const uint y = xb_yaml_start (&x);
		x.yaml_version = xb_u32 (&x, y + 4);
		fprintf (f, "# HAL XData %u.%u, binary YAML version %u (%s endian)\n", data[6], data[7],
			x.yaml_version, x.be ? "big" : "little");
		fputs ("---", f);
		xb_yaml_node (&x, y + 8, 0, false);
	}
	else
		xb_fdg (&x);
	fclose (f);
	if (x.bad)
		return ERROR0 (ERR_WARNING, "XBIN: payload runs outside the file, output truncated: %s\n", path);
	return ERR_OK;
}

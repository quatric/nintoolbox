#include "lib-prc.h"
#include "lib-std.h"

#include <string.h>

// Smash Ultimate parameter binary (.prc, "paracobn"). Reference:
// ultimate-research/prc-rs src/disasm.rs (reader) and src/xml/mod.rs (XML
// mapping), the maintained Rust successor of benhall-7/paracobNET's ParamXML
// used by Ploaj/SSBHLib's CrossMod for the same files.
//
//   0x00  "paracobn" magic (8 bytes)
//   0x08  u32 hash-table byte size (always a multiple of 8)
//   0x0c  u32 reference-section byte size
//   0x10  hash table: hashsize/8 u64 Hash40 labels
//   ...   reference section (strings + struct key tables)
//   ...   parameter section; the root value is always a struct (type 12)
//
// A value is one type byte followed by its payload (all little-endian):
//   1 bool(u8)  2 i8  3 u8  4 i16  5 u16  6 i32  7 u32  8 f32
//   9 hash (i32 label-table index)
//   10 string (u32 offset, relative to the reference section, NUL-terminated)
//   11 list: u32 count, then count u32 offsets, each relative to the type
//      byte, pointing at another typed value
//   12 struct: u32 count, u32 key-table offset (relative to the reference
//      section); the key table holds count (u32 label index, u32 value
//      offset) pairs, the offsets again relative to the type byte.
//
// The XML mapping copies ParamXML/prc-rs: <struct>/<list> containers,
// scalar tags (<bool> <sbyte> <byte> <short> <ushort> <int> <uint> <float>
// <hash40> <string>), struct keys as hash="0x.........." (low 40 bits, the
// convention hash40's Display uses) and list items as index="N".
// Hashes stay numeric: without a label dictionary (ParamXML's Labels.csv)
// they cannot be resolved to names, and guessing would corrupt lookups.

#define PRC_MAX_DEPTH 256

typedef struct
{
	const u8 *data;
	size_t size;
	u64 ref_start;
	u64 hashnum;
	char *buf;
	size_t len;
	size_t cap;
	int depth;
	int failed;
} prc_t;

static int prc_ok (prc_t *p, u64 off, u64 len)
{
	return off <= p->size && len <= p->size - off;
}

static u16 prc_rd16 (prc_t *p, u64 off)
{
	return (u16)p->data[off] | (u16)p->data[off + 1] << 8;
}

static u32 prc_rd32 (prc_t *p, u64 off)
{
	return (u32)p->data[off] | (u32)p->data[off + 1] << 8
		| (u32)p->data[off + 2] << 16 | (u32)p->data[off + 3] << 24;
}

static float prc_rdf32 (prc_t *p, u64 off)
{
	u32 bits = prc_rd32 (p, off);
	float v;
	memcpy (&v, &bits, 4);
	return v;
}

static void prc_reserve (prc_t *p, size_t extra)
{
	if (p->failed || extra > (size_t)1 << 30)
	{
		p->failed = 1;
		return;
	}
	if (p->len + extra + 1 > p->cap)
	{
		size_t ncap = p->cap ? p->cap : 4096;
		while (ncap < p->len + extra + 1)
		{
			ncap *= 2;
			if (ncap > (size_t)1 << 30)
			{
				p->failed = 1;
				return;
			}
		}
		char *nbuf = MALLOC (ncap);
		if (!nbuf)
		{
			p->failed = 1;
			return;
		}
		if (p->len)
			memcpy (nbuf, p->buf, p->len);
		FREE (p->buf);
		p->buf = nbuf;
		p->cap = ncap;
	}
}

static void prc_putn (prc_t *p, const char *s, size_t n)
{
	prc_reserve (p, n);
	if (p->failed)
		return;
	memcpy (p->buf + p->len, s, n);
	p->len += n;
}

static void prc_puts (prc_t *p, const char *s)
{
	prc_putn (p, s, strlen (s));
}

static void prc_indent (prc_t *p, int depth)
{
	for (int i = 0; i < depth; i++)
		prc_puts (p, "  ");
}

// XML text/attribute escaping. Bytes below 0x20 other than tab/LF/CR are
// illegal in XML 1.0 and become U+FFFD, as does a bare DEL.
static void prc_xml_text (prc_t *p, const char *s, size_t n, int attr)
{
	for (size_t i = 0; i < n; i++)
	{
		const unsigned char c = (unsigned char)s[i];
		switch (c)
		{
			case '&': prc_puts (p, "&amp;"); break;
			case '<': prc_puts (p, "&lt;"); break;
			case '>': prc_puts (p, "&gt;"); break;
			case '"': prc_puts (p, attr ? "&quot;" : "\""); break;
			case 9: case 10: case 13: prc_putn (p, (const char *)&s[i], 1); break;
			default:
				if (c < 0x20 || c == 0x7f)
					prc_puts (p, "\xef\xbf\xbd");
				else
					prc_putn (p, (const char *)&s[i], 1);
				break;
		}
	}
}

static void prc_float (prc_t *p, float v)
{
	// Rust's {} prints shortest round-trip ("1", "0.5"), inf/NaN as-is;
	// %.9g is the closest C spelling (extreme exponents differ in form).
	char tmp[32];
	if (v != v)
		prc_puts (p, "NaN");
	else if (v > 3.4028235e38f)
		prc_puts (p, "inf");
	else if (v < -3.4028235e38f)
		prc_puts (p, "-inf");
	else
	{
		snprintf (tmp, sizeof (tmp), "%.9g", (double)v);
		prc_puts (p, tmp);
	}
}

static void prc_decode_at (prc_t *p, u64 pos, ccp key_attr, int depth);

static void prc_open_tag (prc_t *p, ccp tag, ccp key_attr, int depth, int *self_close)
{
	prc_indent (p, depth);
	prc_puts (p, "<");
	prc_puts (p, tag);
	if (key_attr)
		prc_puts (p, key_attr);
	*self_close = 0;
}

static void prc_decode_struct (prc_t *p, u64 pos, ccp key_attr, int depth)
{
	// pos points at the type byte.
	if (!prc_ok (p, pos, 9))
	{
		p->failed = 1;
		return;
	}
	const u64 count = prc_rd32 (p, pos + 1);
	const u64 refpos = prc_rd32 (p, pos + 5);
	if (count > 0x100000 || !prc_ok (p, p->ref_start + refpos, count * 8))
	{
		p->failed = 1;
		return;
	}
	if (!count)
	{
		prc_indent (p, depth);
		prc_puts (p, "<struct");
		if (key_attr)
			prc_puts (p, key_attr);
		prc_puts (p, "/>\n");
		return;
	}

	u32 *hidx = MALLOC (count * 4);
	u32 *voff = MALLOC (count * 4);
	if (!hidx || !voff)
	{
		FREE (hidx);
		FREE (voff);
		p->failed = 1;
		return;
	}
	for (u64 i = 0; i < count; i++)
	{
		hidx[i] = prc_rd32 (p, p->ref_start + refpos + i * 8);
		voff[i] = prc_rd32 (p, p->ref_start + refpos + i * 8 + 4);
		if ((u64)hidx[i] >= p->hashnum)
		{
			FREE (hidx);
			FREE (voff);
			p->failed = 1;
			return;
		}
	}
	// prc-rs sorts the key table by label index before reading.
	for (u64 i = 1; i < count; i++)
	{
		u64 j = i;
		while (j > 0 && hidx[j - 1] > hidx[j])
		{
			u32 t = hidx[j]; hidx[j] = hidx[j - 1]; hidx[j - 1] = t;
			t = voff[j]; voff[j] = voff[j - 1]; voff[j - 1] = t;
			j--;
		}
	}

	prc_indent (p, depth);
	prc_puts (p, "<struct");
	if (key_attr)
		prc_puts (p, key_attr);
	prc_puts (p, ">\n");
	for (u64 i = 0; i < count && !p->failed; i++)
	{
		u64 hash = 0;
		const u64 hoff = 16 + (u64)hidx[i] * 8;
		if (prc_ok (p, hoff, 8))
		{
			u64 lo = prc_rd32 (p, hoff);
			u64 hi = prc_rd32 (p, hoff + 4);
			hash = lo | hi << 32;
		}
		char key[32];
		snprintf (key, sizeof (key), " hash=\"0x%010llx\"",
			(unsigned long long)(hash & 0xffffffffffull));
		if (pos + voff[i] < pos) // wrap guard
		{
			p->failed = 1;
			break;
		}
		// The child key string must outlive the recursive call.
		char *kdup = MALLOC (strlen (key) + 1);
		if (!kdup)
		{
			p->failed = 1;
			break;
		}
		memcpy (kdup, key, strlen (key) + 1);
		prc_decode_at (p, pos + voff[i], kdup, depth + 1);
		FREE (kdup);
	}
	FREE (hidx);
	FREE (voff);
	if (p->failed)
		return;
	prc_indent (p, depth);
	prc_puts (p, "</struct>\n");
}

static void prc_decode_list (prc_t *p, u64 pos, ccp key_attr, int depth)
{
	if (!prc_ok (p, pos, 5))
	{
		p->failed = 1;
		return;
	}
	const u64 count = prc_rd32 (p, pos + 1);
	if (count > 0x100000 || !prc_ok (p, pos + 5, count * 4))
	{
		p->failed = 1;
		return;
	}
	if (!count)
	{
		prc_indent (p, depth);
		prc_puts (p, "<list");
		if (key_attr)
			prc_puts (p, key_attr);
		prc_puts (p, "/>\n");
		return;
	}
	prc_indent (p, depth);
	prc_puts (p, "<list");
	if (key_attr)
		prc_puts (p, key_attr);
	prc_puts (p, ">\n");
	for (u64 i = 0; i < count && !p->failed; i++)
	{
		const u64 off = prc_rd32 (p, pos + 5 + i * 4);
		if (pos + off < pos)
		{
			p->failed = 1;
			break;
		}
		char key[32];
		snprintf (key, sizeof (key), " index=\"%llu", (unsigned long long)i);
		size_t kl = strlen (key);
		key[kl] = '"';
		key[kl + 1] = 0;
		char *kdup = MALLOC (kl + 2);
		if (!kdup)
		{
			p->failed = 1;
			break;
		}
		memcpy (kdup, key, kl + 2);
		prc_decode_at (p, pos + off, kdup, depth + 1);
		FREE (kdup);
	}
	if (p->failed)
		return;
	prc_indent (p, depth);
	prc_puts (p, "</list>\n");
}

static void prc_decode_at (prc_t *p, u64 pos, ccp key_attr, int depth)
{
	if (p->failed || !prc_ok (p, pos, 1))
	{
		p->failed = 1;
		return;
	}
	if (p->depth >= PRC_MAX_DEPTH)
	{
		p->failed = 1;
		return;
	}
	p->depth++;

	const u8 type = p->data[pos];
	int sc = 0;
	switch (type)
	{
	case 1: // bool
		if (!prc_ok (p, pos, 2))
		{
			p->failed = 1;
			break;
		}
		prc_open_tag (p, "bool", key_attr, depth, &sc);
		prc_puts (p, p->data[pos + 1] ? ">true</bool>\n" : ">false</bool>\n");
		break;

	case 2: // i8
	case 3: // u8
		if (!prc_ok (p, pos, 2))
		{
			p->failed = 1;
			break;
		}
		prc_open_tag (p, type == 2 ? "sbyte" : "byte", key_attr, depth, &sc);
		{
			char tmp[16];
			if (type == 2)
				snprintf (tmp, sizeof (tmp), ">%d</%s>\n",
					(int)(int8_t)p->data[pos + 1], "sbyte");
			else
				snprintf (tmp, sizeof (tmp), ">%u</%s>\n",
					p->data[pos + 1], "byte");
			prc_puts (p, tmp);
		}
		break;

	case 4: // i16
	case 5: // u16
		if (!prc_ok (p, pos, 3))
		{
			p->failed = 1;
			break;
		}
		prc_open_tag (p, type == 4 ? "short" : "ushort", key_attr, depth, &sc);
		{
			char tmp[32];
			if (type == 4)
				snprintf (tmp, sizeof (tmp), ">%d</short>\n",
					(int)(int16_t)prc_rd16 (p, pos + 1));
			else
				snprintf (tmp, sizeof (tmp), ">%u</ushort>\n",
					prc_rd16 (p, pos + 1));
			prc_puts (p, tmp);
		}
		break;

	case 6: // i32
	case 7: // u32
		if (!prc_ok (p, pos, 5))
		{
			p->failed = 1;
			break;
		}
		prc_open_tag (p, type == 6 ? "int" : "uint", key_attr, depth, &sc);
		{
			char tmp[48];
			if (type == 6)
				snprintf (tmp, sizeof (tmp), ">%d</int>\n",
					(int32_t)prc_rd32 (p, pos + 1));
			else
				snprintf (tmp, sizeof (tmp), ">%u</uint>\n",
					prc_rd32 (p, pos + 1));
			prc_puts (p, tmp);
		}
		break;

	case 8: // f32
		if (!prc_ok (p, pos, 5))
		{
			p->failed = 1;
			break;
		}
		prc_open_tag (p, "float", key_attr, depth, &sc);
		prc_puts (p, ">");
		prc_float (p, prc_rdf32 (p, pos + 1));
		prc_puts (p, "</float>\n");
		break;

	case 9: // hash: label-table index
		if (!prc_ok (p, pos, 5))
		{
			p->failed = 1;
			break;
		}
		{
			const int32_t idx = (int32_t)prc_rd32 (p, pos + 1);
			if (idx < 0 || (u64)idx >= p->hashnum
				|| !prc_ok (p, 16 + (u64)idx * 8, 8))
			{
				p->failed = 1;
				break;
			}
			u64 hash = (u64)prc_rd32 (p, 16 + (u64)idx * 8)
				| (u64)prc_rd32 (p, 16 + (u64)idx * 8 + 4) << 32;
			prc_open_tag (p, "hash40", key_attr, depth, &sc);
			prc_puts (p, ">");
			{
				char tmp[24];
				snprintf (tmp, sizeof (tmp), "0x%010llx",
					(unsigned long long)(hash & 0xffffffffffull));
				prc_puts (p, tmp);
			}
			prc_puts (p, "</hash40>\n");
		}
		break;

	case 10: // string: offset into the reference section
		if (!prc_ok (p, pos, 5))
		{
			p->failed = 1;
			break;
		}
		{
			const u64 soff = p->ref_start + prc_rd32 (p, pos + 1);
			if (soff >= p->size)
			{
				p->failed = 1;
				break;
			}
			u64 len = 0;
			while (soff + len < p->size && p->data[soff + len])
				len++;
			if (soff + len >= p->size)
			{
				p->failed = 1;
				break;
			}
			prc_open_tag (p, "string", key_attr, depth, &sc);
			prc_puts (p, ">");
			prc_xml_text (p, (const char *)p->data + soff, (size_t)len, 0);
			prc_puts (p, "</string>\n");
		}
		break;

	case 11:
		prc_decode_list (p, pos, key_attr, depth);
		break;

	case 12:
		prc_decode_struct (p, pos, key_attr, depth);
		break;

	default:
		p->failed = 1;
		break;
	}
	p->depth--;
}

bool IsPRC (const u8 *data, size_t size)
{
	if (!data || size < 8)
		return false;
	// Smash Ultimate parameter binary (fully decoded below).
	if (!memcmp (data, "paracobn", 8))
		return size >= 17;
	// Older Smash 4 era variants, recognised but not structurally decoded.
	if (size >= 12 && !memcmp (data, "parambinary", 11))
		return true;
	if (!memcmp (data, "PRC\0", 4) || !memcmp (data, "BPAR", 4))
		return true;
	return false;
}

static int is_paracobn (const u8 *data, size_t size)
{
	return size >= 17 && !memcmp (data, "paracobn", 8);
}

enumError DecodePRC_XML (char **dest_xml, const u8 *data, size_t size)
{
	if (!dest_xml || !data || size < 8 || !IsPRC (data, size))
		return ERR_INVALID_DATA;

	// Legacy variants have no public structural reference to decode
	// against; report the recognition honestly instead of inventing fields.
	if (!is_paracobn (data, size))
	{
		const size_t need = 256;
		char *buf = MALLOC (need);
		if (!buf)
			return ERR_OUT_OF_MEMORY;
		snprintf (buf, need,
			"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
			"<struct>\n"
			"  <!-- Smash Parameter Binary (PRC) variant \"%.4s\": recognised, no structural decoder -->\n"
			"</struct>\n",
			(const char *)data);
		*dest_xml = buf;
		return ERR_OK;
	}

	prc_t prc;
	memset (&prc, 0, sizeof (prc));
	prc.data = data;
	prc.size = size;
	const u32 hashsize = (u32)data[8] | (u32)data[9] << 8
		| (u32)data[10] << 16 | (u32)data[11] << 24;
	const u32 refsize = (u32)data[12] | (u32)data[13] << 8
		| (u32)data[14] << 16 | (u32)data[15] << 24;
	if (hashsize & 7)
		return ERR_INVALID_DATA;
	prc.hashnum = hashsize / 8;
	prc.ref_start = 16 + (u64)hashsize;
	const u64 param_start = prc.ref_start + refsize;
	if (prc.ref_start > size || param_start > size || param_start >= size)
		return ERR_INVALID_DATA;
	if (data[param_start] != 12) // prc-rs: the root must be a struct
		return ERR_INVALID_DATA;

	prc_puts (&prc, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
	prc_decode_at (&prc, param_start, 0, 0);
	if (prc.failed || !prc.buf)
	{
		FREE (prc.buf);
		return ERR_INVALID_DATA;
	}
	prc_reserve (&prc, 1);
	if (prc.failed)
	{
		FREE (prc.buf);
		return ERR_INVALID_DATA;
	}
	prc.buf[prc.len] = 0;
	*dest_xml = prc.buf;
	return ERR_OK;
}

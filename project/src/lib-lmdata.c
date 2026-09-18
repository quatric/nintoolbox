// SPDX-License-Identifier: GPL-2.0+
#include "lib-std.h"
#include "lib-lmdata.h"
#include <string.h>
#include <math.h>
#include <ctype.h>

#include "lmdata_hashes.inc"

static inline u16 lm_be16 (const u8 *p)
{
	return (u16)((u16)p[0] << 8 | p[1]);
}

static inline u16 lm_le16 (const u8 *p)
{
	return (u16)((u16)p[1] << 8 | p[0]);
}

static inline s16 lm_be16s (const u8 *p)
{
	return (s16)lm_be16 (p);
}

static inline u32 lm_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static inline u32 lm_le32 (const u8 *p)
{
	return (u32)p[3] << 24 | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0];
}

static inline float lm_bef32 (const u8 *p)
{
	u32 u = lm_be32 (p);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

static inline float lm_lef32 (const u8 *p)
{
	u32 u = lm_le32 (p);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

static inline void lm_wr16be (u8 *p, u16 v)
{
	p[0] = (u8)(v >> 8);
	p[1] = (u8)v;
}

static inline void lm_wr16le (u8 *p, u16 v)
{
	p[1] = (u8)(v >> 8);
	p[0] = (u8)v;
}

static inline void lm_wr32be (u8 *p, u32 v)
{
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)v;
}

static inline void lm_wr32le (u8 *p, u32 v)
{
	p[3] = (u8)(v >> 24);
	p[2] = (u8)(v >> 16);
	p[1] = (u8)(v >> 8);
	p[0] = (u8)v;
}

static inline void lm_wrf32be (u8 *p, float f)
{
	u32 u;
	memcpy (&u, &f, 4);
	lm_wr32be (p, u);
}

static inline void lm_wrf32le (u8 *p, float f)
{
	u32 u;
	memcpy (&u, &f, 4);
	lm_wr32le (p, u);
}

//-----------------------------------------------------------------------------
///////////////		JMP hash names				///////////////
//-----------------------------------------------------------------------------

static uint32_t lmjmp_hash1 (const char *s)
{
	uint32_t h = 0;
	for (; *s; s++)
	{
		h <<= 8;
		h += (u8)*s;
		const uint32_t r6 = (uint32_t)((4993ul * h) >> 32);
		const uint32_t r0 = (uint8_t)(((h - r6) / 2 + r6) >> 24);
		h -= r0 * 33554393u;
	}
	return h;
}

static uint32_t lmjmp_hash2 (const char *s)
{
	uint32_t h = 0;
	for (; *s; s++)
		h = h * 31 + (u8)*s;
	return h;
}

static ccp lmjmp_name (uint hash, char *buf, size_t bufsz)
{
	for (uint i = 0; nt_hash_names[i]; i++)
	{
		if (lmjmp_hash1 (nt_hash_names[i]) == hash || lmjmp_hash2 (nt_hash_names[i]) == hash)
		{
			snprintf (buf, bufsz, "%s", nt_hash_names[i]);
			return buf;
		}
	}
	snprintf (buf, bufsz, "hash_%08X", hash);
	return buf;
}

static uint lmjmp_unhash (const char *name)
{
	uint h;
	if (sscanf (name, "hash_%X", &h) == 1)
		return h;
	// prefer the V1 hash when the name is known (matches upstream order)
	for (uint i = 0; nt_hash_names[i]; i++)
		if (!strcmp (nt_hash_names[i], name))
			return lmjmp_hash1 (name);
	// unknown fresh name: V2 (Java-style) hash
	return lmjmp_hash2 (name);
}

//-----------------------------------------------------------------------------
///////////////		JMP						///////////////
//-----------------------------------------------------------------------------

static uint lmjmp_typesize (uint type)
{
	switch (type)
	{
		case 0:
		case 2:
			return 4;
		case 4:
			return 2;
		case 5:
			return 1;
		case 1:
		case 6:
			return 0; // variable (string)
		default:
			return 0;
	}
}

static bool lmjmp_validate (const u8 *data, uint size, bool be, uint *rn, uint *fn)
{
	if (size < 16)
		return false;
	const uint nrec = be ? lm_be32 (data) : lm_le32 (data);
	const uint nfields = be ? lm_be32 (data + 4) : lm_le32 (data + 4);
	const uint recoff = be ? lm_be32 (data + 8) : lm_le32 (data + 8);
	const uint recsz = be ? lm_be32 (data + 12) : lm_le32 (data + 12);
	if (!nfields || nfields > 256 || nrec > 200000 || !recsz || recsz > 65536)
		return false;
	if ((u64)16 + (u64)nfields * 12 > size)
		return false;
	if ((u64)recoff + (u64)nrec * recsz > size)
		return false;
	for (uint i = 0; i < nfields; i++)
	{
		const u8 *fp = data + 16 + i * 12;
		const uint off = be ? lm_be16 (fp + 8) : lm_le16 (fp + 8);
		const uint type = fp[11];
		const uint tsz = lmjmp_typesize (type);
		if (!tsz && type != 1 && type != 6)
			return false;
		if (tsz && off + tsz > recsz)
			return false;
		if (!tsz && off >= recsz + 0x100000)
			return false; // string pointing absurdly far out
	}
	if (rn)
		*rn = nrec;
	if (fn)
		*fn = nfields;
	return true;
}

bool IsLMJMP (const u8 *data, size_t size)
{
	if (!data || size > 0xffffffffu)
		return false;
	return lmjmp_validate (data, (uint)size, true, 0, 0)
		|| lmjmp_validate (data, (uint)size, false, 0, 0);
}

void ResetLMJMP (lmjmp_t *jmp)
{
	if (!jmp)
		return;
	FREE (jmp->fields);
	if (jmp->records)
	{
		for (uint i = 0; i < jmp->n_records; i++)
		{
			if (jmp->records[i].values)
			{
				for (uint k = 0; k < jmp->n_fields; k++)
					FREE (jmp->records[i].values[k]);
				FREE (jmp->records[i].values);
			}
			FREE (jmp->records[i].raw);
		}
		FREE (jmp->records);
	}
	memset (jmp, 0, sizeof (*jmp));
}

static char *lm_strdup (const char *s)
{
	const size_t n = strlen (s) + 1;
	char *d = MALLOC (n);
	if (d)
		memcpy (d, s, n);
	return d;
}

// read a zero-terminated string bounded by SIZE
static bool lm_read_str (const u8 *data, uint size, uint off, char *out, size_t outsz, bool jis)
{
	uint len = 0;
	while (off + len < size && data[off + len] && len + 1 < outsz)
		len++;
	if (off + len >= size)
		return false;
	if (!jis)
	{
		memcpy (out, data + off, len);
		out[len] = 0;
		// non-printable bytes become \x escapes on dump; keep raw here
		return true;
	}
	// Shift-JIS: keep bytes verbatim (dump escapes them)
	memcpy (out, data + off, len);
	out[len] = 0;
	return true;
}

enumError ScanLMJMP (lmjmp_t *jmp, const u8 *data, uint size)
{
	if (!jmp || !data || size > 0xffffffffu)
		return ERR_INVALID_DATA;
	memset (jmp, 0, sizeof (*jmp));
	bool be = true;
	uint nrec = 0, nfields = 0;
	if (lmjmp_validate (data, (uint)size, true, &nrec, &nfields))
		be = true;
	else if (lmjmp_validate (data, (uint)size, false, &nrec, &nfields))
		be = false;
	else
		return ERR_NOTHING_TO_DO;
	jmp->big_endian = be;
	const uint recoff = be ? lm_be32 (data + 8) : lm_le32 (data + 8);
	const uint recsz = be ? lm_be32 (data + 12) : lm_le32 (data + 12);
	jmp->rec_size = recsz;
	jmp->fields = CALLOC (nfields ? nfields : 1, sizeof (*jmp->fields));
	jmp->records = CALLOC (nrec ? nrec : 1, sizeof (*jmp->records));
	if (!jmp->fields || !jmp->records)
	{
		ResetLMJMP (jmp);
		return ERR_OUT_OF_MEMORY;
	}
	jmp->n_fields = nfields;
	jmp->n_records = nrec;
	char nbuf[64];
	for (uint i = 0; i < nfields; i++)
	{
		const u8 *fp = data + 16 + i * 12;
		lmjmp_field_t *f = jmp->fields + i;
		f->hash = be ? lm_be32 (fp) : lm_le32 (fp);
		f->bitmask = be ? lm_be32 (fp + 4) : lm_le32 (fp + 4);
		f->offset = be ? lm_be16 (fp + 8) : lm_le16 (fp + 8);
		f->shift = (int8_t)fp[10];
		f->type = fp[11];
		snprintf (f->name, sizeof (f->name), "%s", lmjmp_name (f->hash, nbuf, sizeof (nbuf)));
	}
	const uint strtab = recoff + nrec * recsz;
	for (uint r = 0; r < nrec; r++)
	{
		lmjmp_record_t *rec = jmp->records + r;
		const uint base = recoff + r * recsz;
		rec->raw = MALLOC (recsz ? recsz : 1);
		if (!rec->raw)
		{
			ResetLMJMP (jmp);
			return ERR_OUT_OF_MEMORY;
		}
		memcpy (rec->raw, data + base, recsz);
		rec->raw_size = recsz;
		rec->values = CALLOC (nfields ? nfields : 1, sizeof (*rec->values));
		if (!rec->values)
		{
			ResetLMJMP (jmp);
			return ERR_OUT_OF_MEMORY;
		}
		for (uint i = 0; i < nfields; i++)
		{
			const lmjmp_field_t *f = jmp->fields + i;
			char tmp[256];
			const u8 *vp = data + base + f->offset;
			bool is_str = false;
			switch (f->type)
			{
				case 0:
				{
					const int32_t raw = be ? (int32_t)lm_be32 (vp) : (int32_t)lm_le32 (vp);
					int32_t v;
					if (f->shift >= 0)
						v = (int32_t)(((uint32_t)raw >> f->shift) & f->bitmask);
					else
						v = (int32_t)(((uint32_t)raw << (-f->shift)) & f->bitmask);
					snprintf (tmp, sizeof (tmp), "%d", v);
					break;
				}
				case 2:
				{
					const float v = be ? lm_bef32 (vp) : lm_lef32 (vp);
					snprintf (tmp, sizeof (tmp), "%.9g", v);
					break;
				}
				case 4:
				{
					int v = be ? (int)lm_be16s (vp) : (int)(int16_t)lm_le16 (vp);
					if (f->shift >= 0)
						v = (v >> f->shift) & (int)f->bitmask;
					else
						v = (v << (-f->shift)) & (int)f->bitmask;
					snprintf (tmp, sizeof (tmp), "%d", v);
					break;
				}
				case 5:
				{
					int v = vp[0];
					if (f->shift >= 0)
						v = (v >> f->shift) & (int)f->bitmask;
					else
						v = (v << (-f->shift)) & (int)f->bitmask;
					snprintf (tmp, sizeof (tmp), "%d", v);
					break;
				}
				case 1:
				case 6:
					is_str = true;
					if (!lm_read_str (data, size, base + f->offset, tmp, sizeof (tmp),
							f->type == 6))
					{
						ResetLMJMP (jmp);
						return ERR_INVALID_DATA;
					}
					break;
				default:
					ResetLMJMP (jmp);
					return ERR_INVALID_DATA;
			}
			(void)is_str;
			rec->values[i] = lm_strdup (tmp);
			if (!rec->values[i])
			{
				ResetLMJMP (jmp);
				return ERR_OUT_OF_MEMORY;
			}
		}
	}
	(void)strtab;
	return ERR_OK;
}

enumError CreateLMJMP (u8 **dest, uint *dest_size, const lmjmp_t *jmp)
{
	if (!dest || !dest_size || !jmp || !jmp->n_fields)
		return ERR_INVALID_DATA;
	const bool be = jmp->big_endian;
	const uint nrec = jmp->n_records, nf = jmp->n_fields;
	// record size: preserved from scan, else max fixed-field end
	uint recsz = jmp->rec_size;
	if (!recsz)
	{
		for (uint i = 0; i < nf; i++)
		{
			const uint tsz = lmjmp_typesize (jmp->fields[i].type);
			if (tsz && jmp->fields[i].offset + tsz > recsz)
				recsz = jmp->fields[i].offset + tsz;
		}
		recsz = (recsz + 3) & ~3u;
		if (!recsz)
			return ERR_INVALID_DATA;
	}
	const uint recoff = 16 + nf * 12;
	// string table: collect fresh (offsets absolute)
	typedef struct
	{
		uint rec, field;
	} strref_t;
	strref_t *refs = 0;
	uint nrefs = 0, caprefs = 0;
	for (uint r = 0; r < nrec; r++)
		for (uint i = 0; i < nf; i++)
		{
			if (jmp->fields[i].type != 1 && jmp->fields[i].type != 6)
				continue;
			const char *v = jmp->records[r].values ? jmp->records[r].values[i] : "";
			if (!v)
				v = "";
			if (nrefs >= caprefs)
			{
				const uint nc = caprefs ? caprefs * 2 : 16;
				strref_t *nn = REALLOC (refs, nc * sizeof (*nn));
				if (!nn)
				{
					FREE (refs);
					return ERR_OUT_OF_MEMORY;
				}
				refs = nn;
				caprefs = nc;
			}
			refs[nrefs].rec = r;
			refs[nrefs].field = i;
			nrefs++;
		}
	const uint strtab = recoff + nrec * recsz;
	// string spans: absolute address + length per string field. Inline
	// strings may extend past rec_size; the only hard rules are file
	// bounds and no overlap outside each string's own record span.
	typedef struct
	{
		uint addr, len;
	} strspan_t;
	strspan_t *spans = MALLOC ((nrefs ? nrefs : 1) * sizeof (*spans));
	if (!spans && nrefs)
	{
		FREE (refs);
		return ERR_OUT_OF_MEMORY;
	}
	uint total = strtab;
	for (uint k = 0; k < nrefs; k++)
	{
		const uint r = refs[k].rec, i = refs[k].field;
		const lmjmp_field_t *f = jmp->fields + i;
		const char *v = jmp->records[r].values[i];
		const uint nl = (uint)strlen (v) + 1;
		const uint addr = recoff + r * recsz + f->offset;
		spans[k].addr = addr;
		spans[k].len = nl;
		if ((u64)addr + nl < addr)
		{
			FREE (spans);
			FREE (refs);
			return ERR_INVALID_DATA;
		}
		if (addr + nl > total)
			total = addr + nl;
		const uint own0 = recoff + r * recsz, own1 = own0 + recsz;
		for (uint q = 0; q < k; q++)
		{
			const uint b0 = spans[q].addr, b1 = b0 + spans[q].len;
			const uint a0 = addr, a1 = addr + nl;
			const bool overlap = a0 < b1 && b0 < a1;
			if (!overlap)
				continue;
			// overlap allowed only inside the string's own record span
			// (inline head) and only if fully contained there
			if (a0 < own0 || a1 > own1)
			{
				FREE (spans);
				FREE (refs);
				return ERR_INVALID_DATA;
			}
		}
	}
	u8 *buf = CALLOC (1, total ? total : 1);
	if (!buf)
	{
		FREE (refs);
		return ERR_OUT_OF_MEMORY;
	}
	if (be)
	{
		lm_wr32be (buf, nrec);
		lm_wr32be (buf + 4, nf);
		lm_wr32be (buf + 8, recoff);
		lm_wr32be (buf + 12, recsz);
	}
	else
	{
		lm_wr32le (buf, nrec);
		lm_wr32le (buf + 4, nf);
		lm_wr32le (buf + 8, recoff);
		lm_wr32le (buf + 12, recsz);
	}
	for (uint i = 0; i < nf; i++)
	{
		u8 *fp = buf + 16 + i * 12;
		const uint h = lmjmp_unhash (jmp->fields[i].name);
		if (be)
		{
			lm_wr32be (fp, h);
			lm_wr32be (fp + 4, jmp->fields[i].bitmask);
			lm_wr16be (fp + 8, (u16)jmp->fields[i].offset);
		}
		else
		{
			lm_wr32le (fp, h);
			lm_wr32le (fp + 4, jmp->fields[i].bitmask);
			lm_wr16le (fp + 8, (u16)jmp->fields[i].offset);
		}
		fp[10] = (u8)(int8_t)jmp->fields[i].shift;
		fp[11] = (u8)jmp->fields[i].type;
	}
	// records: start from preserved raw bytes when available (keeps
	// masked-out integer bits and padding byte-exact), else zeros
	for (uint r = 0; r < nrec; r++)
	{
		u8 *rp = buf + recoff + r * recsz;
		if (jmp->records[r].raw && jmp->records[r].raw_size == recsz)
			memcpy (rp, jmp->records[r].raw, recsz);
		for (uint i = 0; i < nf; i++)
		{
			const lmjmp_field_t *f = jmp->fields + i;
			const char *v = jmp->records[r].values ? jmp->records[r].values[i] : "0";
			if (!v)
				v = "0";
			u8 *vp = rp + f->offset;
			switch (f->type)
			{
				case 0:
				{
					const int32_t val = atoi (v);
					const uint64_t fieldmask = ((uint64_t)f->bitmask) << (f->shift >= 0 ? f->shift : 0);
					uint32_t cur = be ? lm_be32 (vp) : lm_le32 (vp);
					uint32_t nw;
					if (f->shift >= 0)
						nw = (cur & ~(uint32_t)fieldmask) | (((uint32_t)val << f->shift) & (uint32_t)fieldmask);
					else
						nw = (cur & ~(uint32_t)f->bitmask) | (((uint32_t)val >> (-f->shift)) & f->bitmask);
					if (be)
						lm_wr32be (vp, nw);
					else
						lm_wr32le (vp, nw);
					break;
				}
				case 2:
				{
					const float fv = (float)atof (v);
					if (be)
						lm_wrf32be (vp, fv);
					else
						lm_wrf32le (vp, fv);
					break;
				}
				case 4:
				{
					const int val = atoi (v);
					uint cur = be ? lm_be16 (vp) : lm_le16 (vp);
					uint nw;
					if (f->shift >= 0)
						nw = (cur & ~(f->bitmask & 0xffffu)) | (((uint)val << f->shift) & (f->bitmask & 0xffffu));
					else
						nw = (cur & ~(f->bitmask & 0xffffu)) | (((uint)val >> (-f->shift)) & (f->bitmask & 0xffffu));
					if (be)
						lm_wr16be (vp, (u16)nw);
					else
						lm_wr16le (vp, (u16)nw);
					break;
				}
				case 5:
				{
					const int val = atoi (v);
					uint cur = vp[0];
					uint nw;
					if (f->shift >= 0)
						nw = (cur & ~(f->bitmask & 0xffu)) | (((uint)val << f->shift) & (f->bitmask & 0xffu));
					else
						nw = (cur & ~(f->bitmask & 0xffu)) | (((uint)val >> (-f->shift)) & (f->bitmask & 0xffu));
					vp[0] = (u8)nw;
					break;
				}
				default:
					break; // strings handled via the table below
			}
		}
	}
	// write the string bytes at their validated spans
	for (uint k = 0; k < nrefs; k++)
	{
		const uint r = refs[k].rec, i = refs[k].field;
		memcpy (buf + spans[k].addr, jmp->records[r].values[i], spans[k].len);
	}
	FREE (spans);
	FREE (refs);
	*dest = buf;
	*dest_size = total;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		text helpers				///////////////
//-----------------------------------------------------------------------------

typedef struct
{
	char *buf;
	size_t len, cap;
} lm_strbuf_t;

static bool lm_sb (lm_strbuf_t *s, const char *line)
{
	const size_t n = strlen (line);
	if (s->len + n + 2 > s->cap)
	{
		size_t nc = s->cap ? s->cap * 2 : 1024;
		while (nc < s->len + n + 2)
			nc *= 2;
		char *nn = REALLOC (s->buf, nc);
		if (!nn)
			return false;
		s->buf = nn;
		s->cap = nc;
	}
	memcpy (s->buf + s->len, line, n);
	s->len += n;
	s->buf[s->len++] = '\n';
	s->buf[s->len] = 0;
	return true;
}

static bool lm_sbf (lm_strbuf_t *s, const char *fmt, ...)
{
	char tmp[512];
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (tmp, sizeof (tmp), fmt, ap);
	va_end (ap);
	return lm_sb (s, tmp);
}

// next line (NUL-terminated in place); skips # comments and blanks when set
static char *lm_next_line (char **cursor, bool skip)
{
	for (;;)
	{
		if (!*cursor || !**cursor)
			return 0;
		char *eol = strchr (*cursor, '\n');
		char *line = *cursor;
		if (eol)
		{
			*eol = 0;
			*cursor = eol + 1;
		}
		else
			*cursor += strlen (*cursor);
		// trim trailing whitespace/CR
		size_t n = strlen (line);
		while (n && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t'))
			line[--n] = 0;
		while (*line == ' ' || *line == '\t')
			line++;
		if (skip && (!*line || *line == '#'))
			continue;
		return line;
	}
}

static const char *lm_typename (uint t)
{
	switch (t)
	{
		case 0:
			return "int32";
		case 1:
			return "string";
		case 2:
			return "float";
		case 4:
			return "int16";
		case 5:
			return "byte";
		case 6:
			return "stringjis";
		default:
			return "unknown";
	}
}

static uint lm_typeid (const char *s)
{
	if (!strcmp (s, "int32"))
		return 0;
	if (!strcmp (s, "string"))
		return 1;
	if (!strcmp (s, "float"))
		return 2;
	if (!strcmp (s, "int16"))
		return 4;
	if (!strcmp (s, "byte"))
		return 5;
	if (!strcmp (s, "stringjis"))
		return 6;
	return 99;
}

// escape non-printables as \xNN for dumps
static void lm_escape (const char *src, char *dst, size_t dstsz)
{
	size_t o = 0;
	for (; *src && o + 4 < dstsz; src++)
	{
		const u8 c = (u8)*src;
		if (c >= 32 && c < 127 && c != '\\')
			dst[o++] = (char)c;
		else if (c == '\\' && o + 2 < dstsz)
		{
			dst[o++] = '\\';
			dst[o++] = '\\';
		}
		else if (o + 4 < dstsz)
		{
			snprintf (dst + o, dstsz - o, "\\x%02X", c);
			o += 4;
		}
	}
	dst[o] = 0;
}

static bool lm_unescape (const char *src, char *dst, size_t dstsz)
{
	size_t o = 0;
	for (; *src && o + 1 < dstsz; src++)
	{
		if (src[0] == '\\' && src[1] == 'x' && isxdigit ((u8)src[2]) && isxdigit ((u8)src[3]))
		{
			char hex[3] = { src[2], src[3], 0 };
			dst[o++] = (char)strtol (hex, 0, 16);
			src += 3;
		}
		else if (src[0] == '\\' && src[1] == '\\')
		{
			dst[o++] = '\\';
			src++;
		}
		else
			dst[o++] = *src;
	}
	dst[o] = 0;
	return true;
}

enumError DumpLMJMP (char **dest, const lmjmp_t *jmp)
{
	if (!dest || !jmp)
		return ERR_INVALID_DATA;
	lm_strbuf_t s = { 0 };
	if (!lm_sbf (&s, "# LMJMP v1 endian=%s records=%u fields=%u recsize=%u",
			jmp->big_endian ? "BE" : "LE", jmp->n_records, jmp->n_fields, jmp->rec_size))
	{
		FREE (s.buf);
		return ERR_OUT_OF_MEMORY;
	}
	for (uint i = 0; i < jmp->n_fields; i++)
	{
		const lmjmp_field_t *f = jmp->fields + i;
		if (!lm_sbf (&s, "field %s %s mask=%08x shift=%d off=%u", f->name, lm_typename (f->type),
				f->bitmask, f->shift, f->offset))
		{
			FREE (s.buf);
			return ERR_OUT_OF_MEMORY;
		}
	}
	for (uint r = 0; r < jmp->n_records; r++)
	{
		if (!lm_sbf (&s, "record %u", r))
		{
			FREE (s.buf);
			return ERR_OUT_OF_MEMORY;
		}
		for (uint i = 0; i < jmp->n_fields; i++)
		{
			char esc[512];
			lm_escape (jmp->records[r].values[i], esc, sizeof (esc));
			if (!lm_sbf (&s, "%s = %s", jmp->fields[i].name, esc))
			{
				FREE (s.buf);
				return ERR_OUT_OF_MEMORY;
			}
		}
	}
	*dest = s.buf ? s.buf : lm_strdup ("");
	return *dest ? ERR_OK : ERR_OUT_OF_MEMORY;
}

enumError ParseLMJMPText (lmjmp_t *jmp, const char *text)
{
	if (!jmp || !text)
		return ERR_INVALID_DATA;
	memset (jmp, 0, sizeof (*jmp));
	jmp->big_endian = true;
	char *copy = lm_strdup (text);
	if (!copy)
		return ERR_OUT_OF_MEMORY;
	char *cur = copy;
	typedef struct
	{
		char *name, *value;
	} kv_t;
	kv_t *kvs = 0;
	uint nkvs = 0, capkvs = 0;
	char *line;
	while ((line = lm_next_line (&cur, true)) != 0)
	{
		if (!strncmp (line, "field ", 6))
		{
			char name[64], type[16];
			uint mask = 0xffffffffu, off = 0;
			int shift = 0;
			if (sscanf (line + 6, "%63s %15s mask=%x shift=%d off=%u", name, type, &mask,
					&shift, &off) < 2)
			{
				FREE (copy);
				for (uint k = 0; k < nkvs; k++)
				{
					FREE (kvs[k].name);
					FREE (kvs[k].value);
				}
				FREE (kvs);
				return ERR_INVALID_DATA;
			}
			lmjmp_field_t *nf = REALLOC (jmp->fields, (jmp->n_fields + 1) * sizeof (*nf));
			if (!nf)
			{
				FREE (copy);
				for (uint k = 0; k < nkvs; k++)
				{
					FREE (kvs[k].name);
					FREE (kvs[k].value);
				}
				FREE (kvs);
				ResetLMJMP (jmp);
				return ERR_OUT_OF_MEMORY;
			}
			jmp->fields = nf;
			lmjmp_field_t *f = nf + jmp->n_fields++;
			snprintf (f->name, sizeof (f->name), "%s", name);
			f->type = lm_typeid (type);
			f->bitmask = mask;
			f->shift = shift;
			f->offset = off;
			f->hash = lmjmp_unhash (name);
		}
		else if (!strncmp (line, "record ", 7))
		{
			lmjmp_record_t *nr
				= REALLOC (jmp->records, (jmp->n_records + 1) * sizeof (*nr));
			if (!nr)
			{
				FREE (copy);
				for (uint k = 0; k < nkvs; k++)
				{
					FREE (kvs[k].name);
					FREE (kvs[k].value);
				}
				FREE (kvs);
				ResetLMJMP (jmp);
				return ERR_OUT_OF_MEMORY;
			}
			jmp->records = nr;
			memset (nr + jmp->n_records, 0, sizeof (*nr));
			jmp->n_records++;
			nkvs = 0; // values attach to the newest record below
		}
		else
		{
			char *eq = strchr (line, '=');
			if (!eq || !jmp->n_records)
			{
				// header comments already skipped; allow endian line
				if (!strncmp (line, "#", 1))
					continue;
				if (strstr (line, "endian=LE"))
					jmp->big_endian = false;
				continue;
			}
			*eq = 0;
			char *name = line, *val = eq + 1;
			while (*name == ' ' || *name == '\t')
				name++;
			char *ne = name + strlen (name);
			while (ne > name && (ne[-1] == ' ' || ne[-1] == '\t'))
				*--ne = 0;
			while (*val == ' ' || *val == '\t')
				val++;
			if (nkvs >= capkvs)
			{
				const uint nc = capkvs ? capkvs * 2 : 16;
				kv_t *nn = REALLOC (kvs, nc * sizeof (*nn));
				if (!nn)
				{
					FREE (copy);
					for (uint k = 0; k < nkvs; k++)
					{
						FREE (kvs[k].name);
						FREE (kvs[k].value);
					}
					FREE (kvs);
					ResetLMJMP (jmp);
					return ERR_OUT_OF_MEMORY;
				}
				kvs = nn;
				capkvs = nc;
			}
			// attach directly to the current record by field name
			lmjmp_record_t *rec = jmp->records + jmp->n_records - 1;
			if (!rec->values)
			{
				rec->values = CALLOC (jmp->n_fields ? jmp->n_fields : 1, sizeof (*rec->values));
				if (!rec->values)
				{
					FREE (copy);
					for (uint k = 0; k < nkvs; k++)
					{
						FREE (kvs[k].name);
						FREE (kvs[k].value);
					}
					FREE (kvs);
					ResetLMJMP (jmp);
					return ERR_OUT_OF_MEMORY;
				}
			}
			uint fi = jmp->n_fields;
			for (uint i = 0; i < jmp->n_fields; i++)
				if (!strcmp (jmp->fields[i].name, name))
				{
					fi = i;
					break;
				}
			if (fi >= jmp->n_fields)
			{
				FREE (copy);
				for (uint k = 0; k < nkvs; k++)
				{
					FREE (kvs[k].name);
					FREE (kvs[k].value);
				}
				FREE (kvs);
				ResetLMJMP (jmp);
				return ERR_INVALID_DATA;
			}
			char unesc[256];
			lm_unescape (val, unesc, sizeof (unesc));
			FREE (rec->values[fi]);
			rec->values[fi] = lm_strdup (unesc);
			if (!rec->values[fi])
			{
				FREE (copy);
				for (uint k = 0; k < nkvs; k++)
				{
					FREE (kvs[k].name);
					FREE (kvs[k].value);
				}
				FREE (kvs);
				ResetLMJMP (jmp);
				return ERR_OUT_OF_MEMORY;
			}
		}
	}
	FREE (copy);
	for (uint k = 0; k < nkvs; k++)
	{
		FREE (kvs[k].name);
		FREE (kvs[k].value);
	}
	FREE (kvs);
	// defaults for missing values; record size from field extents
	for (uint r = 0; r < jmp->n_records; r++)
		for (uint i = 0; i < jmp->n_fields; i++)
			if (!jmp->records[r].values[i])
			{
				jmp->records[r].values[i] = lm_strdup ("0");
				if (!jmp->records[r].values[i])
				{
					ResetLMJMP (jmp);
					return ERR_OUT_OF_MEMORY;
				}
			}
	return ERR_OK;
}

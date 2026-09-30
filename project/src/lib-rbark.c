// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Harmonix Ark header parser; see lib-rbark.h for the format.
//-----------------------------------------------------------------------------
#include "lib-rbark.h"
#include "lib-std.h"
#include <string.h>
#include <stdlib.h>

#define RB_MAX_HDR 0x4000000u // 64 MiB, real headers are well under 20

static bool rb_known_version (u32 v)
{
	return (v >= 2 && v <= 7) || v == 9 || v == 10;
}

static u32 rb_le32 (const u8 *p)
{
	return (u32)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0];
}

static u64 rb_le64 (const u8 *p)
{
	return (u64)rb_le32 (p + 4) << 32 | rb_le32 (p);
}

// One step of the header stream cipher.
static s32 rb_step (s32 k)
{
	const s64 q = k / 0x1F31D;
	s64 v = (s64)(k - q * 0x1F31D) * 0x41A7 - q * 0xB14;
	if (v <= 0)
		v += 0x7FFFFFFF;
	return (s32)v;
}

// Bounds-checked sequential reader over the decrypted header.
typedef struct rb_rd_t
{
	const u8 *p;
	size_t size, pos;
	bool bad;
} rb_rd_t;

static u32 rd32 (rb_rd_t *r)
{
	if (r->pos + 4 > r->size)
	{
		r->bad = true;
		return 0;
	}
	const u32 v = rb_le32 (r->p + r->pos);
	r->pos += 4;
	return v;
}
static u64 rd64 (rb_rd_t *r)
{
	if (r->pos + 8 > r->size)
	{
		r->bad = true;
		return 0;
	}
	const u64 v = rb_le64 (r->p + r->pos);
	r->pos += 8;
	return v;
}
static void skip (rb_rd_t *r, u64 n)
{
	if (n > r->size - r->pos)
		r->bad = true;
	else
		r->pos += (size_t)n;
}
// length-prefixed string; returns a pointer into the header (not terminated)
static const u8 *rd_str (rb_rd_t *r, u32 *len)
{
	*len = rd32 (r);
	if (r->bad || *len > r->size - r->pos)
	{
		r->bad = true;
		*len = 0;
		return 0;
	}
	const u8 *s = r->p + r->pos;
	r->pos += *len;
	return s;
}

void RbArkFree (rbark_t *a)
{
	if (!a)
		return;
	for (uint i = 0; i < a->n; i++)
		FREE (a->e[i].path);
	FREE (a->e);
	memset (a, 0, sizeof (*a));
}

bool RbArkLocate (const rbark_t *a, u64 offset, uint *part, u64 *local)
{
	u64 base = 0;
	for (uint i = 0; i < a->n_parts; i++)
	{
		if (offset < base + a->part_size[i] || i == a->n_parts - 1)
		{
			*part = i;
			*local = offset - base;
			return offset >= base;
		}
		base += a->part_size[i];
	}
	return false;
}

// Decrypt a copy of the header if needed; returns malloc'd plaintext.
static u8 *rb_decrypt (const u8 *hdr, size_t size, size_t *out_size)
{
	if (size < 16 || size > RB_MAX_HDR)
		return 0;
	u8 *p = MALLOC (size);
	if (!p)
		return 0;
	memcpy (p, hdr, size);
	*out_size = size;

	u32 version = rb_le32 (p);
	if (rb_known_version (version))
		return p;

	s32 key = (s32)version;
	for (size_t i = 4; i < size; i++)
	{
		key = rb_step (key);
		p[i] ^= (u8)key;
	}
	version = rb_le32 (p + 4);
	if (!rb_known_version (version))
	{
		version ^= 0xFFFFFFFFu;
		if (!rb_known_version (version))
		{
			FREE (p);
			return 0;
		}
		for (size_t i = 4; i < size; i++)
			p[i] ^= 0xff;
	}
	// drop the key word so the version is first
	memmove (p, p + 4, size - 4);
	*out_size = size - 4;
	return p;
}

// Drop "." / ".." / empty components in place. A few members are stored as
// "../../system/run/..."; dropping the parent hops keeps them inside the
// extraction directory (system/run/...) instead of being refused.
static void rb_clean (char *path)
{
	char *out = path;
	for (char *in = path; *in;)
	{
		char *end = strchr (in, '/');
		const size_t len = end ? (size_t)(end - in) : strlen (in);
		const bool skip_it = !len || (len == 1 && in[0] == '.') || (len == 2 && in[0] == '.' && in[1] == '.');
		if (!skip_it)
		{
			if (out != path)
				*out++ = '/';
			memmove (out, in, len);
			out += len;
		}
		in += len;
		if (*in == '/')
			in++;
	}
	*out = 0;
}

static char *rb_join (const char *dir, const char *file)
{
	const size_t ld = strlen (dir), lf = strlen (file);
	char *s = MALLOC (ld + lf + 2);
	if (!s)
		return 0;
	if (ld)
	{
		memcpy (s, dir, ld);
		s[ld] = '/';
		memcpy (s + ld + 1, file, lf + 1);
	}
	else
		memcpy (s, file, lf + 1);
	return s;
}

enumError RbArkParse (rbark_t *a, const u8 *hdr, size_t size)
{
	memset (a, 0, sizeof (*a));
	size_t psize = 0;
	u8 *p = rb_decrypt (hdr, size, &psize);
	if (!p)
		return ERR_INVALID_DATA;

	rb_rd_t r = { p, psize, 0, false };
	const uint version = rd32 (&r);
	a->version = version;
	bool broken_v4 = false;

	if (version >= 6)
		skip (&r, (u64)rd32 (&r) << 4);

	rd32 (&r); // ark file count
	const u32 nsizes = rd32 (&r);
	if (r.bad || !nsizes || nsizes > RBARK_MAX_PARTS)
	{
		FREE (p);
		return ERR_INVALID_DATA;
	}
	a->n_parts = nsizes;
	if (version != 4)
		for (uint i = 0; i < nsizes; i++)
			a->part_size[i] = rd32 (&r);
	else
	{
		const size_t start = r.pos;
		for (uint i = 0; i < nsizes; i++)
			a->part_size[i] = rd64 (&r);
		if (a->part_size[nsizes - 1] > 0xFFFFFFFFull)
		{
			broken_v4 = true;
			r.pos = start;
			for (uint i = 0; i < nsizes; i++)
				a->part_size[i] = rd32 (&r);
		}
	}

	if (version >= 5 || broken_v4)
	{
		const u32 n = rd32 (&r);
		for (u32 i = 0; i < n && !r.bad; i++)
		{
			u32 l;
			rd_str (&r, &l);
		}
	}
	if (version >= 6 && version <= 9)
		skip (&r, (u64)rd32 (&r) << 2);
	if (version >= 7)
	{
		const u32 nc = rd32 (&r);
		for (u32 i = 0; i < nc && !r.bad; i++)
		{
			const u32 nf = rd32 (&r);
			for (u32 j = 0; j < nf && !r.bad; j++)
			{
				u32 l;
				rd_str (&r, &l);
			}
		}
	}
	if (r.bad)
	{
		FREE (p);
		return ERR_INVALID_DATA;
	}

	enumError err = ERR_OK;
	if (version <= 7)
	{
		const u32 blob = rd32 (&r);
		if (r.bad || blob > psize - r.pos)
		{
			FREE (p);
			return ERR_INVALID_DATA;
		}
		const u8 *sb = p + r.pos;
		r.pos += blob;
		const u32 nidx = rd32 (&r);
		if (r.bad || (u64)nidx * 4 > psize - r.pos)
		{
			FREE (p);
			return ERR_INVALID_DATA;
		}
		const u8 *idx = p + r.pos;
		r.pos += (size_t)nidx * 4;
		const u32 ne = rd32 (&r);
		const bool wide = version >= 4 && !broken_v4;
		const size_t esz = (wide ? 8 : 4) + 16;
		if (r.bad || (u64)ne * esz > psize - r.pos || ne > 0x1000000)
		{
			FREE (p);
			return ERR_INVALID_DATA;
		}
		a->e = CALLOC (ne ? ne : 1, sizeof (*a->e));
		if (!a->e)
		{
			FREE (p);
			return ERR_OUT_OF_MEMORY;
		}
		for (u32 i = 0; i < ne; i++)
		{
			const u64 off = wide ? rd64 (&r) : rd32 (&r);
			const u32 fi = rd32 (&r), di = rd32 (&r);
			const u32 sz = rd32 (&r), isz = rd32 (&r);
			if (fi >= nidx || di >= nidx)
			{
				err = ERR_INVALID_DATA;
				break;
			}
			const u32 fo = rb_le32 (idx + 4 * fi), dof = rb_le32 (idx + 4 * di);
			if (fo >= blob || dof >= blob)
			{
				err = ERR_INVALID_DATA;
				break;
			}
			// the blob is a run of NUL-terminated strings; bound each
			const char *fname = (const char *)sb + fo, *dname = (const char *)sb + dof;
			if (!memchr (fname, 0, blob - fo) || !memchr (dname, 0, blob - dof))
			{
				err = ERR_INVALID_DATA;
				break;
			}
			a->e[i].path = rb_join (dname, fname);
			if (a->e[i].path)
				rb_clean (a->e[i].path);
			a->e[i].offset = off;
			a->e[i].size = sz;
			a->e[i].inflated = isz;
			a->n++;
		}
	}
	else
	{
		const u32 ne = rd32 (&r);
		if (r.bad || ne > 0x1000000)
		{
			FREE (p);
			return ERR_INVALID_DATA;
		}
		a->e = CALLOC (ne ? ne : 1, sizeof (*a->e));
		if (!a->e)
		{
			FREE (p);
			return ERR_OUT_OF_MEMORY;
		}
		for (u32 i = 0; i < ne; i++)
		{
			const u64 off = rd64 (&r);
			u32 l;
			const u8 *s = rd_str (&r, &l);
			rd32 (&r); // flag
			const u32 sz = rd32 (&r);
			if (version <= 9)
				rd32 (&r);
			if (r.bad || !s)
			{
				err = ERR_INVALID_DATA;
				break;
			}
			a->e[i].path = MALLOC ((size_t)l + 1);
			if (a->e[i].path)
			{
				memcpy (a->e[i].path, s, l);
				a->e[i].path[l] = 0;
				rb_clean (a->e[i].path);
			}
			a->e[i].offset = off;
			a->e[i].size = sz;
			a->n++;
		}
	}
	FREE (p);
	if (err || !a->n)
	{
		RbArkFree (a);
		return err ? err : ERR_INVALID_DATA;
	}
	return ERR_OK;
}

bool IsRbArkHeader (const u8 *hdr, size_t size)
{
	rbark_t a;
	if (RbArkParse (&a, hdr, size))
		return false;
	// members must tile the parts: the summed sizes cannot exceed the parts
	u64 total = 0, parts = 0;
	for (uint i = 0; i < a.n; i++)
		total += a.e[i].size;
	for (uint i = 0; i < a.n_parts; i++)
		parts += a.part_size[i];
	const bool ok = total <= parts;
	RbArkFree (&a);
	return ok;
}

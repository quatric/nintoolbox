// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Grip Entertainment .res packages (see lib-gripres.h).
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-gripres.h"
#include "lib-excite.h"
#include <string.h>

#define RES_MAX_SECTIONS 0x10000
#define RES_MAX_TYPES 64

typedef struct
{
	char type[5];
	u32 off, size;
} res_sec_t;

static void res_type_name (char *dest, const u8 *fourcc)
{
	uint n = 0;
	for (uint i = 0; i < 4; i++)
		dest[n++] = fourcc[i] >= 0x21 && fourcc[i] < 0x7f ? fourcc[i] : ' ';
	dest[4] = 0;
	while (n && dest[n - 1] == ' ')
		dest[--n] = 0;
}

enumError ScanGripRES (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || size < 0x80 || memcmp (data, "res\n", 4))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u32 base = rd_be32 (data + 0x0c);
	const u32 toff = rd_be32 (data + 0x2c);
	const u32 tsize = rd_be32 (data + 0x30);
	const u32 ntypes = rd_be32 (data + 0x3c);
	if (ntypes > RES_MAX_TYPES || base < 0x40 + 8 * ntypes || base > size || toff > size
		|| (u64)toff + tsize != size || tsize < 8)
		return EINVAL;
	const u32 nsec = rd_be32 (data + toff);
	if (!nsec || nsec > RES_MAX_SECTIONS || 8 + 24ull * nsec > tsize)
		return EINVAL;

	res_sec_t *sec = CALLOC (nsec, sizeof (*sec));
	nintendo_sarc_entry_t *out = CALLOC (nsec + 2, sizeof (*out));
	if (!sec || !out)
	{
		FREE (sec);
		FREE (out);
		return ERR_CANT_CREATE;
	}
	for (uint i = 0; i < nsec; i++)
	{
		const u8 *e = data + toff + 8 + 24ull * i;
		res_type_name (sec[i].type, e);
		sec[i].off = rd_be32 (e + 4);
		sec[i].size = rd_be32 (e + 8);
		if ((u64)base + sec[i].off + sec[i].size > toff)
		{
			FREE (sec);
			FREE (out);
			return EINVAL;
		}
	}

	uint n = 0;
	bool ok = true;
	char name[64];
	for (uint i = 0; ok && i < nsec; i++)
	{
		const u8 *p = data + base + sec[i].off;
		if (!strcmp (sec[i].type, "strg"))
		{
			// NUL separated names -> one per line
			char *txt = MALLOC (sec[i].size + 1);
			if (!txt)
			{
				ok = false;
				break;
			}
			uint l = 0;
			for (uint k = 0; k < sec[i].size; k++)
			{
				if (p[k])
					txt[l++] = p[k];
				else if (l && txt[l - 1] != '\n')
					txt[l++] = '\n';
			}
			ok = OwnedEntryAdd (out, n, "strings.txt", (const u8 *)txt, l);
			FREE (txt);
			n += ok;
			continue;
		}
		if (!strcmp (sec[i].type, "indx"))
		{
			// "type offset name" per public resource
			const u32 cnt = sec[i].size >= 8 ? rd_be32 (p) : 0;
			if (cnt > 0x10000 || 8 + 12ull * cnt > sec[i].size)
				continue;
			char *txt = CALLOC (1, 32 + (size_t)cnt * 300);
			if (!txt)
				continue;
			size_t l = 0;
			for (uint k = 0; k < cnt; k++)
			{
				const u8 *e = p + 8 + 12ull * k;
				const s32 rel = (s32)rd_be32 (e);
				const s64 np = (s64)(e - data) + rel;
				if (np < 0 || np >= (s64)size)
					continue;
				char t[5];
				res_type_name (t, e + 4);
				const size_t avail = (size_t)size - (size_t)np;
				const int max_len = avail < 240 ? (int)avail : 240;
				l += snprintf (txt + l, 300, "%-5s 0x%08x %.*s\n", t, rd_be32 (e + 8),
					max_len, (const char *)data + np);
			}
			ok = OwnedEntryAdd (out, n, "index.txt", (const u8 *)txt, (uint)l);
			FREE (txt);
			n += ok;
			continue;
		}
		ccp ext = sec[i].type[0] ? sec[i].type : "bin";
		if (!strcmp (ext, "mdat"))
			ext = "fsb"; // FMOD sound bank
		else if (!strcmp (ext, "raw"))
			ext = "bin";
		else if (!strcmp (ext, "lua!"))
			ext = "luac"; // compiled Lua
		snprintf (name, sizeof (name), "%04u_%s.%s", i, sec[i].type[0] ? sec[i].type : "raw", ext);
		for (char *q = name; *q; q++)
			if (*q == '!')
				*q = '_';
		ok = OwnedEntryAdd (out, n, name, p, sec[i].size);
		n += ok;
	}
	FREE (sec);
	if (!ok || !n)
	{
		ResetOwnedEntries (out, n);
		return ok ? EINVAL : ERR_CANT_CREATE;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////			surf textures			///////////////
//-----------------------------------------------------------------------------

typedef struct
{
	uint w, h, gx, need, data;
} surf_t;

static bool surf_parse (surf_t *s, const u8 *d, uint size)
{
	if (!d || size < 0x60 || d[0] || d[1] || d[2] || d[3])
		return false;
	s->w = (uint)d[0x10] << 8 | d[0x11];
	s->h = (uint)d[0x12] << 8 | d[0x13];
	if (!s->w || !s->h || s->w > 2048 || s->h > 2048)
		return false;
	s->data = 0x40;
	switch (d[0x0f])
	{
		case 3: // 8 bit intensity, the pixel data follows a mip table
			s->gx = 1;
			s->need = ((s->w + 7) & ~7u) * ((s->h + 3) & ~3u);
			s->data = rd_be32 (d + 0x20);
			if (s->data < 0x40 || s->data > size)
				return false;
			break;
		case 2:
			s->gx = 14;
			s->need = ((s->w + 7) & ~7u) * ((s->h + 7) & ~7u) / 2;
			break;
		case 6:
			s->gx = 5;
			s->need = ((s->w + 3) & ~3u) * ((s->h + 3) & ~3u) * 2;
			break;
		default:
			return false;
	}
	return (u64)s->data + s->need <= size;
}

bool IsGripSurf (const u8 *data, uint size)
{
	surf_t s;
	return surf_parse (&s, data, size);
}

enumError DecodeGripSurf (u8 **rgba, uint *width, uint *height, const u8 *data, uint size)
{
	surf_t s;
	if (!surf_parse (&s, data, size))
		return ERR_NOTHING_TO_DO;
	u8 *img = 0;
	const enumError err = DecodeGXTexture_RGBA (&img, s.w, s.h, s.gx, data + s.data, s.need, 0, 0, 0);
	if (err)
		return err;
	// rows are stored bottom-up
	const uint row = s.w * 4;
	u8 *tmp = MALLOC (row);
	if (!tmp)
	{
		FREE (img);
		return ERR_OUT_OF_MEMORY;
	}
	for (uint y = 0; y < s.h / 2; y++)
	{
		u8 *a = img + (u64)y * row, *b = img + (u64)(s.h - 1 - y) * row;
		memcpy (tmp, a, row);
		memcpy (a, b, row);
		memcpy (b, tmp, row);
	}
	FREE (tmp);
	*rgba = img;
	*width = s.w;
	*height = s.h;
	return ERR_OK;
}

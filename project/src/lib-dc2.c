// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "DC2" engine assets (see lib-dc2.h).
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-dc2.h"
#include "lib-excite.h"
#include <string.h>

#define DCX_MAX_ENTRIES 0x100000
#define DCX_MAX_NAME 512

#define DCT_HEADER 0x3e
#define DCT_MAX_DIM 4096

enumError ScanDCX (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || size < 4 + 12)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u32 count = rd_be32 (data);
	if (!count || count > DCX_MAX_ENTRIES)
		return EINVAL;

	// Validate the whole directory first: the entries must chain exactly
	// from the end of the directory to the end of the file.
	u64 pos = 4, expect = 0;
	for (uint i = 0; i < count; i++)
	{
		if (pos + 4 > size)
			return EINVAL;
		const u32 nlen = rd_be32 (data + pos);
		if (!nlen || nlen > DCX_MAX_NAME || pos + 4 + nlen + 8 > size)
			return EINVAL;
		for (uint k = 0; k < nlen; k++)
			if (data[pos + 4 + k] < 0x20)
				return EINVAL;
		pos += 4 + nlen;
		const u32 off = rd_be32 (data + pos), fsize = rd_be32 (data + pos + 4);
		if (!i)
			expect = off;
		if (off != expect || (u64)off + fsize > size)
			return EINVAL;
		expect += fsize;
		pos += 8;
	}
	if (expect != size)
		return EINVAL;

	nintendo_sarc_entry_t *out = CALLOC (count, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	pos = 4;
	uint n = 0;
	for (uint i = 0; i < count; i++)
	{
		const u32 nlen = rd_be32 (data + pos);
		char name[DCX_MAX_NAME + 8];
		memcpy (name, data + pos + 4, nlen);
		name[nlen] = 0;
		pos += 4 + nlen;
		const u32 off = rd_be32 (data + pos), fsize = rd_be32 (data + pos + 4);
		pos += 8;

		for (char *p = name; *p; p++)
			if (*p == '\\')
				*p = '/';
		while (name[0] == '/')
			memmove (name, name + 1, strlen (name));
		if (!OwnedNameOk (name))
			snprintf (name, sizeof (name), "%04u.bin", i);
		for (uint k = 0; k < n; k++)
			if (!strcmp (out[k].name, name))
			{
				snprintf (name + strlen (name), 12, ".%u", i);
				break;
			}
		if (!OwnedEntryAdd (out, n, name, data + off, fsize))
		{
			ResetOwnedEntries (out, n);
			return ERR_CANT_CREATE;
		}
		n++;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////			.dct textures			///////////////
//-----------------------------------------------------------------------------

typedef struct
{
	uint w, h, fmt;
	const u8 *img;
	uint img_size;
} dct_t;

static bool dct_parse (dct_t *t, const u8 *d, uint size)
{
	if (!d || size < DCT_HEADER + 32 || memcmp (d, "DC2", 4))
		return false;
	t->w = rd_be32 (d + 0x1b);
	t->h = rd_be32 (d + 0x1f);
	const u32 mips = rd_be32 (d + 0x2b);
	const u32 top = rd_be32 (d + 0x3a);
	if (!t->w || !t->h || t->w > DCT_MAX_DIM || t->h > DCT_MAX_DIM || !mips || mips > 16
		|| rd_be32 (d + 0x23) != t->w || rd_be32 (d + 0x27) != t->h)
		return false;
	const u64 cmpr = ((u64)(t->w + 7) & ~7ull) * ((t->h + 7) & ~7ull) / 2;
	const u64 rgba = ((u64)(t->w + 3) & ~3ull) * ((t->h + 3) & ~3ull) * 4;
	if (top == cmpr)
		t->fmt = 14;
	else if (top == rgba)
		t->fmt = 6;
	else
		return false;
	if ((u64)DCT_HEADER + top > size)
		return false;
	t->img = d + DCT_HEADER;
	t->img_size = top;
	return true;
}

bool IsDCT (const u8 *data, uint size)
{
	dct_t t;
	return dct_parse (&t, data, size);
}

enumError DecodeDCT (u8 **rgba, uint *width, uint *height, const u8 *data, uint size)
{
	dct_t t;
	if (!dct_parse (&t, data, size))
		return ERR_NOTHING_TO_DO;
	const enumError err = DecodeGXTexture_RGBA (rgba, t.w, t.h, t.fmt, t.img, t.img_size, 0, 0, 0);
	if (!err)
	{
		*width = t.w;
		*height = t.h;
	}
	return err;
}

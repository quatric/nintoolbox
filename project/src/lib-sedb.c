// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Square Enix SEDB container scanner and texture decoder; see lib-sedb.h.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-sedb.h"
#include "lib-gxdec.h"
#include <string.h>

#define SEDB_MAX_RECORDS 0x10000
#define SEDB_RES_HEADER 0x30
#define SEDB_DDS_OFFSET 0x20
#define SEDB_PIXELS_OFFSET 0xa0

static u32 sedb_le32 (const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

static u32 sedb_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

bool IsSEDB (const u8 *data, uint size)
{
	if (!data || size < 0x20 || memcmp (data, "SEDB", 4))
		return false;
	const uint hdr = data[0x0e] | data[0x0f] << 8;
	return hdr >= 0x20 && hdr <= 0x100 && hdr <= size;
}

bool IsSEDBRes (const u8 *data, uint size)
{
	return IsSEDB (data, size) && size >= SEDB_RES_HEADER && !memcmp (data + 4, "RES ", 4);
}

bool IsSEDBTexture (const u8 *data, uint size)
{
	return IsSEDB (data, size) && size >= SEDB_PIXELS_OFFSET && !memcmp (data + 4, "txb\0", 4)
		&& !memcmp (data + SEDB_DDS_OFFSET, "DDS ", 4);
}

// Sub-file type tag of the member header ("txb", "shd", "RES "...) as a
// lower case extension; nested RES containers use ".rps" like top-level ones.
static void sedb_type_ext (char *dest, uint dest_size, const u8 *tag)
{
	uint o = 0;
	for (uint i = 0; i < 4 && tag[i] && o + 1 < dest_size; i++)
	{
		const char c = tag[i];
		if (c == ' ')
			continue;
		dest[o++] = c >= 'A' && c <= 'Z' ? c + 'a' - 'A'
			: (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ? c
										: '_';
	}
	dest[o] = 0;
	if (!strcmp (dest, "res"))
		strcpy (dest, "rps");
	if (!*dest)
		strcpy (dest, "bin");
}

enumError ScanSEDBRes (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!IsSEDBRes (data, size))
		return EINVAL;

	const u32 n = sedb_le32 (data + 0x20);
	if (n < 3 || n > SEDB_RES_HEADER + SEDB_MAX_RECORDS
		|| SEDB_RES_HEADER + 16ull * n > size)
		return EINVAL;
	const u64 base = SEDB_RES_HEADER + 16ull * n;
	const u8 *rec = data + SEDB_RES_HEADER;

	// The name table is the last record.
	const u32 name_off = sedb_le32 (rec + 16 * (n - 1) + 4);
	const u32 name_size = sedb_le32 (rec + 16 * (n - 1) + 8);
	if (base + name_off + name_size > size || name_size < 16ull * n)
		return EINVAL;
	const u8 *names = data + base + name_off;

	nintendo_sarc_entry_t *out = CALLOC (n, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint count = 0;
	for (uint i = 0; i < n - 2; i++)
	{
		const u32 off = sedb_le32 (rec + 16 * i + 4);
		const u32 fsize = sedb_le32 (rec + 16 * i + 8);
		if (base + off + fsize > size)
		{
			ResetOwnedEntries (out, count);
			return EINVAL;
		}
		if (fsize < 0x20 || memcmp (data + base + off, "SEDB", 4))
			continue; // string pool / padding record

		char stem[17], ext[8], name[64];
		memcpy (stem, names + 16 * i, 16);
		stem[16] = 0;
		for (char *p = stem; *p; p++)
			if (*p == '/' || *p == '\\' || (u8)*p < 0x20)
				*p = '_';
		if (!*stem)
			snprintf (stem, sizeof (stem), "%04u", i);
		sedb_type_ext (ext, sizeof (ext), data + base + off + 4);
		snprintf (name, sizeof (name), "%s.%s", stem, ext);

		// Names are not unique (e.g. a texture and its shader share one).
		for (uint k = 0; k < count; k++)
			if (!strcmp (out[k].name, name))
			{
				snprintf (name, sizeof (name), "%s_%u.%s", stem, i, ext);
				break;
			}
		if (!OwnedNameOk (name))
			snprintf (name, sizeof (name), "%04u.%s", i, ext);
		if (!OwnedEntryAdd (out, count, name, data + base + off, fsize))
		{
			ResetOwnedEntries (out, count);
			return ERR_CANT_CREATE;
		}
		count++;
	}
	if (!count)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = count;
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			texture decoding		///////////////
///////////////////////////////////////////////////////////////////////////////
//

enumError DecodeSEDBTexture (u8 **rgba, uint *width, uint *height, const u8 *data, uint size)
{
	if (!rgba || !IsSEDBTexture (data, size))
		return EINVAL;

	const u8 *dds = data + SEDB_DDS_OFFSET;
	const uint h = sedb_be32 (dds + 0x0c), w = sedb_be32 (dds + 0x10);
	const u32 pf_flags = sedb_be32 (dds + 0x50);
	const u32 fourcc = sedb_le32 (dds + 0x54);
	const u32 bits = sedb_be32 (dds + 0x58);
	if (!w || !h || w > 4096 || h > 4096)
		return EINVAL;
	const u8 *pix = data + SEDB_PIXELS_OFFSET;
	const u64 avail = size - SEDB_PIXELS_OFFSET;

	if (pf_flags == 4 && fourcc == 0x44585431) // "DXT1" -> GX CMPR
	{
		if ((w & 7) || (h & 7) || avail < (u64)w * h / 2)
			return ERR_NOTHING_TO_DO;
		u8 *out = GxDecodeCmprAlpha4 (pix, 0, w, h);
		if (!out)
			return ERR_CANT_CREATE;
		*rgba = out;
	}
	else if ((pf_flags == 0x20001 && bits == 16) || (pf_flags == 0x41 && bits == 32))
	{
		const bool rgba8 = bits == 32;
		const uint bpp = rgba8 ? 4 : 2;
		if (avail < (u64)w * h * bpp)
			return ERR_NOTHING_TO_DO;
		u8 *out = MALLOC ((size_t)w * h * 4);
		if (!out)
			return ERR_CANT_CREATE;
		for (size_t k = 0; k < (size_t)w * h; k++)
		{
			const u8 *p = pix + k * bpp;
			u8 *d = out + k * 4;
			if (rgba8)
				memcpy (d, p, 4);
			else
			{
				d[0] = d[1] = d[2] = p[0];
				d[3] = p[1];
			}
		}
		*rgba = out;
	}
	else
		return ERR_NOTHING_TO_DO;

	*width = w;
	*height = h;
	return ERR_OK;
}

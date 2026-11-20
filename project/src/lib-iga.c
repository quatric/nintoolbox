// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "IGA" archive scanner; see lib-iga.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-iga.h"
#include "lib-nintendo.h"
#include <string.h>

#define IGA_MAX_ENTRIES 0x100000

static u32 iga_rd32 (const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

// Version 11 (Skylanders Trap Team): header {magic, 11, toc size, count, 0x800, hash, ...} is 0x38 bytes, then u32 name_hash[count] (sorted) and
// {u32 offset, u32 id, u32 size, u32 flags}[count]; there are no names. flags -1 = stored, 0x20000000 = chunked LZMA with a little endian
// u16 chunk size (size = unpacked). Members are named by their hash and sniffed extension.
static enumError ScanIga11 (FILE *f, u64 file_size, stream_entry_t **entries, uint *n_entries)
{
	u8 h[0x30];
	if (fseeko (f, 0, SEEK_SET) || fread (h, 1, sizeof (h), f) != sizeof (h))
		return EINVAL;
	const u32 count = iga_rd32 (h + 12);
	const u64 table = 0x38 + 4ull * count, table_end = table + 16ull * count;
	if (!count || count > IGA_MAX_ENTRIES || table_end > file_size)
		return EINVAL;
	u8 *hs = MALLOC (table_end - 0x38 + 1);
	stream_entry_t *out = CALLOC (count, sizeof (*out));
	if (!hs || !out || fseeko (f, 0x38, SEEK_SET) || fread (hs, 1, table_end - 0x38, f) != table_end - 0x38)
	{
		FREE (hs);
		FREE (out);
		return EINVAL;
	}
	uint n = 0;
	enumError res = ERR_OK;
	for (uint i = 0; i < count; i++)
	{
		const u8 *r = hs + 4ull * count + 16ull * i;
		const u32 off = iga_rd32 (r), size = iga_rd32 (r + 8), flags = iga_rd32 (r + 12);
		const bool lzma = flags == 0x20000000u;
		if ((flags != 0xffffffffu && !lzma) || (u64)off >= file_size || (!lzma && (u64)off + size > file_size))
			continue;
		const char *ext = lzma ? "igz" : "bin"; // chunked members of the retail archives are all IGZ
		u8 m[4] = { 0 };
		if (!lzma && fseeko (f, off, SEEK_SET) == 0 && fread (m, 1, 4, f) == 4)
		{
			if (!memcmp (m, "IGZ\x01", 4))
				ext = "igz";
			else if (!memcmp (m, "FSB5", 4))
				ext = "fsb";
		}
		char name[64];
		snprintf (name, sizeof (name), "%08x.%s", iga_rd32 (hs + 4ull * i), ext);
		for (uint j = 0; j < n; j++)
			if (!strcmp (out[j].name, name))
			{
				snprintf (name, sizeof (name), "%08x_%05u.%s", iga_rd32 (hs + 4ull * i), i, ext);
				break;
			}
		if (!StreamEntryAdd (out, n, name, off, size))
		{
			res = ERR_CANT_CREATE;
			break;
		}
		if (lzma)
			out[n].codec = STREAM_CODEC_IGA11_LZMA;
		n++;
	}
	FREE (hs);
	if (res || !n)
	{
		FreeStreamEntries (out, n);
		return res ? res : EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

enumError ScanIga (FILE *f, u64 file_size, stream_entry_t **entries, uint *n_entries)
{
	if (!f || !entries || !n_entries || file_size < 0x40)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	u8 h[0x30];
	if (fseeko (f, 0, SEEK_SET) || fread (h, 1, sizeof (h), f) != sizeof (h) || memcmp (h, "IGA\x1a", 4)
		|| (iga_rd32 (h + 4) != 4 && iga_rd32 (h + 4) != 2 && iga_rd32 (h + 4) != 11))
		return EINVAL;
	if (iga_rd32 (h + 4) == 11)
		return ScanIga11 (f, file_size, entries, n_entries);
	const u32 count = iga_rd32 (h + 12);
	const u64 names_off = iga_rd32 (h + 24), names_size = iga_rd32 (h + 28);
	const u64 table = 0x30 + 4ull * count, table_end = table + 12ull * count;
	if (!count || count > IGA_MAX_ENTRIES || table_end > file_size || names_off < table_end
		|| names_size < 4ull * count || names_off + names_size > file_size)
		return EINVAL;

	u8 *tab = MALLOC (table_end - table + 1);
	u8 *nm = MALLOC (names_size + 1);
	stream_entry_t *out = CALLOC (count, sizeof (*out));
	if (!tab || !nm || !out
		|| fseeko (f, table, SEEK_SET) || fread (tab, 1, table_end - table, f) != table_end - table
		|| fseeko (f, names_off, SEEK_SET) || fread (nm, 1, names_size, f) != names_size)
	{
		FREE (tab);
		FREE (nm);
		FREE (out);
		return EINVAL;
	}
	nm[names_size] = 0;

	uint n = 0;
	enumError res = ERR_OK;
	for (uint i = 0; i < count; i++)
	{
		const u8 *r = tab + 12ull * i;
		const u32 off = iga_rd32 (r), size = iga_rd32 (r + 4);
		const u32 no = iga_rd32 (nm + 4ull * i);
		const u32 flags = iga_rd32 (r + 8);
		// 0x1000000N: chunked LZMA, size is the unpacked size
		const bool lzma = (flags & 0xf0000000u) == 0x10000000u;
		if (lzma ? (u64)off + 7 > file_size : (u64)off + size > file_size)
			continue;
		if (no >= names_size)
			continue;
		const char *s = (const char *)nm + no;
		// Drop a "c:/" drive prefix and any leading slashes; names are build paths.
		if (s[0] && s[1] == ':')
			s += 2;
		while (*s == '/')
			s++;
		char name[400];
		if (strlen (s) + 8 >= sizeof (name) || !OwnedNameOk (s))
			snprintf (name, sizeof (name), "%05u.bin", i);
		else
			snprintf (name, sizeof (name), "%s", s);
		for (uint j = 0; j < n; j++)
			if (!strcmp (out[j].name, name))
			{
				snprintf (name, sizeof (name), "%05u.bin", i);
				break;
			}
		if (!StreamEntryAdd (out, n, name, off, size))
		{
			res = ERR_CANT_CREATE;
			break;
		}
		if (lzma)
			out[n].codec = STREAM_CODEC_IGA_LZMA;
		n++;
	}
	FREE (tab);
	FREE (nm);
	if (res || !n)
	{
		FreeStreamEntries (out, n);
		return res ? res : EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

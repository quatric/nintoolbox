// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Ubisoft Magma "ABE" BigFile scanner; see lib-abebf.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-abebf.h"
#include "lib-nintendo.h"
#include <string.h>

#define ABE_REC_SIZE 200
#define ABE_NAME_LEN 0x50
#define ABE_MAX_SLOTS 0x100000
#define ABE_MAX_CHUNKS 4096

static u32 abe_rd32 (const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

static bool abe_read (FILE *f, u64 file_size, u64 off, void *buf, size_t len)
{
	return off <= file_size && len <= file_size - off && !fseeko (f, (off_t)off, SEEK_SET)
		&& fread (buf, 1, len, f) == len;
}

static void abe_name (char *out, size_t out_size, const u8 *rec)
{
	char a[ABE_NAME_LEN + 1];
	memcpy (a, rec, ABE_NAME_LEN);
	a[ABE_NAME_LEN] = 0;
	size_t l = strlen (a);
	// "xxxxxxxx\0ext": the extension follows the first NUL.
	const char *ext = l + 1 < ABE_NAME_LEN ? a + l + 1 : "";
	if (!strchr (a, '.') && *ext && strlen (ext) <= 8)
		snprintf (out, out_size, "%s.%s", a, ext);
	else
		snprintf (out, out_size, "%s", a);
	for (char *p = out; *p; p++)
		if (*p == '/' || *p == '\\' || *p == ':' || (u8)*p < 0x20)
			*p = '_';
	// "xxxxxxx." (extension stripped): give it a neutral one.
	l = strlen (out);
	if (l && l + 4 < out_size && out[l - 1] == '.')
		strcpy (out + l, "bin");
}

enumError ScanAbeBf (FILE *f, u64 file_size, stream_entry_t **entries, uint *n_entries)
{
	if (!f || !entries || !n_entries || file_size < 0x1000)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	u8 h[0x30];
	if (!abe_read (f, file_size, 0, h, sizeof (h)) || memcmp (h, "ABE\0", 4)
		|| abe_rd32 (h + 4) != 4)
		return EINVAL;
	const u32 total = abe_rd32 (h + 8);
	u64 chunk = abe_rd32 (h + 0x18);
	if (!total || total > ABE_MAX_SLOTS * 16u || chunk < 0x30 || chunk + 12 > file_size)
		return EINVAL;

	uint cap = total < 1024 ? 1024 : total;
	if (cap > 0x400000)
		cap = 0x400000;
	stream_entry_t *out = CALLOC (cap, sizeof (*out));
	u8 *recs = MALLOC ((size_t)ABE_REC_SIZE * 4096);
	if (!out || !recs)
	{
		FREE (out);
		FREE (recs);
		return ERR_CANT_CREATE;
	}

	uint n = 0, n_chunks = 0;
	enumError res = ERR_OK;
	while (chunk && n_chunks++ < ABE_MAX_CHUNKS && !res)
	{
		u8 ch[12];
		if (!abe_read (f, file_size, chunk, ch, sizeof (ch)))
			break;
		const u32 slots = abe_rd32 (ch), flag = abe_rd32 (ch + 4), next = abe_rd32 (ch + 8);
		if (!slots || slots > ABE_MAX_SLOTS)
			break;
		for (u32 base = 0; base < slots && !res; base += 4096)
		{
			const u32 cnt = slots - base < 4096 ? slots - base : 4096;
			if (!abe_read (f, file_size, chunk + 12 + (u64)base * ABE_REC_SIZE, recs,
					(size_t)cnt * ABE_REC_SIZE))
				break;
			for (u32 i = 0; i < cnt; i++)
			{
				const u8 *r = recs + (size_t)i * ABE_REC_SIZE;
				if (!r[0])
					continue;
				const u64 off = abe_rd32 (r + 0x6c);
				u8 dh[16];
				if (off + 0x20 > file_size || !abe_read (f, file_size, off, dh, sizeof (dh)))
					continue;
				const u32 stored = abe_rd32 (dh), unpacked = abe_rd32 (dh + 4),
					  type = abe_rd32 (dh + 12);
				if (off + 0x20 + stored > file_size)
					continue;
				u8 codec;
				if (type == 2 && stored == unpacked)
					codec = STREAM_CODEC_RAW;
				else if (type == 4 && stored > 8 && unpacked)
					codec = STREAM_CODEC_ABE_LZO;
				else
					continue; // shadow reference or an unrelated record kind

				char name[ABE_NAME_LEN + 16];
				abe_name (name, sizeof (name), r);
				if (!OwnedNameOk (name))
					snprintf (name, sizeof (name), "%08x.bin", abe_rd32 (r + 0x64));
				for (uint j = 0; j < n; j++)
					if (!strcmp (out[j].name, name))
					{
						char alt[ABE_NAME_LEN + 32];
						snprintf (alt, sizeof (alt), "%s.%08x", name, abe_rd32 (r + 0x64));
						snprintf (name, sizeof (name), "%s", alt);
						break;
					}
				if (n == cap)
				{
					cap = cap > 0x200000 ? 0x400000 : cap * 2;
					if (n >= cap)
					{
						res = ERR_CANT_CREATE;
						break;
					}
					stream_entry_t *bigger = REALLOC (out, (size_t)cap * sizeof (*out));
					if (!bigger)
					{
						res = ERR_CANT_CREATE;
						break;
					}
					out = bigger;
					memset (out + n, 0, (size_t)(cap - n) * sizeof (*out));
				}
				if (!StreamEntryAdd (out, n, name, off + 0x20, unpacked))
				{
					res = ERR_CANT_CREATE;
					break;
				}
				out[n].codec = codec;
				n++;
			}
		}
		chunk = (flag == 0 && next > chunk && next + 12 <= file_size && next != 0xaaaaaaaau) ? next : 0;
	}
	FREE (recs);
	if (res || !n)
	{
		FreeStreamEntries (out, n);
		return res ? res : EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

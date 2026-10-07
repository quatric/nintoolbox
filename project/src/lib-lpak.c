// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// 2XL Games LPAK scanner; see lib-lpak.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-lpak.h"
#include <string.h>
#include <zlib.h>

#define LPAK_MAX_ENTRIES 0x100000
#define LPAK_MAX_UNPACKED (512u << 20)

static u32 lp_rd32 (const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

// Inflate consecutive zlib streams until `want` bytes are produced or the
// input is used up. Returns the number of bytes written, 0 on failure.
static size_t lp_inflate (const u8 *in, size_t in_size, u8 *out, size_t want)
{
	size_t done = 0;
	while (in_size && done < want)
	{
		z_stream z;
		memset (&z, 0, sizeof (z));
		if (inflateInit (&z) != Z_OK)
			return 0;
		z.next_in = (Bytef *)in;
		z.avail_in = (uInt)in_size;
		z.next_out = out + done;
		z.avail_out = (uInt)(want - done);
		const int rc = inflate (&z, Z_FINISH);
		const size_t used = z.total_in, made = z.total_out;
		inflateEnd (&z);
		if ((rc != Z_STREAM_END && rc != Z_BUF_ERROR) || !used)
			return done;
		in += used;
		in_size -= used;
		done += made;
	}
	return done;
}

enumError ScanLpak (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size)
{
	if (!entries || !n_entries || !data || size < 0x20 || memcmp (data + 4, "RIFF", 4)
		|| memcmp (data + 12, "LPAK", 4))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;
	u64 end = 8 + (u64)lp_rd32 (data + 8) + 4;
	if (end > size)
		end = size;

	uint cap = 256, n = 0;
	nintendo_sarc_entry_t *out = CALLOC (cap, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	u64 pos = 16;
	while (pos + 12 <= end)
	{
		const u32 csz = lp_rd32 (data + pos + 4);
		const u64 next = pos + 8 + csz + (csz & 1);
		if (memcmp (data + pos, "LIST", 4) || csz < 4 || pos + 8 + csz > end)
			break;

		const u8 *file = 0, *str = 0;
		u32 file_size = 0, str_size = 0;
		for (u64 sub = pos + 12; sub + 8 <= pos + 8 + csz;)
		{
			const u32 sz = lp_rd32 (data + sub + 4);
			if (sub + 8 + sz > pos + 8 + csz)
				break;
			if (!memcmp (data + sub, "file", 4))
				file = data + sub + 8, file_size = sz;
			else if (!memcmp (data + sub, "str ", 4))
				str = data + sub + 8, str_size = sz;
			sub += 8 + sz + (sz & 1);
		}
		pos = next;
		if (!file || file_size < 12 || !str_size)
			continue;
		const u32 usz = lp_rd32 (file + 4), hdr = lp_rd32 (file + 8);
		if (hdr < 12 || hdr > file_size || hdr % 4 || usz > LPAK_MAX_UNPACKED)
			continue;

		// Path: components joined by '/', dropping the empty tail.
		char path[512];
		size_t pl = 0;
		for (u32 i = 0; i < str_size && pl + 2 < sizeof (path);)
		{
			const char *c = (const char *)str + i;
			const size_t cl = strnlen (c, str_size - i);
			if (cl)
			{
				if (pl)
					path[pl++] = '/';
				if (pl + cl + 1 >= sizeof (path))
					break;
				memcpy (path + pl, c, cl);
				pl += cl;
			}
			i += cl + 1;
		}
		path[pl] = 0;
		if (!OwnedNameOk (path))
			snprintf (path, sizeof (path), "%05u.bin", n);

		const u8 *body = file + hdr;
		const size_t body_size = file_size - hdr;
		u8 *buf = 0;
		const u8 *payload = body;
		size_t payload_size = body_size;
		if (body_size > 2 && body[0] == 0x78 && usz)
		{
			buf = MALLOC (usz);
			const size_t got = buf ? lp_inflate (body, body_size, buf, usz) : 0;
			if (got)
			{
				payload = buf;
				payload_size = got;
			}
		}

		if (n == cap)
		{
			cap *= 2;
			if (cap > LPAK_MAX_ENTRIES)
			{
				FREE (buf);
				break;
			}
			nintendo_sarc_entry_t *bigger = REALLOC (out, cap * sizeof (*out));
			if (!bigger)
			{
				FREE (buf);
				ResetOwnedEntries (out, n);
				return ERR_CANT_CREATE;
			}
			out = bigger;
			memset (out + n, 0, (cap - n) * sizeof (*out));
		}
		const bool ok = OwnedEntryAdd (out, n, path, payload, (uint)payload_size);
		FREE (buf);
		if (!ok)
		{
			ResetOwnedEntries (out, n);
			return ERR_CANT_CREATE;
		}
		n++;
	}
	if (!n)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

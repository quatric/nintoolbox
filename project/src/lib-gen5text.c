// SPDX-License-Identifier: GPL-2.0+
#include "lib-gen5text.h"
#include "lib-sbuf.h"

static uint g5_le16 (const u8 *p) { return p[0] | p[1] << 8; }
static uint g5_le32 (const u8 *p) { return (uint)p[0] | (uint)p[1] << 8 | (uint)p[2] << 16 | (uint)p[3] << 24; }
static uint g5_rol3 (uint k) { return ((k << 3) | (k >> 13)) & 0xffff; }

// Checks the structure; returns the number of blocks (0 = not a message archive).
static uint g5_scan (const u8 *d, size_t size, uint *entries)
{
	if (size < 16)
		return 0;
	const uint nb = g5_le16 (d), ne = g5_le16 (d + 2);
	if (nb < 1 || nb > 16 || !ne || g5_le32 (d + 8) || 12 + 4 * (size_t)nb > size)
		return 0;
	for (uint b = 0; b < nb; b++)
	{
		const size_t bo = g5_le32 (d + 12 + 4 * b);
		if (bo < 12 + 4 * (size_t)nb || bo + 4 + 8 * (size_t)ne > size || g5_le32 (d + bo) > size - bo)
			return 0;
		for (uint i = 0; i < ne; i++)
		{
			const u8 *e = d + bo + 4 + 8 * i;
			const size_t off = g5_le32 (e), len = g5_le16 (e + 4);
			if (!len || off > size - bo || 2 * len > size - bo - off)
				return 0;
			uint key = (0x7c89 + 0x2983 * i) & 0xffff;
			for (uint k = 0; k < len; k++)
			{
				const uint w = g5_le16 (d + bo + off + 2 * k) ^ key;
				key = g5_rol3 (key);
				if (k + 1 == len && w != 0xffff)
					return 0;
			}
		}
	}
	*entries = ne;
	return nb;
}

bool IsGen5Text (const u8 *d, size_t size)
{
	uint ne;
	return g5_scan (d, size, &ne) != 0;
}

enumError DecodeGen5Text (const u8 *d, size_t size, char **text, size_t *text_size)
{
	uint ne;
	const uint nb = g5_scan (d, size, &ne);
	if (!nb)
		return ERR_INVALID_DATA;
	sbuf_t b = { 0 };
	sb_puts (&b, "#gen5text\n");
	for (uint blk = 0; blk < nb; blk++)
	{
		const size_t bo = g5_le32 (d + 12 + 4 * blk);
		sb_printf (&b, "@block %u\n", blk);
		for (uint i = 0; i < ne; i++)
		{
			const u8 *e = d + bo + 4 + 8 * i;
			const size_t off = g5_le32 (e), len = g5_le16 (e + 4);
			const uint unk = g5_le16 (e + 6);
			if (unk)
				sb_printf (&b, "%u:%u\t", i, unk);
			else
				sb_printf (&b, "%u\t", i);
			uint key = (0x7c89 + 0x2983 * i) & 0xffff;
			for (uint k = 0; k + 1 < len; k++)
			{
				const uint w = g5_le16 (d + bo + off + 2 * k) ^ key;
				key = g5_rol3 (key);
				if (w < 0x20 || w == '\\' || (w >= 0x7f && w < 0xa0) || w >= 0xd800)
					sb_printf (&b, "\\u%04x", w);
				else if (w < 0x80)
					sb_write (&b, &(char){ (char)w }, 1);
				else
				{
					char u[3];
					if (w < 0x800)
						u[0] = (char)(0xc0 | w >> 6), u[1] = (char)(0x80 | (w & 63));
					else
						u[0] = (char)(0xe0 | w >> 12), u[1] = (char)(0x80 | ((w >> 6) & 63)), u[2] = (char)(0x80 | (w & 63));
					sb_write (&b, u, w < 0x800 ? 2 : 3);
				}
			}
			sb_puts (&b, "\n");
		}
	}
	if (b.fail)
	{
		FREE (b.d);
		return ERR_OUT_OF_MEMORY;
	}
	*text = b.d;
	*text_size = b.n;
	return ERR_OK;
}

typedef struct g5_entry_t
{
	uint unk;
	uint *w; // characters without the terminator
	uint n;
} g5_entry_t;

static void g5_put16 (u8 *p, uint v) { p[0] = (u8)v, p[1] = (u8)(v >> 8); }
static void g5_put32 (u8 *p, uint v) { g5_put16 (p, v & 0xffff), g5_put16 (p + 2, v >> 16); }

enumError EncodeGen5Text (const char *text, size_t size, u8 **bin, size_t *bin_size)
{
	enumError err = ERR_OK;
	g5_entry_t *ent = 0;
	size_t nent = 0, cap = 0;
	uint nb = 0, per = 0, cur = 0;
	uint blk_start[17] = { 0 };
	const char *p = text, *end = text + size;
	while (p < end && !err)
	{
		const char *nl = memchr (p, '\n', (size_t)(end - p));
		const char *le = nl ? nl : end;
		if (le > p && le[-1] == '\r')
			le--;
		if (p == le || *p == '#')
			;
		else if (!strncmp (p, "@block ", 7))
		{
			if (nb == 1)
				per = cur;
			else if (nb > 1 && cur != per)
				err = ERROR0 (ERR_INVALID_DATA, "Gen5 text: blocks differ in entry count\n");
			if (nb >= 16)
				err = ERROR0 (ERR_INVALID_DATA, "Gen5 text: too many blocks\n");
			else
				blk_start[nb++] = (uint)nent;
			cur = 0;
		}
		else
		{
			const char *tab = memchr (p, '\t', (size_t)(le - p));
			if (!tab || !nb)
			{
				err = ERROR0 (ERR_INVALID_DATA, "Gen5 text: bad line\n");
				break;
			}
			if (nent == cap)
			{
				cap = cap ? 2 * cap : 256;
				ent = REALLOC (ent, cap * sizeof (*ent));
				if (!ent)
				{
					err = ERR_OUT_OF_MEMORY;
					break;
				}
			}
			g5_entry_t *e = ent + nent++;
			cur++;
			char *colon = memchr (p, ':', (size_t)(tab - p));
			e->unk = colon ? (uint)strtoul (colon + 1, 0, 10) : 0;
			e->w = MALLOC ((size_t)(le - tab) * sizeof (uint) + 4);
			e->n = 0;
			if (!e->w)
			{
				err = ERR_OUT_OF_MEMORY;
				break;
			}
			for (const char *q = tab + 1; q < le;)
			{
				const u8 c = (u8)*q;
				uint w;
				if (c == '\\' && q + 6 <= le && q[1] == 'u')
				{
					char hex[5] = { q[2], q[3], q[4], q[5], 0 };
					w = (uint)strtoul (hex, 0, 16), q += 6;
				}
				else if (c < 0x80)
					w = c, q++;
				else if ((c & 0xe0) == 0xc0 && q + 2 <= le)
					w = (uint)(c & 31) << 6 | (q[1] & 63), q += 2;
				else if ((c & 0xf0) == 0xe0 && q + 3 <= le)
					w = (uint)(c & 15) << 12 | (uint)(q[1] & 63) << 6 | (q[2] & 63), q += 3;
				else
				{
					err = ERROR0 (ERR_INVALID_DATA, "Gen5 text: unsupported character\n");
					break;
				}
				e->w[e->n++] = w;
			}
		}
		p = nl ? nl + 1 : end;
	}
	if (!err && nb == 1)
		per = cur;
	else if (!err && nb > 1 && cur != per)
		err = ERROR0 (ERR_INVALID_DATA, "Gen5 text: blocks differ in entry count\n");
	if (!err && (!nb || !per || per > 0xffff))
		err = ERROR0 (ERR_INVALID_DATA, "Gen5 text: no entries\n");
	u8 *out = 0;
	size_t on = 0;
	if (!err)
	{
		size_t total = 12 + 4 * (size_t)nb;
		for (size_t i = 0; i < nent; i++)
			total += 8 + 2 * (size_t)(ent[i].n + 1);
		total += 4 * (size_t)nb + 4 * 4;
		out = CALLOC (1, total + 64);
		if (!out)
			err = ERR_OUT_OF_MEMORY;
	}
	if (!err)
	{
		size_t pos = 12 + 4 * (size_t)nb;
		uint biggest = 0;
		for (uint b = 0; b < nb; b++)
		{
			const size_t bo = pos;
			g5_put32 (out + 12 + 4 * b, (uint)bo);
			size_t sp = bo + 4 + 8 * (size_t)per;
			for (uint i = 0; i < per; i++)
			{
				const g5_entry_t *e = ent + blk_start[b] + i;
				uint key = (0x7c89 + 0x2983 * i) & 0xffff;
				u8 *t = out + bo + 4 + 8 * i;
				g5_put32 (t, (uint)(sp - bo));
				g5_put16 (t + 4, e->n + 1);
				g5_put16 (t + 6, e->unk);
				for (uint k = 0; k <= e->n; k++)
				{
					g5_put16 (out + sp, (k < e->n ? e->w[k] : 0xffff) ^ key);
					key = g5_rol3 (key), sp += 2;
				}
			}
			sp = bo + ((sp - bo + 3) & ~(size_t)3);
			g5_put32 (out + bo, (uint)(sp - bo));
			if (sp - bo > biggest)
				biggest = (uint)(sp - bo);
			pos = sp;
		}
		g5_put16 (out, nb);
		g5_put16 (out + 2, per);
		g5_put32 (out + 4, biggest);
		on = pos;
	}
	for (size_t i = 0; i < nent; i++)
		FREE (ent[i].w);
	FREE (ent);
	if (err)
	{
		FREE (out);
		return err;
	}
	*bin = out;
	*bin_size = on;
	return ERR_OK;
}

#include "lib-std.h"
#include "lib-szs.h"
#include "lib-lzo.h"
#include "lib-rpak.h"
#include <string.h>

void ResetRPAK (rpak_t *pak)
{
	if (!pak)
		return;
	FREE (pak->entries);
	memset (pak, 0, sizeof (*pak));
}

enumError ScanRPAK (rpak_t *pak, const u8 *data, uint size)
{
	if (!pak || !data || size < 0x84)
		return ERR_NOTHING_TO_DO;

	memset (pak, 0, sizeof (*pak));

	const u32 strg_length = rd_be32 (data + 0x48);
	const u32 rshd_length = rd_be32 (data + 0x50);
	if (!strg_length && !rshd_length)
		return ERR_NOTHING_TO_DO;

	const u64 rshd_hdr_off = (u64)0x80 + strg_length;
	if (rshd_hdr_off + 4 > size)
		return ERR_NOTHING_TO_DO;
	const u32 n = rd_be32 (data + rshd_hdr_off);
	if (!n || n > 0x1000000)
		return ERR_NOTHING_TO_DO;

	const u64 table_off = rshd_hdr_off + 4;
	const u64 table_size = (u64)n * 24;
	if (table_off + table_size > size)
		return ERR_NOTHING_TO_DO;

	const u64 data_base = (u64)0x80 + strg_length + rshd_length;

	rpak_entry_t *entries = CALLOC (n, sizeof (*entries));
	if (!entries)
		return ERR_CANT_CREATE;

	for (uint i = 0; i < n; i++)
	{
		const u8 *h = data + table_off + i * 24;
		const u32 compressed = rd_be32 (h);
		const u32 magic = rd_be32 (h + 4);
		const u32 id_hi = rd_be32 (h + 8);
		const u32 id_lo = rd_be32 (h + 0xc);
		const u32 dlen = rd_be32 (h + 0x10);
		const u32 ptr = rd_be32 (h + 0x14);

		const u64 off = data_base + ptr;
		if (off + dlen > size)
			continue;

		entries[i].data = data + off;
		entries[i].size = dlen;
		entries[i].magic = magic;
		entries[i].id_hi = id_hi;
		entries[i].id_lo = id_lo;
		entries[i].compressed = compressed != 0;
	}

	for (uint i = 0; i < n; i++)
		if (!entries[i].data)
		{
			FREE (entries);
			return ERR_NOTHING_TO_DO;
		}

	pak->data = data;
	pak->size = size;
	pak->entries = entries;
	pak->n_entries = n;
	return ERR_OK;
}

u8 *DecompressRPAKEntry (const u8 *data, uint size, uint *res_size)
{
	if (!data || size < 8 || memcmp (data, "CMPD", 4))
		return 0;

	const u32 blocks = rd_be32 (data + 4);
	if (blocks > 0x100000 || (u64)8 + (u64)blocks * 8 > size)
		return 0;

	u64 pos = 8 + (u64)blocks * 8;
	u64 total = 0;
	for (uint i = 0; i < blocks; i++)
	{
		const u8 *bh = data + 8 + i * 8;
		const u32 stored = ((u32)bh[1] << 16) | ((u32)bh[2] << 8) | bh[3];
		const u32 usize = rd_be32 (bh + 4);
		if (pos + stored > size || total + usize < total)
			return 0;
		pos += stored;
		total += usize;
	}
	if (total > (256u << 20))
		return 0;

	u8 *out = MALLOC (total);
	if (!out)
		return 0;

	pos = 8 + (u64)blocks * 8;
	u64 opos = 0;
	for (uint i = 0; i < blocks; i++)
	{
		const u8 *bh = data + 8 + i * 8;
		const u32 stored = ((u32)bh[1] << 16) | ((u32)bh[2] << 8) | bh[3];
		const u32 usize = rd_be32 (bh + 4);

		if (stored == usize)
			memcpy (out + opos, data + pos, usize);
		else
		{
			uint sp = 0, so = 0;
			bool segmented = true;
			while (sp < stored)
			{
				if (stored - sp < 2)
				{
					segmented = false;
					break;
				}
				const uint word = (uint)data[pos + sp] << 8 | data[pos + sp + 1];
				sp += 2;
				const bool raw = (word & 0x8000) != 0;
				const uint sn = raw ? (0x10000 - word) : word;
				if (!sn || sn > stored - sp)
				{
					segmented = false;
					break;
				}
				if (raw)
				{
					if (sn > usize - so)
					{
						segmented = false;
						break;
					}
					memcpy (out + opos + so, data + pos + sp, sn);
					so += sn;
				}
				else
				{
					u8 *dec = 0;
					uint dec_size = 0;
					const u8 *seg = data + pos + sp;
					const bool zlib = sn >= 2 && seg[0] == 0x78
						&& (seg[1] == 0x01 || seg[1] == 0x9c || seg[1] == 0xda);
					const enumError derr = zlib ? DecodeZlibGrow (&dec, &dec_size, seg, sn)
												: DecodeLZO1XGrow (&dec, &dec_size, seg, sn);
					if (derr != ERR_OK || dec_size > usize - so)
					{
						FREE (dec);
						segmented = false;
						break;
					}
					memcpy (out + opos + so, dec, dec_size);
					so += dec_size;
					FREE (dec);
				}
				sp += sn;
			}
			if (segmented && sp == stored && so == usize)
				;
			else
			{
				u8 *dec = 0;
				uint dec_size = 0;
				if (DecodeZlibGrow (&dec, &dec_size, data + pos, stored) != ERR_OK
					|| dec_size != usize)
				{
					FREE (dec);
					FREE (out);
					return 0;
				}
				memcpy (out + opos, dec, usize);
				FREE (dec);
			}
		}
		pos += stored;
		opos += usize;
	}

	*res_size = total;
	return out;
}

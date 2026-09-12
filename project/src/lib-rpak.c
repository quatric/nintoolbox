#include "lib-std.h"
#include "lib-szs.h"
#include "lib-lzo.h"
#include "lib-rpak.h"
#include <string.h>
#include <zlib.h>

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

//-----------------------------------------------------------------------------

// CMPD-wrap one entry's payload as a single block, matching the layout
// DecompressRPAKEntry() parses: "CMPD" + block_count(1) + one 8-byte block
// header (flag byte + 24-bit BE stored_size + 32-bit BE uncompressed_size)
// followed by the stored bytes. Falls back to a raw stored block (flag 0x00,
// stored_size==uncompressed_size) whenever zlib doesn't shrink the data, the
// same mix of stored/compressed blocks real retail archives carry.
static u8 *CompressRPAKEntry (const u8 *data, uint size, uint *res_size)
{
	u8 *zdata = 0;
	uint zsize = 0;
	if (size)
	{
		z_stream strm;
		memset (&strm, 0, sizeof (strm));
		if (deflateInit2 (&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY)
			== Z_OK)
		{
			uLongf bound = compressBound ((uLong)size) + 64;
			zdata = MALLOC (bound);
			if (zdata)
			{
				strm.next_in = (Bytef *)data;
				strm.avail_in = size;
				strm.next_out = zdata;
				strm.avail_out = (uInt)bound;
				if (deflate (&strm, Z_FINISH) == Z_STREAM_END)
					zsize = (uint)strm.total_out;
				else
				{
					FREE (zdata);
					zdata = 0;
				}
			}
			deflateEnd (&strm);
		}
	}

	const bool use_zlib = zdata && zsize < size;
	const u8 *stored = use_zlib ? zdata : data;
	const uint stored_size = use_zlib ? zsize : size;

	u8 *out = MALLOC (8 + 8 + stored_size);
	if (!out)
	{
		FREE (zdata);
		return 0;
	}

	memcpy (out, "CMPD", 4);
	wr_be32 (out + 4, 1);
	out[8] = use_zlib ? 0xc0 : 0x00;
	out[9] = (u8)(stored_size >> 16);
	out[10] = (u8)(stored_size >> 8);
	out[11] = (u8)(stored_size);
	wr_be32 (out + 12, size);
	if (stored_size)
		memcpy (out + 16, stored, stored_size);

	FREE (zdata);
	*res_size = 16 + stored_size;
	return out;
}

u8 *CreateRPAK (const rpak_entry_t *entries, uint n_entries, uint *res_size)
{
	if (!res_size)
		return 0;
	*res_size = 0;
	if (!entries && n_entries)
		return 0;

	u8 **payload = CALLOC (n_entries, sizeof (*payload));
	uint *payload_size = CALLOC (n_entries, sizeof (*payload_size));
	if (!payload || !payload_size)
	{
		FREE (payload);
		FREE (payload_size);
		return 0;
	}

	u64 data_size = 0;
	bool ok = true;
	for (uint i = 0; ok && i < n_entries; i++)
	{
		const rpak_entry_t *e = entries + i;
		if (e->compressed)
			payload[i] = CompressRPAKEntry (e->data, e->size, payload_size + i);
		else if (e->size)
		{
			payload[i] = MALLOC (e->size);
			if (payload[i])
			{
				memcpy (payload[i], e->data, e->size);
				payload_size[i] = e->size;
			}
		}
		else
		{
			// Zero-length stored entries are valid: leave payload[i]==0 but
			// still succeed, matching a zero-byte fwrite() on extraction.
			payload_size[i] = 0;
			continue;
		}
		if (!payload[i])
			ok = false;
		else
			data_size += payload_size[i];
	}

	u64 total_size = 0;
	u8 *out = 0;
	if (ok && (u64)4 + (u64)n_entries * 24 > UINT_MAX)
		ok = false;
	if (ok)
	{
		const u32 strg_length = 4;
		const u32 rshd_length = 4 + n_entries * 24;
		const u64 data_base = (u64)0x80 + strg_length + rshd_length;
		total_size = data_base + data_size;
		if (total_size > UINT_MAX)
			ok = false;
		else
		{
			out = MALLOC (total_size);
			if (!out)
				ok = false;
			else
			{
				memset (out, 0, total_size);
				wr_be32 (out + 0x48, strg_length);
				wr_be32 (out + 0x50, rshd_length);
				wr_be32 (out + 0x58, (u32)data_size);

				// STRG section: 0 strings, never needed for extraction.
				wr_be32 (out + 0x80, 0);

				u8 *rshd = out + 0x80 + strg_length;
				wr_be32 (rshd, n_entries);
				u8 *table = rshd + 4;
				u64 opos = 0;
				for (uint i = 0; i < n_entries; i++)
				{
					const rpak_entry_t *e = entries + i;
					u8 *row = table + (u64)i * 24;
					wr_be32 (row, e->compressed ? 1 : 0);
					wr_be32 (row + 4, e->magic);
					wr_be32 (row + 8, e->id_hi);
					wr_be32 (row + 0xc, e->id_lo);
					wr_be32 (row + 0x10, payload_size[i]);
					wr_be32 (row + 0x14, (u32)opos);
					if (payload_size[i])
						memcpy (out + data_base + opos, payload[i], payload_size[i]);
					opos += payload_size[i];
				}
			}
		}
	}

	for (uint i = 0; i < n_entries; i++)
		FREE (payload[i]);
	FREE (payload);
	FREE (payload_size);

	if (!ok)
	{
		FREE (out);
		return 0;
	}

	*res_size = (uint)total_size;
	return out;
}

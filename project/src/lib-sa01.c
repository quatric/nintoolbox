#include "lib-std.h"
#include "lib-sa01.h"
#include <zlib.h>
#include <string.h>
#include <errno.h>

//-----------------------------------------------------------------------------
///////////////	   Mii Maker "SA01" / amiibo "CA01"		///////////////
//-----------------------------------------------------------------------------

// Peel the outer wrapper off a Mii Maker / amiibo container.  Three accepted
// shapes, all of which must yield an inner "SA01"/"CA01" image -- that
// requirement is what makes the heuristic first form safe:
//
//   * bare "SA01"/"CA01": returned as a copy
//   * "ZCMP": 0x80-byte header, zlib payload at 0x80 (amiibo.bms)
//   * big-endian u32 uncompressed size followed by a zlib stream at offset 4
//     (mii_maker.bms)
enumError DecodeSA01Container (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 8)
		return EINVAL;
	*dest = 0;
	*dest_size = 0;

	if (!memcmp (src, "SA01", 4) || !memcmp (src, "CA01", 4))
	{
		u8 *copy = MALLOC (src_size);
		if (!copy)
			return ERR_CANT_CREATE;
		memcpy (copy, src, src_size);
		*dest = copy;
		*dest_size = src_size;
		return ERR_OK;
	}

	uint payload = 0;
	if (!memcmp (src, "ZCMP", 4))
	{
		if (src_size <= 0x80)
			return EINVAL;
		payload = 0x80;
	}
	else if (src[4] == 0x78 && ((u32)src[4] << 8 | src[5]) % 31 == 0)
	{
		// Mii Maker's wrapper is just a big-endian uncompressed size in
		// front of a raw zlib stream.  Before paying for a full inflate on
		// every file the tree walker hands us, require that size to be
		// self-consistent: non-zero, within this tool's output cap, and at
		// least as large as the compressed remainder (zlib never expands a
		// real payload below its own input size here).
		const u32 raw_size = rd_be32 (src);
		if (!raw_size || raw_size > NFMT_MAX_OUTPUT || raw_size + 4 < src_size)
			return EINVAL;
		payload = 4;
	}
	else
		return EINVAL;

	u8 *dec = 0;
	uint dec_size = 0;
	if (DecodeZlibGrow (&dec, &dec_size, src + payload, src_size - payload) != ERR_OK || !dec)
	{
		FREE (dec);
		return EINVAL;
	}
	if (dec_size < 12 || (memcmp (dec, "SA01", 4) && memcmp (dec, "CA01", 4)))
	{
		FREE (dec);
		return EINVAL;
	}
	*dest = dec;
	*dest_size = dec_size;
	return ERR_OK;
}

// Inner archive, layout ported from aluigi's public mii_maker.bms /
// amiibo.bms:
//
//   'SA01' or 'CA01', u32 files, u32 base_off
//   u32 offset[files]   (each relative to base_off)
//   u32 size[files]
//   char name[files][0x80]   -- SA01 only; CA01 has no name section
//
// The scripts read Mii Maker big-endian but `endian guess` the amiibo one,
// so the word order is recovered here from the file count instead of assumed.
enumError ScanSA01 (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || size < 12)
		return EINVAL;
	const bool named = !memcmp (data, "SA01", 4);
	if (!named && memcmp (data, "CA01", 4))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	// "endian guess": pick whichever byte order gives a sane file count.
	const u32 be = rd_be32 (data + 4), le = rd_le32 (data + 4);
	bool big;
	if (be && be <= 0x10000)
		big = true;
	else if (le && le <= 0x10000)
		big = false;
	else
		return EINVAL;

	const u32 files = big ? be : le;
	const u32 base_off = big ? rd_be32 (data + 8) : rd_le32 (data + 8);
	const u32 rec = named ? 0x80 : 0;
	const u64 off_tab = 12;
	const u64 size_tab = off_tab + (u64)files * 4;
	const u64 name_tab = size_tab + (u64)files * 4;
	if (name_tab + (u64)files * rec > size)
		return EINVAL;

	nintendo_sarc_entry_t *out = CALLOC (files, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint n = 0;
	for (u32 i = 0; i < files; i++)
	{
		const u8 *op = data + off_tab + (u64)i * 4;
		const u8 *sp = data + size_tab + (u64)i * 4;
		const u64 foff = (u64)(big ? rd_be32 (op) : rd_le32 (op)) + base_off;
		const u32 fsize = big ? rd_be32 (sp) : rd_le32 (sp);
		if (foff + fsize > size)
			continue;

		char name[0x88];
		if (named)
		{
			const u8 *np = data + name_tab + (u64)i * rec;
			uint len = 0;
			while (len < rec && np[len])
				len++;
			memcpy (name, np, len);
			name[len] = 0;
			if (!OwnedNameOk (name))
				snprintf (name, sizeof (name), "%05u.bin", i);
		}
		else
			snprintf (name, sizeof (name), "%05u.sar", i);

		if (!OwnedEntryAdd (out, n, name, data + foff, fsize))
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

static enumError nintendo_compress_zlib (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	uLongf bound = compressBound ((uLong)src_size);
	u8 *out = MALLOC (bound);
	if (!out)
		return ERR_CANT_CREATE;
	uLongf out_size = bound;
	if (compress (out, &out_size, src, src_size) != Z_OK)
	{
		FREE (out);
		return ERR_CANT_CREATE;
	}
	*dest = out;
	*dest_size = (uint)out_size;
	return ERR_OK;
}

enumError CreateSA01 (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries,
	bool compress, bool big_endian)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0x10000)
		return EINVAL;

	const uint files = n_entries;
	const uint off_tab = 12;
	const uint size_tab = off_tab + files * 4;
	const uint name_tab = size_tab + files * 4;
	const uint header_meta = name_tab + files * 0x80;
	const uint base_off = (header_meta + 15) & ~15u;

	u64 total_size = base_off;
	for (uint i = 0; i < files; i++)
	{
		total_size += entries[i].size;
		total_size = (total_size + 15) & ~15u;
	}

	if (total_size > 0x7fffffff)
		return EFBIG;

	u8 *inner = CALLOC (1, (size_t)total_size);
	if (!inner)
		return ERR_CANT_CREATE;

	memcpy (inner, "SA01", 4);
	if (big_endian)
	{
		wr_be32 (inner + 4, files);
		wr_be32 (inner + 8, base_off);
	}
	else
	{
		wr_le32 (inner + 4, files);
		wr_le32 (inner + 8, base_off);
	}

	u32 cur_data_off = base_off;
	for (uint i = 0; i < files; i++)
	{
		const u32 rel_off = cur_data_off - base_off;
		if (big_endian)
		{
			wr_be32 (inner + off_tab + i * 4, rel_off);
			wr_be32 (inner + size_tab + i * 4, entries[i].size);
		}
		else
		{
			wr_le32 (inner + off_tab + i * 4, rel_off);
			wr_le32 (inner + size_tab + i * 4, entries[i].size);
		}

		ccp name = entries[i].name ? entries[i].name : "";
		uint nlen = (uint)strlen (name);
		if (nlen > 0x7f)
			nlen = 0x7f;
		memcpy (inner + name_tab + i * 0x80, name, nlen);

		if (entries[i].data && entries[i].size)
			memcpy (inner + cur_data_off, entries[i].data, entries[i].size);

		cur_data_off += entries[i].size;
		cur_data_off = (cur_data_off + 15) & ~15u;
	}

	if (compress)
	{
		u8 *zdata = 0;
		uint zsize = 0;
		enumError zerr = nintendo_compress_zlib (&zdata, &zsize, inner, (uint)total_size);
		FREE (inner);
		if (zerr)
			return zerr;

		if (big_endian)
		{
			u8 *out = MALLOC (4 + zsize);
			if (!out)
			{
				FREE (zdata);
				return ERR_CANT_CREATE;
			}
			wr_be32 (out, (u32)total_size);
			memcpy (out + 4, zdata, zsize);
			FREE (zdata);
			*dest = out;
			*dest_size = 4 + zsize;
			return ERR_OK;
		}
		else
		{
			u8 *out = CALLOC (1, 0x80 + zsize);
			if (!out)
			{
				FREE (zdata);
				return ERR_CANT_CREATE;
			}
			memcpy (out, "ZCMP", 4);
			memcpy (out + 0x80, zdata, zsize);
			FREE (zdata);
			*dest = out;
			*dest_size = 0x80 + zsize;
			return ERR_OK;
		}
	}

	*dest = inner;
	*dest_size = (uint)total_size;
	return ERR_OK;
}

enumError CreateCA01 (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries,
	bool compress, bool big_endian)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0x10000)
		return EINVAL;

	const uint files = n_entries;
	const uint off_tab = 12;
	const uint size_tab = off_tab + files * 4;
	const uint header_meta = size_tab + files * 4;
	const uint base_off = (header_meta + 15) & ~15u;

	u64 total_size = base_off;
	for (uint i = 0; i < files; i++)
	{
		total_size += entries[i].size;
		total_size = (total_size + 15) & ~15u;
	}

	if (total_size > 0x7fffffff)
		return EFBIG;

	u8 *inner = CALLOC (1, (size_t)total_size);
	if (!inner)
		return ERR_CANT_CREATE;

	memcpy (inner, "CA01", 4);
	if (big_endian)
	{
		wr_be32 (inner + 4, files);
		wr_be32 (inner + 8, base_off);
	}
	else
	{
		wr_le32 (inner + 4, files);
		wr_le32 (inner + 8, base_off);
	}

	u32 cur_data_off = base_off;
	for (uint i = 0; i < files; i++)
	{
		const u32 rel_off = cur_data_off - base_off;
		if (big_endian)
		{
			wr_be32 (inner + off_tab + i * 4, rel_off);
			wr_be32 (inner + size_tab + i * 4, entries[i].size);
		}
		else
		{
			wr_le32 (inner + off_tab + i * 4, rel_off);
			wr_le32 (inner + size_tab + i * 4, entries[i].size);
		}

		if (entries[i].data && entries[i].size)
			memcpy (inner + cur_data_off, entries[i].data, entries[i].size);

		cur_data_off += entries[i].size;
		cur_data_off = (cur_data_off + 15) & ~15u;
	}

	if (compress)
	{
		u8 *zdata = 0;
		uint zsize = 0;
		enumError zerr = nintendo_compress_zlib (&zdata, &zsize, inner, (uint)total_size);
		FREE (inner);
		if (zerr)
			return zerr;

		if (big_endian)
		{
			u8 *out = MALLOC (4 + zsize);
			if (!out)
			{
				FREE (zdata);
				return ERR_CANT_CREATE;
			}
			wr_be32 (out, (u32)total_size);
			memcpy (out + 4, zdata, zsize);
			FREE (zdata);
			*dest = out;
			*dest_size = 4 + zsize;
			return ERR_OK;
		}
		else
		{
			u8 *out = CALLOC (1, 0x80 + zsize);
			if (!out)
			{
				FREE (zdata);
				return ERR_CANT_CREATE;
			}
			memcpy (out, "ZCMP", 4);
			memcpy (out + 0x80, zdata, zsize);
			FREE (zdata);
			*dest = out;
			*dest_size = 0x80 + zsize;
			return ERR_OK;
		}
	}

	*dest = inner;
	*dest_size = (uint)total_size;
	return ERR_OK;
}


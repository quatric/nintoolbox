// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// CRIWARE CPK archives + CRILAYLA decompression (see lib-cpk.h).
//-----------------------------------------------------------------------------

#include "lib-std.h"
#include "lib-cpk.h"
#include <string.h>
#include <errno.h>

#define CPK_MAX_ENTRIES 0x100000
#define CPK_MAX_OUTPUT NFMT_MAX_OUTPUT

//--- UTF tables -----------------------------------------------------------
// Big-endian columnar tables. Offsets stored in the table are relative to
// payload+8 (past "@UTF"+table_size); strings/data resolve from there.

typedef struct cpk_utf_t
{
	const u8 *payload; // decrypted @UTF payload (owned iff owned)
	bool owned;
	const u8 *strings; // string pool base
	uint n_cols;
	const u8 *col_base; // column descriptors
	uint row_len;
	uint n_rows;
	const u8 *rows; // row data base
} cpk_utf_t;

static void cpk_utf_free (cpk_utf_t *t)
{
	if (t && t->owned)
	{
		FREE ((void *)t->payload);
		t->payload = 0;
	}
}

// Parse one packet's UTF payload (magic already checked by the caller).
// Returns false on any bound violation. Decrypts in place into a copy
// when the payload does not open with "@UTF".
static bool cpk_utf_parse (cpk_utf_t *t, const u8 *payload, uint payload_size)
{
	if (!t || !payload || payload_size < 32)
		return false;
	memset (t, 0, sizeof (*t));
	const u8 *pkt = payload;
	bool owned = false;
	if (memcmp (pkt, "@UTF", 4))
	{
		u8 *dec = MALLOC (payload_size);
		if (!dec)
			return false;
		uint m = 0x655f, tt = 0x4115;
		for (uint i = 0; i < payload_size; i++)
		{
			dec[i] = pkt[i] ^ (u8)(m & 0xff);
			m *= tt;
		}
		if (memcmp (dec, "@UTF", 4))
		{
			FREE (dec);
			return false;
		}
		pkt = dec;
		owned = true;
	}
	const u64 ro = (u64)rd_be32 (pkt + 8) + 8;
	const u64 so = (u64)rd_be32 (pkt + 12) + 8;
	if (ro > payload_size || so > payload_size)
	{
		if (owned)
			FREE ((void *)pkt);
		return false;
	}
	const uint nc = rd_be16 (pkt + 24);
	if (nc > 1024 || (u64)32 + nc * 5 > payload_size)
	{
		if (owned)
			FREE ((void *)pkt);
		return false;
	}
	// Column descriptors: flags u8 (+3 pad when 0) + BE32 name offset.
	uint cp = 32;
	for (uint i = 0; i < nc; i++)
	{
		if (cp >= payload_size)
		{
			if (owned)
				FREE ((void *)pkt);
			return false;
		}
		cp += pkt[cp] == 0 ? 9 : 5;
		if (cp > payload_size)
		{
			if (owned)
				FREE ((void *)pkt);
			return false;
		}
	}
	const uint rl = rd_be16 (pkt + 26);
	const uint nr = rd_be32 (pkt + 28);
	if (nr > CPK_MAX_ENTRIES || ro + (u64)nr * rl > payload_size)
	{
		if (owned)
			FREE ((void *)pkt);
		return false;
	}
	t->payload = pkt;
	t->owned = owned;
	t->strings = pkt + so;
	t->n_cols = nc;
	t->col_base = pkt + 32;
	t->row_len = rl;
	t->n_rows = nr;
	t->rows = pkt + ro;
	return true;
}

// Column descriptor i: flags + name. Returns false out of bounds.
static bool cpk_utf_col (const cpk_utf_t *t, uint payload_size, uint i, u8 *flags, const u8 **name)
{
	if (!t || i >= t->n_cols)
		return false;
	uint cp = 32;
	for (uint k = 0; k <= i; k++)
	{
		if (cp >= payload_size)
			return false;
		u8 fl = t->payload[cp];
		if (fl == 0)
		{
			if ((u64)cp + 9 > payload_size)
				return false;
			cp += 4;
			fl = t->payload[cp];
		}
		if (k == i)
		{
			if ((u64)cp + 5 > payload_size)
				return false;
			const uint no = rd_be32 (t->payload + cp + 1);
			const u8 *base = t->strings;
			uint pool = (uint)(base - t->payload);
			if ((u64)pool + no >= payload_size)
				return false;
			const u8 *s = base + no;
			uint len = 0;
			while ((u64)(s - t->payload) + len < payload_size && s[len] && len < 256)
				len++;
			if ((u64)(s - t->payload) + len >= payload_size)
				return false;
			if (flags)
				*flags = fl;
			if (name)
				*name = s;
			return true;
		}
		cp += 5;
	}
	return false;
}

// Read one PERROW cell as u64. Strings resolve to *str (NUL-checked);
// DATA cells just advance. Returns false on any violation.
static bool cpk_utf_cell (const cpk_utf_t *t, uint payload_size, uint row, uint col, u64 *num,
	const u8 **str)
{
	if (!t || row >= t->n_rows || col >= t->n_cols)
		return false;
	u8 flags = 0;
	if (!cpk_utf_col (t, payload_size, col, &flags, 0))
		return false;
	if ((flags & 0xf0) != 0x50)
		return false;
	// Cell offset: fixed-width prefix per preceding PERROW column.
	uint off = row * t->row_len;
	for (uint k = 0; k < col; k++)
	{
		u8 fl = 0;
		if (!cpk_utf_col (t, payload_size, k, &fl, 0))
			return false;
		if ((fl & 0xf0) != 0x50)
			continue;
		switch (fl & 0x0f)
		{
			case 0:
			case 1:
				off += 1;
				break;
			case 2:
			case 3:
				off += 2;
				break;
			case 4:
			case 5:
				off += 4;
				break;
			case 6:
			case 7:
			case 0xb:
				off += 8;
				break;
			case 8:
				off += 4;
				break;
			case 0xa:
				off += 4;
				break;
			default:
				return false;
		}
	}
	const u8 *rp = t->rows + off;
	const u8 *rend = t->payload + payload_size;
	if (rp >= rend)
		return false;
	switch (flags & 0x0f)
	{
		case 0:
		case 1:
			if (rp + 1 > rend)
				return false;
			if (num)
				*num = rp[0];
			return true;
		case 2:
		case 3:
			if (rp + 2 > rend)
				return false;
			if (num)
				*num = (u64)rp[0] << 8 | rp[1];
			return true;
		case 4:
		case 5:
			if (rp + 4 > rend)
				return false;
			if (num)
				*num = rd_be32 (rp);
			return true;
		case 6:
		case 7:
			if (rp + 8 > rend)
				return false;
			if (num)
				*num = (u64)rd_be32 (rp) << 32 | rd_be32 (rp + 4);
			return true;
		case 8:
			if (rp + 4 > rend)
				return false;
			return true;
		case 0xa:
		{
			if (rp + 4 > rend)
				return false;
			const uint no = rd_be32 (rp);
			const u8 *base = t->strings;
			if ((u64)(base - t->payload) + no >= payload_size)
				return false;
			const u8 *s = base + no;
			uint len = 0;
			while ((u64)(s - t->payload) + len < payload_size && s[len] && len < 256)
				len++;
			if ((u64)(s - t->payload) + len >= payload_size)
				return false;
			if (str)
				*str = s;
			return true;
		}
		default:
			return false;
	}
}

// Find a column index by name.
static bool cpk_utf_find (const cpk_utf_t *t, uint payload_size, ccp want, uint *col)
{
	for (uint i = 0; i < t->n_cols; i++)
	{
		const u8 *name = 0;
		if (!cpk_utf_col (t, payload_size, i, 0, &name))
			return false;
		uint k = 0;
		while (want[k] && name[k] == (u8)want[k])
			k++;
		if (!want[k] && !name[k])
		{
			if (col)
				*col = i;
			return true;
		}
		if (k >= 256)
			return false;
	}
	return false;
}

//--- packet framing --------------------------------------------------------
// magic[4] + i32 LE unk + u64 LE size + payload.

static bool cpk_packet (const u8 *d, uint size, uint off, const char magic[4], const u8 **payload,
	uint *payload_size)
{
	if (!d || (u64)off + 16 > size || memcmp (d + off, magic, 4))
		return false;
	const u64 psz = rd_le64 (d + off + 8);
	if (psz > size || (u64)off + 16 + psz > size)
		return false;
	if (payload)
		*payload = d + off + 16;
	if (payload_size)
		*payload_size = (uint)psz;
	return true;
}

//--- CRILAYLA ---------------------------------------------------------------
// Backwards bitstream ported from esperknight/CriPakTools'
// DecompressCRILAYLA (MIT). Output is the 0x100 header copy plus
// usize payload bytes; total must equal the TOC ExtractSize.

enumError DecodeCRILAYLA (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 16 || memcmp (src, "CRILAYLA", 8))
		return EINVAL;
	*dest = 0;
	*dest_size = 0;
	const uint usize = rd_le32 (src + 8);
	const uint hoff = rd_le32 (src + 12);
	if (!usize || usize > CPK_MAX_OUTPUT || (u64)usize + 0x100 > CPK_MAX_OUTPUT)
		return EINVAL;
	if ((u64)hoff + 0x10 + 0x100 > src_size)
		return EINVAL;
	// input_end in the reference is a signed int counting down; the
	// stream must still have room for at least one flag byte.
	if (src_size < 0x100 + 1)
		return EINVAL;

	u8 *out = MALLOC ((size_t)usize + 0x100);
	if (!out)
		return ERR_CANT_CREATE;
	memcpy (out, src + hoff + 0x10, 0x100);

	long input_offset = (long)src_size - 0x100 - 1;
	const long output_end = (long)0x100 + usize - 1;
	u8 bit_pool = 0;
	int bits_left = 0;
	uint bytes_output = 0;
	static const uint vle_lens[4] = { 2, 3, 5, 8 };
	enumError err = ERR_OK;

	while (bytes_output < usize)
	{
		// get_next_bits(&1): refill from input_offset--, MSB first.
		uint bit = 0;
		{
			uint produced = 0;
			while (produced < 1)
			{
				if (!bits_left)
				{
					if (input_offset < 0)
					{
						err = EINVAL;
						break;
					}
					bit_pool = src[input_offset];
					bits_left = 8;
					input_offset--;
				}
				uint take = bits_left < 1 - produced ? (uint)bits_left : 1 - produced;
				bit = (bit << take)
					| (uint)((bit_pool >> (bits_left - take)) & ((1u << take) - 1));
				bits_left -= (int)take;
				produced += take;
			}
			if (err)
				break;
		}
#define CPK_BITS(n, dst)                                                                       \
	do                                                                                     \
	{                                                                                      \
		uint need = (n), prod = 0;                                                     \
		(dst) = 0;                                                                     \
		while (prod < need)                                                            \
		{                                                                              \
			if (!bits_left)                                                        \
			{                                                                      \
				if (input_offset < 0)                                          \
				{                                                              \
					err = EINVAL;                                          \
					break;                                                 \
				}                                                              \
				bit_pool = src[input_offset];                                  \
				bits_left = 8;                                                 \
				input_offset--;                                                \
			}                                                                      \
			{                                                                      \
				uint take = (uint)bits_left < need - prod ? (uint)bits_left \
									  : need - prod;   \
				(dst) = ((dst) << take)                                            \
					| (uint)((bit_pool >> (bits_left - (int)take))          \
						& ((1u << take) - 1));                             \
				bits_left -= (int)take;                                        \
				prod += take;                                                  \
			}                                                                      \
		}                                                                              \
		if (err)                                                                   \
			break;                                                                 \
	} while (0)

		if (bit > 0)
		{
			uint dist = 0;
			CPK_BITS (13, dist);
			if (err)
				break;
			const long bro = output_end - (long)bytes_output + (long)dist + 3;
			uint len = 3;
			uint lvl = 0;
			while (lvl < 4 && !err)
			{
				uint t = 0;
				CPK_BITS (vle_lens[lvl], t);
				if (err)
					break;
				len += t;
				if (t != (1u << vle_lens[lvl]) - 1)
					break;
				lvl++;
			}
			if (err)
				break;
			if (lvl == 4)
			{
				for (;;)
				{
					uint t = 0;
					CPK_BITS (8, t);
					if (err)
						break;
					len += t;
					if (t != 255)
						break;
				}
				if (err)
					break;
			}
			if (bro < 0 || (u64)len > (u64)usize - bytes_output)
			{
				err = EINVAL;
				break;
			}
			long bp = bro;
			for (uint i = 0; i < len; i++)
			{
				out[output_end - bytes_output] = out[bp];
				bp--;
				bytes_output++;
			}
		}
		else
		{
			uint byte = 0;
			CPK_BITS (8, byte);
			if (err)
				break;
			out[output_end - bytes_output] = (u8)byte;
			bytes_output++;
		}
	}
#undef CPK_BITS

	if (err)
	{
		FREE (out);
		return err;
	}
	*dest = out;
	*dest_size = usize + 0x100;
	return ERR_OK;
}

//--- archive scan ------------------------------------------------------------

enumError ScanCPK (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || size < 0x20 || memcmp (data, "CPK ", 4))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u8 *cpk_payload = 0;
	uint cpk_size = 0;
	if (!cpk_packet (data, size, 0, "CPK ", &cpk_payload, &cpk_size))
		return EINVAL;
	cpk_utf_t hdr;
	if (!cpk_utf_parse (&hdr, cpk_payload, cpk_size))
		return EINVAL;

	uint c_toc = 0, c_etoc = 0, c_content = 0, c_files = 0, c_align = 0;
	bool has_toc = cpk_utf_find (&hdr, cpk_size, "TocOffset", &c_toc);
	const bool has_content = cpk_utf_find (&hdr, cpk_size, "ContentOffset", &c_content);
	const bool has_files = cpk_utf_find (&hdr, cpk_size, "Files", &c_files);
	const bool has_align = cpk_utf_find (&hdr, cpk_size, "Align", &c_align);
	bool has_etoc = cpk_utf_find (&hdr, cpk_size, "EtocOffset", &c_etoc);
	(void)has_etoc;
	u64 toc_off = 0, content_off = 0, files = 0, align = 2048;
	if (has_toc && !cpk_utf_cell (&hdr, cpk_size, 0, c_toc, &toc_off, 0))
	{
		cpk_utf_free (&hdr);
		return EINVAL;
	}
	if (has_content)
		cpk_utf_cell (&hdr, cpk_size, 0, c_content, &content_off, 0);
	if (has_files)
		cpk_utf_cell (&hdr, cpk_size, 0, c_files, &files, 0);
	if (has_align)
		cpk_utf_cell (&hdr, cpk_size, 0, c_align, &align, 0);
	if (!has_toc || toc_off == (u64)-1 || toc_off > size)
	{
		// No TOC (ITOC-only layouts have no path names to recover).
		cpk_utf_free (&hdr);
		return EINVAL;
	}
	if (!files || files > CPK_MAX_ENTRIES)
	{
		cpk_utf_free (&hdr);
		return EINVAL;
	}
	if (!align || align > (1u << 20))
		align = 2048;
	const u64 add = !has_content || content_off > toc_off ? toc_off : content_off;

	const u8 *toc_payload = 0;
	uint toc_size = 0;
	if (!cpk_packet (data, size, (uint)toc_off, "TOC ", &toc_payload, &toc_size))
	{
		cpk_utf_free (&hdr);
		return EINVAL;
	}
	cpk_utf_t toc;
	if (!cpk_utf_parse (&toc, toc_payload, toc_size))
	{
		cpk_utf_free (&hdr);
		return EINVAL;
	}

	uint t_dir = 0, t_file = 0, t_size = 0, t_extract = 0, t_off = 0;
	const bool has_dir = cpk_utf_find (&toc, toc_size, "DirName", &t_dir);
	const bool has_file = cpk_utf_find (&toc, toc_size, "FileName", &t_file);
	const bool has_size = cpk_utf_find (&toc, toc_size, "FileSize", &t_size);
	const bool has_extract = cpk_utf_find (&toc, toc_size, "ExtractSize", &t_extract);
	const bool has_foff = cpk_utf_find (&toc, toc_size, "FileOffset", &t_off);
	if (!has_file || !has_size || !has_foff)
	{
		cpk_utf_free (&toc);
		cpk_utf_free (&hdr);
		return EINVAL;
	}
	if (toc.n_rows > CPK_MAX_ENTRIES)
	{
		cpk_utf_free (&toc);
		cpk_utf_free (&hdr);
		return EINVAL;
	}

	nintendo_sarc_entry_t *out = CALLOC (toc.n_rows ? toc.n_rows : 1, sizeof (*out));
	if (!out)
	{
		cpk_utf_free (&toc);
		cpk_utf_free (&hdr);
		return ERR_CANT_CREATE;
	}

	uint n = 0;
	for (uint i = 0; i < toc.n_rows; i++)
	{
		const u8 *dir = 0, *file = 0;
		u64 fsize = 0, foff = 0, extract = 0;
		bool has_ex = false;
		if (has_dir && !cpk_utf_cell (&toc, toc_size, i, t_dir, 0, &dir))
			continue;
		if (!cpk_utf_cell (&toc, toc_size, i, t_file, 0, &file) || !file || !*file)
			continue;
		if (!cpk_utf_cell (&toc, toc_size, i, t_size, &fsize, 0))
			continue;
		if (!cpk_utf_cell (&toc, toc_size, i, t_off, &foff, 0))
			continue;
		if (has_extract && cpk_utf_cell (&toc, toc_size, i, t_extract, &extract, 0)
			&& extract != 0)
			has_ex = true;
		if (foff + add > size)
			continue;
		const u64 abs_off = foff + add;
		if (!fsize || fsize > size || abs_off + fsize > size)
			continue;
		uint decomp = (uint)fsize;
		if (has_ex)
		{
			if (!extract || extract > CPK_MAX_OUTPUT)
				continue;
			decomp = (uint)extract;
		}

		char name[512];
		if (dir && *dir)
			snprintf (name, sizeof (name), "%s/%s", dir, file);
		else
			snprintf (name, sizeof (name), "%s", file);
		if (!OwnedNameOk (name))
			snprintf (name, sizeof (name), "%04u.bin", i);

		bool ok = false;
		if (has_ex && decomp != fsize)
		{
			u8 *dec = 0;
			uint dec_size = 0;
			if (DecodeCRILAYLA (&dec, &dec_size, data + abs_off, (uint)fsize) == ERR_OK
				&& dec && dec_size == decomp)
			{
				ok = OwnedEntryAdd (out, n, name, dec, dec_size);
				FREE (dec);
			}
			// Undecodable members are skipped, never emitted raw:
			// the stored bytes are still CRILAYLA-compressed.
		}
		else
			ok = OwnedEntryAdd (out, n, name, data + abs_off, (uint)fsize);
		if (!ok)
		{
			ResetOwnedEntries (out, n);
			FREE (out);
			cpk_utf_free (&toc);
			cpk_utf_free (&hdr);
			return ERR_CANT_CREATE;
		}
		n++;
	}

	cpk_utf_free (&toc);
	cpk_utf_free (&hdr);
	if (!n)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

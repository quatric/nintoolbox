#include "lib-std.h"
#include "lib-arika.h"
#include <string.h>
#include <errno.h>

//-----------------------------------------------------------------------------
///////////////		Arika INFO.DAT / GAME.DAT archives		///////////////
//-----------------------------------------------------------------------------

// See the header comment above ExtractArika() for the full layout. Ported
// from GBATEK's "DS Encrypted Arika Archives with ALZ1 compression" page
// plus aluigi's arika.bms/endless_ocean.bms (for the RF2 sub-container and
// the older "ZALZ"-tagged variant, neither of which GBATEK documents).

enumError DecryptArikaInfo (u8 *buf, uint size)
{
	if (!buf)
		return EINVAL;
	if (size < 0x10 || buf[0] == 0)
		return ERR_OK; // already unencrypted
	u8 key[0x10];
	memcpy (key, buf, 0x10);
	for (uint i = 0x10; i < size; i++)
	{
		u8 b = buf[i];
		b = (u8)(b >> 4 | b << 4); // ror 4 (== rol 4 on a byte)
		b ^= 0xff;
		b = (u8)(b - key[i & 0xf]);
		buf[i] = b;
	}
	return ERR_OK;
}

enumError EncryptArikaInfo (u8 *buf, uint size)
{
	if (!buf)
		return EINVAL;
	if (size < 0x10 || buf[0] == 0)
		return ERR_OK; // stays unencrypted
	u8 key[0x10];
	memcpy (key, buf, 0x10);
	for (uint i = 0x10; i < size; i++)
	{
		u8 b = buf[i];
		b = (u8)(b + key[i & 0xf]);
		b ^= 0xff;
		b = (u8)(b >> 4 | b << 4); // rol 4 (== ror 4 on a byte)
		buf[i] = b;
	}
	return ERR_OK;
}

enumError DecodeALZ1 (u8 *dest, uint dest_size, const u8 *src, uint src_size)
{
	if (!dest && dest_size)
		return EINVAL;
	if (!src && src_size)
		return EINVAL;
	u8 ring[0x1000];
	memset (ring, 0, sizeof (ring));
	uint p = 0xfee;
	uint sp = 0, dp = 0;
	uint flags = 0, flag_bits = 0;

	while (dp < dest_size)
	{
		if (!flag_bits)
		{
			if (sp >= src_size)
				return EINVAL;
			flags = src[sp++];
			flag_bits = 8;
		}
		const bool literal = flags & 1;
		flags >>= 1;
		flag_bits--;

		if (literal)
		{
			if (sp >= src_size)
				return EINVAL;
			const u8 b = src[sp++];
			dest[dp++] = b;
			ring[p & 0xfff] = b;
			p++;
		}
		else
		{
			if (sp + 2 > src_size)
				return EINVAL;
			const u8 b0 = src[sp], b1 = src[sp + 1];
			sp += 2;
			uint q = b0 | (uint)(b1 >> 4) << 8;
			uint len = (b1 & 0xf) + 3;
			for (uint i = 0; i < len && dp < dest_size; i++)
			{
				const u8 b = ring[q & 0xfff];
				dest[dp++] = b;
				ring[p & 0xfff] = b;
				p++;
				q++;
			}
		}
	}
	return ERR_OK;
}

enumError EncodeALZ1 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size)
		return EINVAL;
	if (!src && src_size)
		return EINVAL;

	// Worst case: every byte literal -> one flag bit + one byte per byte,
	// flags reloaded every 8 tokens.
	const uint capacity = src_size + (src_size + 7) / 8 + 8;
	u8 *out = MALLOC (capacity ? capacity : 1);
	if (!out)
		return ERR_CANT_CREATE;

	uint dp = 0, sp = 0;
	while (sp < src_size)
	{
		const uint flag_pos = dp++;
		u8 flag = 0;
		for (uint bit = 0; bit < 8 && sp < src_size; bit++)
		{
			uint best_len = 0, best_back = 0;
			const uint max_back = sp < 0x1000 ? sp : 0x1000;
			const uint max_len = src_size - sp < 18 ? src_size - sp : 18;
			// Same backward search style as EncodeLZ10LZ11(): nearby matches
			// first, matches may self-overlap (back < len) exactly like the
			// ring-buffer decoder above naturally allows.
			for (uint back = 1; back <= max_back; back++)
			{
				uint len = 0;
				while (len < max_len && src[sp + len] == src[sp - back + len])
					len++;
				if (len > best_len)
				{
					best_len = len;
					best_back = back;
					if (len == max_len)
						break;
				}
			}
			if (best_len >= 3)
			{
				const uint p = 0xfee + sp;
				const uint q = (p - best_back) & 0xfff;
				out[dp++] = (u8)q;
				out[dp++] = (u8)((q >> 8 & 0xf) << 4 | (best_len - 3));
				sp += best_len;
				// flag bit left 0 -- 0 means "compressed" for ALZ1
			}
			else
			{
				flag |= (u8)(1u << bit);
				out[dp++] = src[sp++];
			}
		}
		out[flag_pos] = flag;
	}
	*dest = out;
	*dest_size = dp;
	return ERR_OK;
}

typedef struct arika_list_t
{
	nintendo_sarc_entry_t *entries;
	uint n, cap;
} arika_list_t;

static void arika_list_push (arika_list_t *list, ccp name, const u8 *data, u32 size)
{
	if (list->n == list->cap)
	{
		list->cap = list->cap ? list->cap * 2 : 16;
		list->entries = REALLOC (list->entries, list->cap * sizeof (*list->entries));
	}
	nintendo_sarc_entry_t *e = list->entries + list->n++;
	e->name = STRDUP (name);
	u8 *copy = size ? MALLOC (size) : 0;
	if (copy)
		memcpy (copy, data, size);
	e->data = copy;
	e->size = size;
}

// Splits an "RF2"-tagged sub-container (Endless Ocean: Blue World groups
// related assets this way) into its member entries, recursing into any
// member that is itself another RF2 container. Falls back to a plain leaf
// push when [off] isn't actually an RF2 tag -- this is also how ordinary,
// non-grouped members reach the list. Ported from EXTRACT_RF2() in aluigi's
// arika.bms/endless_ocean.bms.
//
// RF2 header (16 bytes), all fields little-endian:
//   00h 3   "RF2" signature
//   03h 3   Type tag (unused here)
//   06h 2   Version
//   08h 3   Info size (sub-entry table size in bytes == n_sub * 20h)
//   0Bh 1   Unused
//   0Ch 4   Header size (unused here)
// Sub-entry (20h bytes): 14h-byte name, 4-byte size, 4-byte offset (relative
// to the RF2 tag's own offset), 4-byte flags (unused).
static void walk_rf2_or_leaf (
	arika_list_t *list, const u8 *data, uint size, uint off, u32 leaf_size, ccp name)
{
	if (off + 16 <= size && !memcmp (data + off, "RF2", 3))
	{
		const uint base_off = off;
		const u32 info_size = data[off + 8] | (u32)data[off + 9] << 8 | (u32)data[off + 10] << 16;
		const uint n_sub = info_size / 0x20;
		if (n_sub && n_sub <= 0x10000 && (u64)off + 16 + (u64)n_sub * 0x20 <= size)
		{
			const u8 *rec = data + off + 16;
			for (uint i = 0; i < n_sub; i++, rec += 0x20)
			{
				char raw[21];
				memcpy (raw, rec, 20);
				raw[20] = 0;
				char sub[300];
				snprintf (sub, sizeof (sub), "%s/%.20s", name, raw);
				const u32 ssize = rd_le32 (rec + 20);
				const u32 soff = rd_le32 (rec + 24) + base_off;
				if (ssize)
					walk_rf2_or_leaf (list, data, size, soff, ssize, sub);
			}
			return;
		}
	}
	if (off <= size && leaf_size <= size - off)
		arika_list_push (list, name, data + off, leaf_size);
}

enumError ExtractArika (nintendo_sarc_entry_t **out_entries, uint *out_n_entries,
	const u8 *info_data, uint info_size, const u8 *game_data, uint game_size)
{
	if (!out_entries || !out_n_entries || !info_data || info_size < 0x30 || !game_data)
		return EINVAL;

	u8 *info = MALLOC (info_size);
	if (!info)
		return ERR_CANT_CREATE;
	memcpy (info, info_data, info_size);
	DecryptArikaInfo (info, info_size);

	u32 sector_size = rd_le32 (info + 0x24);
	if (!sector_size)
		sector_size = 0x800;
	const u32 n_entries = rd_le32 (info + 0x2c);
	if (n_entries > 0x40000 || (u64)0x30 + (u64)n_entries * 0x30 > info_size)
	{
		FREE (info);
		return EINVAL;
	}

	arika_list_t list;
	memset (&list, 0, sizeof (list));

	const u8 *rec = info + 0x30;
	for (u32 i = 0; i < n_entries; i++, rec += 0x30)
	{
		char name[0x21];
		memcpy (name, rec, 0x20);
		name[0x20] = 0;
		if (!*name)
			continue; // unused directory slot

		const u32 zsize = rd_le32 (rec + 0x20);
		const u64 byte_off = (u64)rd_le32 (rec + 0x24) * sector_size;
		const u32 dsize = rd_le32 (rec + 0x2c);

		if (!zsize && !dsize)
			continue;
		if (byte_off > game_size)
			continue;
		const u32 avail = (u32)(game_size - byte_off);

		if (zsize == dsize)
		{
			// Stored raw (and possibly itself an RF2 group).
			if (zsize <= avail)
				walk_rf2_or_leaf (&list, game_data, game_size, (uint)byte_off, zsize, name);
			continue;
		}

		if (!dsize || dsize > NFMT_MAX_OUTPUT || zsize > avail)
			continue; // out-of-range entry: skip it, don't abort the archive

		const u8 *e = game_data + byte_off;
		uint hdr;
		if (zsize >= 4 && !memcmp (e, "ALZ1", 4))
			hdr = 4;
		else if (zsize >= 8 && !memcmp (e, "ZALZ", 4))
			hdr = 8; // older titles; same LZSS bitstream (see arika.bms)
		else
			continue; // unrecognised magic on a size-mismatched entry

		u8 *blob = MALLOC (dsize);
		if (!blob)
			continue;
		if (!DecodeALZ1 (blob, dsize, e + hdr, zsize - hdr))
			walk_rf2_or_leaf (&list, blob, dsize, 0, dsize, name);
		FREE (blob);
	}

	FREE (info);
	*out_entries = list.entries;
	*out_n_entries = list.n;
	return ERR_OK;
}

enumError CreateArika (u8 **dest_info, uint *dest_info_size, u8 **dest_game, uint *dest_game_size,
	const nintendo_sarc_entry_t *entries, uint n_entries, ccp title, bool compress)
{
	if (!dest_info || !dest_info_size || !dest_game || !dest_game_size || !entries || !n_entries
		|| n_entries > 0x40000)
		return EINVAL;

	const u32 sector_size = 0x800;
	const uint info_size = 0x30 + n_entries * 0x30;
	u8 *info = CALLOC (1, info_size);
	if (!info)
		return ERR_CANT_CREATE;

	if (title)
		memcpy (info, title, strnlen (title, 0x10));

	wr_le32 (info + 0x24, sector_size);
	wr_le32 (info + 0x28, 1);
	wr_le32 (info + 0x2c, n_entries);

	u8 **payload = CALLOC (n_entries, sizeof (u8 *));
	uint *psize = CALLOC (n_entries, sizeof (uint));
	if (!payload || !psize)
	{
		FREE (info);
		FREE (payload);
		FREE (psize);
		return ERR_CANT_CREATE;
	}

	u64 game_size = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		const u8 *src = entries[i].data;
		const uint ssize = entries[i].size;

		u8 *zdata = 0;
		uint zsize = 0;
		if (compress && ssize)
			EncodeALZ1 (&zdata, &zsize, src, ssize);

		if (zdata && zsize + 4 < ssize)
		{
			payload[i] = MALLOC (zsize + 4);
			memcpy (payload[i], "ALZ1", 4);
			memcpy (payload[i] + 4, zdata, zsize);
			psize[i] = zsize + 4;
		}
		else
		{
			payload[i] = ssize ? MALLOC (ssize) : 0;
			if (ssize)
				memcpy (payload[i], src, ssize);
			psize[i] = ssize;
		}
		FREE (zdata);

		const uint blocks = (psize[i] + sector_size - 1) / sector_size;
		game_size += (u64)blocks * sector_size;
	}

	if (game_size > NFMT_MAX_OUTPUT)
	{
		for (uint i = 0; i < n_entries; i++)
			FREE (payload[i]);
		FREE (payload);
		FREE (psize);
		FREE (info);
		return EFBIG;
	}

	u8 *game = CALLOC (1, game_size ? game_size : 1);
	if (!game)
	{
		for (uint i = 0; i < n_entries; i++)
			FREE (payload[i]);
		FREE (payload);
		FREE (psize);
		FREE (info);
		return ERR_CANT_CREATE;
	}

	uint cur = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		u8 *rec = info + 0x30 + i * 0x30;
		ccp nm = entries[i].name ? entries[i].name : "";
		strncpy ((char *)rec, nm, 0x1f);

		wr_le32 (rec + 0x20, psize[i]);
		wr_le32 (rec + 0x24, cur / sector_size);
		const uint blocks = (psize[i] + sector_size - 1) / sector_size;
		wr_le32 (rec + 0x28, blocks);
		wr_le32 (rec + 0x2c, entries[i].size);

		if (psize[i])
			memcpy (game + cur, payload[i], psize[i]);
		FREE (payload[i]);
		cur += blocks * sector_size;
	}
	FREE (payload);
	FREE (psize);

	EncryptArikaInfo (info, info_size);

	*dest_info = info;
	*dest_info_size = info_size;
	*dest_game = game;
	*dest_game_size = (uint)game_size;
	return ERR_OK;
}

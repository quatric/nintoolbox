// Excite Truck RST archive format -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-quicklz.h"

//-----------------------------------------------------------------------------
// Monster Games RST ("0TSR" / "RST0") archive & TOC ("0SERCOTE" / "ETOCRES0")
// Used in Excitebots: Trick Racing, Excite Truck, NASCAR Heat, etc.
// Supports both Little-Endian and Big-Endian, plus QuickLZ ("PMCr") compression.
//-----------------------------------------------------------------------------

enumError ExtractRST (nintendo_sarc_entry_t **out_entries, uint *out_n_entries, const u8 *car_data,
	uint car_size, const u8 *toc_data, uint toc_size)
{
	if (!out_entries || !out_n_entries || !car_data || car_size < 0x40)
		return EINVAL;

	bool be = false;
	if (!memcmp (car_data, "RST0", 4))
		be = true;
	else if (!memcmp (car_data, "0TSR", 4))
		be = false;
	else
		return EINVAL;

	u32 (*r32) (const u8 *) = be ? rd_be32 : rd_le32;

	u32 files_count = r32 (car_data + 0x20);
	if (!files_count || files_count > 20000)
		return EINVAL;

	u32 data_offset = r32 (car_data + 0x18);
	if (!data_offset || data_offset >= car_size)
		data_offset = 0x80;

	const u8 *payload_bytes = car_data + data_offset;
	uint payload_len = car_size > data_offset ? car_size - data_offset : 0;

	u8 *decompressed_payload = 0;
	uint decompressed_len = 0;

	// Two-region virtual address space, matching the retail encoder's split
	// between compressible and (mostly incompressible, e.g. streamed audio
	// and some large textures) assets: TOC offsets < decompressed_len index
	// into the QuickLZ-decompressed block; offsets >= decompressed_len index
	// into the *raw*, uncompressed bytes that follow the QuickLZ stream
	// in-file (tail_bytes/tail_len below). Every real retail TOC has entries
	// in both halves -- treating this as a single flat payload silently
	// drops every entry whose offset lands in the tail (large streamed
	// textures, music/SFX), so both must be considered.
	const u8 *tail_bytes = 0;
	uint tail_len = 0;

	// Check for QuickLZ ("PMCr" / "rMCP" wrapper)
	if (payload_len >= 16
		&& (!memcmp (payload_bytes, "PMCr", 4) || !memcmp (payload_bytes, "rMCP", 4)))
	{
		u32 qlz_hdr_len = r32 (payload_bytes + 8);
		const u8 *qlz_stream = payload_bytes + 16;
		uint qlz_len
			= (qlz_hdr_len > 0 && qlz_hdr_len <= payload_len - 16) ? qlz_hdr_len : payload_len - 16;
		enumError qerr
			= DecodeQuickLZ (&decompressed_payload, &decompressed_len, qlz_stream, qlz_len);
		if (!qerr && decompressed_payload)
		{
			uint tail_off = 16 + qlz_len;
			if (payload_len > tail_off)
			{
				tail_bytes = payload_bytes + tail_off;
				tail_len = payload_len - tail_off;
			}
			payload_bytes = decompressed_payload;
			payload_len = decompressed_len;
		}
	}

	// Excite Truck's RST predates Excitebots': its TOC keeps each name inline
	// in a fixed 32-byte field at the head of a 68-byte record, with no
	// separate string pool, and its file offsets are absolute within the
	// archive rather than relative to a decompressed payload -- that archive
	// carries no QuickLZ stream at all, so there is nothing to be relative to.
	// Read with the later layout it yielded 44 of 350 files under names taken
	// from the wrong place, some of which then collided.
	//
	// The layout identifies itself rather than being inferred from a version
	// number: the header, the records and the count have to account for the
	// TOC exactly, which the later layout never does.
	if (toc_data && toc_size == 0x28 + (u64)files_count * 0x44)
	{
		nintendo_sarc_entry_t *entries = CALLOC (files_count, sizeof (nintendo_sarc_entry_t));
		if (!entries)
		{
			if (decompressed_payload)
				FREE (decompressed_payload);
			return ENOMEM;
		}
		uint count = 0;

		for (uint i = 0; i < files_count; i++)
		{
			const u8 *e = toc_data + 0x28 + (size_t)i * 0x44;
			const u32 fsize = rd_le32 (e + 0x28);
			const u32 foff = rd_le32 (e + 0x2c);
			if (!fsize) // a declared but absent resource
				continue;
			if ((u64)foff + fsize > car_size)
				continue;

			char name[33];
			memcpy (name, e, 32);
			name[32] = 0;
			if (!*name)
				continue;

			entries[count].name = STRDUP (name);
			entries[count].size = fsize;
			entries[count].data = MALLOC (fsize);
			if (!entries[count].data)
				break;
			memcpy ((void *)entries[count].data, car_data + foff, fsize);
			count++;
		}

		if (decompressed_payload)
			FREE (decompressed_payload);
		*out_entries = entries;
		*out_n_entries = count;
		return ERR_OK;
	}

	if (toc_data && toc_size >= 0x0C + (files_count + 1) * 0x28)
	{
		const bool compact_toc = rd_le32 (toc_data) == 3 && toc_size >= 0x20;
		const bool toc_be = !compact_toc && !memcmp (toc_data, "ETOCRES0", 8);
		u32 (*tr32) (const u8 *) = toc_be ? rd_be32 : rd_le32;
		const uint toc_count = compact_toc ? tr32 (toc_data + 0x0c) : files_count;
		const uint entries_off = compact_toc ? 0x20 : 0x0c;
		const uint names_off = entries_off + (compact_toc ? toc_count : toc_count + 1) * 0x28;
		if (names_off >= toc_size)
		{
			if (decompressed_payload)
				FREE (decompressed_payload);
			return EINVAL;
		}

		nintendo_sarc_entry_t *entries = CALLOC (toc_count, sizeof (nintendo_sarc_entry_t));
		uint count = 0;

		for (uint i = 0; i < toc_count; i++)
		{
			uint entry_off = entries_off + (compact_toc ? i : i + 1) * 0x28;
			u32 name_rel = tr32 (toc_data + entry_off);
			u32 fsize = tr32 (toc_data + entry_off + 0x0C);
			u32 foff = tr32 (toc_data + entry_off + 0x10);

			if (fsize > 0)
			{
				// Retail TOC offsets are relative to the uncompressed payload,
				// not to the 0x80-byte RST file header. Offsets past the
				// decompressed block continue into the raw tail region (see
				// the tail_bytes/tail_len comment above).
				uint src_off = foff;
				const u8 *src_base;
				uint max_avail;

				if (src_off < payload_len)
				{
					src_base = payload_bytes;
					max_avail = payload_len;
				}
				else
				{
					src_base = tail_bytes;
					max_avail = tail_len;
					src_off -= payload_len;
				}

				if (src_base && fsize <= max_avail && src_off <= max_avail - fsize)
				{
					uint n_pos = names_off + name_rel;
					if (n_pos < toc_size)
					{
						ccp name_ptr = (ccp)(toc_data + n_pos);
						entries[count].name = STRDUP (name_ptr);
						entries[count].size = fsize;
						entries[count].data = MALLOC (fsize);
						memcpy ((void *)entries[count].data, src_base + src_off, fsize);
						count++;
					}
				}
			}
		}

		if (decompressed_payload)
			FREE (decompressed_payload);

		*out_entries = entries;
		*out_n_entries = count;
		return ERR_OK;
	}

	// WiiWare games such as Excitebike World Rally store the usual
	// per-resource TOCs in a compressed `tocres.res` RST.  Its payload is a
	// compact directory: FILES_COUNT 0x28-byte records followed by names,
	// without the standalone 0SERCOTE header.
	if (!toc_data && payload_len >= files_count * 0x28)
	{
		const uint names_off = files_count * 0x28;
		nintendo_sarc_entry_t *entries = CALLOC (files_count, sizeof (*entries));
		uint count = 0;
		for (uint i = 0; i < files_count; i++)
		{
			const u8 *rec = payload_bytes + i * 0x28;
			const uint name_rel = r32 (rec);
			const uint fsize = r32 (rec + 0x0c);
			const uint foff = r32 (rec + 0x10);
			if (!fsize || foff > payload_len || fsize > payload_len - foff
				|| name_rel > payload_len - names_off)
				continue;
			ccp name = (ccp)payload_bytes + names_off + name_rel;
			if (!memchr (name, 0, payload_len - names_off - name_rel))
				continue;
			entries[count].name = STRDUP (name);
			entries[count].size = fsize;
			entries[count].data = MALLOC (fsize);
			memcpy ((void *)entries[count].data, payload_bytes + foff, fsize);
			count++;
		}
		if (decompressed_payload)
			FREE (decompressed_payload);
		if (count)
		{
			*out_entries = entries;
			*out_n_entries = count;
			return ERR_OK;
		}
		FREE (entries);
	}

	if (decompressed_payload)
		FREE (decompressed_payload);

	return EINVAL;
}

enumError CreateRST (u8 **dest_car, uint *dest_car_size, u8 **dest_toc, uint *dest_toc_size,
	const nintendo_sarc_entry_t *entries, uint n_entries, bool compress, bool big_endian)
{
	if (!dest_car || !dest_car_size || !dest_toc || !dest_toc_size || !entries || !n_entries)
		return EINVAL;

	void (*w32) (u8 *, u32) = big_endian ? wr_be32 : wr_le32;

	// Build TOC String Table
	u8 *str_pool = MALLOC (65536);
	uint str_pool_cap = 65536;
	uint str_pool_len = 0;
	uint *name_offsets = CALLOC (n_entries, sizeof (uint));

	for (uint i = 0; i < n_entries; i++)
	{
		ccp fn = entries[i].name ? entries[i].name : "file";
		ccp slash = strrchr (fn, '/');
		if (slash)
			fn = slash + 1;

		size_t slen = strlen (fn) + 1;
		if (str_pool_len + slen > str_pool_cap)
		{
			str_pool_cap = (str_pool_cap + (uint)slen) * 2;
			str_pool = REALLOC (str_pool, str_pool_cap);
		}
		name_offsets[i] = str_pool_len;
		memcpy (str_pool + str_pool_len, fn, slen);
		str_pool_len += (uint)slen;
	}

	// Build uncompressed raw payload (starts logically at offset 0x80)
	uint uncompressed_payload_size = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		uncompressed_payload_size = (uncompressed_payload_size + 0x7F) & ~0x7Fu;
		uncompressed_payload_size += entries[i].size;
	}
	uncompressed_payload_size = (uncompressed_payload_size + 0x7F) & ~0x7Fu;

	u8 *raw_payload = CALLOC (1, uncompressed_payload_size);
	uint *file_offsets = CALLOC (n_entries, sizeof (uint));
	uint cur_off = 0;

	for (uint i = 0; i < n_entries; i++)
	{
		cur_off = (cur_off + 0x7F) & ~0x7Fu;
		file_offsets[i] = cur_off; // retail TOC offsets are relative to the uncompressed payload
		if (entries[i].data && entries[i].size)
			memcpy (raw_payload + cur_off, entries[i].data, entries[i].size);
		cur_off += entries[i].size;
	}

	u8 *final_payload = 0;
	uint final_payload_size = 0;
	uint compressed_size = uncompressed_payload_size;

	if (compress)
	{
		u8 *qlz_buf = 0;
		uint qlz_size = 0;
		enumError qerr
			= EncodeQuickLZ (&qlz_buf, &qlz_size, raw_payload, uncompressed_payload_size);
		if (!qerr && qlz_buf)
		{
			uint pmcr_len = 16 + qlz_size;
			uint pmcr_aligned = (pmcr_len + 0x7F) & ~0x7Fu;
			final_payload = CALLOC (1, pmcr_aligned);
			if (big_endian)
			{
				memcpy (final_payload, "rMCP", 4);
				wr_be32 (final_payload + 4, 0x19397e49);
				wr_be32 (final_payload + 8, qlz_size);
				wr_be32 (final_payload + 12, uncompressed_payload_size);
			}
			else
			{
				memcpy (final_payload, "PMCr", 4);
				wr_le32 (final_payload + 4, 0x19397e49);
				wr_le32 (final_payload + 8, qlz_size);
				wr_le32 (final_payload + 12, uncompressed_payload_size);
			}
			memcpy (final_payload + 16, qlz_buf, qlz_size);
			final_payload_size = pmcr_aligned;
			compressed_size = qlz_size;
			FREE (qlz_buf);
		}
		else
		{
			final_payload = raw_payload;
			final_payload_size = uncompressed_payload_size;
			raw_payload = 0;
		}
	}
	else
	{
		final_payload = raw_payload;
		final_payload_size = uncompressed_payload_size;
		raw_payload = 0;
	}

	if (raw_payload)
		FREE (raw_payload);

	// Build CAR buffer (0x80 header + final_payload)
	uint total_car_size = 0x80 + final_payload_size;
	u8 *car_buf = CALLOC (1, total_car_size);

	if (big_endian)
		memcpy (car_buf, "RST0", 4);
	else
		memcpy (car_buf, "0TSR", 4);

	w32 (car_buf + 0x04, 0x40); // header size
	w32 (car_buf + 0x08, 0x0e); // version
	w32 (car_buf + 0x0c, 0x03);
	w32 (car_buf + 0x10, total_car_size);
	w32 (car_buf + 0x14, 0x19397e49);
	w32 (car_buf + 0x18, 0x80); // data offset
	w32 (car_buf + 0x1c, 0);
	w32 (car_buf + 0x20, n_entries);
	w32 (car_buf + 0x24, uncompressed_payload_size);
	w32 (car_buf + 0x28, compressed_size);
	w32 (car_buf + 0x2c, compress ? 0x80 : 0);
	w32 (car_buf + 0x30, 0);
	w32 (car_buf + 0x34, 0x140);
	w32 (car_buf + 0x38, 0);
	w32 (car_buf + 0x3c, 0);

	memcpy (car_buf + 0x80, final_payload, final_payload_size);
	FREE (final_payload);

	// Build TOC buffer
	uint toc_hdr_size = 0x0C;
	uint toc_entries_size = (n_entries + 1) * 0x28;
	uint total_toc_size = toc_hdr_size + toc_entries_size + str_pool_len;
	u8 *toc_buf = CALLOC (1, total_toc_size);

	if (big_endian)
	{
		memcpy (toc_buf, "ETOCRES0", 8);
		wr_be32 (toc_buf + 0x08, 3);
	}
	else
	{
		memcpy (toc_buf, "0SERCOTE", 8);
		wr_le32 (toc_buf + 0x08, 3);
	}

	// Entry 0 (meta record)
	u8 *e0 = toc_buf + 0x0C;
	w32 (e0 + 0x00, 0);
	memcpy (e0 + 0x04, "!IGM", 4);
	w32 (e0 + 0x08, 0);
	w32 (e0 + 0x0C, 32);
	w32 (e0 + 0x10, 0x19397e49);
	w32 (e0 + 0x14, 0);
	w32 (e0 + 0x18, uncompressed_payload_size);
	w32 (e0 + 0x1C, compressed_size);
	w32 (e0 + 0x20, compress ? 0x80 : 0);
	w32 (e0 + 0x24, 0x140);

	// Entries 1..N
	for (uint i = 0; i < n_entries; i++)
	{
		u8 *ei = toc_buf + 0x0C + (i + 1) * 0x28;
		w32 (ei + 0x00, name_offsets[i]);

		ccp fn = entries[i].name ? entries[i].name : "file";
		ccp ext = strrchr (fn, '.');
		char typ[5] = " ATD";
		if (ext)
		{
			if (!strcasecmp (ext, ".tex") || !strcasecmp (ext, ".tm0"))
				memcpy (typ, " XET", 4);
			else if (!strcasecmp (ext, ".mod"))
				memcpy (typ, "LDOM", 4);
			else if (!strcasecmp (ext, ".val"))
				memcpy (typ, "TLAV", 4);
			else if (!strcasecmp (ext, ".can"))
				memcpy (typ, "nAhC", 4);
			else if (!strcasecmp (ext, ".lyt"))
				memcpy (typ, "TYAL", 4);
			else if (!strcasecmp (ext, ".fnt"))
				memcpy (typ, "STMP", 4);
		}
		memcpy (ei + 0x04, typ, 4);
		w32 (ei + 0x08, 0);
		w32 (ei + 0x0C, entries[i].size);
		w32 (ei + 0x10, file_offsets[i]);
		w32 (ei + 0x14, 0);
		w32 (ei + 0x18, 0);
		w32 (ei + 0x1C, 0);
		w32 (ei + 0x20, 0);
		w32 (ei + 0x24, 0);
	}

	memcpy (toc_buf + toc_hdr_size + toc_entries_size, str_pool, str_pool_len);

	FREE (str_pool);
	FREE (name_offsets);
	FREE (file_offsets);

	*dest_car = car_buf;
	*dest_car_size = total_car_size;
	*dest_toc = toc_buf;
	*dest_toc_size = total_toc_size;
	return ERR_OK;
}

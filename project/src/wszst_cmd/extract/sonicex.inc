// Sonic Team "_Ex" packs (Sonic and the Secret Rings "Now/_Ex1xx", no extension). Big-endian: u32 1, u8 count,
// 3 zero bytes, u32 id, count x u32 member offsets (the first one is the table end rounded up to 0x20), the last
// member ends at the file end. A member is either raw or wrapped as "compress v1.0\0" + u16 0x0040 + u32le size
// + stream (offset 0x14): Okumura LZSS with a 4096 byte ring buffer that is zero filled (not space filled),
// write position 0xfee, control bits LSB first (1 = literal), match = 12 bit ring position + 4 bit length - 3.
// Decoded members are written as "<NN>.<ext>" (".gvr" for GCIX textures, ".ngif" for NGIF layouts, else ".bin").
static bool sonic_lzss (u8 *out, uint out_size, const u8 *in, size_t in_size)
{
	u8 ring[4096] = {0};
	uint r = 0xfee, op = 0;
	size_t ip = 0;
	while (op < out_size && ip < in_size)
	{
		const uint c = in[ip++];
		for (uint b = 0; b < 8 && op < out_size; b++)
		{
			if (c >> b & 1)
			{
				if (ip >= in_size)
					return false;
				out[op++] = ring[r] = in[ip++];
				r = (r + 1) & 0xfff;
			}
			else
			{
				if (ip + 2 > in_size)
					return false;
				const uint pos = in[ip] | (in[ip + 1] & 0xf0) << 4, len = (in[ip + 1] & 15) + 3;
				ip += 2;
				for (uint k = 0; k < len && op < out_size; k++)
				{
					out[op++] = ring[r] = ring[(pos + k) & 0xfff];
					r = (r + 1) & 0xfff;
				}
			}
		}
	}
	return op == out_size;
}

static enumError extract_sonicex_file (ccp arg, ccp basedir, uint depth)
{
	const char *leaf = strrchr (arg, '/') ? strrchr (arg, '/') + 1 : arg;
	if (strncmp (leaf, "_Ex", 3))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 0x20 ? raw[4] : 0;
	if (rd_be32 (raw) != 1 || !count || count > 64 || raw[5] || raw[6] || raw[7]
		|| rd_be32 (raw + 12) != ((12 + 4 * count + 0x1f) & ~0x1fu))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (count, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	bool ok = true;
	for (uint i = 0; i < count && ok; i++)
	{
		const size_t off = rd_be32 (raw + 12 + 4 * i), end = i + 1 < count ? rd_be32 (raw + 16 + 4 * i) : sz;
		if (off >= end || end > sz)
		{
			ok = false;
			break;
		}
		const u8 *m = raw + off;
		size_t ml = end - off;
		u8 *dec = 0;
		if (ml > 0x14 && !memcmp (m, "compress v1.0", 14))
		{
			const uint size = m[0x10] | m[0x11] << 8 | m[0x12] << 16 | (uint)m[0x13] << 24;
			if (!size || size > (1u << 28) || !(dec = MALLOC (size)) || !sonic_lzss (dec, size, m + 0x14, ml - 0x14))
			{
				FREE (dec);
				ok = false;
				break;
			}
			m = dec;
			ml = size;
		}
		ccp ext = ml >= 4 && !memcmp (m, "GCIX", 4) ? "gvr" : ml >= 4 && !memcmp (m, "NGIF", 4) ? "ngif" : "bin";
		char name[32];
		snprintf (name, sizeof (name), "%02u.%s", i, ext);
		ok = OwnedEntryAdd (entries, n, name, m, (uint)ml);
		if (ok)
			n++;
		FREE (dec);
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (ok && n)
		err = write_owned_entries (arg, basedir, depth, "SONICEX", entries, n);
	else
		ResetOwnedEntries (entries, n);
	FREE (raw);
	return err;
}

// Natsume "add" PAC archives (Gintama: Yorozuya Chuubu - Tsukkomable Douga, Wii, `*.pac`; the layout is the one
// the QuickBMS `add_pac.bms` script describes for version <= 4): "add\0", u8 version at +4, table offset at +8, entry size
// at +0xc (0x20), u32 count at +0x10, file size at +0x14, then 0x20-byte entries {offset, size, 0, timestamp, name offset, 0, 0, 0} (big-endian on Wii,
// little-endian elsewhere) with NUL terminated names stored behind the table. Members named "*z" (brresz, brtexz,
// anmz ...) are Natsume "LZS\0" streams: u8 unit bits at +6 (8 or 16), u8 length bits at +5, u32 flag/match stream
// offset at +8, u32 literal pool offset at +0xc, u32 unpacked size at +0x10, u8 length threshold at +0x19. The flag
// stream is made of 16-bit words (bit 0 first, 1 = literal unit from the pool); a match word holds
// (distance << length bits) | (length - threshold), counted in units of unit-bits/8 bytes. The decoded member is
// stored without the trailing "z".
static bool addpac_lzs (const u8 *m, size_t msz, u8 **out, size_t *out_sz)
{
	*out = 0;
	if (msz < 0x20 || memcmp (m, "LZS", 4))
		return false;
	const uint nb = m[5], unit = m[6], thr = m[0x19];
	if (!nb || nb > 15 || (unit != 8 && unit != 16))
		return false;
	const size_t u = unit / 8, comd0 = rd_be32 (m + 8), rawd0 = rd_be32 (m + 0xc), dsz = rd_be32 (m + 0x10);
	if (comd0 < 0x20 || comd0 > rawd0 || rawd0 > msz || dsz > (256u << 20))
		return false;
	u8 *o = MALLOC (dsz ? dsz : 1);
	if (!o)
		return false;
	size_t c = comd0, r = rawd0, n = 0;
	const uint mask = (1u << nb) - 1;
	while (n < dsz)
	{
		if (c + 2 > rawd0)
			goto fail;
		uint flags = (m[c] << 8) | m[c + 1];
		c += 2;
		for (uint k = 0; k < 16 && n < dsz; k++, flags >>= 1)
		{
			if (flags & 1)
			{
				if (r + u > msz || n + u > dsz)
					goto fail;
				memcpy (o + n, m + r, u);
				r += u;
				n += u;
			}
			else
			{
				if (c + 2 > rawd0)
					goto fail;
				const uint w = (m[c] << 8) | m[c + 1];
				c += 2;
				const size_t len = ((w & mask) + thr) * u, dist = (size_t)(w >> nb) * u;
				if (!dist || dist > n || len > dsz - n)
					goto fail;
				for (size_t i = 0; i < len; i++, n++)
					o[n] = o[n - dist];
			}
		}
	}
	*out = o;
	*out_sz = dsz;
	return true;
fail:
	FREE (o);
	return false;
}

static enumError extract_addpac_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pac"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 || memcmp (raw, "add", 4) || raw[4] > 4)
		goto done;
	const bool be = rd_be32 (raw + 8) == 0x20 && rd_be32 (raw + 0xc) == 0x20;
	if (!be && (rd_le32 (raw + 8) != 0x20 || rd_le32 (raw + 0xc) != 0x20))
		goto done;
#define AD32(p) (be ? rd_be32 (p) : rd_le32 (p))
	const uint count = AD32 (raw + 0x10);
	if (!count || count > 100000 || 0x20 + 0x20 * (size_t)count > sz)
		goto done;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = raw + 0x20 + 0x20 * (size_t)i;
		const size_t o = AD32 (e), s = AD32 (e + 4), no = AD32 (e + 0x10);
		if (o > sz || s > sz - o || no >= sz || !memchr (raw + no, 0, sz - no))
			goto done;
	}
	nintendo_sarc_entry_t *entries = CALLOC (count, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	uint k = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = raw + 0x20 + 0x20 * (size_t)i;
		const size_t o = AD32 (e), s = AD32 (e + 4);
		if (!s)
			continue;
		char name[160];
		snprintf (name, sizeof (name), "%.150s", (ccp)raw + AD32 (e + 0x10));
		for (char *p = name; *p; p++)
			if (*p == '/' || *p == '\\' || (u8)*p < 0x20)
				*p = '_';
		if (!*name)
			snprintf (name, sizeof (name), "%04u.bin", i);
		for (uint j = 0; j < k; j++)
			if (!strcasecmp (entries[j].name, name))
			{
				char t[160];
				snprintf (t, sizeof (t), "%04u_%.150s", i, name);
				snprintf (name, sizeof (name), "%s", t);
				break;
			}
		u8 *dec = 0;
		size_t dsz = 0;
		bool good;
		if (addpac_lzs (raw + o, s, &dec, &dsz))
		{
			const size_t l = strlen (name);
			if (l > 1 && name[l - 1] == 'z')
				name[l - 1] = 0;
			good = OwnedEntryAdd (entries, k, name, dec, (uint)dsz);
			FREE (dec);
		}
		else
			good = OwnedEntryAdd (entries, k, name, raw + o, (uint)s);
		if (good)
			k++;
	}
#undef AD32
	if (k)
		err = write_owned_entries (arg, basedir, depth, "ADDPAC", entries, k);
	else
		ResetOwnedEntries (entries, count);
done:
	FREE (raw);
	return err;
}

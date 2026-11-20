// Infinity Ward / Treyarch fastfiles (".ff", Call of Duty: World at War, Wii, big endian): {"IWffu100", u32 version (0x19b)} followed by one zlib stream (a few footer bytes behind it) that inflates to the
// zone: a 0x30 byte header {u32 size, u32 external size, u32 block sizes ...}, the asset list and the asset records streamed behind it. A raw file asset (game scripts ".gsc", configs, menus, as plain text) is
// {u32 name pointer (-1 = inline), u32 length, u32 data pointer (-1)} followed by the NUL terminated name and the data (length bytes and a NUL). The extractor writes the whole inflated zone as "zone.bin" and
// every raw file it can follow (found by that record shape and a path-like name) under its name. Other asset types (images, models, sounds) stay inside zone.bin.
static bool iwff_name_ok (const u8 *p, size_t max, size_t *len)
{
	size_t n = 0;
	bool dot = false;
	while (n < max && n < 200 && p[n])
	{
		const u8 c = p[n];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '/' || c == '.' || c == '-' || c == '~'))
			return false;
		dot = dot || (c == '.' && n > 0);
		n++;
	}
	if (n < 3 || n >= 200 || n >= max || p[n] || !dot)
		return false;
	*len = n;
	return true;
}

static enumError extract_iw_ff_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".ff"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x20 || memcmp (raw, "IWffu100", 8) || sz > 0x40000000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t cap = sz * 6 + 0x10000, n = 0;
	u8 *zone = MALLOC (cap);
	z_stream zs;
	memset (&zs, 0, sizeof zs);
	bool ok = zone && inflateInit (&zs) == Z_OK;
	if (ok)
	{
		zs.next_in = raw + 12;
		zs.avail_in = (uInt)(sz - 12);
		int rc = Z_OK;
		while (ok && rc != Z_STREAM_END)
		{
			if (n == cap)
			{
				cap *= 2;
				if (cap > 0x30000000)
				{
					ok = false;
					break;
				}
				u8 *z2 = MALLOC (cap);
				if (!z2)
				{
					ok = false;
					break;
				}
				memcpy (z2, zone, n);
				FREE (zone);
				zone = z2;
			}
			zs.next_out = zone + n;
			zs.avail_out = (uInt)(cap - n);
			rc = inflate (&zs, Z_NO_FLUSH);
			n = cap - zs.avail_out;
			if (rc != Z_OK && rc != Z_STREAM_END)
				ok = false;
		}
		inflateEnd (&zs);
	}
	FREE (raw);
	if (!ok || n < 0x40 || rd_be32 (zone) + 0x30 > n + 0x30 * 2)
	{
		FREE (zone);
		return ERR_NOTHING_TO_DO;
	}
	// collect the raw files
	uint maxent = 2;
	for (size_t i = 0x30; i + 16 < n; i++)
		if (zone[i] == 0xff && zone[i + 1] == 0xff && zone[i + 2] == 0xff && zone[i + 3] == 0xff)
			maxent++;
	nintendo_sarc_entry_t *entries = CALLOC (maxent < 100000 ? maxent : 100000, sizeof (*entries));
	uint cnt = 0;
	bool good = entries && OwnedEntryAdd (entries, cnt, "zone.bin", zone, (uint)n);
	if (good)
		cnt++;
	size_t i = 0x30;
	while (good && i + 16 < n && cnt < 99999)
	{
		const u8 *s = zone + i;
		size_t nl;
		const u64 len = rd_be32 (s + 4);
		if (rd_be32 (s) == 0xffffffffu && rd_be32 (s + 8) == 0xffffffffu && len < 0x4000000 && iwff_name_ok (s + 12, n - i - 12, &nl) && i + 12 + nl + 1 + len <= n)
		{
			char name[220];
			snprintf (name, sizeof (name), "%s", (ccp)s + 12);
			good = OwnedEntryAdd (entries, cnt, name, s + 12 + nl + 1, (uint)len);
			if (good)
				cnt++;
			i += 12 + nl + 1 + len;
			continue;
		}
		i++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	FREE (zone);
	if (good && cnt)
		err = write_owned_entries (arg, basedir, depth, "IW-FF", entries, cnt);
	else
		FREE (entries);
	return err;
}

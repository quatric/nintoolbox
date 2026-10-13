// Nippon Ichi "WIIIDARC" sound archive (Phantom Brave: We Meet Again, Wii; "se.dat", "voice.dat",
// "voice_jp.dat"). Big-endian: "WIIIDARC", u32 count @8, count u16 sound ids from 0x20, then (aligned to
// 0x20) count 0x30-byte entries {char name[0x20], u32 0, u32 size @0x28, u32 offset @0x2c}. Every member
// is a ".wsd" wave: u32 0x20, two size words, a standard 0x60-byte DSP header at +0x20 and the ADPCM
// data at +0x80. Members are written as "<name>.dsp" (-N appended on repeated names).
static enumError extract_wiiidarc_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 0x40 ? rd_be32 (raw + 8) : 0;
	const size_t tab = ((size_t)0x20 + 2 * (size_t)count + 31) & ~(size_t)31;
	if (sz < 0x40 || memcmp (raw, "WIIIDARC", 8) || !count || count > 20000 || tab + (size_t)count * 0x30 > sz)
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
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = raw + tab + (size_t)i * 0x30;
		const u32 size = rd_be32 (e + 0x28), off = rd_be32 (e + 0x2c);
		if (size < 0x80 || (size_t)off + size > sz || rd_be32 (raw + off) != 0x20)
			break;
		const u8 *h = raw + off + 0x20;
		const size_t have = ((size_t)rd_be32 (h + 4) + 1) / 2; // the last frame may be cut short
		const size_t per = (((size_t)rd_be32 (h + 4) + 15) / 16) * 8;
		if (!have || have > size - 0x80 || rd_be16 (h + 0x0e))
			break;
		char base[0x21];
		memcpy (base, e, 0x20);
		base[0x20] = 0;
		char *dot = strrchr (base, '.');
		if (dot)
			*dot = 0;
		for (char *p = base; *p; p++)
			if (*p == '/' || *p == '\\' || (u8)*p < 0x20)
				*p = '_';
		if (!*base)
			snprintf (base, sizeof (base), "entry%04u", i);
		char name[0x30];
		uint dup = 0;
		for (;;)
		{
			snprintf (name, sizeof (name), dup ? "%s-%u.dsp" : "%s%.0u.dsp", base, dup);
			uint k = 0;
			while (k < n && strcmp (entries[k].name, name))
				k++;
			if (k == n)
				break;
			dup++;
		}
		u8 *out = CALLOC (1, 0x60 + per);
		if (!out)
			break;
		memcpy (out, h, 0x60);
		memcpy (out + 0x60, raw + off + 0x80, have);
		if (!rd_be32 (out + 0x18))
			wr_be32 (out + 0x18, 2); // current address left unset by the game's own loader
		const bool added = OwnedEntryAdd (entries, n, name, out, 0x60 + per);
		FREE (out);
		if (!added)
			break;
		n++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (n == count)
		err = write_owned_entries (arg, basedir, depth, "WIIIDARC", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}

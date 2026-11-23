// "LMPK" and "LMP2" level/map files (".ngr" of many DS puzzle and casual games, for example Jewel Legends, 5 in 1 Mahjong, 100 Classic Games), little-endian: {char "LMPK", u32 unpacked size, zlib stream} or {char "LMP2", the 13 byte header of a raw LZMA
// stream (properties byte, u32 dictionary size, u64 unpacked size) and the LZMA data}. The unpacked data is written as "<file without .ngr>.bin"; the unpacked size must match, and the data is not decoded further.
static enumError extract_lmpngr_file (ccp arg, ccp basedir, uint depth)
{
	u8 magic[4];
	FILE *f = fopen (arg, "rb");
	const bool hit = f && fread (magic, 1, 4, f) == 4 && (!memcmp (magic, "LMPK", 4) || !memcmp (magic, "LMP2", 4));
	if (f)
		fclose (f);
	if (!hit)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *out = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = 0;
	enumError err = ERR_NOTHING_TO_DO;
	uint outsz = 0;
	if (!memcmp (raw, "LMPK", 4))
	{
		const u64 us = sz >= 8 ? rd_le32 (raw + 4) : 0;
		if (!us || us > 0x10000000 || sz <= 8 || sz > UINT_MAX)
			goto done;
		out = MALLOC (us);
		z_stream z;
		memset (&z, 0, sizeof (z));
		if (!out || inflateInit (&z) != Z_OK)
			goto done;
		z.next_in = raw + 8;
		z.avail_in = sz - 8;
		z.next_out = out;
		z.avail_out = us;
		const int r = inflate (&z, Z_FINISH);
		outsz = z.total_out;
		inflateEnd (&z);
		if (r != Z_STREAM_END || outsz != us)
			goto done;
	}
	else
	{
		if (sz <= 17 || sz > UINT_MAX)
			goto done;
		const u64 us = rd_le32 (raw + 9) | (u64)rd_le32 (raw + 13) << 32;
		if (!us || us > 0x10000000 || DecodeLZMAprops (&out, (uint)us, raw + 4, raw + 17, (uint)sz - 17) != ERR_OK || !out)
			goto done;
		outsz = (uint)us;
	}
	entries = CALLOC (1, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	{
		const char *p = strrchr (arg, '/');
		char name[PATH_MAX];
		snprintf (name, sizeof (name), "%s", p ? p + 1 : arg);
		char *e = strrchr (name, '.');
		if (e && !strcasecmp (e, ".ngr"))
			*e = 0;
		const size_t l = strlen (name);
		snprintf (name + l, sizeof (name) - l, ".bin");
		if (OwnedEntryAdd (entries, 0, name, out, outsz))
		{
			err = write_owned_entries (arg, basedir, depth, "NGR", entries, 1);
			entries = 0;
		}
	}
done:
	if (entries)
		ResetOwnedEntries (entries, 1);
	FREE (out);
	FREE (raw);
	return err;
}

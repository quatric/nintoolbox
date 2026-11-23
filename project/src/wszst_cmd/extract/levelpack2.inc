// Fluid Studios "LevelPackV2" level files (Hydroventure, WiiWare: ".lpf2"), big-endian: {char "LevelPackV2\0", char version[8] ("00020001"), u32 unpacked size of the LZ77 layer (0 = none), u32 unpacked size of the Huffman
// layer (0 = none), u32 ? (0x5c8603ae in the sample), data at 0x20}. The data is Nintendo BIOS compression applied in two layers, as the game's loader does: first Huffman (type 0x20/0x28) when the second size is set, then LZ77
// (type 0x10) on the result when the first size is set. Written as "<file>.d/<name without .lpf2>.bin" (the level data is not decoded).
static enumError extract_levelpack2_file (ccp arg, ccp basedir, uint depth)
{
	u8 magic[12];
	FILE *f = fopen (arg, "rb");
	const bool hit = f && fread (magic, 1, 12, f) == 12 && !memcmp (magic, "LevelPackV2\0", 12);
	if (f)
		fclose (f);
	if (!hit)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *a = 0, *b = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = 0;
	enumError err = ERR_NOTHING_TO_DO;
	const u32 lzsz = sz > 0x20 ? rd_be32 (raw + 0x14) : 0, hfsz = sz > 0x20 ? rd_be32 (raw + 0x18) : 0;
	if (sz <= 0x20 || sz > UINT_MAX)
		goto done;
	const u8 *cur = raw + 0x20;
	uint cursz = (uint)(sz - 0x20);
	if (hfsz)
	{
		uint n = 0;
		if (DecodeNintendoHuff (&a, &n, cur, cursz) || !a || n != hfsz)
			goto done;
		cur = a;
		cursz = n;
	}
	if (lzsz)
	{
		uint n = 0;
		if (DecodeLZ10LZ11 (&b, &n, cur, cursz) || !b || n != lzsz)
			goto done;
		cur = b;
		cursz = n;
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
		if (e && !strcasecmp (e, ".lpf2"))
			*e = 0;
		const size_t l = strlen (name);
		snprintf (name + l, sizeof (name) - l, ".bin");
		if (OwnedEntryAdd (entries, 0, name, cur, cursz))
		{
			err = write_owned_entries (arg, basedir, depth, "LPF2", entries, 1);
			entries = 0;
		}
	}
done:
	if (entries)
		ResetOwnedEntries (entries, 1);
	FREE (a);
	FREE (b);
	FREE (raw);
	return err;
}

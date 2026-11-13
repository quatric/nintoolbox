// Rockstar Leeds streamed animation archives (Manhunt 2 "strmanim_wii.bin", big endian): a small index of u32 words that holds {offset, size} pairs (offsets aligned to 0x800) next to
// group markers, and the animation packs themselves, each starting "ANPK" u32 3, "NAME" {u32 length, animation name} like the packs in an ".ifp". The pairs are found by checking
// that they point at an "ANPK" tag inside the file; every pack is written as "<name>.bin.d/NNNN_<animation name>.anpk" (keyframe data not decoded).
static enumError extract_mh2_strmanim_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x1000 || rd_be32 (c) > 64 || rd_be32 (c + 16) != 0x800)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = 0;
	uint n = 0, cap = 0;
	enumError err = ERR_NOTHING_TO_DO;
	size_t last = 0;
	for (size_t i = 4; i + 8 <= 0x800 && i + 8 <= cs; i += 4)
	{
		const size_t off = rd_be32 (c + i), sz = rd_be32 (c + i + 4);
		if (off < 0x800 || (off & 0x7ff) || off <= last || sz < 32 || off > cs || sz > cs - off || memcmp (c + off, "ANPK", 4))
			continue;
		char name[96] = "anim";
		if (!memcmp (c + off + 8, "NAME", 4))
		{
			size_t nl = rd_be32 (c + off + 12);
			if (nl && nl < 80 && off + 16 + nl <= cs)
			{
				snprintf (name, sizeof name, "%.*s", (int)nl, (ccp)c + off + 16);
				for (char *k = name; *k; k++)
					if (!isalnum ((u8)*k) && *k != '_' && *k != '-')
						*k = '_';
			}
		}
		char path[PATH_MAX];
		snprintf (path, sizeof path, "%04u_%.70s.anpk", n, name);
		if (n == cap)
		{
			cap = cap ? 2 * cap : 64;
			entries = REALLOC (entries, cap * sizeof (*entries));
			if (!entries)
			{
				err = ERR_OUT_OF_MEMORY;
				goto done;
			}
			memset (entries + n, 0, (cap - n) * sizeof (*entries));
		}
		if (!OwnedEntryAdd (entries, n, path, c + off, (uint)sz))
			goto done;
		n++;
		last = off;
		i += 4;
	}
	if (n)
	{
		err = write_owned_entries (arg, basedir, depth, "MH2-STRMANIM", entries, n);
		entries = 0;
	}
done:
	if (entries)
		ResetOwnedEntries (entries, n);
	FREE (c);
	return err;
}

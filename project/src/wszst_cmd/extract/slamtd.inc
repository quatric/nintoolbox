// Slam Games engine texture banks (".std", "SLAM TD\0"; Tele 7 Jeux, Margot's Word Brain), little-endian: the same
// 16 byte head and chunk list as the audio banks ("DFTR", then one "IMAG" per image). An IMAG payload is
// {u16 width, u16 height, u32 0, u16 format, u16 7, u32 0, u32 PGF size, NUL terminated name, PGF image}. The
// images are PGF wavelet files; they are split out unchanged as "<name>.pgf" (the wavelet codec itself is not decoded).
static enumError extract_slamtd_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".std"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x30 || memcmp (raw, "SLAM TD\0", 8))
		goto done;
	uint n_img = 0;
	for (size_t o = 16; o + 8 <= sz;)
	{
		const size_t n = rd_le32 (raw + o + 4);
		if (n > sz - o - 8)
			goto done;
		if (!memcmp (raw + o, "IMAG", 4))
			n_img++;
		o += 8 + n;
	}
	if (!n_img || n_img > 100000)
		goto done;
	nintendo_sarc_entry_t *entries = CALLOC (n_img, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	uint k = 0;
	for (size_t o = 16; o + 8 <= sz;)
	{
		const size_t n = rd_le32 (raw + o + 4);
		const u8 *p = raw + o + 8;
		if (!memcmp (raw + o, "IMAG", 4) && n > 20 + 4)
		{
			const u8 *z = memchr (p + 20, 0, n - 20);
			const size_t psz = rd_le32 (p + 16);
			if (z && psz >= 4 && psz <= n - (size_t)(z + 1 - p) && !memcmp (z + 1, "PGF", 3))
			{
				char label[96];
				uint l = 0;
				for (const u8 *c = p + 20; c < z && l < sizeof (label) - 1; c++, l++)
					label[l] = (*c >= 0x20 && *c < 0x7f && *c != '/' && *c != '\\' && *c != ':') ? (char)*c : '_';
				label[l] = 0;
				char name[160];
				snprintf (name, sizeof (name), "%s.pgf", *label ? label : "image");
				for (uint j = 0; j < k; j++)
					if (!strcasecmp (entries[j].name, name))
					{
						snprintf (name, sizeof (name), "%s_%u.pgf", *label ? label : "image", k);
						break;
					}
				if (OwnedEntryAdd (entries, k, name, z + 1, (uint)psz))
					k++;
			}
		}
		o += 8 + n;
	}
	if (k)
		err = write_owned_entries (arg, basedir, depth, "SLAMTD", entries, k);
	else
		ResetOwnedEntries (entries, n_img);
done:
	FREE (raw);
	return err;
}

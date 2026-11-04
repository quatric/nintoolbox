// Atlus ".pak" (Trauma Center: Second Opinion / New Blood, Trauma Team; Wii): a flat sequence of members,
// each a 0x100-byte header {char name[0xfc] (NUL padded, may contain '/'), u32 LE size} followed by the
// payload, padded to a 0x40 boundary; the stream ends with a 0x100-byte terminator. Members are
// usually LZ10-packed ("10" + 24-bit size); those are unpacked while extracting.
static enumError extract_atluspak_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pak"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (sz / 0x100 + 1, sizeof (*entries));
	uint n = 0;
	bool good = entries != 0;
	size_t p = 0;
	while (good && p + 0x100 <= sz)
	{
		const u8 *h = raw + p;
		uint len = 0;
		while (len < 0xfc && h[len])
			len++;
		if (!len || len == 0xfc)
			break;
		bool ok = true;
		for (uint i = 0; i < 0xfc; i++)
			if (i < len ? (h[i] < 0x20 || h[i] > 0x7e) : h[i] != 0)
				ok = false;
		const size_t psz = rd_le32 (h + 0xfc);
		if (!ok || psz > sz - p - 0x100)
			break;
		// LZ10 members ("10" + 24-bit unpacked size) are unpacked here so the tree continues below them
		const u8 *pl = raw + p + 0x100;
		u8 *dec = 0;
		uint dsz = 0;
		size_t out_sz = psz;
		if (psz > 4 && pl[0] == 0x10 && !DecodeLZ10LZ11 (&dec, &dsz, pl, (uint)psz) && dec
		    && dsz == (uint)(pl[1] | pl[2] << 8 | pl[3] << 16))
		{
			pl = dec;
			out_sz = dsz;
		}
		else
		{
			FREE (dec);
			dec = 0;
		}
		// duplicate names (seen in the model packs) get "~<index>" before the extension
		char mname[0x110];
		snprintf (mname, sizeof (mname), "%s", (ccp)h);
		for (uint i = 0; i < n; i++)
			if (!strcmp (entries[i].name, mname))
			{
				char *dot = strrchr (mname, '.');
				char ext[0x20] = "";
				if (dot && strlen (dot) < sizeof (ext) && !strchr (dot, '/'))
				{
					snprintf (ext, sizeof (ext), "%s", dot);
					*dot = 0;
				}
				snprintf (mname + strlen (mname), sizeof (mname) - strlen (mname), "~%u%s", n, ext);
				break;
			}
		good = OwnedEntryAdd (entries, n, mname, pl, (uint)out_sz);
		FREE (dec);
		if (good)
			n++;
		p = (p + 0x100 + psz + 0x3f) & ~(size_t)0x3f;
	}
	// the walk must account for the whole file (up to the 0x100-byte terminator)
	enumError err = ERR_NOTHING_TO_DO;
	if (good && n && (p == sz || p + 0x100 == sz))
		err = write_owned_entries (arg, basedir, depth, "ATLUSPAK", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}

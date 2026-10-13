// Clover / Okami (Wii) offset-table containers (".dat", ".bin", ".i" inside the _data AFS archives). Big-endian:
// u32 count, count u32 member offsets (strictly increasing, 0x20 aligned, the first behind the table); a member
// runs to the next offset (the last to the end of the file). Members are written as "NN.<ext>" with the extension
// taken from the content: "bres" -> .brres, an Okami texture ("TEX" body, see okamit.inc) -> .t, the tags mtb3 /
// scr / SCA / MEH ... -> .mtb / .scr / .sca / .meh ..., otherwise .bin; a table none of whose members is recognised is
// left alone; the recursion then decodes the BRRES models / textures and the .t images.
static enumError extract_okamidat_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".dat") && !is_ext (arg, ".bin") && !is_ext (arg, ".i"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 8 ? rd_be32 (raw) : 0;
	bool ok = count >= 2 && count <= 4096 && 4 + 4 * (size_t)count <= sz;
	for (uint i = 0; ok && i < count; i++)
	{
		const size_t o = rd_be32 (raw + 4 + 4 * i);
		ok = !(o & 0x1f) && o >= 4 + 4 * (size_t)count && o < sz && (!i || o > rd_be32 (raw + 4 * i));
	}
	if (!ok)
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
	uint n = 0, known = 0;
	for (uint i = 0; i < count && ok; i++)
	{
		const size_t o = rd_be32 (raw + 4 + 4 * i), e = i + 1 < count ? rd_be32 (raw + 8 + 4 * i) : sz;
		const u8 *m = raw + o;
		char tag[8] = "bin";
		ccp ext = tag;
		if (e - o >= 16)
		{
			static const char *const tags[] = { "mtb3", "scr", "SCA", "MEH", "BMH", "SSD", "MRT", "SEH", "SCL", "SCI",
				"SCM", "ISL" };
			if (!memcmp (m, "bres", 4))
				ext = "brres", known++;
			else if (rd_be32 (m + 8) == 0xc && rd_be32 (m + 4) && rd_be32 (m + 4) < 64 && rd_be32 (m) == 0x20af30)
				ext = "t", known++;
			else
				for (uint k = 0; k < sizeof (tags) / sizeof (*tags); k++)
					if (!memcmp (m, tags[k], strlen (tags[k])) && (!m[strlen (tags[k])] || tags[k][3] == '3'))
					{
						snprintf (tag, sizeof (tag), "%s", !strcmp (tags[k], "mtb3") ? "mtb" : tags[k]);
						for (char *q = tag; *q; q++)
							*q = (char)tolower ((u8)*q);
						known++;
						break;
					}
		}
		char name[32];
		snprintf (name, sizeof (name), "%03u.%s", i, ext);
		ok = OwnedEntryAdd (entries, n, name, m, (uint)(e - o));
		n += ok;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (ok && n && known)
		err = write_owned_entries (arg, basedir, depth, "OKAMI", entries, n);
	else
		ResetOwnedEntries (entries, n);
	FREE (raw);
	return err;
}

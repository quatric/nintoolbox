// Manhunt 2 (Wii) "MH2Z" wrapper (".txd", ".bsp", ".dff", ".ifp"): magic "MH2Z", u32 unpacked size (big-endian), then one zlib stream. The unpacked file (a TXD texture
// dictionary, a "WRLD" world, a "CLMP" model or an IFP animation) is written into "<file>.d" under the same name, so the usual extractors (TXD textures) run on it.
static enumError extract_mh2z_file (ccp arg, ccp basedir, uint depth)
{
	u8 magic[4];
	FILE *f = fopen (arg, "rb");
	const bool hit = f && fread (magic, 1, 4, f) == 4 && !memcmp (magic, "MH2Z", 4);
	if (f)
		fclose (f);
	if (!hit)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *body = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 12)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const size_t us = rd_be32 (raw + 4);
	if (!us || us > 0x40000000)
		goto done;
	body = MALLOC (us);
	uLongf dl = us;
	if (!body || uncompress (body, &dl, raw + 8, sz - 8) != Z_OK || dl != us)
		goto done;
	const char *sl = strrchr (arg, '/');
	nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
	if (!entries)
		goto done;
	if (!OwnedEntryAdd (entries, 0, sl ? sl + 1 : arg, body, (uint)us))
	{
		FREE (entries);
		goto done;
	}
	err = write_owned_entries (arg, basedir, depth, "MH2Z", entries, 1);
done:
	FREE (body);
	FREE (raw);
	return err;
}

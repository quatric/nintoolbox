// Sonic Team ".gncp" GUI packages ("CPAF", u32be size of the layout part; Sonic and the Secret Rings /
// Sonic and the Black Knight): a chunked 2D layout ("NGIF", "nCPJ", ".TXL" texture list, "NGTL" ...) is followed by
// the textures it uses, complete GVR files ("GCIX" / "GBIX" + "GVRT", payload size in the LE u32 at +0x14) stored
// back to back. The layout chunks are not decoded; the textures are written as "tex<NNN>.gvr" (the GVR handler
// turns them into PNG).
static enumError extract_gncp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".gncp"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x40 || memcmp (raw, "CPAF", 4) || rd_be32 (raw + 4) > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	// texture start: first "GCIX"/"GBIX" followed by "GVRT" at +0x10
	size_t p = 0x20;
	nintendo_sarc_entry_t *entries = 0;
	uint n = 0, cap = 0;
	bool ok = true;
	while (p + 0x20 <= sz && ok)
	{
		if ((memcmp (raw + p, "GCIX", 4) && memcmp (raw + p, "GBIX", 4)) || memcmp (raw + p + 0x10, "GVRT", 4))
		{
			p++;
			continue;
		}
		size_t len = 0x18 + (size_t)raw[p + 0x14] + ((size_t)raw[p + 0x15] << 8) + ((size_t)raw[p + 0x16] << 16)
			+ ((size_t)raw[p + 0x17] << 24);
		if (len > sz - p)
			len = sz - p;
		if (n == cap)
		{
			cap = cap ? cap * 2 : 16;
			nintendo_sarc_entry_t *ne = REALLOC (entries, cap * sizeof (*entries));
			if (!ne)
			{
				ok = false;
				break;
			}
			entries = ne;
		}
		char name[32];
		snprintf (name, sizeof (name), "tex%03u.gvr", n);
		ok = OwnedEntryAdd (entries, n, name, raw + p, (uint)len);
		if (ok)
			n++;
		p += len;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (ok && n)
		err = write_owned_entries (arg, basedir, depth, "GNCP", entries, n);
	else
		ResetOwnedEntries (entries, n);
	FREE (raw);
	return err;
}

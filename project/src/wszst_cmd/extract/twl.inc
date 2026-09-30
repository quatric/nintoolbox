// Nintendo DSi packaging: installable ".tad" titles (the DSi's WAD), the
// SD-card "DSiWare export" .bin files, and the modcrypt layer of DSi SRLs
// (.nds/.dsi/.srl). Crypto follows WinterMute/twltool; see lib-twl.h.
//
// Only files that prove themselves are claimed: a .tad needs a valid ticket
// and TMD, a .bin must decrypt its first ES block with the fixed key and
// verify its MAC, and an SRL must have the modcrypt flag set. Everything
// else falls through to the other extractors.
static enumError extract_twl_file (ccp arg, ccp basedir, uint depth)
{
	const bool tad = is_ext (arg, ".tad");
	const bool bin = is_ext (arg, ".bin");
	const bool srl = is_ext (arg, ".nds") || is_ext (arg, ".dsi") || is_ext (arg, ".srl");
	if (!tad && !bin && !srl)
		return ERR_NOTHING_TO_DO;

	// cheap prefix probe, so a big unrelated .bin/.nds is never read whole
	if (bin || srl)
	{
		FILE *f = fopen (arg, "rb");
		if (!f)
			return ERR_NOTHING_TO_DO;
		u8 head[0x4100];
		const size_t got = fread (head, 1, sizeof (head), f);
		fclose (f);
		if (bin ? !IsDsiExportBin (head, got) : !TwlSrlIsModcrypted (head, got))
			return ERR_NOTHING_TO_DO;
	}

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	ccp tag = "DSI-TAD";
	if (tad)
		err = ScanDsiTad (&entries, &n_entries, raw, raw_size);
	else if (bin)
	{
		tag = "DSI-EXPORT";
		err = ScanDsiExportBin (&entries, &n_entries, raw, raw_size);
	}
	else
	{
		tag = "DSI-MODCRYPT";
		// decrypt in place and hand the buffer over as the single entry
		if (raw_size > UINT_MAX || !TwlSrlModcrypt (raw, raw_size)
			|| !TwlSrlClearModcryptFlag (raw, raw_size))
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		entries = CALLOC (1, sizeof (*entries));
		if (!entries)
		{
			FREE (raw);
			return ERR_OUT_OF_MEMORY;
		}
		char name[64];
		snprintf (name, sizeof (name), "decrypted%s", is_ext (arg, ".dsi") ? ".dsi" : ".srl");
		entries[0].name = STRDUP (name);
		entries[0].data = raw;
		entries[0].size = (uint)raw_size;
		n_entries = 1;
		raw = 0;
		err = ERR_OK;
	}
	FREE (raw);
	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, tag, entries, n_entries);
}

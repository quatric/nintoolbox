// Sumo Digital "SSR_Wii" engine tool resource container (Sonic & Sega
// All-Stars Racing, Wii .stz): a single zlib stream wrapping one FourCC-tagged
// chunk blob. Written out as one member so it goes through the same
// beside_source_dest() naming as every other archive extractor.
static enumError extract_sumostz_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (!IsSumoSTZ (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u8 *dec = 0;
	uint dec_size = 0;
	char tag[4];
	const enumError derr = DecodeSumoSTZ (&dec, &dec_size, raw, raw_size, tag);
	FREE (raw);
	if (derr || !dec_size)
	{
		FREE (dec);
		return ERR_NOTHING_TO_DO;
	}

	char ext[8];
	bool ascii = true;
	for (uint i = 0; i < 4 && ascii; i++)
		ascii = tag[i] >= 'A' && tag[i] <= 'Z';
	if (ascii)
		snprintf (ext, sizeof (ext), "%c%c%c%c", tolower ((u8)tag[0]), tolower ((u8)tag[1]),
			tolower ((u8)tag[2]), tolower ((u8)tag[3]));
	else
		snprintf (ext, sizeof (ext), "bin");

	nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
	char name[32];
	snprintf (name, sizeof (name), "data.%s", ext);
	const bool added = entries && OwnedEntryAdd (entries, 0, name, dec, dec_size);
	FREE (dec);
	if (!added)
	{
		FREE (entries);
		return ERR_CANT_CREATE;
	}
	return write_owned_entries (arg, basedir, depth, "STZ", entries, 1);
}

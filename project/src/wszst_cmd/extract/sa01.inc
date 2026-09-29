// Mii Maker (Wii U) "SA01" and amiibo Settings (3DS) "CA01", from
// mii_maker.bms / amiibo.bms. Every accepted outer wrapper has to decode to
// an inner SA01/CA01 image, so the heuristic bare-size+zlib form can't fire
// on an unrelated zlib file.
static enumError extract_sa01_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size > UINT_MAX || raw_size < 12)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u8 *inner = 0;
	uint inner_size = 0;
	const enumError derr = DecodeSA01Container (&inner, &inner_size, raw, (uint)raw_size);
	FREE (raw);
	if (derr || !inner)
	{
		FREE (inner);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	const enumError serr = ScanSA01 (&entries, &n_entries, inner, inner_size);
	const bool named = !memcmp (inner, "SA01", 4);
	FREE (inner);
	if (serr || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, named ? "SA01" : "CA01", entries, n_entries);
}

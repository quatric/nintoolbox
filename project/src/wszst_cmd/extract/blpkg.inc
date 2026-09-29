// Barking Lizards Technologies "pkg\0" script/data archive (Nickelodeon:
// The Naked Brothers Band - The Video Game, Wii); see lib-blpkg.h for the
// layout. Content-sniffed by magic, not by extension, since real samples
// use plain ".pkg" alongside an unrelated existing ".pkg" format.
static enumError extract_blpkg_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX || !IsBLPKG (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = ScanBLPKG (&entries, &n_entries, raw, (uint)raw_size);
	FREE (raw);
	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "PKG (Barking Lizards)", entries, n_entries);
}

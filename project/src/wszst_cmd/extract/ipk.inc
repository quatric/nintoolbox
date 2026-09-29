// Ubisoft UbiArt IPK archive (Just Dance, Rayman Origins/Legends;
// Wii / Wii U / Switch / PC), ported from just_dance_2014.bms and the
// PartyService/ubiart-archive-tools packer semantics. Extract-only,
// like CPK: members may be zlib- or LZMA-compressed.
static enumError extract_ipk_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".ipk"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = ScanIPK (&entries, &n_entries, raw, (uint)raw_size);
	FREE (raw);
	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "IPK", entries, n_entries);
}

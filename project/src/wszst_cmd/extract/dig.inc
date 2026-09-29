// Bomberman Land "cddata*.dig" streaming resource package (Wii); see
// lib-dig.h for the layout. Extract-only; members carry no on-disk name
// and are emitted as numbered blobs. The unsupported "type == 2" composite
// variant (5 of 15 real samples) is rejected by ScanDIG(), not guessed at.
static enumError extract_dig_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".dig"))
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
	err = ScanDIG (&entries, &n_entries, raw, (uint)raw_size);
	FREE (raw);
	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "DIG", entries, n_entries);
}

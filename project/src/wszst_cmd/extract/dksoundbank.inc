// Donkey Konga "CHDp"/"C3Dp" hit-sound bank (.chd/.c3d; Donkey Konga /
// Donkey Konga 2 / Donkey Konga 3, GameCube; files/se/*.chd+*.cbd or
// *.c3d+*.cbd) -> one .wav per named clip. Triggers on the sibling ".cbd"
// existing next to the header file, like extract_lspk_file()'s .pk/.pkh
// pair -- see lib-donkeykonga.h for the full layout.
static enumError extract_dksoundbank_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".chd") && !is_ext (arg, ".c3d"))
		return ERR_NOTHING_TO_DO;

	// Sibling-*input* lookup: must stay next to 'arg' on disk regardless of
	// --dest (which only ever renames the *output*).
	char cbd_path[PATH_MAX];
	{
		char base[PATH_MAX];
		const uint len = snprintf (base, sizeof (base), "%s", arg);
		if (len >= sizeof (base))
			return ERR_NOTHING_TO_DO;
		char *dot = strrchr (base, '.');
		char *slash = strrchr (base, '/');
		if (dot && (!slash || dot > slash))
			*dot = 0;
		snprintf (cbd_path, sizeof (cbd_path), "%s.cbd", base);
	}
	struct stat st;
	if (stat (cbd_path, &st) != 0 || !S_ISREG (st.st_mode))
		return ERR_NOTHING_TO_DO;

	u8 *chd_raw = 0;
	size_t chd_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &chd_raw, &chd_size, 0, 0, 0, false) || !chd_raw)
	{
		FREE (chd_raw);
		return ERR_NOTHING_TO_DO;
	}
	if (chd_size > UINT_MAX || !IsDKSoundBank (chd_raw, chd_size))
	{
		FREE (chd_raw);
		return ERR_NOTHING_TO_DO;
	}

	u8 *cbd_raw = 0;
	size_t cbd_size = 0;
	if (LoadFileAlloc (cbd_path, 0, 0, &cbd_raw, &cbd_size, 0, 0, 0, false) || cbd_size > UINT_MAX)
	{
		FREE (chd_raw);
		FREE (cbd_raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	const enumError serr
		= ScanDKSoundBank (&entries, &n_entries, chd_raw, chd_size, cbd_raw, cbd_size);
	FREE (chd_raw);
	FREE (cbd_raw);
	if (serr || !n_entries)
		return ERR_NOTHING_TO_DO;

	return write_owned_entries (arg, basedir, depth, "DKSB", entries, n_entries);
}

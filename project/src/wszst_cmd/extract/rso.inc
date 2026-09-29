// Nintendo RSO relocatable modules (Skylanders: SuperChargers Racing
// gamelogic.rso and similar PowerPC dynamic-load modules). Extract-only;
// see lib-rso.h for the layout and the no-magic detection heuristic.
//
// Worms: Battle Islands (Wii, Team17) ships the identical on-disk layout
// under DataWii/Wow3.sel (byte-for-byte the same header/section-table
// shape as its sibling DataWii/Modules.rso -- confirmed by hand: same
// zeroed link pointers, section_info_offset==0x58, and a clean printable
// module name at name_offset, "c:\...\Wow3.elf"); ".sel" just appears to
// be this title's own name for a loadable RSO module, so it's accepted
// here alongside ".rso" rather than treated as a separate format.
static enumError extract_rso_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".rso") && !is_ext (arg, ".sel"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = ScanRSO (&entries, &n_entries, raw, raw_size);
	FREE (raw);
	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "RSO", entries, n_entries);
}

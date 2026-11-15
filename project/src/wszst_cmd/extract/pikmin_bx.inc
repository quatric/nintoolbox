// Pikmin 1 sound bank "pikibank.bx": a table of {offset, size} records followed by JAudio "IBNK" instrument banks and
// "WSYS" wave systems (offsets inside a WSYS are relative to it). The waves live in the sibling ".aw" files
// (SndData/Banks/<name>.aw, raw AFC) and are written as "wsys<n>/<aw>/<nnnn>.wav".
static enumError extract_pikmin_bx_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bx"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t cap = 64, n = 0;
	nintendo_sarc_entry_t *entries = CALLOC (cap, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint id = 0;
	for (size_t p = 0; p + 0x18 <= sz; p += 4)
		if (!memcmp (raw + p, "WSYS", 4))
			jaudio_wsys_waves (arg, raw, sz, p, id++, &entries, &n, &cap);
	FREE (raw);
	if (!n)
	{
		FREE (entries);
		return ERR_NOTHING_TO_DO;
	}
	return write_owned_entries (arg, basedir, depth, "PIKMIN-BX", entries, n);
}

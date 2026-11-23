// Mix Superstar ".mxs" samples (WiiWare): a small chunk-table header of varying length (0x20 or 0x40 bytes in the files seen) in front of one standard GameCube DSP-ADPCM stream ({u32 samples, u32 nibbles, u32 rate, u16 loop flag, u16 format 0,
// u32 loop start, u32 loop end, u32 current address (2), 16 s16 coefficients, ...}, 0x60 bytes, then the ADPCM frames). The header is found by scanning the first 0x100 bytes for a plausible DSP header (nibbles = whole 14-sample frames times 16, plus the
// rest of a partial frame + 2, loop end = nibbles - 1, rate 4000-96000, 8 bytes of data per 16 nibbles inside the file). The header and frames are written as "<file>.d/<file>.dsp" for the DSP decoder (WAV); the chunk table is not interpreted.
static enumError extract_mxsdsp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".mxs"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	for (size_t b = 0; b + 0x60 <= sz && b <= 0x100; b += 4)
	{
		const u64 samples = rd_be32 (raw + b), nibbles = rd_be32 (raw + b + 4), rate = rd_be32 (raw + b + 8);
		if (!samples || samples > 0x10000000 || rate < 4000 || rate > 96000 || (samples / 14) * 16 + (samples % 14 ? samples % 14 + 2 : 0) != nibbles
			|| rd_be32 (raw + b + 0x14) != nibbles - 1 || rd_be32 (raw + b + 0x18) != 2 || rd_be16 (raw + b + 0x0c) > 1 || rd_be16 (raw + b + 0x0e))
			continue;
		const size_t dl = (size_t)((nibbles + 15) / 16) * 8;
		if (b + 0x60 + dl > sz)
			continue;
		nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
		const char *n = strrchr (arg, '/');
		char name[PATH_MAX];
		snprintf (name, sizeof (name), "%s.dsp", n ? n + 1 : arg);
		if (!entries)
			err = ERR_OUT_OF_MEMORY;
		else if (OwnedEntryAdd (entries, 0, name, raw + b, (uint)(0x60 + dl)))
			err = write_owned_entries (arg, basedir, depth, "MXS", entries, 1);
		else
			ResetOwnedEntries (entries, 1);
		break;
	}
	FREE (raw);
	return err;
}

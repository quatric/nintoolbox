// "_NGCSD02" sound stream (Totally Spies! Totally Party, unpacked from BIGFILE members):
//   +0x00 "_NGCSD02", +0x08 f32 duration, +0x0c sample count, +0x12 u8 flag (0 mono, 1 mono + trailing
//   data, 2 stereo + trailing data), then one standard 0x60-byte DSP header per channel from +0x18, then
//   the ADPCM frames interleaved one 8-byte frame per channel. Anything after the frames (a lower-rate
//   copy / cue data) is ignored. Each channel becomes a standard "audio.dsp" / "chN.dsp".
static enumError extract_ngcsd_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".ngcsd"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x78 || memcmp (raw, "_NGCSD02", 8) || raw[0x12] > 2)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint nch = raw[0x12] ? raw[0x12] : 1;
	const size_t base = 0x18 + 0x60 * (size_t)nch, ns = rd_be32 (raw + 0x18), frames = (ns + 13) / 14;
	if (!ns || base + frames * 8 * nch > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (nch, sizeof (*entries));
	uint n = 0;
	bool good = entries != 0;
	for (uint c = 0; c < nch && good; c++)
	{
		u8 *out = CALLOC (1, 0x60 + frames * 8);
		if (!out)
		{
			good = false;
			break;
		}
		memcpy (out, raw + 0x18 + 0x60 * c, 0x60);
		for (size_t f = 0; f < frames; f++)
			memcpy (out + 0x60 + f * 8, raw + base + (f * nch + c) * 8, 8);
		char name[16];
		if (nch == 1)
			snprintf (name, sizeof (name), "audio.dsp");
		else
			snprintf (name, sizeof (name), "ch%u.dsp", c);
		good = OwnedEntryAdd (entries, n, name, out, (uint)(0x60 + frames * 8));
		FREE (out);
		if (good)
			n++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (good && n)
		err = write_owned_entries (arg, basedir, depth, "NGCSD", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}

// Kylotonn engine ".snd" sound members (Cocoto Festival, Wii): a zero u32, then little-endian chunks
// {tag, u32 size including the 8 header bytes}: NAME, FREQ, STER, SIZE, CSZE, SAMP. The SAMP payload is a standard
// 0x60-byte GameCube/Wii DSP header plus ADPCM data with every 16-bit word byte-swapped (the file was
// written by a little-endian tool). Swapping the words back gives a plain mono ".dsp", written as "ch0.dsp".
static enumError extract_kylotonn_snd_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".snd"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u8 *samp = 0;
	size_t slen = 0;
	if (sz >= 0x20 && !rd_be32 (raw) && !memcmp (raw + 4, "NAME", 4))
	{
		for (size_t p = 4; p + 8 <= sz;)
		{
			const u32 cs = raw[p + 4] | raw[p + 5] << 8 | raw[p + 6] << 16 | (u32)raw[p + 7] << 24;
			if (cs < 8 || cs > sz - p)
				break;
			if (!memcmp (raw + p, "SAMP", 4))
			{
				samp = raw + p + 8;
				slen = cs - 8;
				break;
			}
			p += cs;
		}
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (samp && slen >= 0x62)
	{
		const u32 nib0 = (u32)samp[5] << 24 | samp[4] << 16 | samp[7] << 8 | samp[6]; // BE nibble count after the word swap
		const size_t want = 0x60 + (((size_t)nib0 + 15) / 16) * 8;
		u8 *out = CALLOC (1, want > slen ? want : slen);
		if (!out)
		{
			FREE (raw);
			return ERR_OUT_OF_MEMORY;
		}
		for (size_t i = 0; i + 1 < slen; i += 2)
		{
			out[i] = samp[i + 1];
			out[i + 1] = samp[i];
		}
		if (slen & 1)
			out[slen - 1] = samp[slen - 1];
		const u32 nibbles = rd_be32 (out + 4);
		size_t per = (((size_t)nibbles + 15) / 16) * 8;
		if (per && rd_be16 (out + 0x0e) == 0 && per <= 0x1000000) // the final frame may be cut short (zero-filled)
		{
			nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
			if (entries && OwnedEntryAdd (entries, 0, "ch0.dsp", out, 0x60 + per))
				err = write_owned_entries (arg, basedir, depth, "KYLOTONN-SND", entries, 1);
			else
				FREE (entries);
		}
		FREE (out);
	}
	FREE (raw);
	return err;
}

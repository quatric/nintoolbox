// Nintendo DS SDK wave archive (".swar"): "SWAR", file size @8, "DATA" @0x10, wave count @0x38 and u32 LE wave offsets from 0x3c. Every wave starts with
// a 0x10 byte header {type 0 PCM8 / 1 PCM16 / 2 IMA, loop, rate u16 @2, ..., IMA predictor s16 @0xc, step index @0xe} followed by the sample data; each wave
// is decoded to "<file>.d/<name>_<NNN>.wav" (after vgmstream swar.c; the SDAT converter reads the same archives for SoundFonts).
static enumError extract_nds_swar_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".swar") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const uint cnt = sz > 0x40 ? rd_le32 (raw + 0x38) : 0;
	if (sz > 0x40 && !memcmp (raw, "SWAR", 4) && !memcmp (raw + 0x10, "DATA", 4) && cnt && cnt < 0x10000 && 0x3c + (size_t)cnt * 4 <= sz)
	{
		char dest[PATH_MAX], stem[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (stem, '.');
		if (dot) *dot = 0;
		size_t end = rd_le32 (raw + 8);
		if (end > sz)
			end = sz;
		for (uint i = 0; i < cnt; i++)
		{
			const size_t h = rd_le32 (raw + 0x3c + i * 4);
			size_t next = i + 1 < cnt ? rd_le32 (raw + 0x3c + (i + 1) * 4) : end;
			if (!h || h + 0x10 > sz || next > sz || next <= h + 0x10)
				continue;
			const uint type = raw[h], rate = raw[h + 2] | raw[h + 3] << 8;
			const size_t bytes = next - h - 0x10;
			if (type > 2 || !rate)
				continue;
			uint n = type == 2 ? (uint)(bytes * 2) : type == 1 ? (uint)(bytes / 2) : (uint)bytes;
			s16 *pcm = CALLOC ((size_t)n + 1, sizeof (s16));
			if (!pcm)
				break;
			if (type == 2)
				n = nds_strm_ima_block (raw + h + 0xc, (uint)bytes + 4, pcm, n);
			else
				for (uint k = 0; k < n; k++)
					pcm[k] = type == 1 ? (s16)(raw[h + 0x10 + 2 * k] | raw[h + 0x11 + 2 * k] << 8) : (s16)((s8)raw[h + 0x10 + k] << 8);
			char name[PATH_MAX];
			snprintf (name, sizeof (name), "%s_%03u.wav", stem, i);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE NDS-SWAR:%s #%u (1 ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, i, rate, n);
			if (testmode)
				err = ERR_OK;
			else
			{
				size_t wn = 0;
				u8 *w = nds_strm_make_wav (pcm, n, 1, rate, &wn);
				if (w && !shade_write (arg, dest, basedir, name, w, (uint)wn))
					err = ERR_OK;
				FREE (w);
			}
			FREE (pcm);
		}
	}
	FREE (raw);
	return err;
}

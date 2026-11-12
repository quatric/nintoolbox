// Next Level Games / Namco DS "RRDS" mono IMA-ADPCM sounds (Metroid Prime Hunters, Ridge Racer DS; ".rrds" is a made-up extension): u32 LE data end
// (+0x18 = file size), rate u32 @8, loop byte offset @0x14, IMA header {s16 predictor, u16 step index} @0x18 and nibbles from 0x1c, decoded to
// "<name>.wav" (after vgmstream nds_rrds.c).
static enumError extract_nds_rrds_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".rrds") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const uint rate = sz > 0x20 ? rd_le32 (raw + 8) : 0;
	if (sz > 0x20 && (size_t)rd_le32 (raw) + 0x18 == sz && rate >= 1000 && rate <= 192000 && (raw[0x1a] | raw[0x1b] << 8) <= 88)
	{
		const uint max = (uint)(sz - 0x1c) * 2;
		s16 *pcm = CALLOC (max + 1, sizeof (s16));
		if (pcm)
		{
			const uint n = nds_strm_ima_block (raw + 0x18, (uint)(sz - 0x18), pcm, max);
			char dest[PATH_MAX], name[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (name, '.');
			if (dot) strcpy (dot, ".wav");
			if (n)
			{
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sDECODE NDS-RRDS:%s (1 ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, rate, n);
				if (testmode)
					err = ERR_OK;
				else
				{
					size_t wn = 0;
					u8 *w = nds_strm_make_wav (pcm, n, 1, rate, &wn);
					err = w ? shade_write (arg, dest, basedir, name, w, (uint)wn) : ERR_OUT_OF_MEMORY;
					FREE (w);
				}
			}
			FREE (pcm);
		}
	}
	FREE (raw);
	return err;
}

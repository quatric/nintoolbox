// Vicarious Visions DS audio (".hwas": "sawh", Spider-Man 3, Tony Hawk's Downhill Jam, Guitar Hero On Tour): block size @4, rate @8, channels @0xc (mono only),
// loop start/data/end byte offsets @0x10/0x14/0x18, headerless IMA-ADPCM blocks from 0x200 whose predictor and step index are reset to zero at every block;
// decoded to "<name>.wav" (after vgmstream hwas.c / blocked_hwas.c).
static enumError extract_nds_hwas_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".hwas") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const uint blk = sz > 0x200 ? rd_le32 (raw + 4) : 0, rate = sz > 0x200 ? rd_le32 (raw + 8) : 0;
	size_t bytes = sz > 0x200 ? rd_le32 (raw + 0x14) : 0;
	if (sz > 0x200 && !memcmp (raw, "sawh", 4) && rd_le32 (raw + 0xc) == 1 && blk >= 0x100 && blk <= 0x100000 && rate >= 1000 && rate <= 192000 && bytes)
	{
		if (bytes > sz - 0x200)
			bytes = sz - 0x200;
		const uint max = (uint)bytes * 2;
		s16 *pcm = CALLOC ((size_t)max + 1, sizeof (s16));
		u8 *buf = MALLOC (blk + 4);
		if (pcm && buf)
		{
			uint done = 0;
			for (size_t o = 0; o < bytes && done < max; o += blk)
			{
				const size_t n = blk < bytes - o ? blk : bytes - o;
				memset (buf, 0, 4);
				memcpy (buf + 4, raw + 0x200 + o, n);
				done += nds_strm_ima_block (buf, (uint)n + 4, pcm + done, max - done);
			}
			char dest[PATH_MAX], name[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (name, '.');
			if (dot) strcpy (dot, ".wav");
			if (done)
			{
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sDECODE NDS-HWAS:%s (1 ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, rate, done);
				if (testmode)
					err = ERR_OK;
				else
				{
					size_t wn = 0;
					u8 *w = nds_strm_make_wav (pcm, done, 1, rate, &wn);
					err = w ? shade_write (arg, dest, basedir, name, w, (uint)wn) : ERR_OUT_OF_MEMORY;
					FREE (w);
				}
			}
		}
		FREE (pcm);
		FREE (buf);
	}
	FREE (raw);
	return err;
}

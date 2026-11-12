// Dragon Booster DS audio (".zsd": "ZSD\0"): channels @0xc, rate @0x10, total samples @0x18 (all channels), data offset @0x20, PCM8 samples (signed, interleaved
// per sample for stereo); decoded to "<name>.wav" (after vgmstream zsd.c).
static enumError extract_nds_zsd_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".zsd") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const uint ch = sz > 0x24 ? rd_le32 (raw + 0xc) : 0, rate = sz > 0x24 ? rd_le32 (raw + 0x10) : 0;
	const size_t total = sz > 0x24 ? rd_le32 (raw + 0x18) : 0, off = sz > 0x24 ? rd_le32 (raw + 0x20) : 0;
	if (sz > 0x24 && !memcmp (raw, "ZSD", 4) && !raw[3] && ch >= 1 && ch <= 2 && rate >= 1000 && rate <= 192000 && total && off < sz)
	{
		size_t n = total < sz - off ? total : sz - off;
		n -= n % ch;
		s16 *pcm = MALLOC (n * 2);
		if (pcm && n)
		{
			for (size_t i = 0; i < n; i++)
				pcm[i] = (s16)((s8)raw[off + i] << 8);
			char dest[PATH_MAX], name[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (name, '.');
			if (dot) strcpy (dot, ".wav");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE NDS-ZSD:%s (%u ch, %u Hz, %zu samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, n / ch);
			if (testmode)
				err = ERR_OK;
			else
			{
				size_t wn = 0;
				u8 *w = nds_strm_make_wav (pcm, (uint)(n / ch), ch, rate, &wn);
				err = w ? shade_write (arg, dest, basedir, name, w, (uint)wn) : ERR_OUT_OF_MEMORY;
				FREE (w);
			}
		}
		FREE (pcm);
	}
	FREE (raw);
	return err;
}

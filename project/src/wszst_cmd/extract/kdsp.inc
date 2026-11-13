// Kuju "KDSP" voice files (".ksp": Rebel Raiders: Operation Nighthawk subtitle voices; big endian): magic, 4 words (0x9740-style size, 4, 0x4c00-style size, sample rate)
// @4..0x14, 0xcd padding up to 0x30, then 16-bit big-endian mono PCM to the end of the file. Decoded to "<name>.wav".
static enumError extract_kdsp_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".ksp") || (export_count == 0 && !opt_decode) || !file_has_magic (arg, "KDSP", 4))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const uint rate = sz > 0x30 ? rd_be32 (raw + 0x10) : 0;
	if (sz > 0x32 && rate >= 4000 && rate <= 96000 && rd_be32 (raw + 8) == 4)
	{
		const uint ns = (uint)((sz - 0x30) / 2);
		s16 *pcm = MALLOC ((size_t)ns * 2);
		if (pcm)
		{
			for (uint i = 0; i < ns; i++)
				pcm[i] = (s16)rd_be16 (raw + 0x30 + 2 * (size_t)i);
			char dest[PATH_MAX], name[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (name, '.');
			if (dot) strcpy (dot, ".wav");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE KDSP:%s (%u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, rate, ns);
			if (testmode)
				err = ERR_OK;
			else
			{
				size_t wn = 0;
				u8 *w = nds_strm_make_wav (pcm, ns, 1, rate, &wn);
				err = w ? shade_write (arg, dest, basedir, name, w, (uint)wn) : ERR_OUT_OF_MEMORY;
				FREE (w);
			}
			FREE (pcm);
		}
	}
	FREE (raw);
	return err;
}

// Radirgy GeneriC GameCube sound (".gcw"): "GCSW", rate @8, channels @0xc, samples @0x10, loop start/end @0x14/0x18, loop flag @0x1c, PCM16 BE from 0x20
// with 0x8000 byte per-channel interleave; decoded to "<name>.wav" (after vgmstream gcsw.c).
static enumError extract_gcsw_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".gcw") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const uint rate = sz > 0x20 ? rd_be32 (raw + 8) : 0, ch = sz > 0x20 ? rd_be32 (raw + 0xc) : 0, ns = sz > 0x20 ? rd_be32 (raw + 0x10) : 0;
	if (sz > 0x20 && !memcmp (raw, "GCSW", 4) && rate >= 1000 && rate <= 192000 && ch >= 1 && ch <= 8 && ns && ns < 0x8000000)
	{
		s16 *pcm = CALLOC ((size_t)ns * ch, sizeof (s16));
		if (pcm)
		{
			uint done[8] = { 0 };
			size_t off = 0x20;
			bool progress = true;
			while (progress && off < sz)
			{
				progress = false;
				for (uint c = 0; c < ch; c++, off += 0x8000)
					for (size_t i = 0; i + 1 < 0x8000 && off + i + 1 < sz && done[c] < ns; i += 2, progress = true)
						pcm[(size_t)done[c]++ * ch + c] = (s16)(raw[off + i] << 8 | raw[off + i + 1]);
			}
			uint got = ns;
			for (uint c = 0; c < ch; c++)
				got = done[c] < got ? done[c] : got;
			if (got)
			{
				char dest[PATH_MAX], name[PATH_MAX];
				beside_source_dest (dest, sizeof (dest), arg);
				const char *sl = strrchr (arg, '/');
				snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
				char *dot = strrchr (name, '.');
				if (dot) strcpy (dot, ".wav");
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sDECODE GCSW:%s (%u ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, got);
				if (testmode)
					err = ERR_OK;
				else
				{
					size_t wn = 0;
					u8 *w = nds_strm_make_wav (pcm, got, ch, rate, &wn);
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

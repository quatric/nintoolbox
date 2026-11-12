// Nintendo standalone AFC streams (".afc"/".stx": Super Mario Sunshine, Wind Waker, Pikmin) decoded to "<name>.wav". Header (BE on GameCube, LE on the
// Switch ports; picked by where the 4 bits per sample value sits at 0xa): u32 size @0, samples @4, u16 rate @8, 4 @0xa, 16 @0xc, loop flag @0x10, loop
// start @0x14; data from 0x20 is 2 channels interleaved every 9 byte AFC frame (after vgmstream afc.c).
static enumError extract_nintendo_afc_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if ((!is_ext (arg, ".afc") && !is_ext (arg, ".stx")) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const bool be = sz > 0x20 && raw[0x0a] == 0;
	#define A32(p) (be ? (u32)((p)[0] << 24 | (p)[1] << 16 | (p)[2] << 8 | (p)[3]) : rd_le32 (p))
	#define A16(p) (be ? (uint)((p)[0] << 8 | (p)[1]) : (uint)((p)[0] | (p)[1] << 8))
	if (sz > 0x20 && A32 (raw) <= sz && A16 (raw + 0xa) == 4 && A16 (raw + 0xc) == 16)
	{
		const uint frames = A32 (raw + 4), rate = A16 (raw + 8);
		const size_t avail = (sz - 0x20) / 18 * 16;
		const uint n = frames < avail ? frames : (uint)avail;
		s16 *pcm = rate && n && frames < 0x8000000 ? CALLOC ((size_t)n * 2, sizeof (s16)) : 0;
		if (pcm)
		{
			int h1[2] = { 0 }, h2[2] = { 0 };
			uint done = 0;
			for (size_t f = 0x20; done < n && f + 18 <= sz; f += 18)
				for (uint c = 0; c < 2; c++)
				{
					const u8 *p = raw + f + 9 * c;
					const int scale = 1 << (p[0] >> 4);
					const int c1 = nast_afc_coef[p[0] & 15][0], c2 = nast_afc_coef[p[0] & 15][1];
					for (uint i = 0; i < 16 && done + i < n; i++)
					{
						int nib = i & 1 ? p[1 + i / 2] & 15 : p[1 + i / 2] >> 4;
						nib = nib >= 8 ? nib - 16 : nib;
						int s = (((nib * scale) << 11) + c1 * h1[c] + c2 * h2[c]) >> 11;
						s = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
						h2[c] = h1[c];
						h1[c] = s;
						pcm[(size_t)(done + i) * 2 + c] = (s16)s;
					}
					if (c == 1)
						done += 16;
				}
			done = done < n ? done : n;
			char dest[PATH_MAX], name[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (name, '.');
			if (dot) strcpy (dot, ".wav");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE NINTENDO-AFC:%s (2 ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, rate, done);
			if (testmode)
				err = ERR_OK;
			else
			{
				size_t wn = 0;
				u8 *w = nds_strm_make_wav (pcm, done, 2, rate, &wn);
				err = w ? shade_write (arg, dest, basedir, name, w, (uint)wn) : ERR_OUT_OF_MEMORY;
				FREE (w);
			}
		}
		FREE (pcm);
	}
	#undef A32
	#undef A16
	FREE (raw);
	return err;
}

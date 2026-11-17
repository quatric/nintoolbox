// Academy of Champions: Soccer (Wii) "WII/Sounds/_Packages/*.asb" sound packages (not the Nintendo animation "ASB "): a flat stream of big-endian chunks {tag[4], u32 length, payload}: "ftyp" {-1}, "fcnt" {count},
// then per sound "snd " {4 bytes}, "path" (e.g. "/Vocals/Celebration/Altair/Altair_celebration_1.dsp"), "stor" ("MEMORY"), "fmt " ("GCADPCM"), "rate" {u32}, "chan" {u32}, "data" {length, payload}. A GCADPCM
// payload is a variably zero-padded standard DSP header {ns, nib, rate, 0, 2, 2 (end address not set), 2, 16 coefficients} followed by the ADPCM frames; the header is located by its "2, 2, 2" address words.
// Sounds are written to "<file>.d/<path>" as DSP-ADPCM files (end address rebuilt, rate from the header word, which is 16000 where the "rate" chunk says 32000) and decoded to WAV.
static enumError extract_asbsound_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".asb"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0x40 || memcmp (raw, "ftyp", 4) || rd_be32 (raw + 4) != 4 || memcmp (raw + 12, "fcnt", 4))
		goto done;
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		char path[200];
		path[0] = 0;
		uint n = 0, total = 0;
		for (int pass = 0; pass < 2; pass++)
		{
			n = 0;
			path[0] = 0;
			for (size_t o = 0; o + 8 <= sz;)
			{
				const u32 l = rd_be32 (raw + o + 4);
				if ((u64)o + 8 + l > sz)
					break;
				const u8 *pl = raw + o + 8;
				if (!memcmp (raw + o, "path", 4))
				{
					snprintf (path, sizeof (path), "%.*s", (int)(l < sizeof (path) - 1 ? l : sizeof (path) - 1), (ccp)pl);
					for (char *p = path; *p; p++)
						if (*p == '\\')
							*p = '/';
				}
				else if (!memcmp (raw + o, "data", 4) && path[0] && !strstr (path, ".."))
				{
					size_t p = 0;
					while (p + 24 < l && memcmp (pl + p, "\0\0\0\2\0\0\0\2\0\0\0\2", 12))
						p++;
					if (p >= 16 && p + 12 + 32 <= l)
					{
						const u8 *h = pl + p - 16;
						const u32 ns = rd_be32 (h), nib = rd_be32 (h + 4), rate = rd_be32 (h + 8);
						const size_t dl = (nib + 1) / 2, ds = p - 16 + 0x60;
						if (nib >= 16 && ns <= nib / 16 * 14 + 14 && ns + 28 >= nib / 16 * 14 && rate >= 4000 && rate <= 96000 && ds + dl <= l + 8)
						{
							n++;
							if (pass && !testmode)
							{
								u8 hd[0x60];
								memset (hd, 0, sizeof (hd));
								wr_be32 (hd, ns);
								wr_be32 (hd + 4, nib);
								wr_be32 (hd + 8, rate);
								wr_be32 (hd + 0x10, 2);
								wr_be32 (hd + 0x14, nib - 1);
								wr_be32 (hd + 0x18, 2);
								memcpy (hd + 0x1c, pl + p + 12, 32);
								const size_t avail = l > ds ? l - ds : 0;
								hd[0x3f] = avail ? pl[ds] : 0;
								char fn[PATH_MAX];
								ccp rel = path[0] == '/' ? path + 1 : path;
								snprintf (fn, sizeof (fn), "%s/%s%s", dest, basedir ? basedir : "", rel);
								File_t F;
								err = CreateFileOpt (&F, true, fn, false, arg);
								const size_t wl = dl < avail ? dl : avail;
								if (F.f && (fwrite (hd, 1, sizeof (hd), F.f) != sizeof (hd) || fwrite (pl + ds, 1, wl, F.f) != wl))
									err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)wl, fn);
								ResetFile (&F, opt_preserve);
								if (err)
									goto done;
							}
						}
					}
					path[0] = 0;
				}
				o += 8 + l;
			}
			if (!pass)
			{
				total = n;
				if (!total)
					goto done;
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sEXTRACT ASB:%s (%u sounds) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, total, dest);
				err = ERR_OK;
				if (testmode)
					break;
			}
		}
		if (!err && !testmode)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (raw);
	return err;
}

// Disney Think Fast (Wii) "SPEECH1/PCKSP_*.SPB" speech packs (~775 MB): a run of complete DSP-ADPCM files (0x60 byte big-endian DSP header + frames, sa=2, ca=2, ea=nibbles-1), each starting on a 0x8000 boundary,
// with no index. Every valid header found on a 0x8000 boundary is copied to "<file>.d/NNNN.dsp" and decoded to WAV by the DSP handler. The file is streamed (never loaded whole).
static enumError extract_spbspeech_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".spb") && !is_ext (arg, ".SPB"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	struct stat st;
	enumError err = ERR_NOTHING_TO_DO;
	u8 h[0x60];
	if (fstat (fileno (f), &st) || st.st_size < 0x8000 || fread (h, 1, sizeof (h), f) != sizeof (h))
		goto done;
	{
		const u64 fsz = (u64)st.st_size;
		#define SPB_OK(h) (rd_be32 ((h) + 4) >= 32 && rd_be32 ((h) + 4) < 0x10000000 && rd_be32 ((h) + 8) >= 4000 && rd_be32 ((h) + 8) <= 96000 && rd_be32 ((h) + 0x10) == 2 && rd_be32 ((h) + 0x14) == rd_be32 ((h) + 4) - 1 && rd_be32 ((h) + 0x18) == 2 && !rd_be16 ((h) + 0x0c) && !rd_be16 ((h) + 0x0e))
		if (!SPB_OK (h))
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SPB:%s -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
		err = ERR_OK;
		uint n = 0;
		for (u64 off = 0; off + sizeof (h) <= fsz && !err && !testmode; off += 0x8000)
		{
			if (fseeko (f, (off_t)off, SEEK_SET) || fread (h, 1, sizeof (h), f) != sizeof (h) || !SPB_OK (h))
				continue;
			const u32 len = (rd_be32 (h + 4) + 15) / 16 * 8;
			if (off + sizeof (h) + len > fsz)
				continue;
			u8 *buf = MALLOC (len);
			if (!buf || fread (buf, 1, len, f) != len)
			{
				FREE (buf);
				continue;
			}
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%05u.dsp", dest, basedir ? basedir : "", n++);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && (fwrite (h, 1, sizeof (h), F.f) != sizeof (h) || fwrite (buf, 1, len, F.f) != len))
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", len, path);
			ResetFile (&F, opt_preserve);
			FREE (buf);
		}
		#undef SPB_OK
		if (!err && !testmode)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	fclose (f);
	return err;
}

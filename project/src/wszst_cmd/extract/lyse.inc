// Ubisoft Lyn sound engine "LySE" wrapper ("*.wav" members of Just Dance BigFiles): a short header {"LySE", u32 ...} followed by a standard RIFF/WAVE file. The RIFF part is written to "<name>_riff.wav".
static enumError extract_lyse_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".wav"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *d = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const long sz = ftell (f);
		if (sz < 0x40 || sz > 0x20000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)sz);
		if (fread (d, 1, (size_t)sz, f) != (size_t)sz || memcmp (d, "LySE", 4))
			goto done;
		size_t r = 0;
		while (r < 0x40 && memcmp (d + r, "RIFF", 4))
			r++;
		if (r >= 0x40 || memcmp (d + r + 8, "WAVE", 4))
			goto done;
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT LYSE:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg);
		err = ERR_OK;
		if (!testmode)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%.*s_riff.wav", (int)strlen (arg) - 4, arg);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (d + r, 1, (size_t)sz - r, F.f) != (size_t)sz - r)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", (size_t)sz - r, path);
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (d);
	fclose (f);
	return err;
}

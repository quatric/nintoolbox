// Resident Evil (GameCube remake, Resident Evil Archives) "*.emg" / "*.emd": the file starts with a list of ascending, 0x20 aligned be32 section offsets (zero terminated, the rest of the header up to the first section is zero); the last section runs to the end of the file, or the final offset equals the file size. Sections are written to "<file>.d/NN.bin".
static enumError extract_reoff_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".emg") && !is_ext (arg, ".emd"))
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
		if (sz < 0x100 || sz > 0x40000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)sz);
		if (fread (d, 1, (size_t)sz, f) != (size_t)sz)
			goto done;
		u32 off[64];
		uint n = 0;
		while (n < 64 && 4 * n + 4 <= (size_t)sz)
		{
			const u32 v = be32 (d + 4 * n);
			if (!v)
				break;
			if ((v & 0x1f) || v > (u32)sz || (n && v <= off[n - 1]))
				goto done;
			off[n++] = v;
		}
		if (n < 2 || off[0] < 4 * n + 4 || off[0] > 0x1000)
			goto done;
		for (size_t i = 4 * n; i < off[0]; i++)
			if (d[i])
				goto done;
		uint secs = n;
		if (off[n - 1] == (u32)sz)
			secs--;
		if (secs < 2)
			goto done;
		off[secs] = (u32)sz;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT REOFF:%s (%u sections) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, secs, dest);
		err = ERR_OK;
		for (uint i = 0; i < secs && !err && !testmode; i++)
		{
			const u32 s = off[i + 1] - off[i];
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%02u.bin", dest, i);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && s && fwrite (d + off[i], 1, s, F.f) != s)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (d);
	fclose (f);
	return err;
}

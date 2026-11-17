// Are You Smarter than a 5th Grader? (Wii) "Audio/*.bob" voice clips: a NUL-terminated caption (e.g. "Woohoo!"), a NUL-terminated lip-sync string (digits / letters), then a plain MPEG layer III stream
// (frame sync 0xff 0xfx). The MP3 data is written to "<file>.mp3" and the caption + lip-sync strings to "<file>.txt".
static enumError extract_bobmp3_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".bob"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40)
		goto done;
	{
		size_t p = 0;
		for (uint s = 0; s < 2; s++)
		{
			while (p < sz && p < 0x400 && raw[p] && raw[p] >= 0x20 && raw[p] < 0x7f)
				p++;
			if (p >= sz || raw[p])
				goto done;
			p++;
		}
		if (p + 4 >= sz || raw[p] != 0xff || (raw[p + 1] & 0xe0) != 0xe0 || (raw[p + 1] & 0x06) != 0x02)
			goto done;
		char mp3[PATH_MAX], txt[PATH_MAX];
		snprintf (mp3, sizeof (mp3), "%s.mp3", arg);
		snprintf (txt, sizeof (txt), "%s.txt", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT BOB:%s (MP3, %u bytes) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, (uint)(sz - p), mp3);
		err = ERR_OK;
		if (!testmode)
		{
			File_t F;
			err = CreateFileOpt (&F, true, mp3, false, arg);
			if (F.f && fwrite (raw + p, 1, sz - p, F.f) != sz - p)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)(sz - p), mp3);
			ResetFile (&F, opt_preserve);
			if (!err)
			{
				err = CreateFileOpt (&F, true, txt, false, arg);
				if (F.f && fwrite (raw, 1, p - 1, F.f) != p - 1)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)(p - 1), txt);
				ResetFile (&F, opt_preserve);
			}
		}
	}
done:
	FREE (raw);
	return err;
}

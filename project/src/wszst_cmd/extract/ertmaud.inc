// Telltale Games (Back to the Future: The Game, Wii) "*.aud" sound effects: "ERTM" {u32 version 1, u32 name crc, u32 crc, ...} with an Ogg Vorbis stream embedded in the resource (first "OggS" within the first 0x100
// bytes, running to the end of the file). Written to "<file>.ogg". (The ".vox" voice files of the same games hold Speex frames and are decoded by ertmvox.inc.)
static enumError extract_ertmaud_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".aud"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x100 || memcmp (raw, "ERTM", 4))
		goto done;
	{
		size_t o = 0;
		for (size_t i = 8; i < 0x100 && !o; i++)
			if (!memcmp (raw + i, "OggS", 4) && !raw[i + 4] && raw[i + 5] == 2 && !memcmp (raw + i + 0x1d, "vorbis", 6))
				o = i;
		if (!o)
			goto done;
		char ogg[PATH_MAX];
		snprintf (ogg, sizeof (ogg), "%s.ogg", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT ERTM:%s (Ogg Vorbis, %u bytes) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, (uint)(sz - o), ogg);
		err = ERR_OK;
		if (!testmode)
		{
			File_t F;
			err = CreateFileOpt (&F, true, ogg, false, arg);
			if (F.f && fwrite (raw + o, 1, sz - o, F.f) != sz - o)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)(sz - o), ogg);
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (raw);
	return err;
}

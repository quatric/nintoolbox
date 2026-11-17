// "created by ntcompress2" wrapper (Animal Kingdom: Wildlife Expedition "*.crfnt" and other assets): a 0x20-byte ASCII banner followed by a standard Nintendo LZ10/LZ11 stream. The decompressed payload
// (an "RFNT" font for ".crfnt") is written to "<file>.dec" with the extension "c" dropped from ".crfnt" -> ".brfnt" where applicable, then extracted recursively.
static enumError extract_ntcompress_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *dec = 0;
	uint dsz = 0;
	if (sz < 0x30 || memcmp (raw, "created by ntcompress", 21) || (raw[0x20] != 0x10 && raw[0x20] != 0x11))
		goto done;
	if (DecodeLZ10LZ11 (&dec, &dsz, raw + 0x20, (uint)(sz - 0x20)) || !dec || !dsz)
		goto done;
	{
		char path[PATH_MAX];
		const size_t n = strlen (arg);
		if (n > 6 && !strcasecmp (arg + n - 6, ".crfnt"))
			snprintf (path, sizeof (path), "%.*s.brfnt", (int)(n - 6), arg);
		else
			snprintf (path, sizeof (path), "%s.dec", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECOMPRESS NTCOMPRESS:%s (%u bytes) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dsz, path);
		err = ERR_OK;
		if (!testmode)
		{
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (dec, 1, dsz, F.f) != dsz)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", dsz, path);
			ResetFile (&F, opt_preserve);
			if (!err)
			{
				const enumError sub = extract_one_file (path, 0, depth + 1);
				if (sub != ERR_NOTHING_TO_DO && err < sub)
					err = sub;
			}
		}
	}
done:
	FREE (raw);
	FREE (dec);
	return err;
}

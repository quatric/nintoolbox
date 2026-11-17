// "MTXZ" (Shin Chan Wii "*.mz"): {"MTXZ", u32 0, be32 decompressed size, u32 0} followed by a zlib stream at 0x10; the inflated data is written to "<file>.dec".
static enumError extract_mtxz_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir; (void)depth;
	u8 *raw = 0, *out = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".mz") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x18 || memcmp (raw, "MTXZ", 4))
		goto done;
	{
		const u32 us = rd_be32 (raw + 8);
		if (!us || us > 0x20000000)
			goto done;
		out = MALLOC (us);
		uLongf dl = us;
		if (uncompress (out, &dl, raw + 0x10, (uLong)(sz - 0x10)) != Z_OK || dl != us)
			goto done;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s.dec", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT MTXZ:%s (%u bytes) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, us, path);
		err = ERR_OK;
		if (testmode)
			goto done;
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (out, 1, us, F.f) != us)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", us, path);
		ResetFile (&F, opt_preserve);
	}
done:
	FREE (out);
	FREE (raw);
	return err;
}

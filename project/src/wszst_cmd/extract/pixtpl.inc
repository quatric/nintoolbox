// Earth Seeker (Wii) "2D/*.pix" texture wrappers: chunks "FPIX" / "DPIX" / "XPIX" (little-endian sizes) in front of a complete TPL image (0x0020af30) that runs to the end of the file. The TPL is written as
// "<file>.tpl" and decoded to "<file>.tpl.png" by the TPL decoder.
static enumError extract_pixtpl_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".pix"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x100 || memcmp (raw, "FPIX", 4))
		goto done;
	{
		size_t o = 0;
		for (size_t p = 0x10; p + 0x20 < sz && p < 0x400; p += 4)
			if (!memcmp (raw + p, "\x00\x20\xaf\x30", 4))
			{
				o = p;
				break;
			}
		if (!o)
			goto done;
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.tpl", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT PIX:%s -> TPL:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
		err = ERR_OK;
		if (!testmode)
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f && fwrite (raw + o, 1, sz - o, F.f) != sz - o)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)(sz - o), dest);
			ResetFile (&F, opt_preserve);
			if (!err)
				decode_image_if_possible (dest);
		}
	}
done:
	FREE (raw);
	return err;
}

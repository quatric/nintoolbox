// Petz engine "TTPL" texture (Petz Sports, Petz Dogz/Horsez, ...): an 8-byte
// wrapper {"TTPL", u32 size} around an ordinary Wii TPL. Decoded to
// "<name>.png" in decode mode (xx); the TPL is not left behind.
static enumError extract_ttpl_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (export_count <= 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size < 0x20 || memcmp (raw, "TTPL", 4) || memcmp (raw + 8, "\x00\x20\xaf\x30", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char tmp[PATH_MAX], png[PATH_MAX], tmp_png[PATH_MAX];
	snprintf (tmp, sizeof (tmp), "%s.ttpl-tmp.tpl", arg);
	snprintf (tmp_png, sizeof (tmp_png), "%s.png", tmp);
	snprintf (png, sizeof (png), "%s.png", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE TTPL:%s -> PNG:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, png);
	if (testmode)
	{
		FREE (raw);
		return ERR_OK;
	}

	enumError err = SaveFile (tmp, 0, 0, raw + 8, raw_size - 8, 0);
	FREE (raw);
	if (!err)
		err = decode_image_if_possible (tmp);
	if (!err && rename (tmp_png, png))
		err = ERROR1 (ERR_CANT_CREATE, "Can't create PNG: %s\n", png);
	unlink (tmp);
	return err;
}

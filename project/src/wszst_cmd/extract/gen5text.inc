// Pokemon Black / White / Black 2 / White 2 message archive ("file_0000.bin" inside a/0/0/2 and a/0/0/3): written as "<file>.gen5.txt"
static enumError extract_gen5text_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if ((!is_ext (arg, ".bin") && strrchr (FindFilename (arg, 0), '.')) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	char *text = 0;
	size_t text_n = 0;
	if (sz < 0x400000 && IsGen5Text (raw, sz) && !DecodeGen5Text (raw, sz, &text, &text_n))
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dir (dest, sizeof (dest), arg);
		snprintf (name, sizeof (name), "%s.gen5.txt", FindFilename (arg, 0));
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE GEN5-TEXT:%s -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, name);
		err = testmode ? ERR_OK : shade_write (arg, dest, basedir, name, (u8 *)text, (uint)text_n);
	}
	FREE (text);
	FREE (raw);
	return err;
}

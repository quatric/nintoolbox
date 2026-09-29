// Decode a CyberConnect2 "WIMG" texture (And-Kensaku, Wii) to a sibling PNG.
// See lib-wimg.c. Reached both for loose ".wimg" files and for the many that
// fall out of ".rz"/".res" (Pres) extraction.
static enumError decode_wimg_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".wimg"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX || raw_size < 0x24 || memcmp (raw, "WIMG", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u8 *rgba = 0;
	uint w = 0, h = 0;
	enumError err = DecodeWIMG (&rgba, &w, &h, raw, (uint)raw_size);
	FREE (raw);
	if (err || !rgba)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_image_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT WIMG:%s (%ux%u) -> PNG:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, w, h, dest);

	if (testmode)
	{
		FREE (rgba);
		return ERR_OK;
	}
	return SaveDecodedRGBAToPNG (rgba, w, h, &be_func, dest, 0, opt_overwrite);
}

// Decode a loose Nintendo Message Binary (.bmg) to a sibling, human-readable
// text file. A .bmg reached through an archive member (ConvertHelper()) has
// always been able to do this via the explicit BINARY/TEXT/cat commands, but
// nothing in this file's extraction dispatch chain ever called ConvertHelper
// -- so a standalone .bmg sitting directly in an extracted tree (as ships in
// many DS/Wii titles' raw filesystem, not inside any SARC/U8/BRRES) was
// silently skipped by XX/XDECODE/XEXPORT/XALL. Gate on opt_decode, matching
// every other caller of the BMG/MSBT-style text decoders: CMD_EXTRACT alone
// leaves the raw asset untouched for a byte-exact CREATE round trip; the
// decode-oriented modes set opt_decode and expect a readable sibling.
static enumError extract_bmg_file (ccp arg, ccp basedir, uint depth)
{
	if (!opt_decode || !is_ext (arg, ".bmg"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX || GetByMagicFF (raw, (uint)raw_size, (uint)raw_size) != FF_BMG)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	bmg_t bmg;
	enumError err = ScanBMG (&bmg, true, arg, raw, raw_size);
	FREE (raw);
	if (err)
	{
		ResetBMG (&bmg);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".txt");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BMG:%s -> TEXT:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	if (!testmode)
		err = SaveTextXBMG (&bmg, dest, true);
	ResetBMG (&bmg);
	return err;
}

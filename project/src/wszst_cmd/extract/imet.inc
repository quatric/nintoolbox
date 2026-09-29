// A Wii channel banner (/opening.bnr on a disc, 00000000.app in a channel):
// an IMET header holding the title in 10 languages, followed by a plain U8
// archive with banner.bin / icon.bin / sound.bin inside.  Write the titles as
// a text sidecar and the U8 archive as a sibling file, then let the ordinary
// extraction pass take it from there -- the U8 members are IMD5/LZ77-wrapped
// U8 archives themselves, which decompress_nintendo_file3() now unwraps, so
// the BRLYT/BRLAN/TPL/BRSTM resources inside come out without further help.
static enumError extract_imet_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, UINT_MAX, 2, 0, false))
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX || !IsIMET (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	imet_t imet;
	if (ScanIMET (&imet, raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
	{
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".txt", false);
		// A plain literal --dest (no \1 substitution codes) comes back from
		// SubstDest() unchanged: that's the same path this function also
		// wants for the .u8 scaffold and for the final extraction directory
		// below, so appending nothing here would make all three collide.
		if (!strcmp (dest, opt_dest))
			snprintf (dest, sizeof (dest), "%s.txt", opt_dest);
	}
	else
		snprintf (dest, sizeof (dest), "%s.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT IMET banner titles:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	enumError err = ERR_OK;
	if (!testmode)
	{
		char *text = TextIMET (&imet);
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f)
		{
			fputs (text, F.f);
			ResetFile (&F, opt_preserve);
		}
		FREE (text);
	}

	char u8dest[PATH_MAX];
	if (opt_dest)
	{
		SubstDest (u8dest, sizeof (u8dest), arg, opt_dest, 0, ".u8", false);
		if (!strcmp (u8dest, opt_dest))
			snprintf (u8dest, sizeof (u8dest), "%s.u8", opt_dest);
	}
	else
		snprintf (u8dest, sizeof (u8dest), "%s.u8", arg);
	const uint u8_size = (uint)raw_size - imet.u8_offset;
	if (verbose >= 0 || testmode)
		fprintf (
			stdlog, "%sEXTRACT IMET payload:%s -> U8:%s\n", testmode ? "WOULD " : "", arg, u8dest);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, u8dest, false, arg);
		if (F.f && fwrite (raw + imet.u8_offset, 1, u8_size, F.f) != u8_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", u8_size, u8dest);
		ResetFile (&F, opt_preserve);
	}

	ResetIMET (&imet);
	FREE (raw);

	if (!err && !testmode)
	{
		const enumError sub_err = extract_one_file (u8dest, basedir, depth + 1);
		if (sub_err != ERR_NOTHING_TO_DO && err < sub_err)
			err = sub_err;

		// The staged U8 is scaffolding, not a deliverable: its members are on
		// disk now.  Leaving it behind would let a later CREATE sweep the
		// whole banner back in as one bogus extra file -- the same hygiene
		// decompress_nintendo_file3()'s callers apply to their intermediates.
		if (!opt_export_raw)
		{
			struct stat st;
			if (!stat (u8dest, &st) && S_ISREG (st.st_mode))
				unlink (u8dest);
		}
	}
	return err;
}

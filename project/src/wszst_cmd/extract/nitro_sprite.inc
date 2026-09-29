// Export the structural half of a Nitro sprite set as XML.  NCGR/NCLR pixels
// remain ordinary wimgt inputs; the manifest records the exact OAM and frame
// values that connect those pixels to NCER/NANR cell/animation resources.
static enumError extract_nitro_sprite_manifest (ccp arg)
{
	if (!is_ext (arg, ".bncr") && !is_ext (arg, ".nanr") && !is_ext (arg, ".ncer")
		&& !is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *data = 0;
	size_t file_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &data, &file_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (file_size > UINT_MAX)
	{
		FREE (data);
		return ERR_FILE_TOO_BIG;
	}
	const nfmt_type_t type = DetectNintendoFormat (data, file_size, arg).type;
	if (type != NFMT_NCER && type != NFMT_NANR)
	{
		FREE (data);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".xml", false);
	else
		snprintf (dest, sizeof (dest), "%s.xml", arg);

	if (!opt_overwrite && !access (dest, F_OK))
	{
		FREE (data);
		return ERR_OK;
	}

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT %s XML:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", GetNintendoFormatName (type), arg, dest);
	if (testmode)
	{
		FREE (data);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (!F.f)
	{
		FREE (data);
		return err;
	}
	if (type == NFMT_NCER)
	{
		nintendo_ncer_t ncer;
		err = ScanNCER (&ncer, data, file_size);
		if (err)
			err = ERR_INVALID_DATA;
		if (!err)
			fprintf (F.f, "<?xml version=\"1.0\"?>\n<ncer cells=\"%u\">\n", ncer.n_cells);
		for (uint i = 0; !err && i < ncer.n_cells; i++)
		{
			uint n_obj;
			const u8 *oam;
			err = GetNCERCell (&ncer, i, &n_obj, &oam);
			if (!err)
				fprintf (F.f, "  <cell index=\"%u\" objects=\"%u\">\n", i, n_obj);
			for (uint j = 0; !err && j < n_obj; j++, oam += 6)
				if (fprintf (F.f, "    <obj attr0=\"0x%04x\" attr1=\"0x%04x\" attr2=\"0x%04x\"/>\n",
						le16 (oam), le16 (oam + 2), le16 (oam + 4))
					< 0)
					err = ERR_WRITE_FAILED;
			if (!err && fprintf (F.f, "  </cell>\n") < 0)
				err = ERR_WRITE_FAILED;
		}
		if (!err && fprintf (F.f, "</ncer>\n") < 0)
			err = ERR_WRITE_FAILED;
	}
	else
	{
		nintendo_nanr_t nanr;
		err = ScanNANR (&nanr, data, file_size);
		if (err)
			err = ERR_INVALID_DATA;
		if (!err)
			fprintf (F.f, "<?xml version=\"1.0\"?>\n<nanr animations=\"%u\" frames=\"%u\">\n",
				nanr.n_animations, nanr.n_frames);
		for (uint i = 0; !err && i < nanr.n_animations; i++)
		{
			uint n_frames;
			const u8 *frames;
			err = GetNANRAnimation (&nanr, i, &n_frames, &frames);
			if (!err)
				fprintf (F.f, "  <animation index=\"%u\" frames=\"%u\">\n", i, n_frames);
			for (uint j = 0; !err && j < n_frames; j++, frames += 8)
			{
				const uint off = le32 (frames);
				if (fprintf (F.f, "    <frame cell=\"%u\" duration=\"%u\" data-offset=\"0x%x\"/>\n",
						le16 (nanr.frame_data + off), le16 (frames + 4), off)
					< 0)
					err = ERR_WRITE_FAILED;
			}
			if (!err && fprintf (F.f, "  </animation>\n") < 0)
				err = ERR_WRITE_FAILED;
		}
		if (!err && fprintf (F.f, "</nanr>\n") < 0)
			err = ERR_WRITE_FAILED;
	}
	if (ferror (F.f) && !err)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing XML failed: %s\n", dest);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err ? err : ERR_OK;
}

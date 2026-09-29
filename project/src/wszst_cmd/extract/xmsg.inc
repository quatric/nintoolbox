static enumError extract_xmsg_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".xmsg") && !is_ext (arg, ".bin") && !is_ext (arg, ".xml"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsXMSG (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	xmsg_t xmsg;
	err = ScanXMSG (&xmsg, raw, raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO;

	char *xml = 0;
	size_t xml_len = 0;
	err = ExtractXMSGXml (&xmsg, &xml, &xml_len);
	if (err || !xml)
	{
		ResetXMSG (&xmsg);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, ".xml", 0, false);
	else
		snprintf (dest, sizeof (dest), "%s.xml", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT XMSG (Wii Party):%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	if (testmode)
	{
		FREE (xml);
		ResetXMSG (&xmsg);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f && fwrite (xml, 1, xml_len, F.f) != xml_len)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing XML failed: %s\n", dest);
	ResetFile (&F, opt_preserve);
	FREE (xml);

	// Also extract human-readable text dump
	char *txt = 0;
	size_t txt_len = 0;
	if (ExtractXMSGText (&xmsg, &txt, &txt_len) == ERR_OK && txt && txt_len > 0)
	{
		char txt_dest[PATH_MAX];
		snprintf (txt_dest, sizeof (txt_dest), "%s", dest);
		char *ext = strrchr (txt_dest, '.');
		if (ext && !strcmp (ext, ".xml"))
			snprintf (ext, sizeof (txt_dest) - (ext - txt_dest), ".txt");
		else
			snprintf (txt_dest + strlen (txt_dest), sizeof (txt_dest) - strlen (txt_dest), ".txt");

		File_t F_txt;
		if (CreateFileOpt (&F_txt, true, txt_dest, false, arg) == ERR_OK)
		{
			if (F_txt.f)
				fwrite (txt, 1, txt_len, F_txt.f);
			ResetFile (&F_txt, opt_preserve);
		}
		FREE (txt);
	}

	ResetXMSG (&xmsg);
	return err;
}

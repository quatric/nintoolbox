	// A zlib stream has a compact but self-describing header.  Do not make
	// recursive XX depend on its filename: archive members are routinely
	// named .bin (or have no extension at all).  DecodeZlib() remains the
	// final validation, so a coincidental RFC 1950 header is left untouched.
	if ((src_ext && (!strcasecmp (src_ext, ".zlib") || !strcasecmp (src_ext, ".deflate")))
		|| (size >= 6 && IsZlib (data, size) >= 0))
	{
		const bool raw_deflate = src_ext && !strcasecmp (src_ext, ".deflate");
		err = DecodeZlib (&decoded, &decoded_size, data, size, raw_deflate);
		FREE (data);
		if (err)
			return err;

		char dest[PATH_MAX];
		if (opt_dest && !dest_out)
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".bin", false);
		else
		{
			snprintf (dest, sizeof (dest), "%s", arg);
			char *dot = strrchr (dest, '.');
			bool stripped = false;
			if (dot && (!strcasecmp (dot, ".zlib") || !strcasecmp (dot, ".deflate")))
			{
				*dot = 0;
				stripped = true;
			}
			char *dot2 = strrchr (dest, '.');
			if (!stripped || !dot2)
			{
				const nfmt_info_t dec_info = DetectNintendoFormat (decoded, decoded_size, dest);
				ccp ext = ".bin";
				if (decoded_size >= 4 && be32 (decoded) == U8_MAGIC_NUM)
					ext = ".u8";
				else if (dec_info.type == NFMT_SARC || dec_info.type == NFMT_BFMA)
					ext = ".sarc";
				else if (dec_info.type == NFMT_NARC)
					ext = ".narc";
				else if (dec_info.type == NFMT_BFRES)
					ext = ".bfres";
				else if (dec_info.type == NFMT_BCRES)
					ext = ".bcres";
				else if (decoded_size >= 4 && !memcmp (decoded, "darc", 4))
					ext = ".darc";
				else if (decoded_size >= 4 && !memcmp (decoded, "RARC", 4))
					ext = ".rarc";
				else if (decoded_size >= 4 && !memcmp (decoded, "WARC", 4))
					ext = ".warc";
				snprintf (dest + strlen (dest), sizeof (dest) - strlen (dest), "%s", ext);
			}
		}
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECOMPRESS %s:%s -> RAW:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", raw_deflate ? "Deflate" : "Zlib", arg, dest);
		if (!testmode)
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f && fwrite (decoded, 1, decoded_size, F.f) != decoded_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", decoded_size, dest);
			ResetFile (&F, opt_preserve);
		}
		FREE (decoded);
		if (!err && dest_out)
			snprintf (dest_out, dest_out_size, "%s", dest);
		return err;
	}

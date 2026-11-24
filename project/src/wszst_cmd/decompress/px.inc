	// Pokemon Mystery Dungeon PX containers ("AT4PX" / "PKDPX", see DecodePX): ".at4px" / ".pkdpx"
	// files and PX-wrapped ".dat" files; the magic and an exact decode are the gate.
	if (size >= 18 && (!memcmp (data, "AT4PX", 5) || !memcmp (data, "PKDPX", 5)))
	{
		err = DecodePX (&decoded, &decoded_size, data, size);
		FREE (data);
		if (err)
			return ERR_NOTHING_TO_DO;
		char dest[PATH_MAX];
		if (opt_dest && !dest_out)
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".bin", false);
		else
		{
			snprintf (dest, sizeof (dest), "%s", arg);
			char *dot = decompression_extension (dest);
			if (dot && (!strcasecmp (dot, ".at4px") || !strcasecmp (dot, ".pkdpx")))
				*dot = 0;
			snprintf (dest + strlen (dest), sizeof (dest) - strlen (dest), ".bin");
		}
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECOMPRESS PX:%s -> RAW:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
		if (!testmode)
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f && fwrite (decoded, 1, decoded_size, F.f) != decoded_size)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", decoded_size, dest);
			const enumError close_err = ResetFile (&F, opt_preserve);
			if (close_err > err)
				err = close_err;
		}
		FREE (decoded);
		if (!err && dest_out)
			snprintf (dest_out, dest_out_size, "%s", dest);
		return err;
	}

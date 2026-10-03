	// Banpresto/Now Production "MTXZ" wrapper (seen on most ".mz" assets in
	// Crayon Shin-chan: Saikyou Kazoku Kasukabe King, Wii). 16-byte header:
	// "MTXZ" magic, a 4-byte field always seen 0, a big-endian u32 giving
	// the inflated size, and a 4-byte field always seen 0, followed by a
	// raw zlib stream. The inflated payload always turns out to be this
	// engine's own little resource table -- an LE entry count/offset/size
	// array pointing at one or more "HGPK"-tagged blobs -- so name the
	// output .hgpk to route it into extract_hgpk_pak_file() on the
	// recursive hop instead of leaving it as an inert .bin dump.
	if (size >= 16 && !memcmp (data, "MTXZ", 4))
	{
		err = DecodeZlibGrow (&decoded, &decoded_size, data + 16, (uint)size - 16);
		FREE (data);
		if (err)
			return err;

		char dest[PATH_MAX];
		if (opt_dest && !dest_out)
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".hgpk", false);
		else
		{
			snprintf (dest, sizeof (dest), "%s", arg);
			char *dot = decompression_extension (dest);
			if (dot)
				*dot = 0;
			snprintf (dest + strlen (dest), sizeof (dest) - strlen (dest), ".hgpk");
		}
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECOMPRESS MTXZ:%s -> RAW:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, dest);
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

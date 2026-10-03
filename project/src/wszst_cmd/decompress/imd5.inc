	// Wii banner members (banner.bin / icon.bin / sound.bin, and other Wii
	// menu resources) are wrapped in an IMD5 header and usually an "LZ77"
	// layer on top of the plain LZ10/LZ11 stream.  Strip both here so the
	// U8 archive inside reaches the ordinary extraction pass -- that is what
	// turns a channel banner into its BRLYT/BRLAN/TPL/BRSTM parts.
	if (size > IMD5_SIZE && (IsIMD5 (data, size) || !memcmp (data, "LZ77", 4)))
	{
		bool was_compressed = false;
		err = UnwrapWiiBannerFile (&decoded, &decoded_size, &was_compressed, data, size);
		FREE (data);
		if (err)
			return err == ERR_NOTHING_TO_DO ? ERR_NOTHING_TO_DO : err;

		char dest[PATH_MAX];
		ccp ext = decoded_size >= 4 && be32 (decoded) == U8_MAGIC_NUM ? ".u8" : ".bin";
		if (opt_dest && !dest_out)
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ext, false);
		else
			snprintf (dest, sizeof (dest), "%s%s", arg, ext);
		// Same rule as the generic path below: an existing unwrapped payload
		// is reused, not an error, when the tree is visited a second time.
		if (!opt_overwrite && !access (dest, F_OK))
		{
			if (dest_out && dest_out_size)
				snprintf (dest_out, dest_out_size, "%s", dest);
			FREE (decoded);
			return ERR_OK;
		}
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECOMPRESS %s:%s -> RAW:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", was_compressed ? "IMD5+LZ77" : "IMD5", arg, dest);
		if (!testmode)
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f && fwrite (decoded, 1, decoded_size, F.f) != decoded_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", decoded_size, dest);
			const enumError close_err = ResetFile (&F, opt_preserve);
			if (close_err > err)
				err = close_err;
		}
		FREE (decoded);
		if (!err && dest_out)
			snprintf (dest_out, dest_out_size, "%s", dest);
		return err;
	}

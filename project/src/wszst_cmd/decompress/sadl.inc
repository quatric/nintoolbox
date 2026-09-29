	const bool is_sadl
		= size >= 8 && (!memcmp (data, "sadl\0\0\0\0", 8) || !memcmp (data, "SADL", 4));
	if (is_sadl
		|| (src_ext
			&& (!strcasecmp (src_ext, ".bfwav") || !strcasecmp (src_ext, ".bcwav")
				|| !strcasecmp (src_ext, ".cwav") || !strcasecmp (src_ext, ".fwav")
				|| !strcasecmp (src_ext, ".brwav") || !strcasecmp (src_ext, ".rwav"))))
	{
		err = is_sadl ? DecodeSADL_WAV (&decoded, &decoded_size, data, size)
					  : DecodeBXWAV (&decoded, &decoded_size, data, size);
		FREE (data);
		if (err || !decoded || !decoded_size)
		{
			FREE (decoded);
			return is_sadl ? (err ? err : ERR_INVALID_DATA) : ERR_NOTHING_TO_DO;
		}

		char dest[PATH_MAX];
		if (opt_dest)
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".wav", false);
		else
			snprintf (dest, sizeof (dest), "%s.wav", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECOMPRESS %s -> WAV:%s\n", verbose > 0 ? "\n" : "",
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
		if (!err && is_final_out)
			*is_final_out = true; // a decoded WAV, not an intermediate
		return err;
	}

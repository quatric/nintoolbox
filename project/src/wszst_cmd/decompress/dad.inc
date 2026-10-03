	const bool is_dad = IsSomaDad (data, size);
	if (is_dad)
	{
		size_t out_sz = 0;
		err = DecompressSomaDad_Alloc (&decoded, &out_sz, data, size);
		decoded_size = (uint)out_sz;
		FREE (data);
		if (err || !decoded || !decoded_size)
		{
			FREE (decoded);
			return err ? err : ERR_INVALID_DATA;
		}

		char dest[PATH_MAX];
		ccp out_ext = ".bin";
		if (decoded_size >= 4)
		{
			if (!memcmp (decoded, "OBP1", 4))
				out_ext = ".obp";
			else if (!memcmp (decoded, "BGP1", 4))
				out_ext = ".bgp";
			else if (!memcmp (decoded, "PACK", 4))
				out_ext = ".pack";
			else if (!memcmp (decoded, "DFN\0", 4))
				out_ext = ".dfn";
		}
		if (opt_dest)
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, out_ext, false);
		else
		{
			char base_name[PATH_MAX];
			snprintf (base_name, sizeof (base_name), "%s", arg);
			char *dot = strrchr (base_name, '.');
			if (dot && !strcasecmp (dot, ".dad"))
				*dot = '\0';
			snprintf (dest, sizeof (dest), "%s%s", base_name, out_ext);
		}

		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECOMPRESS SOMA-DAD:%s -> %s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, dest);
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

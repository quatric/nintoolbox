	if (src_ext && !strcasecmp (src_ext, ".wux"))
	{
		FREE (data);
		char dest[PATH_MAX];
		if (opt_dest)
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".wud", false);
		else
		{
			snprintf (dest, sizeof (dest), "%s", arg);
			char *dot = strrchr (dest, '.');
			if (dot)
				*dot = 0;
			snprintf (dest + strlen (dest), sizeof (dest) - strlen (dest), ".wud");
		}
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECOMPRESS WUX:%s -> RAW:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, dest);
		if (!testmode)
		{
			if (!wux_decompress (arg, dest))
				return ERR_WRITE_FAILED;
		}
		if (dest_out)
			snprintf (dest_out, dest_out_size, "%s", dest);
		return ERR_OK;
	}

	// The 4-byte zstd frame magic (28 B5 2F FD) and the skippable-frame
	// magics are strong enough to trust during recursive extraction, not
	// just for an explicit `decompress` call.  Some assets are stored
	// double-compressed -- e.g. Tomodachi Life's romfs ships
	// "*.bntx.zs" whose payload is itself another bare zstd frame -- and
	// without the magic path here the inner frame is left undecoded on the
	// second hop.  (Contrast IsZlib, whose 2-byte check stays gated on
	// !dest_out because it false-positives on arbitrary data.)
	if ((src_ext
			&& (!strcasecmp (src_ext, ".zs") || !strcasecmp (src_ext, ".zst")
				|| !strcasecmp (src_ext, ".zstd")))
		|| (size >= 4 && IsZSTD (data, size) > 0))
	{
		err = DecodeZSTD (&decoded, &decoded_size, data, size);
		FREE (data);
		if (err)
			return err;

		// Peel stacked zstd frames in one hop.  Tomodachi Life's romfs
		// stores icon textures double-compressed: the outer "*.bntx.zs"
		// frame decodes to another bare zstd frame that decodes to the real
		// BNTX.  Doing this here (rather than leaning on the extractor to
		// recurse) keeps a single decompressed artefact whose name and
		// format detection reflect the actual payload.
		for (int guard = 0;
			guard < 8 && decoded && decoded_size >= 4 && IsZSTD (decoded, decoded_size) > 0;
			guard++)
		{
			u8 *inner = 0;
			uint inner_size = 0;
			if (DecodeZSTD (&inner, &inner_size, decoded, decoded_size) != ERR_OK || !inner)
				break;
			FREE (decoded);
			decoded = inner;
			decoded_size = inner_size;
		}

		char dest[PATH_MAX];
		if (opt_dest && !dest_out)
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".bin", false);
		else
		{
			snprintf (dest, sizeof (dest), "%s", arg);
			char *dot = decompression_extension (dest);
			bool stripped = false;
			if (dot
				&& (!strcasecmp (dot, ".zs") || !strcasecmp (dot, ".zst")
					|| !strcasecmp (dot, ".zstd")))
			{
				*dot = 0;
				stripped = true;
			}
			char *dot2 = decompression_extension (dest);
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
			fprintf (stdlog, "%s%sDECOMPRESS ZSTD:%s -> RAW:%s\n", verbose > 0 ? "\n" : "",
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

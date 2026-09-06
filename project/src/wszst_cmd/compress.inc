	FREE (packed);
	return err;
}

static enumError cmd_compress ()
{
	static const char dest_fname[] = "\1P/\1N\1?T";
	CheckOptDest (dest_fname, false);

	enumError max_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];
		enumError native_err = compress_nintendo_file (arg);
		if (native_err != ERR_NOTHING_TO_DO)
		{
			if (max_err < native_err)
				max_err = native_err;
			continue;
		}

		szs_file_t szs;
		InitializeSZS (&szs);
		enumError err = LoadSZS (&szs, arg, true, opt_ignore > 0, false);

		if (err <= ERR_WARNING && err != ERR_NOT_EXISTS)
		{
			file_format_t ff_dest = szs.fform_arch;
			if (ff_dest == FF_U8 && opt_fform == FF_WU8 || ff_dest == FF_WU8 && opt_fform == FF_U8)
			{
				ff_dest = opt_fform;
			}
			PRINT ("FF = %s , %s => %s\n", GetNameFF_SZS (&szs), GetNameFF_SZScurrent (&szs),
				GetNameFF (0, ff_dest));

			char dest[PATH_MAX];
			SubstDest (dest, sizeof (dest), arg, opt_dest, dest_fname,
				GetExtFF (fform_compr, ff_dest), false);
			if (verbose >= 0 || testmode)
			{
				fprintf (stdlog, "%s%sCOMPRESS %s:%s -> %s:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", GetNameFF_SZS (&szs), arg,
					GetNameFF (fform_compr, ff_dest), dest);
				fflush (stdlog);
			}

			if (err <= ERR_WARNING && err != ERR_NOT_EXISTS)
			{
				PatchSZS (&szs);
				if (opt_norm || need_norm > 0)
					NormalizeSZS (&szs);

				if (ff_dest == FF_WU8)
					err = EncodeWU8 (&szs);

				if (err <= ERR_WARNING && err != ERR_NOT_EXISTS)
				{
					szs.dest_fname = dest;
					CompressSZS (&szs, 0, true);
					szs.dest_fname = 0;

					if (szs.cache_used && (int)max_err < ERR_CACHE_USED && parallel_count <= 0)
						max_err = ERR_CACHE_USED;

					File_t F;
					err = CreateFileOpt (&F, true, dest, testmode, arg);
					if (F.f)
					{
						SetFileAttrib (&F.fatt, &szs.fatt, 0);
						size_t wstat = fwrite (szs.cdata, 1, szs.csize, F.f);
						if (wstat != szs.csize)
							err = FILEERROR1 (&F, ERR_WRITE_FAILED,
								"Writing %zu bytes failed: %s\n", szs.csize, dest);
					}
					ResetFile (&F, opt_preserve);
					LinkCacheData (szs.cache_fname, dest, szs.cdata, szs.csize);

					if (!err && opt_remove_src)
						RemoveSource (arg, dest, verbose >= 0, testmode);
				}
			}
		}

		if (max_err < err)
			max_err = err;
		ResetSZS (&szs);
	}

	ResetStringField (&plist);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command decompress		///////////////
///////////////////////////////////////////////////////////////////////////////

// Decompress raw Nintendo codecs through the normal wszst destination and
// overwrite path. They are deliberately handled before LoadSZS(), because
// their payload is not an archive.
static enumError decompress_nintendo_file3 (
	ccp arg, char *dest_out, uint dest_out_size, bool *is_final_out);

static enumError decompress_nintendo_file (ccp arg)
{
	return decompress_nintendo_file3 (arg, 0, 0, 0);
}

static enumError decompress_nintendo_file2 (ccp arg, char *dest_out, uint dest_out_size)
{
	return decompress_nintendo_file3 (arg, dest_out, dest_out_size, 0);
}

// 'is_final_out' distinguishes this function's two kinds of output. Most
// branches decompress a payload that may itself be a container, so the caller
// recurses into it and discards it if nothing recognises it. A few branches
// instead *convert* to a finished deliverable -- BXWAV -> WAV is one -- and
// those must survive that discard. See extract_one_file().
static enumError decompress_nintendo_file3 (
	ccp arg, char *dest_out, uint dest_out_size, bool *is_final_out)
{
	if (is_final_out)
		*is_final_out = false;
	// Like extract_at7_file(), this has no extension gate (compression magic
	// can appear in any filename) so it runs against every file reaching
	// extract_one_file() -- including multi-gigabyte pass-through sources.
	// Cap the load and suppress LoadFileAlloc's own messages so an oversized
	// file is declined quietly instead of fully read into memory (see
	// extract_at7_file()'s comment for how this was found: the raw EFBIG
	// errno, 27, used to be returned here and aliased ERU_WARNING in this
	// file's own enumError enum, silently aborting the whole XX pipeline).
	u8 *data = 0, *decoded = 0;
	size_t file_size = 0;
	uint decoded_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &data, &file_size, UINT_MAX, 2, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	const uint size = file_size;

	// BLZ ("backward LZSS", DS ARM9/ARM7/overlay compression) has no magic
	// at the start to detect by -- everything is a footer at the end, and
	// any file could coincidentally have a plausible-looking one. So unlike
	// every other codec here it's dispatched by the SOURCE extension, not
	// DetectNintendoFormat()'s header-magic table; the pass-through side
	// (ndstool-staged arm9.bin/arm7.bin/overlay) calls DecodeBLZ() directly
	// instead, for the same reason.
	ccp src_ext = strrchr (arg, '.');
	if (src_ext && !strcasecmp (src_ext, ".blz"))
	{
		err = DecodeBLZ (&decoded, &decoded_size, data, size);
		FREE (data);
		if (err)
			return err;

		char dest[PATH_MAX];
		if (opt_dest)
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".bin", false);
		else
		{
			snprintf (dest, sizeof (dest), "%s", arg);
			char *dot = strrchr (dest, '.');
			if (dot)
				*dot = 0;
			snprintf (dest + strlen (dest), sizeof (dest) - strlen (dest), ".bin");
		}
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECOMPRESS BLZ:%s -> RAW:%s\n", verbose > 0 ? "\n" : "",
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

	if ((src_ext && (!strcasecmp (src_ext, ".zlib") || !strcasecmp (src_ext, ".deflate")))
		|| (size >= 6 && IsZlib (data, size) >= 0 && !dest_out))
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

	if ((src_ext && (!strcasecmp (src_ext, ".zs") || !strcasecmp (src_ext, ".zst") || !strcasecmp (src_ext, ".zstd")))
		|| (size >= 4 && IsZSTD (data, size) > 0 && !dest_out))
	{
		err = DecodeZSTD (&decoded, &decoded_size, data, size);
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
			if (dot && (!strcasecmp (dot, ".zs") || !strcasecmp (dot, ".zst") || !strcasecmp (dot, ".zstd")))
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

	if (src_ext
		&& (!strcasecmp (src_ext, ".bfwav") || !strcasecmp (src_ext, ".bcwav")
			|| !strcasecmp (src_ext, ".cwav") || !strcasecmp (src_ext, ".fwav")
			|| !strcasecmp (src_ext, ".brwav") || !strcasecmp (src_ext, ".rwav")))
	{
		err = DecodeBXWAV (&decoded, &decoded_size, data, size);
		FREE (data);
		if (err || !decoded || !decoded_size)
		{
			FREE (decoded);
			return ERR_NOTHING_TO_DO;
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

	const nfmt_info_t info = DetectNintendoFormat (data, size, arg);
	// A short unrecognized prefix (e.g. AquaSpace's "CX00" tag ahead of a
	// standard LZ10/LZ11 stream) is reported via payload_offset rather than
	// baked into the returned type, so every codec here shares one place
	// that skips it instead of each decoder needing to know about wrappers.
	const u8 *pdata = info.payload_offset < size ? data + info.payload_offset : data;
	const uint psize = info.payload_offset < size ? size - info.payload_offset : size;
	switch (info.type)
	{
		case NFMT_LZ10:
		case NFMT_LZ11:
			err = DecodeLZ10LZ11 (&decoded, &decoded_size, pdata, psize);
			break;
		case NFMT_MVDK:
			err = DecodeMVDK (&decoded, &decoded_size, pdata, psize);
			break;
		case NFMT_HUFF4:
		case NFMT_HUFF8:
			err = DecodeNintendoHuff (&decoded, &decoded_size, data, size);
			break;
		case NFMT_RL:
			err = DecodeNintendoRL (&decoded, &decoded_size, data, size);
			break;
		case NFMT_ASH0:
			err = DecodeASH0 (&decoded, &decoded_size, data, size);
			break;
		case NFMT_YAY0:
			err = DecodeYay0 (&decoded, &decoded_size, data, size);
			break;
		case NFMT_LZH8:
			err = DecodeLZH8 (&decoded, &decoded_size, data, size);
			break;
		case NFMT_QLZ:
			err = DecodeQuickLZ (&decoded, &decoded_size, data, size);
			break;
		case NFMT_STPL:
			err = DecodeCamelot (&decoded, &decoded_size, data, size);
			break;
		case NFMT_RNC:
			err = DecodeRNC (&decoded, &decoded_size, data, size);
			break;
		case NFMT_ROMC:
			err = DecodeRomC (&decoded, &decoded_size, data, size);
			break;
		case NFMT_AT7:
			err = DecodeAT7 (&decoded, &decoded_size, data, size);
			break;
		case NFMT_FZIP:
			err = DecodeFZIP (&decoded, &decoded_size, data, size);
			break;
		case NFMT_VLX:
			err = DecodeVLX (&decoded, &decoded_size, data, size);
			break;
		case NFMT_PUCRUNCH:
			err = DecodePuCrunch (&decoded, &decoded_size, data, size);
			break;
		case NFMT_LZX:
			err = DecodeLZX (&decoded, &decoded_size, data, size);
			break;
		case NFMT_DIFF8:
			err = DecodeDiff8 (&decoded, &decoded_size, data, size);
			break;
		case NFMT_DIFF16:
			err = DecodeDiff16 (&decoded, &decoded_size, data, size);
			break;
		case NFMT_LZOVL:
			err = DecodeLZOvl (&decoded, &decoded_size, data, size);
			break;
		case NFMT_PSDK:
			err = DecodePSDK (&decoded, &decoded_size, data, size);
			break;

		default:
			FREE (data);
			return ERR_NOTHING_TO_DO;
	}
	FREE (data);
	if (err)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	if (opt_dest && strcmp (opt_dest, "\1P/\1N\1?T"))
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0,
			info.type == NFMT_STPL		 ? ".tpl"
				: info.type == NFMT_ROMC ? ".z64"
										 : ".bin",
			false);
	else
	{
		snprintf (dest, sizeof (dest), "%s", arg);
		char *dot = strrchr (dest, '.');
		bool stripped = false;
		if (dot
			&& (!strcasecmp (dot, ".lz") || !strcasecmp (dot, ".lz10") || !strcasecmp (dot, ".lz11")
				|| !strcasecmp (dot, ".ash") || !strcasecmp (dot, ".yay0")
				|| !strcasecmp (dot, ".qlz") || !strcasecmp (dot, ".rnc")
				|| !strcasecmp (dot, ".romc") || !strcasecmp (dot, ".at7")
				|| !strcasecmp (dot, ".stpl") || !strcasecmp (dot, ".p")
				|| !strcasecmp (dot, ".lzs") || !strcasecmp (dot, ".fzip")
				|| !strcasecmp (dot, ".vlx") || !strcasecmp (dot, ".pc")
				|| !strcasecmp (dot, ".lzx") || !strcasecmp (dot, ".diff")
				|| !strcasecmp (dot, ".ovl") || !strcasecmp (dot, ".psdk")
				|| !strcasecmp (dot, ".cmp") || !strcasecmp (dot, ".lzh8")
				|| !strcasecmp (dot, ".lh")))
		{
			*dot = 0;
			stripped = true;
		}

		char *dot2 = strrchr (dest, '.');
		if (!stripped || !dot2)
		{
			const nfmt_info_t dec_info = DetectNintendoFormat (decoded, decoded_size, dest);
			ccp ext = ".bin";
			if (info.type == NFMT_STPL || dec_info.type == NFMT_TPL)
				ext = ".tpl";
			else if (dec_info.type == NFMT_NCGR)
				ext = ".ncgr";
			else if (dec_info.type == NFMT_NCLR)
				ext = ".nclr";
			else if (dec_info.type == NFMT_NCER)
				ext = ".ncer";
			else if (dec_info.type == NFMT_NANR)
				ext = ".nanr";
			else if (dec_info.type == NFMT_NSCR)
				ext = ".nscr";
			else if (dec_info.type == NFMT_NSBTX)
				ext = ".nsbtx";
			else if (dec_info.type == NFMT_NFTR)
				ext = ".nftr";
			else if (dec_info.type == NFMT_BNFR)
				ext = ".bnfr";
			else if (dec_info.type == NFMT_BNLL)
				ext = ".bnll";
			else if (dec_info.type == NFMT_BNCL)
				ext = ".bncl";
			else if (dec_info.type == NFMT_BNBL)
				ext = ".bnbl";
			else if (dec_info.type == NFMT_SARC)
				ext = ".sarc";
			else if (dec_info.type == NFMT_NARC)
				ext = ".narc";
			else if (dec_info.type == NFMT_BFRES)
				ext = ".bfres";
			else if (decoded_size >= 4 && !memcmp (decoded, "WARC", 4))
				ext = ".warc";
			snprintf (dest + strlen (dest), sizeof (dest) - strlen (dest), "%s", ext);
		}
	}
	if (!opt_overwrite && !access (dest, F_OK))
	{
		if (dest_out && dest_out_size)
			snprintf (dest_out, dest_out_size, "%s", dest);
		FREE (decoded);
		return ERR_OK;
	}

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECOMPRESS %s:%s -> RAW:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", GetNintendoFormatName (info.type), arg, dest);

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

static enumError cmd_decompress ()
{
	static const char dest_fname[] = "\1P/\1N\1?T";
	CheckOptDest (dest_fname, false);

	enumError max_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];
		enumError native_err = decompress_nintendo_file (arg);
		if (native_err != ERR_NOTHING_TO_DO)
		{
			if (max_err < native_err)
				max_err = native_err;
			continue;
		}

		szs_file_t szs;
		InitializeSZS (&szs);
		enumError err = LoadSZS (&szs, arg, true, opt_ignore > 0, false);

		if (err <= ERR_WARNING && err != ERR_NOT_EXISTS)
		{
			file_format_t ff_dest = szs.fform_arch;
			if (ff_dest == FF_U8 && opt_fform == FF_WU8 || ff_dest == FF_WU8 && opt_fform == FF_U8)
			{
				ff_dest = opt_fform;
			}
			PRINT ("FF = %s , %s => %s\n", GetNameFF_SZS (&szs), GetNameFF_SZScurrent (&szs),
				GetNameFF (0, ff_dest));

			char dest[PATH_MAX];
			ccp ext = GetExtFF (ff_dest, 0);
			SubstDest (dest, sizeof (dest), arg, opt_dest, ext, ext, false);

			if (verbose >= 0 || testmode)
			{
				fprintf (stdlog, "%s%sDECOMPRESS %s:%s -> %s:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", GetNameFF_SZS (&szs), arg, GetNameFF (0, ff_dest),
					dest);
				fflush (stdlog);
			}

			if (err <= ERR_WARNING && err != ERR_NOT_EXISTS)
			{
				PatchSZS (&szs);
				if (opt_norm || need_norm > 0)
					NormalizeSZS (&szs);

				if (ff_dest == FF_WU8)
					err = EncodeWU8 (&szs);

				if (err <= ERR_WARNING && err != ERR_NOT_EXISTS)
				{
					File_t F;
					err = CreateFileOpt (&F, true, dest, testmode, arg);
					if (F.f)
					{
						SetFileAttrib (&F.fatt, &szs.fatt, 0);
						size_t wstat = fwrite (szs.data, 1, szs.size, F.f);
						if (wstat != szs.size)
							err = FILEERROR1 (&F, ERR_WRITE_FAILED,
								"Writing %zu bytes failed: %s\n", szs.size, dest);
					}
					ResetFile (&F, opt_preserve);
					if (!err && opt_remove_src)
						RemoveSource (arg, dest, verbose >= 0, testmode);
				}
			}
		}

		if (max_err < err)
			max_err = err;
		ResetSZS (&szs);
	}

	ResetStringField (&plist);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command create			///////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct sarc_build_list_t
{
	nintendo_sarc_entry_t *entry;
	uint used, size;
} sarc_build_list_t;

static void reset_sarc_build_list (sarc_build_list_t *list)
{
	for (uint i = 0; i < list->used; i++)
	{
		FREE ((void *)list->entry[i].name);
		FREE ((void *)list->entry[i].data);
	}
	FREE (list->entry);
	memset (list, 0, sizeof (*list));
}

static int cmp_strptr_sarc (const void *a, const void *b)
{
	return strcmp (*(char *const *)a, *(char *const *)b);
}

static enumError collect_sarc_dir (sarc_build_list_t *list, ccp root, ccp rel)
{
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s%s%s", root, *rel ? "/" : "", rel);
	DIR *dir = opendir (path);
	if (!dir)
		return ERROR0 (ERR_NOT_EXISTS, "Can't open SARC input directory: %s\n", path);

	// readdir() order is filesystem-dependent and not reproducible (see the
	// analogous comment in lib-szs-create.c's scan_data()): sort the names
	// first so CREATE -> EXTRACT -> CREATE is a pure function of the tree's
	// content, never of directory/inode history.
	uint n_names = 0, cap_names = 0;
	char **names = 0;
	struct dirent *de;
	while ((de = readdir (dir)))
	{
		if (n_names == cap_names)
		{
			cap_names = cap_names ? cap_names * 2 : 16;
			names = REALLOC (names, cap_names * sizeof (*names));
		}
		names[n_names++] = STRDUP (de->d_name);
	}
	closedir (dir);
	dir = 0;
	if (n_names)
		qsort (names, n_names, sizeof (*names), cmp_strptr_sarc);

	enumError err = ERR_OK;
	for (uint name_idx = 0; !err && name_idx < n_names; name_idx++)
	{
		ccp name = names[name_idx];
		if (!strcmp (name, ".") || !strcmp (name, ".."))
			continue;
		// wszst-setup.txt (and its cache sibling) is metadata written by
		// EXTRACT for the generic SZS tree format, not archive payload --
		// including it here as a member is why re-CREATE-ing an
		// EXTRACT-ed rarc/nccarc/etc. tree used to balloon or corrupt the
		// second-generation archive.
		if (!strcmp (name, SZS_SETUP_FILE) || !strcmp (name, ".wszst-cache.txt"))
			continue;
		const uint dlen = strlen (name);
		if (dlen >= 2 && name[dlen - 1] == 'd' && name[dlen - 2] == '.')
			continue;
		char child_rel[PATH_MAX], child_path[PATH_MAX];
		snprintf (child_rel, sizeof (child_rel), "%s%s%s", rel, *rel ? "/" : "", name);
		snprintf (child_path, sizeof (child_path), "%s/%s", root, child_rel);
		struct stat st;
		if (lstat (child_path, &st))
		{
			err = ERROR0 (ERR_NOT_EXISTS, "Can't stat SARC input: %s\n", child_path);
			break;
		}
		if (S_ISDIR (st.st_mode))
			err = collect_sarc_dir (list, root, child_rel);
		else if (S_ISREG (st.st_mode))
		{
			if (list->used == list->size)
			{
				const uint nsize = list->size ? 2 * list->size : 32;
				void *ptr = REALLOC (list->entry, nsize * sizeof (*list->entry));
				if (!ptr)
				{
					err = ERR_CANT_CREATE;
					break;
				}
				list->entry = ptr;
				list->size = nsize;
			}
			u8 *data = 0;
			size_t size = 0;
			err = LoadFileAlloc (child_path, 0, 0, &data, &size, 0, 0, 0, false);
			if (err || size > UINT_MAX)
			{
				FREE (data);
				if (!err)
					err = ERR_FILE_TOO_BIG;
				if (err)
					ERROR0 (err, "Can't load SARC input: %s\n", child_path);
				break;
			}
			nintendo_sarc_entry_t *entry = list->entry + list->used++;
			entry->name = STRDUP (child_rel);
			entry->data = data;
			entry->size = size;
		}
	}
	for (uint i = 0; i < n_names; i++)
		FREE (names[i]);
	FREE (names);
	return err;
}

// The conventional .sarc spelling keeps Nintendo's big-endian form.  The
// explicit suffixes make it possible to create the Wii U/Switch-style little
// endian variant without adding a global option whose meaning would leak into
// all existing archive formats.
static enumError create_sarc_dir (ccp source, ccp dest, bool big_endian)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateSARC (&data, &size, list.entry, list.used, big_endian);
	if (!err && !testmode)
	{
		u8 *final_data = data;
		uint final_size = size;
		u8 *comp_data = 0;
		uint comp_size = 0;
		if (is_ext (dest, ".fzip"))
		{
			err = EncodeFZIP (&comp_data, &comp_size, data, size);
			if (!err)
			{
				final_data = comp_data;
				final_size = comp_size;
			}
		}
		if (!err)
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, source);
			if (F.f && fwrite (final_data, 1, final_size, F.f) != final_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", final_size, dest);
			ResetFile (&F, opt_preserve);
		}
		FREE (comp_data);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_narc_dir (ccp source, ccp dest, bool is_le)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateNARC (&data, &size, list.entry, list.used, is_le);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_darc_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateDARC (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_pac_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreatePAC (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

// Star Fox Zero DAT (Wii U). Names keep the "<ext>/<name>" folder shape the
// extractor writes, which CreateSFZDAT turns back into the type section.
// ".dat" is already spoken for on this tool's extract side (HAL's HSD and A2
// bank archives both use it), so CREATE only routes a directory to the Star
// Fox Zero builder when the directory actually has that container's shape:
// every top-level entry is a short type/extension folder, which is exactly
// what extract_sfzdat_file writes and nothing else here produces.
static bool looks_like_sfzdat_dir (ccp source)
{
	DIR *dir = opendir (source);
	if (!dir)
		return false;
	uint n_type_dirs = 0;
	bool ok = true;
	for (const struct dirent *de; ok && (de = readdir (dir));)
	{
		ccp nm = de->d_name;
		if (*nm == '.' || !strcmp (nm, "wszst-setup.txt") || !strcmp (nm, ".wszst-cache.txt"))
			continue;
		const uint len = (uint)strlen (nm);
		if (len < 1 || len > 4)
		{
			ok = false;
			break;
		}
		for (uint i = 0; i < len; i++)
			if (!isalnum ((int)(u8)nm[i]))
			{
				ok = false;
				break;
			}
		if (!ok)
			break;
		char sub[PATH_MAX];
		snprintf (sub, sizeof (sub), "%s/%s", source, nm);
		struct stat st;
		if (stat (sub, &st) || !S_ISDIR (st.st_mode))
		{
			ok = false;
			break;
		}
		n_type_dirs++;
	}
	closedir (dir);
	return ok && n_type_dirs > 0;
}

static enumError create_sfzdat_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateSFZDAT (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_warc_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateWARC (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		u8 *final_data = data;
		uint final_size = size;
		u8 *comp_data = 0;
		uint comp_size = 0;
		if (is_ext (dest, ".fzip"))
		{
			err = EncodeFZIP (&comp_data, &comp_size, data, size);
			if (!err)
			{
				final_data = comp_data;
				final_size = comp_size;
			}
		}
		if (!err)
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, source);
			if (F.f && fwrite (final_data, 1, final_size, F.f) != final_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", final_size, dest);
			ResetFile (&F, opt_preserve);
		}
		FREE (comp_data);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_bg4_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateBG4 (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_sa01_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateSA01 (&data, &size, list.entry, list.used, true, true);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_ca01_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateCA01 (&data, &size, list.entry, list.used, true, false);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_cram_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateCramARC (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_xpck_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateXPCKArchive (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_ztab_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateZTABArchive (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_mdr_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateMDRArchive (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_pvol_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreatePVOLArchive (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_stpk_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateSTPKArchive (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_f9res_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateF9ResArchive (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_gpkg_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateGPKG (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_mkgpdx_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateMKGPDXPacArchive (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_mtxt_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateMTXTArchive (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_sir0_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateSIR0Archive (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_gar_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateGARArchive (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_arika_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *dest_info = 0;
	uint dest_info_size = 0;
	u8 *dest_game = 0;
	uint dest_game_size = 0;
	if (!err)
		err = CreateArika (&dest_info, &dest_info_size, &dest_game, &dest_game_size,
			list.entry, list.used, "*Dr.Mario-DSi!!!", true);
	if (!err && !testmode)
	{
		char info_path[PATH_MAX];
		char game_path[PATH_MAX];
		ccp slash = strrchr (dest, '/');
		if (slash)
		{
			snprintf (info_path, sizeof (info_path), "%.*s/INFO.DAT", (int)(slash - dest), dest);
			snprintf (game_path, sizeof (game_path), "%.*s/GAME.DAT", (int)(slash - dest), dest);
		}
		else
		{
			snprintf (info_path, sizeof (info_path), "INFO.DAT");
			snprintf (game_path, sizeof (game_path), "GAME.DAT");
		}
		File_t F_info, F_game;
		err = CreateFileOpt (&F_info, true, info_path, false, source);
		if (!err && F_info.f && fwrite (dest_info, 1, dest_info_size, F_info.f) != dest_info_size)
			err = FILEERROR1 (&F_info, ERR_WRITE_FAILED, "Writing INFO.DAT failed: %s\n", info_path);
		ResetFile (&F_info, opt_preserve);

		if (!err)
		{
			err = CreateFileOpt (&F_game, true, game_path, false, source);
			if (!err && F_game.f && fwrite (dest_game, 1, dest_game_size, F_game.f) != dest_game_size)
				err = FILEERROR1 (&F_game, ERR_WRITE_FAILED, "Writing GAME.DAT failed: %s\n", game_path);
			ResetFile (&F_game, opt_preserve);
		}
	}
	FREE (dest_info);
	FREE (dest_game);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_lspk_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *dest_pkh = 0;
	uint dest_pkh_size = 0;
	u8 *dest_pk = 0;
	uint dest_pk_size = 0;
	if (!err)
		err = CreateLSPKArchive (&dest_pkh, &dest_pkh_size, &dest_pk, &dest_pk_size,
			list.entry, list.used);
	if (!err && !testmode)
	{
		char pkh_path[PATH_MAX];
		char pk_path[PATH_MAX];
		snprintf (pk_path, sizeof (pk_path), "%s", dest);
		char *dot = strrchr (pk_path, '.');
		char *slash = strrchr (pk_path, '/');
		if (dot && (!slash || dot > slash))
			*dot = 0;
		snprintf (pkh_path, sizeof (pkh_path), "%s.pkh", pk_path);
		snprintf (pk_path, sizeof (pk_path), "%s.pk", pk_path);

		File_t F_pkh, F_pk;
		err = CreateFileOpt (&F_pkh, true, pkh_path, false, source);
		if (!err && F_pkh.f && fwrite (dest_pkh, 1, dest_pkh_size, F_pkh.f) != dest_pkh_size)
			err = FILEERROR1 (&F_pkh, ERR_WRITE_FAILED, "Writing PKH failed: %s\n", pkh_path);
		ResetFile (&F_pkh, opt_preserve);

		if (!err)
		{
			err = CreateFileOpt (&F_pk, true, pk_path, false, source);
			if (!err && F_pk.f && fwrite (dest_pk, 1, dest_pk_size, F_pk.f) != dest_pk_size)
				err = FILEERROR1 (&F_pk, ERR_WRITE_FAILED, "Writing PK failed: %s\n", pk_path);
			ResetFile (&F_pk, opt_preserve);
		}
	}
	FREE (dest_pkh);
	FREE (dest_pk);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_dtls_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *dest_ls = 0;
	uint dest_ls_size = 0;
	u8 *dest_dt = 0;
	uint dest_dt_size = 0;
	if (!err)
		err = CreateDTLS (&dest_ls, &dest_ls_size, &dest_dt, &dest_dt_size,
			list.entry, list.used, false, true);
	if (!err && !testmode)
	{
		char ls_path[PATH_MAX];
		char dt_path[PATH_MAX];
		snprintf (ls_path, sizeof (ls_path), "%s", dest);
		char *dot = strrchr (ls_path, '.');
		char *slash = strrchr (ls_path, '/');
		if (dot && (!slash || dot > slash))
			*dot = 0;
		snprintf (dt_path, sizeof (dt_path), "%s.dt", ls_path);
		snprintf (ls_path + strlen (ls_path), sizeof (ls_path) - strlen (ls_path), ".ls");

		File_t F_ls, F_dt;
		err = CreateFileOpt (&F_ls, true, ls_path, false, source);
		if (!err && F_ls.f && fwrite (dest_ls, 1, dest_ls_size, F_ls.f) != dest_ls_size)
			err = FILEERROR1 (&F_ls, ERR_WRITE_FAILED, "Writing LS failed: %s\n", ls_path);
		ResetFile (&F_ls, opt_preserve);

		if (!err)
		{
			err = CreateFileOpt (&F_dt, true, dt_path, false, source);
			if (!err && F_dt.f && fwrite (dest_dt, 1, dest_dt_size, F_dt.f) != dest_dt_size)
				err = FILEERROR1 (&F_dt, ERR_WRITE_FAILED, "Writing DT failed: %s\n", dt_path);
			ResetFile (&F_dt, opt_preserve);
		}
	}
	FREE (dest_ls);
	FREE (dest_dt);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_ptlg_dir (ccp source, ccp dest, bool is_gc)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *dest_ptlg = 0;
	uint dest_ptlg_size = 0;
	if (!err)
		err = CreatePTLGArchive (&dest_ptlg, &dest_ptlg_size, list.entry, list.used, is_gc);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (!err && F.f && fwrite (dest_ptlg, 1, dest_ptlg_size, F.f) != dest_ptlg_size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing PTLG failed: %s\n", dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (dest_ptlg);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_apak_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateAPAKArchive (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_nxarc_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateNXARCArchive (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_pkz_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreatePKZArchive (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_tmpk_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateTMPKArchive (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_vibs_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateVIBSArchive (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_zlarc_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateZLARCArchive (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_fsys_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateFSYS (&data, &size, list.entry, list.used, true);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_gsh_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;

	gsh_shader_input_t *inputs = 0;
	uint n_inputs = 0;
	if (!err)
	{
		inputs = CALLOC (list.used, sizeof (*inputs));
		if (!inputs)
			err = ERR_CANT_CREATE;
	}

	for (uint i = 0; !err && i < list.used; i++)
	{
		ccp name = list.entry[i].name ? list.entry[i].name : "";
		gtx_shader_stage_t stage = GTX_SHADER_VERTEX;
		if (strcasestr (name, "pixel") || strcasestr (name, "frag") || strcasestr (name, ".ps"))
			stage = GTX_SHADER_PIXEL;
		else if (strcasestr (name, "geom") || strcasestr (name, ".gs"))
			stage = GTX_SHADER_GEOMETRY;
		else if (strcasestr (name, "comp") || strcasestr (name, ".cs"))
			stage = GTX_SHADER_COMPUTE;

		u8 *prog = 0;
		uint prog_sz = 0;
		if (list.entry[i].size && is_ext (name, ".latte"))
		{
			char *text = MALLOC (list.entry[i].size + 1);
			if (text)
			{
				memcpy (text, list.entry[i].data, list.entry[i].size);
				text[list.entry[i].size] = 0;
				err = AssembleLatteCF (&prog, &prog_sz, text);
				FREE (text);
			}
		}
		else
		{
			prog = MALLOC (list.entry[i].size);
			if (prog)
			{
				memcpy (prog, list.entry[i].data, list.entry[i].size);
				prog_sz = list.entry[i].size;
			}
		}

		if (err || !prog)
		{
			FREE (prog);
			err = ERR_INVALID_DATA;
			break;
		}

		inputs[n_inputs].stage = stage;
		inputs[n_inputs].program_data = prog;
		inputs[n_inputs].program_size = prog_sz;
		n_inputs++;
	}

	u8 *data = 0;
	uint size = 0;
	if (!err && n_inputs)
		err = EncodeGSHShaders (&data, &size, inputs, n_inputs);

	for (uint i = 0; i < n_inputs; i++)
		FREE ((void *)inputs[i].program_data);
	FREE (inputs);

	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_hwl_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;

	u8 *idx_data = 0, *bin_data = 0;
	uint idx_size = 0, bin_size = 0;
	if (!err)
		err = CreateHWLegends (&idx_data, &idx_size, &bin_data, &bin_size, list.entry, list.used);

	if (!err && !testmode)
	{
		char idx_path[PATH_MAX], bin_path[PATH_MAX];
		snprintf (idx_path, sizeof (idx_path), "%s", dest);
		snprintf (bin_path, sizeof (bin_path), "%s", dest);
		char *dot = strrchr (idx_path, '.');
		if (dot && !strcasecmp (dot, ".bin"))
			strcpy (dot, ".idx");
		dot = strrchr (bin_path, '.');
		if (dot && !strcasecmp (dot, ".idx"))
			strcpy (dot, ".bin");

		File_t F;
		err = CreateFileOpt (&F, true, idx_path, false, source);
		if (F.f && fwrite (idx_data, 1, idx_size, F.f) != idx_size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", idx_size, idx_path);
		ResetFile (&F, opt_preserve);

		if (!err)
		{
			err = CreateFileOpt (&F, true, bin_path, false, source);
			if (F.f && fwrite (bin_data, 1, bin_size, F.f) != bin_size)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", bin_size, bin_path);
			ResetFile (&F, opt_preserve);
		}
	}

	FREE (idx_data);
	FREE (bin_data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_sze_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;

	u8 *sarc_data = 0;
	uint sarc_size = 0;
	if (!err)
		err = CreateSARC (&sarc_data, &sarc_size, list.entry, list.used, false);

	u8 *sze_data = 0;
	uint sze_size = 0;
	if (!err && sarc_data && sarc_size)
		err = EncodeSZE (&sze_data, &sze_size, sarc_data, sarc_size, 0, 0, 1);

	FREE (sarc_data);

	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (sze_data, 1, sze_size, F.f) != sze_size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", sze_size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (sze_data);
	reset_sarc_build_list (&list);
	return err;
}

static bool looks_like_rflres_dir (ccp dir)
{
	char test[PATH_MAX];
	snprintf (test, sizeof (test), "%s/beard", dir);
	struct stat st;
	if (stat (test, &st) == 0 && S_ISDIR (st.st_mode))
		return true;
	snprintf (test, sizeof (test), "%s/faceline", dir);
	if (stat (test, &st) == 0 && S_ISDIR (st.st_mode))
		return true;
	snprintf (test, sizeof (test), "%s/eye", dir);
	if (stat (test, &st) == 0 && S_ISDIR (st.st_mode))
		return true;
	snprintf (test, sizeof (test), "%s/hair", dir);
	if (stat (test, &st) == 0 && S_ISDIR (st.st_mode))
		return true;
	return false;
}

static enumError create_rflres_dir (ccp source, ccp dest, bool big_endian)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;

	// Extraction may have dropped a viewable ".png" companion beside a
	// texture entry's raw ".bin" (see export_rflres_tex_png_if_possible());
	// those companions are display-only and must never become archive
	// entries of their own on repack.
	uint kept = 0;
	for (uint i = 0; i < list.used; i++)
	{
		ccp name = list.entry[i].name;
		const size_t len = name ? strlen (name) : 0;
		if (len > 4 && !strcasecmp (name + len - 4, ".png"))
		{
			FREE ((void *)list.entry[i].name);
			FREE ((void *)list.entry[i].data);
			continue;
		}
		if (kept != i)
			list.entry[kept] = list.entry[i];
		kept++;
	}
	list.used = kept;
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;

	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateMiiRes (&data, &size, list.entry, list.used, big_endian);

	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_rarc_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateRARC (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_sar_dir (ccp source, ccp dest, ccp magic_type)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;

	sound_archive_t sar;
	memset (&sar, 0, sizeof (sar));
	bool is_cafe = (magic_type[0] == 'F');
	sar.is_cafe_or_switch = is_cafe;
	sar.is_big_endian = is_cafe;
	snprintf (sar.magic, sizeof (sar.magic), "%s", magic_type);

	sar.n_entries = list.used;
	sar.entries = CALLOC (list.used, sizeof (sar_file_entry_t));
	for (uint i = 0; i < list.used; i++)
	{
		sar.entries[i].file_id = i;
		sar.entries[i].data = list.entry[i].data;
		sar.entries[i].size = list.entry[i].size;
		sar.entries[i].name = STRDUP (list.entry[i].name);
	}

	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateSoundArchive (&data, &size, &sar);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	ResetSoundArchive (&sar);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_rst_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *car_data = 0, *toc_data = 0;
	uint car_size = 0, toc_size = 0;
	bool compress = !OptionUsed[OPT_NO_COMPRESS];
	bool big_endian = false;
	if (!err)
		err = CreateRST (&car_data, &car_size, &toc_data, &toc_size, list.entry, list.used,
			compress, big_endian);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (car_data, 1, car_size, F.f) != car_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", car_size, dest);
		ResetFile (&F, opt_preserve);

		char toc_dest[PATH_MAX];
		snprintf (toc_dest, sizeof (toc_dest), "%s", dest);
		char *dot = strrchr (toc_dest, '.');
		if (dot)
			snprintf (dot, sizeof (toc_dest) - (dot - toc_dest), ".toc");
		else
			snprintf (toc_dest + strlen (toc_dest), sizeof (toc_dest) - strlen (toc_dest), ".toc");

		File_t Ftoc;
		err = CreateFileOpt (&Ftoc, true, toc_dest, false, source);
		if (Ftoc.f && fwrite (toc_data, 1, toc_size, Ftoc.f) != toc_size)
			err = FILEERROR1 (
				&Ftoc, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", toc_size, toc_dest);
		ResetFile (&Ftoc, opt_preserve);
	}
	FREE (car_data);
	FREE (toc_data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_gfa_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateGFA (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_ccf_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateCCF (&data, &size, list.entry, list.used, true);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_nccarc_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateNCCARC (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static enumError create_at7_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	ccp ext = strrchr (dest, '.');
	bool compress = ext && (!strcasecmp (ext, ".at7p") || !strcasecmp (ext, ".at7x"));
	if (!err)
		err = CreateAT7 (&data, &size, list.entry, list.used, compress);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

static inline uint morton8_swz (uint x, uint y)
{
	return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2)
		| ((y & 4) << 3);
}

static enumError create_ctpk_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	if (err)
	{
		reset_sarc_build_list (&list);
		return err;
	}

	typedef struct
	{
		char name[PATH_MAX];
		u8 *tex_data;
		uint data_size;
		uint width;
		uint height;
	} ctpk_item_t;

	ctpk_item_t *items = CALLOC (list.used, sizeof (ctpk_item_t));
	if (!items)
	{
		reset_sarc_build_list (&list);
		return ERR_CANT_CREATE;
	}

	for (uint i = 0; i < list.used; i++)
	{
		ccp name = list.entry[i].name ? list.entry[i].name : "tex";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;
		snprintf (items[i].name, sizeof (items[i].name), "%s", name);

		if (list.entry[i].size >= 8 && !memcmp (list.entry[i].data, "\x89PNG\r\n\x1a\n", 8))
		{
			char full_path[PATH_MAX];
			snprintf (full_path, sizeof (full_path), "%s/%s", source, list.entry[i].name);
			Image_t img;
			if (LoadPNG (&img, true, false, full_path, 0) == ERR_OK && img.data && img.width
				&& img.height)
			{
				uint w = img.width, h = img.height;
				uint tw = (w + 7) & ~7u, th = (h + 7) & ~7u;
				uint image_size = 4 * tw * th;
				u8 *tex_data = CALLOC (1, image_size);
				if (tex_data)
				{
					const u8 *rgba = img.data;
					for (uint y = 0; y < h; y++)
						for (uint x = 0; x < w; x++)
						{
							uint pos
								= ((y / 8) * (tw / 8) + (x / 8)) * 64 + morton8_swz (x & 7, y & 7);
							const u8 *s = rgba + 4 * (y * img.xwidth + x);
							u8 *d = tex_data + 4 * pos;
							d[0] = s[0];
							d[1] = s[1];
							d[2] = s[2];
							d[3] = s[3];
						}
					items[i].tex_data = tex_data;
					items[i].data_size = image_size;
					items[i].width = w;
					items[i].height = h;
				}
				ResetIMG (&img);
			}
		}
		else
		{
			items[i].tex_data = MALLOC (list.entry[i].size);
			if (items[i].tex_data)
			{
				memcpy (items[i].tex_data, list.entry[i].data, list.entry[i].size);
				items[i].data_size = list.entry[i].size;
				items[i].width = 64;
				items[i].height = 64;
			}
		}
	}

	uint names_size = 0;
	for (uint i = 0; i < list.used; i++)
		names_size += (uint)strlen (items[i].name) + 1;

	uint header_size = 0x20;
	uint entries_size = 0x20 * list.used;
	uint string_table_size = (names_size + 3) & ~3u;
	uint texture_offset = (header_size + entries_size + string_table_size + 0x7F) & ~0x7Fu;

	uint total_tex_size = 0;
	for (uint i = 0; i < list.used; i++)
	{
		total_tex_size += items[i].data_size;
		total_tex_size = (total_tex_size + 0x7F) & ~0x7Fu;
	}

	uint total_size = texture_offset + total_tex_size;
	u8 *out = CALLOC (1, total_size);
	if (!out)
	{
		for (uint i = 0; i < list.used; i++)
			FREE (items[i].tex_data);
		FREE (items);
		reset_sarc_build_list (&list);
		return ERR_CANT_CREATE;
	}

	memcpy (out, "CTPK", 4);
	wr_le16 (out + 4, 1);
	wr_le16 (out + 6, (u16)list.used);
	wr_le32 (out + 8, texture_offset);
	wr_le32 (out + 12, total_tex_size);

	uint str_off = header_size + entries_size;
	uint data_off = 0;

	for (uint i = 0; i < list.used; i++)
	{
		u8 *e = out + header_size + i * 0x20;
		size_t nlen = strlen (items[i].name);

		wr_le32 (e + 0, str_off);
		wr_le32 (e + 4, items[i].data_size);
		wr_le32 (e + 8, data_off);
		wr_le32 (e + 12, 0); // format = RGBA8
		wr_le16 (e + 16, (u16)items[i].width);
		wr_le16 (e + 18, (u16)items[i].height);
		e[20] = 1;
		e[21] = 0;

		memcpy (out + str_off, items[i].name, nlen + 1);
		str_off += (uint)nlen + 1;

		if (items[i].data_size > 0 && items[i].tex_data)
			memcpy (out + texture_offset + data_off, items[i].tex_data, items[i].data_size);

		data_off += items[i].data_size;
		data_off = (data_off + 0x7F) & ~0x7Fu;

		FREE (items[i].tex_data);
	}
	FREE (items);
	reset_sarc_build_list (&list);

	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (out, 1, total_size, F.f) != total_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", total_size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (out);
	return err;
}

static enumError create_mpbin_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	if (err)
	{
		reset_sarc_build_list (&list);
		return err;
	}

	uint num_files = list.used;
	uint header_table_size = 4 + 4 * num_files;
	header_table_size = (header_table_size + 3) & ~3u;

	uint cur_off = header_table_size;
	u32 *file_offsets = CALLOC (num_files, sizeof (u32));
	if (!file_offsets)
	{
		reset_sarc_build_list (&list);
		return ERR_CANT_CREATE;
	}

	for (uint i = 0; i < num_files; i++)
	{
		file_offsets[i] = cur_off;
		cur_off += 8 + list.entry[i].size;
		cur_off = (cur_off + 3) & ~3u;
	}

	u8 *out = CALLOC (1, cur_off);
	if (!out)
	{
		FREE (file_offsets);
		reset_sarc_build_list (&list);
		return ERR_CANT_CREATE;
	}

	wr_be32 (out + 0, num_files);
	for (uint i = 0; i < num_files; i++)
		wr_be32 (out + 4 + 4 * i, file_offsets[i]);

	for (uint i = 0; i < num_files; i++)
	{
		u8 *fh = out + file_offsets[i];
		wr_be32 (fh + 0, list.entry[i].size);
		wr_be32 (fh + 4, 0); // uncompressed
		if (list.entry[i].size > 0 && list.entry[i].data)
			memcpy (fh + 8, list.entry[i].data, list.entry[i].size);
	}
	FREE (file_offsets);
	reset_sarc_build_list (&list);

	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (out, 1, cur_off, F.f) != cur_off)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", cur_off, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (out);
	return err;
}

typedef struct ncer_xml_cell_t
{
	uint count, object_off;
} ncer_xml_cell_t;

// The XML reader intentionally accepts the narrow, stable manifest emitted by
// extract_nitro_sprite_manifest().  It is not a general XML parser: rejecting
// extensions outside that vocabulary prevents a malformed project from being
// converted into an unsafe binary layout.
static enumError create_ncer_xml ( ccp source, ccp dest )
{
    u8 *xml = 0;
    size_t xml_size = 0;
    enumError err = LoadFileAlloc(source,0,0,&xml,&xml_size,16<<20,0,0,false);
    if (err) return err;

    mxml_node_t *tree = mxmlLoadString(NULL, (ccp)xml, MXML_OPAQUE_CALLBACK);
    if (!tree) { FREE(xml); return ERR_INVALID_DATA; }

    mxml_node_t *ncer_node = mxmlFindElement(tree, tree, "ncer", NULL, NULL, MXML_DESCEND);
    if (!ncer_node) { mxmlDelete(tree); FREE(xml); return ERR_INVALID_DATA; }

    const char *cells_attr = mxmlElementGetAttr(ncer_node, "cells");
    if (!cells_attr) { mxmlDelete(tree); FREE(xml); return ERR_INVALID_DATA; }

    uint n_cells = strtoul(cells_attr, NULL, 10);
    if (!n_cells || n_cells > 65535) { mxmlDelete(tree); FREE(xml); return ERR_INVALID_DATA; }

    ncer_xml_cell_t *cells = CALLOC(n_cells,sizeof(*cells));
    u16 *objects = 0;
    uint n_objects = 0, obj_alloc = 0;

    mxml_node_t *cell_node = mxmlFindElement(ncer_node, ncer_node, "cell", NULL, NULL, MXML_DESCEND_FIRST);
    for (uint i = 0; i < n_cells; i++)
    {
        if (!cell_node) { err = ERR_INVALID_DATA; break; }
        const char *idx_attr = mxmlElementGetAttr(cell_node, "index");
        const char *cnt_attr = mxmlElementGetAttr(cell_node, "objects");
        if (!idx_attr || !cnt_attr) { err = ERR_INVALID_DATA; break; }

        uint index = strtoul(idx_attr, NULL, 10);
        uint count = strtoul(cnt_attr, NULL, 10);
        if (index != i || count > 65535-n_objects) { err = ERR_INVALID_DATA; break; }

        cells[i].count = count;
        cells[i].object_off = n_objects;

        mxml_node_t *obj_node = mxmlFindElement(cell_node, cell_node, "obj", NULL, NULL, MXML_DESCEND_FIRST);
        for (uint j = 0; j < count; j++)
        {
            if (!obj_node) { err = ERR_INVALID_DATA; break; }
            const char *a0_attr = mxmlElementGetAttr(obj_node, "attr0");
            const char *a1_attr = mxmlElementGetAttr(obj_node, "attr1");
            const char *a2_attr = mxmlElementGetAttr(obj_node, "attr2");
            if (!a0_attr || !a1_attr || !a2_attr) { err = ERR_INVALID_DATA; break; }

            uint a0 = strtoul(a0_attr, NULL, 16);
            uint a1 = strtoul(a1_attr, NULL, 16);
            uint a2 = strtoul(a2_attr, NULL, 16);
            if (a0 > 0xffff || a1 > 0xffff || a2 > 0xffff) { err = ERR_INVALID_DATA; break; }

            if (n_objects == obj_alloc)
            {
                const uint new_alloc = obj_alloc ? obj_alloc*2 : 64;
                u16 *new_objects = REALLOC(objects,3*new_alloc*sizeof(*objects));
                if (!new_objects) { err = ERR_CANT_CREATE; break; }
                objects = new_objects; obj_alloc = new_alloc;
            }
            objects[3*n_objects] = a0; objects[3*n_objects+1] = a1;
            objects[3*n_objects+2] = a2; n_objects++;
            obj_node = mxmlFindElement(obj_node, cell_node, "obj", NULL, NULL, MXML_NO_DESCEND);
        }
        if (err) break;
        cell_node = mxmlFindElement(cell_node, ncer_node, "cell", NULL, NULL, MXML_NO_DESCEND);
    }
    mxmlDelete(tree);
    const u64 chunk64 = ( 0x20ull + 8ull*n_cells + 6ull*n_objects + 3 ) & ~3ull;
    if (!err && (chunk64 > UINT_MAX || chunk64+0x10 > UINT_MAX)) err = ERR_FILE_TOO_BIG;
    u8 *out = !err ? CALLOC(1,0x10+(uint)chunk64) : 0;
    if (!err && !out) err = ERR_CANT_CREATE;
    if (!err)
    {
        memcpy(out,"RECN",4); write_le16(out+4,0xfeff); write_le16(out+6,0x100);
        write_le32(out+8,0x10+(uint)chunk64); write_le16(out+12,0x10); write_le16(out+14,1);
        u8 *kbec = out+0x10;
        memcpy(kbec,"KBEC",4); write_le32(kbec+4,chunk64); write_le16(kbec+8,n_cells);
        write_le32(kbec+12,0x18); // cell table is at KBEC+0x20
        u8 *obj = kbec+0x20+8*n_cells;
        for (uint i = 0; i < n_cells; i++)
        {
            u8 *cell = kbec+0x20+8*i;
            write_le16(cell,cells[i].count); write_le32(cell+4,6*cells[i].object_off);
        }
        for (uint i = 0; i < n_objects; i++)
        {
            write_le16(obj+6*i,objects[3*i]); write_le16(obj+6*i+2,objects[3*i+1]);
            write_le16(obj+6*i+4,objects[3*i+2]);
        }
        if (verbose >= 0 || testmode)
            fprintf(stdlog,"%sCREATE NCER XML:%s -> %s\n",testmode ? "WOULD " : "",source,dest);
        if (!testmode)
        {
            File_t F;
            CreateFILE(&F,true,dest,testmode,false,true,false,false);
            if (F.f && fwrite(out,1,0x10+(uint)chunk64,F.f) != 0x10+(uint)chunk64)
                err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing NCER failed: %s\n",dest);
            ResetFile(&F,opt_preserve);
        }
    }
    FREE(out); FREE(objects); FREE(cells); FREE(xml);
    return err;
}

typedef struct nanr_xml_frame_t
{
	uint cell, duration, data_off;
} nanr_xml_frame_t;

static enumError create_nanr_xml ( ccp source, ccp dest )
{
    u8 *xml = 0;
    size_t xml_size = 0;
    enumError err = LoadFileAlloc(source,0,0,&xml,&xml_size,16<<20,0,0,false);
    if (err) return err;
    uint n_anims = 0, n_frames = 0;

    mxml_node_t *tree = mxmlLoadString(NULL, (ccp)xml, MXML_OPAQUE_CALLBACK);
    if (!tree) { FREE(xml); return ERR_INVALID_DATA; }

    mxml_node_t *nanr_node = mxmlFindElement(tree, tree, "nanr", NULL, NULL, MXML_DESCEND);
    if (!nanr_node) { mxmlDelete(tree); FREE(xml); return ERR_INVALID_DATA; }

    const char *anims_attr = mxmlElementGetAttr(nanr_node, "animations");
    const char *frames_attr = mxmlElementGetAttr(nanr_node, "frames");
    if (!anims_attr || !frames_attr) { mxmlDelete(tree); FREE(xml); return ERR_INVALID_DATA; }

    n_anims = strtoul(anims_attr, NULL, 10);
    n_frames = strtoul(frames_attr, NULL, 10);
    if (!n_anims || !n_frames || n_anims > 65535 || n_frames > 65535) {
        mxmlDelete(tree); FREE(xml); return ERR_INVALID_DATA;
    }

    uint *anim_first = CALLOC(n_anims,sizeof(*anim_first));
    uint *anim_count = CALLOC(n_anims,sizeof(*anim_count));
    nanr_xml_frame_t *frames = CALLOC(n_frames,sizeof(*frames));
    if (!anim_first || !anim_count || !frames) err = ERR_CANT_CREATE;
    uint frame_used = 0, data_size = 0;

    mxml_node_t *anim_node = mxmlFindElement(nanr_node, nanr_node, "animation", NULL, NULL, MXML_DESCEND_FIRST);
    for (uint i = 0; !err && i < n_anims; i++)
    {
        if (!anim_node) { err = ERR_INVALID_DATA; break; }
        const char *idx_attr = mxmlElementGetAttr(anim_node, "index");
        const char *cnt_attr = mxmlElementGetAttr(anim_node, "frames");
        if (!idx_attr || !cnt_attr) { err = ERR_INVALID_DATA; break; }

        uint index = strtoul(idx_attr, NULL, 10);
        uint count = strtoul(cnt_attr, NULL, 10);
        if (index != i || !count || count > n_frames-frame_used) { err = ERR_INVALID_DATA; break; }

        anim_first[i] = frame_used; anim_count[i] = count;

        mxml_node_t *frame_node = mxmlFindElement(anim_node, anim_node, "frame", NULL, NULL, MXML_DESCEND_FIRST);
        for (uint j = 0; j < count; j++)
        {
            if (!frame_node) { err = ERR_INVALID_DATA; break; }
            const char *cell_attr = mxmlElementGetAttr(frame_node, "cell");
            const char *dur_attr = mxmlElementGetAttr(frame_node, "duration");
            const char *off_attr = mxmlElementGetAttr(frame_node, "data-offset");
            if (!cell_attr || !dur_attr || !off_attr) { err = ERR_INVALID_DATA; break; }

            uint cell = strtoul(cell_attr, NULL, 10);
            uint duration = strtoul(dur_attr, NULL, 10);
            uint off = strtoul(off_attr, NULL, 16);
            if (cell > 0xffff || duration > 0xffff || off > 0xfffffffdu || off+2 < off) { err = ERR_INVALID_DATA; break; }

            frames[frame_used++] = (nanr_xml_frame_t){ cell, duration, off };
            if (data_size < off+2) data_size = off+2;
            
            frame_node = mxmlFindElement(frame_node, anim_node, "frame", NULL, NULL, MXML_NO_DESCEND);
        }
        anim_node = mxmlFindElement(anim_node, nanr_node, "animation", NULL, NULL, MXML_NO_DESCEND);
    }
    mxmlDelete(tree);
    if (!err && frame_used != n_frames) err = ERR_INVALID_DATA;
    const u64 data_base = 0x20ull + 16ull*n_anims + 8ull*n_frames;
    const u64 chunk64 = (data_base + data_size + 3) & ~3ull;
    if (!err && (chunk64 > UINT_MAX || chunk64+0x10 > UINT_MAX)) err = ERR_FILE_TOO_BIG;
    u8 *out = !err ? CALLOC(1,0x10+(uint)chunk64) : 0;
    if (!err && !out) err = ERR_CANT_CREATE;
    if (!err)
    {
        memcpy(out,"RNAN",4); write_le16(out+4,0xfeff); write_le16(out+6,0x100);
        write_le32(out+8,0x10+(uint)chunk64); write_le16(out+12,0x10); write_le16(out+14,1);
        u8 *knba = out+0x10;
        memcpy(knba,"KNBA",4); write_le32(knba+4,chunk64);
        write_le16(knba+8,n_anims); write_le16(knba+10,n_frames);
        write_le32(knba+12,0x18); write_le32(knba+16,0x18+16*n_anims);
        write_le32(knba+20,0x18+16*n_anims+8*n_frames);
        u8 *anims = knba+0x20, *frame_ptr = anims+16*n_anims, *frame_data = frame_ptr+8*n_frames;
        for (uint i = 0; i < n_anims; i++)
        {
            write_le32(anims+16*i,anim_count[i]); write_le16(anims+16*i+6,1);
            write_le32(anims+16*i+8,1); write_le32(anims+16*i+12,8*anim_first[i]);
        }
        for (uint i = 0; i < n_frames; i++)
        {
            write_le32(frame_ptr+8*i,frames[i].data_off);
            write_le16(frame_ptr+8*i+4,frames[i].duration);
            write_le16(frame_data+frames[i].data_off,frames[i].cell);
        }
        if (verbose >= 0 || testmode)
            fprintf(stdlog,"%sCREATE NANR XML:%s -> %s\n",testmode ? "WOULD " : "",source,dest);
        if (!testmode)
        {
            File_t F;
            CreateFILE(&F,true,dest,testmode,false,true,false,false);
            if (F.f && fwrite(out,1,0x10+(uint)chunk64,F.f) != 0x10+(uint)chunk64)
                err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing NANR failed: %s\n",dest);
            ResetFile(&F,opt_preserve);
        }
    }
    FREE(out); FREE(frames); FREE(anim_count); FREE(anim_first); FREE(xml);
    return err;
}

static enumError encode_byml_file (ccp source, ccp dest)
{
	u8 *text = 0;
	size_t text_len = 0;
	enumError err = LoadFileAlloc (source, 0, 0, &text, &text_len, 16 << 20, 0, 0, false);
	if (err)
		return err;
	u8 *byml = 0;
	uint byml_size = 0;
	ccp ext = strrchr (dest, '.');
	bool is_le = true;
	if (ext && !strcasecmp (ext, ".be"))
		is_le = false;
	err = EncodeBYML_Text (&byml, &byml_size, (const char *)text, (uint)text_len, is_le, 1);
	FREE (text);
	if (err)
		return err;
	if (!testmode)
	{
		File_t F;
		CreateFILE (&F, true, dest, testmode, false, true, false, false);
		if (F.f && fwrite (byml, 1, byml_size, F.f) != byml_size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing BYML failed: %s\n", dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (byml);
	return err;
}

static enumError encode_sequence_file (ccp source, ccp dest)
{
	ccp ext = strrchr (dest, '.');
	seq_format_t fmt = SEQ_FMT_RSEQ;
	if (ext)
	{
		if (!strcasecmp (ext, ".cseq") || !strcasecmp (ext, ".bcseq"))
			fmt = SEQ_FMT_CSEQ;
		else if (!strcasecmp (ext, ".fseq") || !strcasecmp (ext, ".bfseq"))
			fmt = SEQ_FMT_FSEQ_BE;
		else if (!strcasecmp (ext, ".sseq"))
			fmt = SEQ_FMT_SSEQ;
		else if (!strcasecmp (ext, ".bms") || !strcasecmp (ext, ".bmc"))
			fmt = SEQ_FMT_BMS;
	}

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (source, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return err;

	u8 *out = 0;
	size_t out_size = 0;

	ccp src_ext = strrchr (source, '.');
	if (src_ext && !strcasecmp (src_ext, ".mid"))
	{
		err = SequenceFromMIDI (&out, &out_size, raw, raw_size, fmt);
	}
	else
	{
		err = AssembleSequence (&out, &out_size, (const char *)raw, fmt);
	}
	FREE (raw);

	if (!err && out && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (out, 1, out_size, F.f) != out_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", out_size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (out);
	return err;
}

static enumError encode_image_from_png (ccp png_path, ccp dest_path)
{
	Image_t img;
	enumError err = LoadIMG (&img, true, png_path, 0, false, true, false);
	if (err || !img.data)
		return err ? err : ERR_NOTHING_TO_DO;

	ccp dot = strrchr (dest_path, '.');
	if (!dot)
	{
		ResetIMG (&img);
		return ERR_NOTHING_TO_DO;
	}

	// If target exists, inherit its internal pixel format / palette format.
	// Resolve the freshly decoded PNG (still raw IMG_X_* here) to a real,
	// correctly packed format first -- stomping img.iform directly (as this
	// used to do) short-circuits Transform2InternIMG()/ExecTransformIMG(),
	// which key off the raw IMG_X_* tag: with iform already overwritten to a
	// concrete format, they silently no-op and the still-raw pixel buffer
	// gets written out mislabeled as the target format, corrupting the
	// image. Do the real conversion here instead so callers below always
	// receive fully packed pixel data.
	Image_t orig_img;
	if (LoadIMG (&orig_img, true, dest_path, 0, false, true, false) == ERR_OK)
	{
		const image_format_t inherited_iform = orig_img.iform;
		const palette_format_t inherited_pform = orig_img.pform;
		ResetIMG (&orig_img);

		Transform2InternIMG (&img);
		err = ExecTransformIMG (&img);
		if (err)
		{
			ResetIMG (&img);
			return err;
		}

		if (inherited_iform != IMG_INVALID && inherited_iform != img.iform)
			err = ConvertIMG (&img, false, 0, inherited_iform,
				inherited_pform != PAL_INVALID ? inherited_pform : img.pform);
		else if (inherited_pform != PAL_INVALID && inherited_pform != img.pform)
			err = ConvertIMG (&img, false, 0, img.iform, inherited_pform);
		if (err)
		{
			ResetIMG (&img);
			return err;
		}
	}

	if (!strcasecmp (dot, ".tpl"))
	{
		if (img.iform == IMG_INVALID || img.iform == IMG_X_RGB)
			img.iform = IMG_CMPR;
		err = SaveTPL (&img, FF_TPL, 0, dest_path, true);
	}
	else if (!strcasecmp (dot, ".bti"))
	{
		if (img.iform == IMG_INVALID || img.iform == IMG_X_RGB)
			img.iform = IMG_CMPR;
		err = SaveBTI (&img, 0, 0, dest_path, true);
	}
	else if (!strcasecmp (dot, ".tex0") || !strcasecmp (dot, ".tex"))
	{
		if (img.iform == IMG_INVALID || img.iform == IMG_X_RGB)
			img.iform = IMG_CMPR;
		err = SaveTEX (&img, 0, 0, dest_path, true, false);
	}
	else if (!strcasecmp (dot, ".breft"))
	{
		err = SaveBREFTIMG (&img, 0, 0, dest_path, true);
	}
	else if (!strcasecmp (dot, ".bflim") || !strcasecmp (dot, ".bclim"))
	{
		const bool bclim = !strcasecmp (dot, ".bclim");
		Transform2XIMG (&img);
		if (img.iform == IMG_X_RGB)
		{
			const uint rgba_size = img.width * img.height * 4;
			u8 *rgba = MALLOC (rgba_size);
			if (rgba)
			{
				for (uint y = 0; y < img.height; y++)
					memcpy (rgba + 4 * y * img.width, img.data + 4 * y * img.xwidth, 4 * img.width);
				u8 *data = 0;
				uint size = 0;
				err = EncodeFLIM_RGBA (&data, &size, rgba, img.width, img.height, bclim);
				FREE (rgba);
				if (!err && data)
				{
					File_t F;
					err = CreateFileOpt (&F, true, dest_path, false, dest_path);
					if (F.f && fwrite (data, 1, size, F.f) != size)
						err = FILEERROR1 (
							&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest_path);
					ResetFile (&F, opt_preserve);
					FREE (data);
				}
			}
		}
	}
	else if (!strcasecmp (dot, ".ncgr"))
	{
		Transform2XIMG (&img);
		if (img.iform == IMG_X_RGB)
		{
			const uint rgba_size = img.width * img.height * 4;
			u8 *rgba = MALLOC (rgba_size);
			if (rgba)
			{
				for (uint y = 0; y < img.height; y++)
					memcpy (rgba + 4 * y * img.width, img.data + 4 * y * img.xwidth, 4 * img.width);
				u8 *data = 0;
				uint size = 0;
				err = EncodeNCGR_RGBA (&data, &size, rgba, img.width, img.height, true);
				FREE (rgba);
				if (!err && data)
				{
					File_t F;
					err = CreateFileOpt (&F, true, dest_path, false, dest_path);
					if (F.f && fwrite (data, 1, size, F.f) != size)
						err = FILEERROR1 (
							&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest_path);
					ResetFile (&F, opt_preserve);
					FREE (data);
				}
			}
		}
	}
	else if (!strcasecmp (dot, ".nclr"))
	{
		Transform2XIMG (&img);
		if (img.iform == IMG_X_RGB)
		{
			const uint rgba_size = img.width * img.height * 4;
			u8 *rgba = MALLOC (rgba_size);
			if (rgba)
			{
				for (uint y = 0; y < img.height; y++)
					memcpy (rgba + 4 * y * img.width, img.data + 4 * y * img.xwidth, 4 * img.width);
				u8 *data = 0;
				uint size = 0;
				err = EncodeNCLR_RGBA (&data, &size, rgba, img.width, img.height);
				FREE (rgba);
				if (!err && data)
				{
					File_t F;
					err = CreateFileOpt (&F, true, dest_path, false, dest_path);
					if (F.f && fwrite (data, 1, size, F.f) != size)
						err = FILEERROR1 (
							&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest_path);
					ResetFile (&F, opt_preserve);
					FREE (data);
				}
			}
		}
	}
	else
	{
		file_format_t ff = GetImageFFByFName (dest_path, FF_UNKNOWN, false);
		if (ff != FF_UNKNOWN)
			err = SaveIMG (&img, ff, 0, 0, dest_path, true);
		else
			err = ERR_NOTHING_TO_DO;
	}

	ResetIMG (&img);
	return err;
}

// Repack a directory extracted by extract_bigf_file() back into an EA BIGF
// archive.  This is a plain, working reconstruction (member data emitted in
// directory-scan order, packed with no inter-member padding) rather than a
// byte-exact retail reproduction -- no retail .big sample was available to
// derive the original member ordering/alignment rules from, unlike the SZS
// formats above.  extract -> create -> extract round-trips losslessly.
static enumError create_bigf_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;

	uint table_size = 0;
	for (uint i = 0; !err && i < list.used; i++)
		table_size += 8 + strlen (list.entry[i].name) + 1;
	// Retail terminates the variable-length table with a four-byte "L234"
	// marker and pads with zeros up to the (4-byte aligned) data offset.
	const uint header_size = 16;
	const uint data_off = (header_size + table_size + 4 + 3) & ~3u;

	u64 total_size = data_off;
	for (uint i = 0; !err && i < list.used; i++)
		total_size += list.entry[i].size;
	if (!err && total_size > UINT_MAX)
		err = ERR_FILE_TOO_BIG;

	if (!err && !testmode)
	{
		u8 *data = CALLOC (1, total_size);
		if (!data)
			err = ERR_CANT_CREATE;
		else
		{
			memcpy (data, "BIGF", 4);
			wr_le32 (data + 4, (u32)total_size);
			wr_be32 (data + 8, list.used);
			wr_be32 (data + 12, data_off);

			u8 *table = data + header_size;
			u64 member_off = data_off;
			for (uint i = 0; i < list.used; i++)
			{
				const nintendo_sarc_entry_t *e = list.entry + i;
				wr_be32 (table, (u32)member_off);
				wr_be32 (table + 4, e->size);
				table += 8;
				const uint nlen = strlen (e->name) + 1;
				memcpy (table, e->name, nlen);
				table += nlen;
				memcpy (data + member_off, e->data, e->size);
				member_off += e->size;
			}
			memcpy (table, "L234", 4);

			File_t F;
			err = CreateFileOpt (&F, true, dest, false, source);
			if (F.f && fwrite (data, 1, total_size, F.f) != total_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %llu bytes failed: %s\n",
					(unsigned long long)total_size, dest);
			ResetFile (&F, opt_preserve);
			FREE (data);
		}
	}
	reset_sarc_build_list (&list);
	return err;
}

// A bare "file_%04u%n" scan also matches decoded side-products of a member
// that extract_tree_complete() expanded further (e.g. "file_0002.rseq.mid",
// "file_0002.rseq.txt", or a "file_0000.brres.d" preview directory), which
// would otherwise look like duplicate or extra members. Require the name to
// end exactly at one of arcv_member_ext()'s known single extensions.
static bool arcv_member_name_ok (ccp nm, uint *idx)
{
	int consumed = 0;
	if (sscanf (nm, "file_%u%n", idx, &consumed) != 1 || !consumed)
		return false;
	ccp rest = nm + consumed;
	static const char *known_ext[]
		= { ".bin", ".brres", ".rseq", ".rstm", ".rwav", ".rarc", ".pac", 0 };
	for (uint i = 0; known_ext[i]; i++)
		if (!strcmp (rest, known_ext[i]))
			return true;
	return false;
}

// ARCV shares the .arc extension with several other archive formats (RARC,
// Brawl PAC, etc.), so disambiguate at CREATE time the same way looks_like_
// sfzdat_dir()/looks_like_rflres_dir() do for the equally overloaded .dat
// extension: treat a directory as ARCV input once it holds at least one of
// extract_arcv_file()'s unnamed "file_%04u<ext>" members. Anything else in
// the directory is ignored rather than rejected -- extract_tree_complete()
// may have expanded a member further into "file_0000.brres.d/" or emitted
// derived side-products like "file_0002.rseq.mid" alongside the original.
static bool looks_like_arcv_dir (ccp source)
{
	DIR *dir = opendir (source);
	if (!dir)
		return false;
	uint n_members = 0;
	for (const struct dirent *de; (de = readdir (dir));)
	{
		uint idx;
		if (arcv_member_name_ok (de->d_name, &idx))
			n_members++;
	}
	closedir (dir);
	return n_members > 0;
}

// Repack a directory extracted by extract_arcv_file() back into a Namco/Tose
// ARCV archive.  Members carry no on-disk name, only an ordinal encoded into
// the extractor's "file_%04u<ext>" filename, so recover that ordinal from the
// filename instead of relying on directory scan order (which is not
// guaranteed to match numerically for 4+ digit indices on all filesystems).
typedef struct arcv_member_t
{
	uint index;
	u8 *data;
	size_t size;
} arcv_member_t;

static int cmp_arcv_member (const void *a, const void *b)
{
	const arcv_member_t *ma = a, *mb = b;
	return ma->index < mb->index ? -1 : ma->index > mb->index ? 1 : 0;
}

static enumError create_arcv_dir (ccp source, ccp dest)
{
	DIR *dir = opendir (source);
	if (!dir)
		return ERROR0 (ERR_NOT_EXISTS, "Can't open ARCV input directory: %s\n", source);

	arcv_member_t *list = 0;
	uint used = 0, size = 0;
	enumError err = ERR_OK;
	struct dirent *de;
	while (!err && (de = readdir (dir)))
	{
		uint idx;
		if (!arcv_member_name_ok (de->d_name, &idx))
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s", source, de->d_name);
		struct stat st;
		if (stat (path, &st) || !S_ISREG (st.st_mode))
			continue;

		if (used == size)
		{
			const uint nsize = size ? 2 * size : 32;
			void *ptr = REALLOC (list, nsize * sizeof (*list));
			if (!ptr)
			{
				err = ERR_CANT_CREATE;
				break;
			}
			list = ptr;
			size = nsize;
		}
		u8 *data = 0;
		size_t fsize = 0;
		err = LoadFileAlloc (path, 0, 0, &data, &fsize, 0, 0, 0, false);
		if (err)
		{
			ERROR0 (err, "Can't load ARCV input: %s\n", path);
			break;
		}
		list[used].index = idx;
		list[used].data = data;
		list[used].size = fsize;
		used++;
	}
	closedir (dir);

	if (!err && !used)
		err = ERR_NOTHING_TO_DO;
	if (!err)
		qsort (list, used, sizeof (*list), cmp_arcv_member);
	// The extractor names members by ordinal position (0, 1, 2, ...), so a
	// gap or duplicate here means the directory wasn't produced by it.
	for (uint i = 0; !err && i < used; i++)
		if (list[i].index != i)
			err = ERROR0 (ERR_INVALID_DATA,
				"ARCV input directory has a non-contiguous member index: %s/file_%04u*\n",
				source, list[i].index);

	if (!err && !testmode)
	{
		const uint table_end = 12 + used * 12;
		u64 total_size = table_end;
		for (uint i = 0; i < used; i++)
			total_size += list[i].size;
		if (total_size > UINT_MAX)
			err = ERR_FILE_TOO_BIG;
		else
		{
			u8 *out = CALLOC (1, total_size);
			if (!out)
				err = ERR_CANT_CREATE;
			else
			{
				memcpy (out, "ARCV", 4);
				wr_le32 (out + 4, used);
				wr_le32 (out + 8, (u32)total_size);

				u64 off = table_end;
				for (uint i = 0; i < used; i++)
				{
					u8 *entry = out + 12 + i * 12;
					wr_le32 (entry, (u32)off);
					wr_le32 (entry + 4, (u32)list[i].size);
					wr_le32 (entry + 8, (u32)crc32 (0, list[i].data, list[i].size));
					memcpy (out + off, list[i].data, list[i].size);
					off += list[i].size;
				}

				File_t F;
				err = CreateFileOpt (&F, true, dest, false, source);
				if (F.f && fwrite (out, 1, total_size, F.f) != total_size)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED,
						"Writing %llu bytes failed: %s\n", (unsigned long long)total_size, dest);
				ResetFile (&F, opt_preserve);
				FREE (out);
			}
		}
	}

	for (uint i = 0; i < used; i++)
		FREE (list[i].data);
	FREE (list);
	return err;
}

// Repack a directory extracted by extract_jarc_file() back into a Level-5
// jARC archive. Like ARCV, jARC members carry no stored name -- the reader
// names them "file_%04u.<ext>" from an ordinal plus an extension sniffed out
// of the member's own magic -- so recover the ordinal from the filename and
// accept whatever single extension the sniffer chose.
static bool jarc_member_name_ok (ccp nm, uint *idx)
{
	int consumed = 0;
	if (sscanf (nm, "file_%u%n", idx, &consumed) != 1 || !consumed)
		return false;
	ccp rest = nm + consumed;
	// Exactly one extension, and nothing that looks like a derived
	// side-product ("file_0002.rseq.mid") or an expanded member directory.
	return *rest == '.' && rest[1] && !strchr (rest + 1, '.');
}

static enumError create_jarc_dir (ccp source, ccp dest)
{
	DIR *dir = opendir (source);
	if (!dir)
		return ERROR0 (ERR_NOT_EXISTS, "Can't open jARC input directory: %s\n", source);

	arcv_member_t *list = 0;
	uint used = 0, size = 0;
	enumError err = ERR_OK;
	struct dirent *de;
	while (!err && (de = readdir (dir)))
	{
		uint idx;
		if (!jarc_member_name_ok (de->d_name, &idx))
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s", source, de->d_name);
		struct stat st;
		if (stat (path, &st) || !S_ISREG (st.st_mode))
			continue;

		if (used == size)
		{
			const uint nsize = size ? 2 * size : 32;
			void *ptr = REALLOC (list, nsize * sizeof (*list));
			if (!ptr)
			{
				err = ERR_CANT_CREATE;
				break;
			}
			list = ptr;
			size = nsize;
		}
		u8 *data = 0;
		size_t fsize = 0;
		err = LoadFileAlloc (path, 0, 0, &data, &fsize, 0, 0, 0, false);
		if (err)
		{
			ERROR0 (err, "Can't load jARC input: %s\n", path);
			break;
		}
		list[used].index = idx;
		list[used].data = data;
		list[used].size = fsize;
		used++;
	}
	closedir (dir);

	if (!err && !used)
		err = ERR_NOTHING_TO_DO;
	if (!err)
		qsort (list, used, sizeof (*list), cmp_arcv_member);
	for (uint i = 0; !err && i < used; i++)
		if (list[i].index != i)
			err = ERROR0 (ERR_INVALID_DATA,
				"jARC input directory has a non-contiguous member index: %s/file_%04u*\n",
				source, list[i].index);

	if (!err && !testmode)
	{
		const uint data_start = 0x10 + used * 16;
		u64 total_size = data_start;
		for (uint i = 0; i < used; i++)
			total_size += list[i].size;
		if (total_size > UINT_MAX)
			err = ERR_FILE_TOO_BIG;
		else
		{
			u8 *out = CALLOC (1, total_size);
			if (!out)
				err = ERR_CANT_CREATE;
			else
			{
				// Big-endian, the byte order every retail jARC uses.
				memcpy (out, "jARC", 4);
				wr_be32 (out + 4, (u32)total_size);
				wr_be32 (out + 8, used);
				wr_be32 (out + 12, 0x10); // table offset

				u64 off = data_start;
				for (uint i = 0; i < used; i++)
				{
					u8 *entry = out + 0x10 + i * 16;
					wr_be32 (entry, (u32)off);
					wr_be32 (entry + 4, (u32)list[i].size);
					memcpy (out + off, list[i].data, list[i].size);
					off += list[i].size;
				}

				File_t F;
				err = CreateFileOpt (&F, true, dest, false, source);
				if (F.f && fwrite (out, 1, total_size, F.f) != total_size)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED,
						"Writing %llu bytes failed: %s\n", (unsigned long long)total_size, dest);
				ResetFile (&F, opt_preserve);
				FREE (out);
			}
		}
	}

	for (uint i = 0; i < used; i++)
		FREE (list[i].data);
	FREE (list);
	return err;
}

// Repack a directory extracted by extract_gpak_file() back into a nameless
// .pak. Members are ordinal-named, exactly like ARCV and jARC.
static enumError create_gpak_dir (ccp source, ccp dest)
{
	DIR *dir = opendir (source);
	if (!dir)
		return ERROR0 (ERR_NOT_EXISTS, "Can't open GPAK input directory: %s\n", source);

	arcv_member_t *list = 0;
	uint used = 0, size = 0;
	enumError err = ERR_OK;
	struct dirent *de;
	while (!err && (de = readdir (dir)))
	{
		uint idx;
		if (!jarc_member_name_ok (de->d_name, &idx))
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s", source, de->d_name);
		struct stat st;
		if (stat (path, &st) || !S_ISREG (st.st_mode))
			continue;

		if (used == size)
		{
			const uint nsize = size ? 2 * size : 32;
			void *ptr = REALLOC (list, nsize * sizeof (*list));
			if (!ptr)
			{
				err = ERR_CANT_CREATE;
				break;
			}
			list = ptr;
			size = nsize;
		}
		u8 *data = 0;
		size_t fsize = 0;
		err = LoadFileAlloc (path, 0, 0, &data, &fsize, 0, 0, 0, false);
		if (err)
		{
			ERROR0 (err, "Can't load GPAK input: %s\n", path);
			break;
		}
		list[used].index = idx;
		list[used].data = data;
		list[used].size = fsize;
		used++;
	}
	closedir (dir);

	if (!err && !used)
		err = ERR_NOTHING_TO_DO;
	if (!err)
		qsort (list, used, sizeof (*list), cmp_arcv_member);
	for (uint i = 0; !err && i < used; i++)
		if (list[i].index != i)
			err = ERROR0 (ERR_INVALID_DATA,
				"GPAK input directory has a non-contiguous member index: %s/file_%04u*\n",
				source, list[i].index);

	if (!err && !testmode)
	{
		nintendo_sarc_entry_t *ent = CALLOC (used, sizeof (*ent));
		if (!ent)
			err = ERR_CANT_CREATE;
		else
		{
			for (uint i = 0; i < used; i++)
			{
				ent[i].data = list[i].data;
				ent[i].size = (uint)list[i].size;
			}
			u8 *out = 0;
			uint out_size = 0;
			err = CreateGPAK (&out, &out_size, ent, used);
			if (!err)
			{
				File_t F;
				err = CreateFileOpt (&F, true, dest, false, source);
				if (F.f && fwrite (out, 1, out_size, F.f) != out_size)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED,
						"Writing %u bytes failed: %s\n", out_size, dest);
				ResetFile (&F, opt_preserve);
			}
			FREE (out);
			FREE (ent);
		}
	}

	for (uint i = 0; i < used; i++)
		FREE (list[i].data);
	FREE (list);
	return err;
}

// Repack a directory extracted by extract_namco_vcra_file() into a Namco
// Museum Remix VCRA archive.
//
// VCRA has two entry layouts, selected by whether the word at 0x0C is
// non-zero: a 44-byte entry carrying a CRC, or a 64-byte entry without one
// whose table starts at 0x40. The writer emits the second, so the word at
// 0x0C stays zero and no CRC has to be invented for members the reader
// never checked one against.
static enumError create_vcra_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;

	if (!err)
		for (uint i = 0; i < list.used; i++)
		{
			ccp name = list.entry[i].name ? list.entry[i].name : "";
			if (strlen (name) > 56)
			{
				err = ERROR0 (ERR_INVALID_DATA,
					"VCRA member name exceeds the format's 56-byte field: %s\n", name);
				break;
			}
		}

	u8 *out = 0;
	uint total = 0;
	if (!err)
	{
		const uint table = 64, entry_size = 64;
		uint off = table + list.used * entry_size;
		off = (off + 31) & ~31u;
		total = off;
		for (uint i = 0; i < list.used; i++)
			total = (total + list.entry[i].size + 31) & ~31u;

		out = CALLOC (1, total);
		if (!out)
			err = ERR_CANT_CREATE;
		else
		{
			memcpy (out, "VCRA", 4);
			wr_le32 (out + 4, list.used);
			wr_le32 (out + 8, total);

			for (uint i = 0; i < list.used; i++)
			{
				u8 *entry = out + table + i * entry_size;
				wr_le32 (entry, off);
				wr_le32 (entry + 4, list.entry[i].size);
				strncpy ((char *)entry + 8, list.entry[i].name ? list.entry[i].name : "", 56);
				if (list.entry[i].data && list.entry[i].size)
					memcpy (out + off, list.entry[i].data, list.entry[i].size);
				off = (off + list.entry[i].size + 31) & ~31u;
			}
		}
	}

	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (out, 1, total, F.f) != total)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", total, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (out);
	reset_sarc_build_list (&list);
	return err;
}

static bool looks_like_f9res_dir (ccp source)
{
	DIR *dir = opendir (source);
	if (!dir)
		return false;
	struct dirent *ent;
	bool found = false;
	while ((ent = readdir (dir)) != 0)
	{
		if (ent->d_name[0] == '.')
			continue;
		if (strlen (ent->d_name) >= 9 && ent->d_name[4] == '_')
		{
			found = true;
			break;
		}
	}
	closedir (dir);
	return found;
}

static enumError create_archive_from_dir (ccp source_dir, ccp dest)
{
	ccp ext = strrchr (dest, '.');
	if (!ext)
		ext = "";

	if (!strcasecmp (ext, ".sdat"))
		return PassthruPack (source_dir, dest);

	// Audio and video formats with extracted preview trees must NOT be repacked into U8 archives
	if (!strcasecmp (ext, ".brsar") || !strcasecmp (ext, ".sdat") || !strcasecmp (ext, ".thp")
		|| !strcasecmp (ext, ".moflex") || !strcasecmp (ext, ".mo") || !strcasecmp (ext, ".brstm")
		|| !strcasecmp (ext, ".bcstm") || !strcasecmp (ext, ".bfstm") || !strcasecmp (ext, ".bns")
		|| !strcasecmp (ext, ".ast") || !strcasecmp (ext, ".dsp"))
	{
		return ERR_NOTHING_TO_DO;
	}

	enumError pas_err = PassthruPack (source_dir, dest);
	if (pas_err != ERR_NOTHING_TO_DO)
		return pas_err;

	const bool sarc_le = (!strcasecmp (ext, ".sarcle") || !strcasecmp (ext, ".le")
		|| (strlen (dest) >= 8 && !strcasecmp (ext - 5, ".sarc.le")));

	const bool is_sarc = !strcasecmp (ext, ".sarc") || !strcasecmp (ext, ".bfma")
		|| (strlen (dest) >= 10 && !strcasecmp (dest + strlen (dest) - 10, ".sarc.fzip"))
		|| sarc_le;
	if (is_sarc)
		return create_sarc_dir (source_dir, dest, !sarc_le);
	if (!strcasecmp (ext, ".narc"))
		return create_narc_dir (source_dir, dest, true);
	if (!strcasecmp (ext, ".darc"))
		return create_darc_dir (source_dir, dest);
	if (!strcasecmp (ext, ".pac") || !strcasecmp (ext, ".pcs"))
		return create_pac_dir (source_dir, dest);
	if (!strcasecmp (ext, ".warc")
		|| (strlen (dest) >= 10 && !strcasecmp (dest + strlen (dest) - 10, ".warc.fzip")))
		return create_warc_dir (source_dir, dest);
	if (!strcasecmp (ext, ".bg4"))
		return create_bg4_dir (source_dir, dest);
	if (!strcasecmp (ext, ".sa01"))
		return create_sa01_dir (source_dir, dest);
	if (!strcasecmp (ext, ".ca01"))
		return create_ca01_dir (source_dir, dest);
	if (!strcasecmp (ext, ".cram"))
		return create_cram_dir (source_dir, dest);
	if (!strcasecmp (ext, ".fsys"))
		return create_fsys_dir (source_dir, dest);
	if (!strcasecmp (ext, ".xc") || !strcasecmp (ext, ".xpck"))
		return create_xpck_dir (source_dir, dest);
	if (!strcasecmp (ext, ".ztab") || !strcasecmp (ext, ".tab"))
		return create_ztab_dir (source_dir, dest);
	if (!strcasecmp (ext, ".mdr"))
		return create_mdr_dir (source_dir, dest);
	if (!strcasecmp (ext, ".pvol"))
		return create_pvol_dir (source_dir, dest);
	if (!strcasecmp (ext, ".stpk") || !strcasecmp (ext, ".srd"))
		return create_stpk_dir (source_dir, dest);
	if (!strcasecmp (ext, ".res") && looks_like_f9res_dir (source_dir))
		return create_f9res_dir (source_dir, dest);
	if (!strcasecmp (ext, ".zlarc"))
		return create_zlarc_dir (source_dir, dest);
	if (!strcasecmp (ext, ".jarc"))
		return create_jarc_dir (source_dir, dest);
	if (!strcasecmp (ext, ".gpak"))
		return create_gpak_dir (source_dir, dest);
	if (!strcasecmp (ext, ".vcra"))
		return create_vcra_dir (source_dir, dest);
	if (!strcasecmp (ext, ".apak"))
		return create_apak_dir (source_dir, dest);
	if (!strcasecmp (ext, ".nxarc"))
		return create_nxarc_dir (source_dir, dest);
	if (!strcasecmp (ext, ".pkz"))
		return create_pkz_dir (source_dir, dest);
	if (!strcasecmp (ext, ".tmpk") || !strcasecmp (ext, ".pack"))
		return create_tmpk_dir (source_dir, dest);
	if (!strcasecmp (ext, ".vibs"))
		return create_vibs_dir (source_dir, dest);
	if (!strcasecmp (ext, ".mtxt"))
		return create_mtxt_dir (source_dir, dest);
	if (!strcasecmp (ext, ".mkgpdx"))
		return create_mkgpdx_dir (source_dir, dest);
	if (!strcasecmp (ext, ".pkg") || !strcasecmp (ext, ".gpkg"))
		return create_gpkg_dir (source_dir, dest);
	if (!strcasecmp (ext, ".gsh"))
		return create_gsh_dir (source_dir, dest);
	if (!strcasecmp (ext, ".sze"))
		return create_sze_dir (source_dir, dest);
	if (!strcasecmp (ext, ".dat") && looks_like_sfzdat_dir (source_dir))
		return create_sfzdat_dir (source_dir, dest);
	if ((!strcasecmp (ext, ".dat") || !strcasecmp (ext, ".rflres") || !strcasecmp (ext, ".rfl")
		|| !strcasecmp (ext, ".fflres") || !strcasecmp (ext, ".ffl")
		|| !strcasecmp (ext, ".cflres") || !strcasecmp (ext, ".cfl")
		|| !strcasecmp (ext, ".aflres") || !strcasecmp (ext, ".afl")
		|| !strcasecmp (ext, ".nflres") || !strcasecmp (ext, ".nfl") || !strcasecmp (ext, ".nfl_res")) && looks_like_rflres_dir (source_dir))
	{
		const bool be = strcasecmp (ext, ".cfl") && strcasecmp (ext, ".cflres")
			&& strcasecmp (ext, ".afl") && strcasecmp (ext, ".aflres")
			&& strcasecmp (ext, ".nfl") && strcasecmp (ext, ".nflres") && strcasecmp (ext, ".nfl_res");
		return create_rflres_dir (source_dir, dest, be);
	}
	if (!strcasecmp (ext, ".gfa"))
		return create_gfa_dir (source_dir, dest);
	if (!strcasecmp (ext, ".rarc"))
		return create_rarc_dir (source_dir, dest);
	if (!strcasecmp (ext, ".bcsar") || !strcasecmp (ext, ".csar"))
		return create_sar_dir (source_dir, dest, "CSAR");
	if (!strcasecmp (ext, ".bfsar") || !strcasecmp (ext, ".fsar"))
		return create_sar_dir (source_dir, dest, "FSAR");
	if (!strcasecmp (ext, ".bcwar") || !strcasecmp (ext, ".cwar"))
		return create_sar_dir (source_dir, dest, "CWAR");
	if (!strcasecmp (ext, ".bfwar") || !strcasecmp (ext, ".fwar"))
		return create_sar_dir (source_dir, dest, "FWAR");
	if (!strcasecmp (ext, ".bcgrp") || !strcasecmp (ext, ".cgrp"))
		return create_sar_dir (source_dir, dest, "CGRP");
	if (!strcasecmp (ext, ".bfgrp") || !strcasecmp (ext, ".fgrp"))
		return create_sar_dir (source_dir, dest, "FGRP");
	if (!strcasecmp (ext, ".car") || !strcasecmp (ext, ".res") || !strcasecmp (ext, ".trk")
		|| !strcasecmp (ext, ".lvl") || !strcasecmp (ext, ".rst"))
		return create_rst_dir (source_dir, dest);
	if (!strcasecmp (ext, ".ctpk"))
		return create_ctpk_dir (source_dir, dest);
	if (!strcasecmp (ext, ".ccf"))
		return create_ccf_dir (source_dir, dest);
	if (!strcasecmp (ext, ".nccarc"))
		return create_nccarc_dir (source_dir, dest);
	if (!strcasecmp (ext, ".at7") || !strcasecmp (ext, ".at7p") || !strcasecmp (ext, ".at7x"))
		return create_at7_dir (source_dir, dest);
	if (!strcasecmp (ext, ".idx"))
		return create_hwl_dir (source_dir, dest);
	if (!strcasecmp (ext, ".bin"))
		return create_mpbin_dir (source_dir, dest);
	if (!strcasecmp (ext, ".big"))
		return create_bigf_dir (source_dir, dest);
	if (!strcasecmp (ext, ".arc") && looks_like_arcv_dir (source_dir))
		return create_arcv_dir (source_dir, dest);

	// Default: U8/Yaz0/BRRES/ARC archive via CreateSZS
	SetupParam_t sp;
	InitializeSetupParam (&sp);
	ScanSetupParam (&sp, true, source_dir, SZS_SETUP_FILE, 0, true);

	szs_file_t szs;
	InitializeSZS (&szs);
	enumError err
		= CreateSZS (&szs, dest, source_dir, 0, &sp, 0, verbose > 0 ? UINT_MAX : 0, false);
	if (err <= ERR_WARNING && !szs.unchanged && err != ERR_NOT_EXISTS)
	{
		File_t F;
		CreateFILE (&F, true, dest, testmode, false, true, false, false);
		if (F.f)
		{
			SetFileAttrib (&F.fatt, &szs.fatt, 0);
			const u8 *data = szs.cdata ? szs.cdata : szs.data;
			const size_t size = szs.cdata ? szs.csize : szs.size;
			const size_t wstat = fwrite (data, 1, size, F.f);
			if (wstat != size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", size, dest);
		}
		ResetFile (&F, opt_preserve);
		LinkCacheSZS (&szs, dest);
	}
	ResetSZS (&szs);
	ResetSetupParam (&sp);
	return err;
}

static const char *cand_archive_exts[] = { ".wbfs", ".iso", ".ciso", ".wdf", ".wia", ".gcz", ".gcm",
	".gca", ".nds", ".srl", ".dsi", ".3ds", ".cia", ".cxi", ".ncch", ".app", ".wad", ".wud", ".wux",
	".rpx", ".rpl", ".nsp", ".xci", ".nca", ".szs", ".carc", ".arc", ".brres", ".sarc", ".narc",
	".darc", ".pac", ".pcs", ".gfa", ".rarc", ".warc", ".bcsar", ".bfsar", ".bcwar", ".bfwar",
	".bcgrp", ".bfgrp", ".rst", ".car", ".res", ".trk", ".lvl", ".wu8", ".wbz", ".wlz", ".bntx",
	".bcres", ".bfres", ".bch", ".big", ".xc", ".xpck", ".ztab", ".tab", ".mdr", ".pvol", ".srd",
	".stpk", ".zlarc", 0 };

static bool is_archive_dir_name (ccp dir, size_t len)
{
	struct stat st;
	// Check for FST / NDS / 3DS / WiiU / Switch / WAD structural markers inside dir
	char check_path[PATH_MAX];
	snprintf (check_path, sizeof (check_path), "%s/setup.txt", dir);
	if (stat (check_path, &st) == 0)
		return true;
	snprintf (check_path, sizeof (check_path), "%s/DATA/sys/main.dol", dir);
	if (stat (check_path, &st) == 0)
		return true;
	snprintf (check_path, sizeof (check_path), "%s/sys/main.dol", dir);
	if (stat (check_path, &st) == 0)
		return true;
	snprintf (check_path, sizeof (check_path), "%s/arm9.bin", dir);
	if (stat (check_path, &st) == 0)
		return true;
	snprintf (check_path, sizeof (check_path), "%s/romfs", dir);
	if (stat (check_path, &st) == 0 && S_ISDIR (st.st_mode))
		return true;
	snprintf (check_path, sizeof (check_path), "%s/exefs", dir);
	if (stat (check_path, &st) == 0 && S_ISDIR (st.st_mode))
		return true;
	snprintf (check_path, sizeof (check_path), "%s/code", dir);
	if (stat (check_path, &st) == 0 && S_ISDIR (st.st_mode))
		return true;
	// hacbrewpack's Switch NSP repack branch requires control/control.nacp;
	// that's what the Control NCA's second extraction pass under
	// passthru_archive() actually produces -- a plain "content" dir (the
	// previous check here) is never created by any extraction path.
	snprintf (check_path, sizeof (check_path), "%s/control/control.nacp", dir);
	if (stat (check_path, &st) == 0 && S_ISREG (st.st_mode))
		return true;
	snprintf (check_path, sizeof (check_path), "%s/00000000.app", dir);
	if (stat (check_path, &st) == 0)
		return true;
	// wit's XEXTRACT (x-wad.c) layout: cert.bin/tik.bin/tmd.bin plus each
	// decrypted content as "%08x.app" -- content IDs rarely start at 0, so
	// the 00000000.app check above alone misses most real WADs.
	snprintf (check_path, sizeof (check_path), "%s/tmd.bin", dir);
	if (stat (check_path, &st) == 0)
		return true;
	snprintf (check_path, sizeof (check_path), "%s/cert.bin", dir);
	if (stat (check_path, &st) == 0)
		return true;

	if (len < 4 || strcasecmp (dir + len - 2, ".d") != 0)
		return false;
	ccp last_slash = strrchr (dir, '/');
	ccp fname = last_slash ? last_slash + 1 : dir;
	size_t flen = strlen (fname);
	if (flen < 4 || strcasecmp (fname + flen - 2, ".d") != 0)
		return false;

	// Check if wszst-setup.txt exists inside the .d directory
	char setup_path[PATH_MAX];
	snprintf (setup_path, sizeof (setup_path), "%s/%s", dir, SZS_SETUP_FILE);
	if (stat (setup_path, &st) == 0 && S_ISREG (st.st_mode))
		return true;

	char base[PATH_MAX];
	snprintf (base, sizeof (base), "%.*s", (int)(flen - 2), fname);
	ccp ext = strrchr (base, '.');
	if (ext)
	{
		for (int c = 0; cand_archive_exts[c]; c++)
		{
			if (!strcasecmp (ext, cand_archive_exts[c]))
				return true;
		}
	}

	// Check if there is a sibling container file next to this .d directory
	char parent[PATH_MAX];
	if (last_slash)
		snprintf (parent, sizeof (parent), "%.*s", (int)(last_slash - dir), dir);
	else
		snprintf (parent, sizeof (parent), ".");

	for (int c = 0; cand_archive_exts[c]; c++)
	{
		char cand[PATH_MAX];
		snprintf (cand, sizeof (cand), "%s/%s%s", parent, base, cand_archive_exts[c]);
		if (stat (cand, &st) == 0 && S_ISREG (st.st_mode))
			return true;
	}

	return false;
}

static void cleanup_extracted_artifacts (ccp root, bool is_root)
{
	DIR *dir = opendir (root);
	if (!dir)
		return;
	struct dirent *de;
	while ((de = readdir (dir)))
	{
		if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, ".."))
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s", root, de->d_name);
		struct stat st;
		if (lstat (path, &st))
			continue;
		const size_t nlen = strlen (de->d_name);

		if (S_ISDIR (st.st_mode))
		{
			if (nlen > 2 && !strcasecmp (de->d_name + nlen - 2, ".d"))
			{
				remove_dir_recursive (path);
			}
			else
			{
				cleanup_extracted_artifacts (path, false);
			}
		}
		else if (S_ISREG (st.st_mode))
		{
			if (!strcmp (de->d_name, "wszst-setup.txt"))
			{
				// The root directory's own setup file is still needed by the
				// caller (cmd_create's ScanSetupParam, right after this call)
				// to recover the original archive/compression format -- e.g.
				// "file-format = U8" for an uncompressed source .arc. Deleting
				// it here defaulted every repack of that root to YAZ0-compressed
				// output regardless of what the source actually was. Nested
				// .d dirs' own setup files were already consumed by
				// repack_tree_bottom_up(), so those are still safe to remove.
				if (!is_root)
					unlink (path);
				continue;
			}
			// Companion model files (.glb, .dae)
			if (nlen > 4
				&& (!strcasecmp (de->d_name + nlen - 4, ".glb")
					|| !strcasecmp (de->d_name + nlen - 4, ".dae")))
			{
				static const char *m_exts[] = { ".brres", ".bmd", ".bch", ".bcres", ".bfres",
					".mdl0", ".hsf", ".msh", ".mod", 0 };
				const size_t stem_len = strlen (path) - 4;
				bool did_unlink = false;
				for (int k = 0; m_exts[k]; k++)
				{
					char cand[PATH_MAX];
					snprintf (cand, sizeof (cand), "%.*s%s", (int)stem_len, path, m_exts[k]);
					struct stat st_m;
					if (stat (cand, &st_m) == 0 && S_ISREG (st_m.st_mode))
					{
						unlink (path);
						did_unlink = true;
						break;
					}
				}
				// KCL exports append rather than replace the extension
				// (e.g. "foo.kcl" -> "foo.kcl.glb", like "foo.kcl.obj"), so the
				// stem itself -- not stem+".kcl" -- is the candidate source.
				if (!did_unlink && stem_len > 4
					&& !strcasecmp (path + stem_len - 4, ".kcl"))
				{
					char cand[PATH_MAX];
					snprintf (cand, sizeof (cand), "%.*s", (int)stem_len, path);
					struct stat st_m;
					if (stat (cand, &st_m) == 0 && S_ISREG (st_m.st_mode))
						unlink (path);
				}
			}
			// Companion XML files
			if (nlen > 9
				&& (!strcasecmp (de->d_name + nlen - 9, ".ncer.xml")
					|| !strcasecmp (de->d_name + nlen - 9, ".nanr.xml")))
			{
				char cand[PATH_MAX];
				snprintf (cand, sizeof (cand), "%.*s", (int)(strlen (path) - 4), path);
				struct stat st_c;
				if (stat (cand, &st_c) == 0 && S_ISREG (st_c.st_mode))
					unlink (path);
			}
			// BFLYT/BCLYT/BRLYT/BRLAN (layout/animation) decode to *.tflyt,
			// not *.xml -- see decode_bflyt_if_possible()/cmd_convert().
			if (nlen > 6 && !strcasecmp (de->d_name + nlen - 6, ".tflyt"))
			{
				char cand[PATH_MAX];
				snprintf (cand, sizeof (cand), "%.*s", (int)(strlen (path) - 6), path);
				struct stat st_c;
				if (stat (cand, &st_c) == 0 && S_ISREG (st_c.st_mode))
					unlink (path);
			}
			// Companion YAML files
			if (nlen > 10 && !strcasecmp (de->d_name + nlen - 10, ".byml.yaml"))
			{
				char cand[PATH_MAX];
				snprintf (cand, sizeof (cand), "%.*s", (int)(strlen (path) - 5), path);
				struct stat st_c;
				if (stat (cand, &st_c) == 0 && S_ISREG (st_c.st_mode))
					unlink (path);
			}
			// Companion TXT files
			if (nlen > 4 && !strcasecmp (de->d_name + nlen - 4, ".txt")
				&& strcmp (de->d_name, "setup.txt") != 0)
			{
				char cand[PATH_MAX];
				snprintf (cand, sizeof (cand), "%.*s", (int)(strlen (path) - 4), path);
				struct stat st_c;
				if (stat (cand, &st_c) == 0 && S_ISREG (st_c.st_mode))
					unlink (path);
			}
			// Companion PNG files that have corresponding native image files
			if (nlen > 4 && !strcasecmp (de->d_name + nlen - 4, ".png"))
			{
				char stem[PATH_MAX];
				snprintf (stem, sizeof (stem), "%.*s", (int)(strlen (path) - 4), path);
				ccp stem_ext = strrchr (stem, '.');
				bool has_target = false;
				if (stem_ext
					&& (!strcasecmp (stem_ext, ".tpl") || !strcasecmp (stem_ext, ".tex0")
						|| !strcasecmp (stem_ext, ".tex") || !strcasecmp (stem_ext, ".bti")
						|| !strcasecmp (stem_ext, ".bflim") || !strcasecmp (stem_ext, ".bclim")
						|| !strcasecmp (stem_ext, ".ncgr") || !strcasecmp (stem_ext, ".nclr")
						|| !strcasecmp (stem_ext, ".bntx") || !strcasecmp (stem_ext, ".dsb")
						|| !strcasecmp (stem_ext, ".plt0") || !strcasecmp (stem_ext, ".breft")))
				{
					struct stat st_t;
					if (stat (stem, &st_t) == 0 && S_ISREG (st_t.st_mode))
						has_target = true;
				}
				if (!has_target)
				{
					static const char *cand_img_exts[]
						= { ".tpl", ".tex0", ".bti", ".bflim", ".bclim", ".ncgr", ".nclr", ".bntx",
							  ".dsb", ".plt0", ".breft", ".tex", ".art", ".img", 0 };
					for (int k = 0; cand_img_exts[k]; k++)
					{
						char cand[PATH_MAX];
						snprintf (cand, sizeof (cand), "%s%s", stem, cand_img_exts[k]);
						struct stat st_t;
						if (stat (cand, &st_t) == 0 && S_ISREG (st_t.st_mode))
						{
							has_target = true;
							break;
						}
					}
				}
				if (has_target)
					unlink (path);
			}
		}
	}
	closedir (dir);
}

static enumError repack_tree_bottom_up (ccp root, uint depth)
{
	if (depth > 32)
		return ERR_FILE_TOO_BIG;
	DIR *dir = opendir (root);
	if (!dir)
		return ERR_NOT_EXISTS;

	enumError max_err = ERR_OK;
	struct dirent *de;

	// 1. Recurse into subdirectories first (depth-first / bottom-up)
	while ((de = readdir (dir)))
	{
		if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, ".."))
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s", root, de->d_name);
		struct stat st;
		if (lstat (path, &st))
			continue;
		if (S_ISDIR (st.st_mode))
		{
			enumError err = repack_tree_bottom_up (path, depth + 1);
			if (max_err < err)
				max_err = err;
		}
	}
	rewinddir (dir);

	// 2. Process modified files & .d directories in this directory
	while ((de = readdir (dir)))
	{
		if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, ".."))
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s", root, de->d_name);
		struct stat st;
		if (lstat (path, &st))
			continue;
		const size_t nlen = strlen (de->d_name);

		// Check for .png files with corresponding native image files
		if (S_ISREG (st.st_mode) && nlen > 4 && !strcasecmp (de->d_name + nlen - 4, ".png"))
		{
			char target_img[PATH_MAX] = { 0 };
			struct stat st_target;
			bool found_target = false;

			// 1. Check direct prefix, e.g. "foo.tpl.png" -> "foo.tpl", "bar.tex0.png" -> "bar.tex0"
			char stem[PATH_MAX];
			snprintf (stem, sizeof (stem), "%.*s", (int)(strlen (path) - 4), path);
			ccp stem_ext = strrchr (stem, '.');
			if (stem_ext
				&& (!strcasecmp (stem_ext, ".tpl") || !strcasecmp (stem_ext, ".tex0")
					|| !strcasecmp (stem_ext, ".tex") || !strcasecmp (stem_ext, ".bti")
					|| !strcasecmp (stem_ext, ".bflim") || !strcasecmp (stem_ext, ".bclim")
					|| !strcasecmp (stem_ext, ".ncgr") || !strcasecmp (stem_ext, ".nclr")
					|| !strcasecmp (stem_ext, ".bntx") || !strcasecmp (stem_ext, ".dsb")
					|| !strcasecmp (stem_ext, ".plt0") || !strcasecmp (stem_ext, ".breft")))
			{
				if (stat (stem, &st_target) == 0 && S_ISREG (st_target.st_mode))
				{
					snprintf (target_img, sizeof (target_img), "%s", stem);
					found_target = true;
				}
			}

			// 2. Check candidate extensions, e.g. "foo.png" -> "foo.tpl", "foo.tex0", "foo.bti",
			// etc.
			if (!found_target)
			{
				static const char *cand_img_exts[] = { ".tpl", ".tex0", ".bti", ".bflim", ".bclim",
					".ncgr", ".nclr", ".bntx", ".dsb", ".plt0", ".breft", 0 };
				for (int k = 0; cand_img_exts[k]; k++)
				{
					char cand[PATH_MAX];
					snprintf (cand, sizeof (cand), "%s%s", stem, cand_img_exts[k]);
					if (stat (cand, &st_target) == 0 && S_ISREG (st_target.st_mode))
					{
						snprintf (target_img, sizeof (target_img), "%s", cand);
						found_target = true;
						break;
					}
				}
			}

			if (found_target)
			{
				time_t ref_img_mtime = st_target.st_mtime;
				struct stat st_s;
				char s_path[PATH_MAX];
				snprintf (s_path, sizeof (s_path), "%s/%s", root, SZS_SETUP_FILE);
				if (stat (s_path, &st_s) == 0 && st_s.st_mtime > ref_img_mtime)
					ref_img_mtime = st_s.st_mtime;

				// Only re-encode if the PNG was actually modified by the user
				if (st.st_mtime > ref_img_mtime + 2)
				{
					enumError err = encode_image_from_png (path, target_img);
					if (err <= ERR_WARNING)
					{
						if (verbose >= 0)
							fprintf (stdlog, "REPACK IMAGE %s -> %s\n", path, target_img);
					}
					else if (max_err < err)
						max_err = err;
				}
				unlink (path);
			}
		}

		// Check for .glb or .dae files with sibling models
		const bool is_dae_file = nlen > 4 && !strcasecmp (de->d_name + nlen - 4, ".dae");
		const bool is_glb_file = nlen > 4 && !strcasecmp (de->d_name + nlen - 4, ".glb");
		if (S_ISREG (st.st_mode) && (is_dae_file || is_glb_file))
		{
			bool handled = false;
			bool found_sibling = false; // a recognised parent model exists, even if left untouched
										// (unmodified preview)
			static const char *model_exts[] = { ".brres", ".bmd", ".bch", ".bcres", ".bfres",
				".mdl0", ".hsf", ".msh", ".mod", 0 };

			// wmdlt DECODE names a multi-model archive's per-model exports
			// "<archive-stem>.<modelname>.glb" (see wmdlt.c: "%s.%s.glb"). A plain
			// "<glb-stem><native-ext>" lookup then never matches the single shared
			// native sibling file (e.g. "foo.brres"), so every model past the first
			// looked unowned and fell through to the "create a new Switch BFRES"
			// fallback below -- silently turning BRRES models into bogus BFRES files.
			// Try the direct stem first (unchanged single-model behaviour), and only
			// if that finds nothing, retry with the ".<modelname>" suffix stripped.
			char glb_stem[PATH_MAX];
			snprintf (glb_stem, sizeof (glb_stem), "%.*s", (int)(strlen (path) - 4), path);
			const char *model_bases[3] = { glb_stem, 0, 0 };
			char glb_multi_stem[PATH_MAX];
			ccp glb_last_dot = strrchr (glb_stem, '.');
			if (glb_last_dot && glb_last_dot != glb_stem)
			{
				snprintf (glb_multi_stem, sizeof (glb_multi_stem), "%.*s",
					(int)(glb_last_dot - glb_stem), glb_stem);
				model_bases[1] = glb_multi_stem;
			}

			for (int b = 0; model_bases[b] && !handled && !found_sibling; b++)
			for (int k = 0; model_exts[k]; k++)
			{
				char parent_model[PATH_MAX];
				snprintf (parent_model, sizeof (parent_model), "%s%s", model_bases[b],
					model_exts[k]);
				struct stat st_m;
				if (!stat (parent_model, &st_m) && S_ISREG (st_m.st_mode))
				{
					found_sibling = true;
					time_t ref_m_mtime = st_m.st_mtime;
					struct stat st_s;
					char s_path[PATH_MAX];
					snprintf (s_path, sizeof (s_path), "%s/%s", root, SZS_SETUP_FILE);
					if (stat (s_path, &st_s) == 0 && st_s.st_mtime > ref_m_mtime)
						ref_m_mtime = st_s.st_mtime;

					if (st.st_mtime > ref_m_mtime + 2)
					{
						if (!strcasecmp (model_exts[k], ".hsf"))
						{
							model_t *mdl = ParseGLBFile (path);
							if (mdl)
							{
								enumError enc = EncodeModelToHSF (mdl, parent_model);
								if (enc <= ERR_WARNING && verbose >= 0)
									fprintf (stdlog, "REPACK HSF %s -> %s\n", path, parent_model);
								else if (max_err < enc)
									max_err = enc;
								FreeModel (mdl);
							}
							unlink (path);
							handled = true;
							break;
						}
						if (!strcasecmp (model_exts[k], ".msh"))
						{
							model_t *mdl = ParseGLBFile (path);
							if (mdl)
							{
								enumError enc = EncodeExciteMSH (mdl, parent_model);
								if (enc <= ERR_WARNING && verbose >= 0)
									fprintf (stdlog, "REPACK MSH %s -> %s\n", path, parent_model);
								else if (max_err < enc)
									max_err = enc;
								FreeModel (mdl);
							}
							unlink (path);
							handled = true;
							break;
						}
						if (!strcasecmp (model_exts[k], ".mod"))
						{
							model_t *mdl = ParseGLBFile (path);
							if (mdl)
							{
								enumError enc = EncodeExciteMOD (mdl, parent_model);
								if (enc <= ERR_WARNING && verbose >= 0)
									fprintf (stdlog, "REPACK MOD %s -> %s\n", path, parent_model);
								else if (max_err < enc)
									max_err = enc;
								FreeModel (mdl);
							}
							unlink (path);
							handled = true;
							break;
						}
						raw_data_t parent_raw;
						InitializeRawData (&parent_raw);
						if (LoadRawData (&parent_raw, false, parent_model, 0, false, 0) == ERR_OK)
						{
							model_t *mdl = ParseGLBFile (path);
							if (mdl)
							{
								uint8_t *injected = 0;
								size_t inj_size = 0;
								if (InjectDAEIntoModel (parent_raw.data, parent_raw.data_size, mdl,
										&injected, &inj_size)
									&& injected)
								{
									SaveFILE (parent_model, 0, true, injected, (uint)inj_size, 0);
									FREE (injected);
									unlink (path);
									handled = true;
									if (verbose >= 0)
										fprintf (
											stdlog, "REPACK INJECT %s -> %s\n", path, parent_model);
								}
								FreeModel (mdl);
							}
							ResetRawData (&parent_raw);
						}
					}
					if (handled)
						break;
				}
			}

			// Fallback: no sibling model file found; create a new Switch BFRES.
			// Only do this in a directory our own decoder never touched, e.g. a
			// hand-authored dir containing just "model.dae". A directory that
			// carries our hash-cache marker was produced/managed by wszst's own
			// extract/pack pipeline (any container format, not just Switch); an
			// orphan .glb there is just a read-only preview the decoder emitted,
			// not something to repack -- synthesizing a bogus Switch BFRES for
			// it corrupts the (possibly non-Switch) container and pointlessly
			// redoes work the decoder already did.
			// wszst-setup.txt marks the top of a tree our own extract/xexport
			// pipeline produced. It sits at the root of that tree, which may be
			// an ancestor of the directory actually being packed (e.g. CREATE
			// is pointed straight at a "<Group>(NW4R)" subfolder inside a
			// bigger extracted BRRES) -- walk upward looking for it.
			bool decoder_managed_dir = false;
			{
				char anc[PATH_MAX];
				snprintf (anc, sizeof (anc), "%s", root);
				for (int up = 0; up < 16 && anc[0]; up++)
				{
					char marker[PATH_MAX];
					snprintf (marker, sizeof (marker), "%s/%s", anc, SZS_SETUP_FILE);
					struct stat st_marker;
					if (stat (marker, &st_marker) == 0)
					{
						decoder_managed_dir = true;
						break;
					}
					char cache_marker[PATH_MAX];
					snprintf (cache_marker, sizeof (cache_marker), "%s/%s", anc, SZS_HASH_CACHE_FILE);
					struct stat st_cache;
					if (stat (cache_marker, &st_cache) == 0)
					{
						decoder_managed_dir = true;
						break;
					}
					ccp slash = strrchr (anc, '/');
					if (!slash || slash == anc)
						break;
					anc[slash - anc] = 0;
				}
			}
			if (!handled && !found_sibling && !decoder_managed_dir)
			{
				char bfres_path[PATH_MAX];
				snprintf (
					bfres_path, sizeof (bfres_path), "%.*s.bfres", (int)(strlen (path) - 4), path);
				struct stat st_bfres;
				if (stat (bfres_path, &st_bfres) != 0 || !S_ISREG (st_bfres.st_mode))
				{
					model_t *mdl = ParseGLBFile (path);
					if (mdl)
					{
						uint8_t *created = 0;
						size_t created_size = 0;
						if (CreateSwitchBFRES (mdl, &created, &created_size) && created)
						{
							SaveFILE (bfres_path, 0, true, created, (uint)created_size, 0);
							FREE (created);
							if (verbose >= 0)
								fprintf (stdlog, "REPACK CREATE %s -> %s\n", path, bfres_path);
							unlink (path);
						}
						FreeModel (mdl);
					}
				}
			}
			else if (!handled && !found_sibling && decoder_managed_dir)
			{
				// Genuine orphan case: our own decoder emitted this .glb/.dae as a
				// read-only preview (no repackable sibling exists, e.g. a Mii
				// reference like "mii_000.rsd" that has no embedded model at all).
				// It must not fall through to the generic directory packer, which
				// would otherwise silently stuff it into the archive as bogus new
				// content -- bloating the container and shifting/duplicating
				// entries the game never expected. Delete it; a future EXTRACT
				// regenerates the same preview on demand.
				unlink (path);
				if (verbose >= 0)
					fprintf (stdlog, "SKIP PREVIEW %s\n", path);
			}
		}

		// Check for .ncer.xml or .nanr.xml or .byml.yaml files
		if (S_ISREG (st.st_mode) && nlen > 9 && !strcasecmp (de->d_name + nlen - 9, ".ncer.xml"))
		{
			char target_ncer[PATH_MAX];
			snprintf (target_ncer, sizeof (target_ncer), "%.*s", (int)(strlen (path) - 4), path);
			struct stat st_ncer;
			time_t ref_x_mtime = 0;
			if (stat (target_ncer, &st_ncer) == 0)
				ref_x_mtime = st_ncer.st_mtime;
			struct stat st_s;
			char s_path[PATH_MAX];
			snprintf (s_path, sizeof (s_path), "%s/%s", root, SZS_SETUP_FILE);
			if (stat (s_path, &st_s) == 0 && st_s.st_mtime > ref_x_mtime)
				ref_x_mtime = st_s.st_mtime;

			if (ref_x_mtime == 0 || st.st_mtime > ref_x_mtime + 2)
			{
				enumError err = create_ncer_xml (path, target_ncer);
				if (err <= ERR_WARNING && verbose >= 0)
					fprintf (stdlog, "REPACK NCER XML %s -> %s\n", path, target_ncer);
				else if (max_err < err)
					max_err = err;
			}
			unlink (path);
		}
		if (S_ISREG (st.st_mode) && nlen > 9 && !strcasecmp (de->d_name + nlen - 9, ".nanr.xml"))
		{
			char target_nanr[PATH_MAX];
			snprintf (target_nanr, sizeof (target_nanr), "%.*s", (int)(strlen (path) - 4), path);
			struct stat st_nanr;
			time_t ref_x_mtime = 0;
			if (stat (target_nanr, &st_nanr) == 0)
				ref_x_mtime = st_nanr.st_mtime;
			struct stat st_s;
			char s_path[PATH_MAX];
			snprintf (s_path, sizeof (s_path), "%s/%s", root, SZS_SETUP_FILE);
			if (stat (s_path, &st_s) == 0 && st_s.st_mtime > ref_x_mtime)
				ref_x_mtime = st_s.st_mtime;

			if (ref_x_mtime == 0 || st.st_mtime > ref_x_mtime + 2)
			{
				enumError err = create_nanr_xml (path, target_nanr);
				if (err <= ERR_WARNING && verbose >= 0)
					fprintf (stdlog, "REPACK NANR XML %s -> %s\n", path, target_nanr);
				else if (max_err < err)
					max_err = err;
			}
			unlink (path);
		}
		if (S_ISREG (st.st_mode) && nlen > 10 && !strcasecmp (de->d_name + nlen - 10, ".byml.yaml"))
		{
			char target_byml[PATH_MAX];
			snprintf (target_byml, sizeof (target_byml), "%.*s", (int)(strlen (path) - 5), path);
			struct stat st_byml;
			time_t ref_x_mtime = 0;
			if (stat (target_byml, &st_byml) == 0)
				ref_x_mtime = st_byml.st_mtime;
			struct stat st_s;
			char s_path[PATH_MAX];
			snprintf (s_path, sizeof (s_path), "%s/%s", root, SZS_SETUP_FILE);
			if (stat (s_path, &st_s) == 0 && st_s.st_mtime > ref_x_mtime)
				ref_x_mtime = st_s.st_mtime;

			if (ref_x_mtime == 0 || st.st_mtime > ref_x_mtime + 2)
			{
				enumError err = encode_byml_file (path, target_byml);
				if (err <= ERR_WARNING && verbose >= 0)
					fprintf (stdlog, "REPACK BYML %s -> %s\n", path, target_byml);
				else if (max_err < err)
					max_err = err;
			}
			unlink (path);
		}

		// Check for .d directories (e.g. foo.brres.d, foo.szs.d, foo.wbfs.d, bowling.d, local.d,
		// etc.)
		if (S_ISDIR (st.st_mode) && nlen > 2 && !strcasecmp (de->d_name + nlen - 2, ".d"))
		{
			char target_file[PATH_MAX];
			snprintf (target_file, sizeof (target_file), "%.*s", (int)(strlen (path) - 2), path);

			struct stat st_target;
			bool target_exists
				= (stat (target_file, &st_target) == 0 && S_ISREG (st_target.st_mode));
			if (!target_exists)
			{
				for (int c = 0; cand_archive_exts[c]; c++)
				{
					char cand[PATH_MAX];
					snprintf (cand, sizeof (cand), "%s%s", target_file, cand_archive_exts[c]);
					if (stat (cand, &st_target) == 0 && S_ISREG (st_target.st_mode))
					{
						snprintf (target_file, sizeof (target_file), "%s", cand);
						target_exists = true;
						break;
					}
				}
			}

			// Check wszst-setup.txt inside path to deduce format if not found
			if (!target_exists)
			{
				SetupParam_t sp;
				InitializeSetupParam (&sp);
				if (ScanSetupParam (&sp, true, path, SZS_SETUP_FILE, 0, true) == ERR_OK)
				{
					ccp sp_ext = GetExtFF (sp.fform_file, sp.fform_arch);
					if (sp_ext && *sp_ext)
					{
						char cand[PATH_MAX];
						snprintf (cand, sizeof (cand), "%s%s", target_file, sp_ext);
						snprintf (target_file, sizeof (target_file), "%s", cand);
						if (stat (target_file, &st_target) == 0 && S_ISREG (st_target.st_mode))
							target_exists = true;
					}
				}
				ResetSetupParam (&sp);
			}

			// Only repack if an existing target container exists, or if this .d folder
			// is recognized as an archive directory
			if (!target_exists && !is_archive_dir_name (path, strlen (path)))
				continue;

			// Media extraction produces one editable MP4 preview. If its content is
			// newer than the source, send it back through mobipeg using settings
			// recovered from the source; otherwise discard the preview directory.
			ccp t_ext = strrchr (target_file, '.');
			if (t_ext
				&& (!strcasecmp (t_ext, ".thp") || !strcasecmp (t_ext, ".moflex")
					|| !strcasecmp (t_ext, ".mo") || !strcasecmp (t_ext, ".mods")))
			{
				DIR *md = opendir (path); struct dirent *me; char preview[PATH_MAX] = {0};
				while (md && (me = readdir (md)))
				{
					size_t ml = strlen (me->d_name);
					if (ml > 4 && !strcasecmp (me->d_name + ml - 4, ".mp4"))
						{ snprintf (preview, sizeof (preview), "%s/%s", path, me->d_name); break; }
				}
				if (md) closedir (md);
				struct stat ps;
				const bool media_changed = *preview && target_exists && !stat (preview, &ps)
					&& ps.st_mtime > st_target.st_mtime;
				if (media_changed)
				{
					enumError err = PassthruReencodeMedia (preview, target_file);
					if (err == ERR_NOTHING_TO_DO)
					{
						ERROR0 (ERR_WARNING,
							"edited media retained because mobipeg is unavailable: %s", preview);
						if (max_err < ERR_WARNING) max_err = ERR_WARNING;
						continue;
					}
					if (err > ERR_WARNING)
					{
						if (max_err < err) max_err = err;
						continue; // keep the edited preview so a failed encode loses nothing
					}
				}
				remove_dir_recursive (path);
				continue;
			}

			// Audio previews and archives are not generic U8 directories.
			if (t_ext
				&& (!strcasecmp (t_ext, ".brsar") || !strcasecmp (t_ext, ".sdat")
					|| !strcasecmp (t_ext, ".brstm")
					|| !strcasecmp (t_ext, ".bcstm") || !strcasecmp (t_ext, ".bfstm")
					|| !strcasecmp (t_ext, ".bns") || !strcasecmp (t_ext, ".ast")
					|| !strcasecmp (t_ext, ".dsp")))
			{
				remove_dir_recursive (path);
				continue;
			}

			time_t ref_mtime = 0;
			struct stat st_setup;
			char setup_path[PATH_MAX];
			snprintf (setup_path, sizeof (setup_path), "%s/%s", path, SZS_SETUP_FILE);
			if (stat (setup_path, &st_setup) == 0)
				ref_mtime = st_setup.st_mtime;
			else if (target_exists)
				ref_mtime = st_target.st_mtime;

			bool need_rebuild
				= !target_exists || (ref_mtime > 0 ? is_dir_newer_than (path, ref_mtime) : true);

			if (need_rebuild)
			{
				enumError err = create_archive_from_dir (path, target_file);
				if (err <= ERR_WARNING)
				{
					if (verbose >= 0)
						fprintf (stdlog, "REPACK %s/ -> %s\n", path, target_file);
					remove_dir_recursive (path);
				}
				else if (max_err < err)
					max_err = err;
			}
			else
			{
				// Target is already up to date; clean up the leftover .d directory
				remove_dir_recursive (path);
			}
		}
	}
	closedir (dir);
	return max_err;
}

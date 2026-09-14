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
			ResetFile (&F, opt_preserve);
		}
		FREE (decoded);
		if (!err && dest_out)
			snprintf (dest_out, dest_out_size, "%s", dest);
		return err;
	}

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

	// The 4-byte zstd frame magic (28 B5 2F FD) and the skippable-frame
	// magics are strong enough to trust during recursive extraction, not
	// just for an explicit `decompress` call.  Some assets are stored
	// double-compressed -- e.g. Tomodachi Life's romfs ships
	// "*.bntx.zs" whose payload is itself another bare zstd frame -- and
	// without the magic path here the inner frame is left undecoded on the
	// second hop.  (Contrast IsZlib, whose 2-byte check stays gated on
	// !dest_out because it false-positives on arbitrary data.)
	if ((src_ext && (!strcasecmp (src_ext, ".zs") || !strcasecmp (src_ext, ".zst") || !strcasecmp (src_ext, ".zstd")))
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

	// LZ4 frame and skippable-frame magics are unambiguous.  As with zstd
	// above, use them at every XX recursion level rather than requiring a
	// .lz4 suffix on an extracted member.
	if ((src_ext && !strcasecmp (src_ext, ".lz4"))
		|| (size >= 4 && IsLZ4 (data, size) > 0))
	{
		err = DecodeLZ4 (&decoded, &decoded_size, data, size);
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
			if (dot && !strcasecmp (dot, ".lz4"))
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
			fprintf (stdlog, "%s%sDECOMPRESS LZ4:%s -> RAW:%s\n", verbose > 0 ? "\n" : "",
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

		// LoadSZS() succeeds for any file it can classify, compressed or
		// not.  Only the compressed wrappers have anything to decompress;
		// without this guard an uncompressed input (e.g. a BWAV audio
		// stream) is written straight back out as a byte-identical ".bin".
		if (err <= ERR_WARNING && err != ERR_NOT_EXISTS && !IsCompressedFF (szs.fform_file))
		{
			ResetSZS (&szs);
			continue;
		}

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


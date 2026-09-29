	if (err == ERR_OK)
	{
		// Every check above (SARC/GFA/PAC/DARC, BRFNT/BFLYT auto-decode,
		// model export) ran against the still-compressed bytes and correctly
		// declined. decompress_nintendo_file2() wrote the decompressed
		// payload to its own destination path (== 'arg' only when 'arg'
		// already lives under the --dest tree; a genuinely different path
		// otherwise -- recursing on 'arg' itself would just re-read the
		// still-compressed source forever and hit the depth cap). Re-run the
		// same dispatch chain against that destination now that it might
		// actually be one of those -- confirmed necessary on a real retail
		// 3DS disc, where every DARC-bundled layout family ships
		// LZ11-compressed (extract_darc_file() never saw a "darc" magic
		// before this, only the compressed bytes in front of it). The
		// decompressed payload can never itself start with the same codec's
		// magic byte again, so this terminates in one extra hop in practice;
		// the depth cap only guards against a corrupt/adversarial file.
		const enumError sub_err = depth < 32
			? extract_one_file (decompressed_path, basedir, depth + 1)
			: ERR_FILE_TOO_BIG;

		// If nothing further recognised the decompressed payload as a real
		// container (extract_one_file() left it sitting there untouched,
		// same file it wrote above), it's just an inert dump kept for
		// inspection -- the same class of bug as extract_mii_file()'s
		// glb/png previews: leaving it on disk lets a later CREATE sweep it
		// into the rebuilt archive as bogus new content. Discard it unless
		// the caller explicitly asked to keep raw dumps.
		// ... but not when the payload is itself the deliverable. A .bfwav
		// decodes to a finished WAV, which no container probe claims, so
		// this discard used to delete every decoded wave the extractor
		// produced -- BFWAV/BCWAV/BRWAV conversion never left a file behind.
		//
		// ... and not when --auto was given either. --auto's own advertised
		// purpose ("recursively decode recognised raw Nintendo compression
		// streams found in the extracted tree") is to KEEP exactly this
		// class of output; cmd_extract()'s single-archive path already
		// honours it that way via auto_decompress_tree(). But XX/XDECODE/
		// XEXPORT/XALL given a plain directory (not an archive file) never
		// go through cmd_extract()'s per-archive branch at all -- they walk
		// straight into extract_tree()/extract_one_file(), which passes
		// depth+1 for every single regular file found by ordinary directory
		// recursion (see extract_tree()). So depth>0 here is true for
		// essentially every loose file in a real, already-unpacked game
		// tree, not just for a payload actually unwrapped from inside a
		// nested container -- which silently discarded every decoded
		// LZ10/LZ11/etc. leaf asset (raw NDS tile/graphics data, etc.) that
		// didn't also happen to parse as some further container format.
		if (!opt_export_raw && !OptionUsed[OPT_AUTO] && !decompressed_is_final && depth > 0)
		{
			struct stat st_leftover;
			if (!stat (decompressed_path, &st_leftover) && S_ISREG (st_leftover.st_mode))
				unlink (decompressed_path);
		}
		return sub_err;
	}
	if (err != ERR_NOTHING_TO_DO)
		return err;

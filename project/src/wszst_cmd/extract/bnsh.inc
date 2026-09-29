static void bnsh_stage_filename_safe (char *dest, uint destsz, ccp stage)
{
	// bnsh_stage_name[] carries "(unk)" suffixes for display; strip them for filenames
	// and map anything non-alphanumeric to '_' so EXTRACT sidecars are portable.
	uint j = 0;
	for (uint i = 0; stage[i] && j + 1 < destsz; i++)
	{
		if (stage[i] == '(')
			break;
		char c = stage[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'
			|| c == '-')
			dest[j++] = c;
		else if (c == ' ' || c == '.')
			dest[j++] = '_';
	}
	dest[j] = 0;
	if (!j && destsz > 8)
		snprintf (dest, destsz, "stage");
}

static enumError extract_bnsh_manifest (ccp arg)
{
	if (!is_ext (arg, ".bnsh"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsBNSH (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".txt", false);
	else
		snprintf (dest, sizeof (dest), "%s.txt", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE BNSH:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	if (testmode)
	{
		FREE (raw);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f)
		err = DecodeBNSH_Text (F.f, raw, raw_size);
	ResetFile (&F, opt_preserve);

	// Sidecars: every in-bounds shader blob becomes a sibling file so the raw GPU
	// binaries / GLSL-NX source chunks (BinaryShaderLibrary ShaderCodeDataBinary /
	// ShaderCodeDataSource / ShaderCodeDataCompressed) are usable without parsing the
	// manifest. Mirrors the WTB .png sidecar pattern below: base name from the source
	// archive, extension by kind (.glsl / .zlib.bin / .bin), skipped when the target
	// already exists unless --overwrite is given.
	if (!err)
	{
		bnsh_blobs_t blobs = { 0, 0 };
		if (!ScanBNSH_Blobs (&blobs, raw, raw_size) && blobs.n_blobs)
		{
			char base[PATH_MAX];
			snprintf (base, sizeof (base), "%s", arg);
			char *dot = strrchr (base, '.');
			if (dot && (dot == base || dot[-1] != '/'))
				*dot = 0;

			for (uint i = 0; i < blobs.n_blobs; i++)
			{
				const bnsh_blob_t *b = blobs.blobs + i;
				char safe_stage[40];
				bnsh_stage_filename_safe (safe_stage, sizeof (safe_stage), b->stage);
				const ccp ext = b->is_source ? "glsl" : b->is_compressed ? "zlib.bin" : "bin";
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s.v%02u.%s.%s.%u.%s", base, b->variation,
					b->program_kind, safe_stage, b->blob_index, ext);
				if (!opt_overwrite && !access (path, F_OK))
					continue;
				FILE *of = fopen (path, "wb");
				if (!of)
					continue;
				fwrite (raw + b->offset, 1, b->size, of);
				fclose (of);
				if (verbose >= 0)
					fprintf (stdlog, "EXTRACT BNSH:%s[%u/%s/%s/%u] -> %s\n", arg, b->variation,
						b->program_kind, b->stage, b->blob_index, path);
			}
		}
		ResetBNSH_Blobs (&blobs);
	}

	FREE (raw);
	return err;
}

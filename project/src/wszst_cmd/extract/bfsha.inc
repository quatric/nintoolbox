static enumError extract_bfsha_manifest (ccp arg)
{
	if (!is_ext (arg, ".bfsha"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsBFSHA (raw, raw_size))
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
		fprintf (stdlog, "%s%sDECODE BFSHA:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	if (testmode)
	{
		FREE (raw);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f)
		err = DecodeBFSHA_Text (F.f, raw, raw_size);
	ResetFile (&F, opt_preserve);

	// Sidecars: each shader model's embedded BNSH blob (BinaryShaderLibrary
	// ShaderModel.BnshFile, located by ScanBFSHA_ModelRefs) becomes a sibling
	// "<base>.<model>.bnsh" file so `wszst EXTRACT model.bnsh` (or a second
	// `wszst EXTRACT` pass) can decode its variations without re-parsing the
	// parent archive. Same overwrite/testmode discipline as the BNSH sidecars.
	if (!err)
	{
		bfsha_models_t models = { 0, 0 };
		if (!ScanBFSHA_ModelRefs (&models, raw, raw_size) && models.n_models)
		{
			char base[PATH_MAX];
			snprintf (base, sizeof (base), "%s", arg);
			char *dot = strrchr (base, '.');
			if (dot && (dot == base || dot[-1] != '/'))
				*dot = 0;

			for (uint i = 0; i < models.n_models; i++)
			{
				const bfsha_model_ref_t *m = models.models + i;
				if (!m->bnsh_size || (u64)m->bnsh_offset + m->bnsh_size > raw_size)
					continue;
				char safe_name[256];
				uint j = 0;
				for (uint k = 0; m->name[k] && j + 1 < sizeof (safe_name); k++)
				{
					char c = m->name[k];
					if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
						|| c == '_' || c == '-' || c == '.')
						safe_name[j++] = c;
					else
						safe_name[j++] = '_';
				}
				safe_name[j] = 0;
				if (!j)
					snprintf (safe_name, sizeof (safe_name), "model%u", i);
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s.%s.bnsh", base, safe_name);
				if (!opt_overwrite && !access (path, F_OK))
					continue;
				FILE *of = fopen (path, "wb");
				if (!of)
					continue;
				fwrite (raw + m->bnsh_offset, 1, m->bnsh_size, of);
				fclose (of);
				if (verbose >= 0)
					fprintf (stdlog, "EXTRACT BFSHA:%s[%s] -> BNSH:%s\n", arg, m->name, path);
			}
		}
		ResetBFSHA_ModelRefs (&models);
	}

	FREE (raw);
	return err;
}

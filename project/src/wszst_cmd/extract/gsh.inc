// Export each Latte program embedded in a shader-only Gfx2/GSH container to
// a semantic listing with a lossless raw section. Reassembling the RAW fields
// reproduces the complete program, including clauses and unknown opcodes/bits.
static enumError extract_gsh_shaders (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".gsh"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err || raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	gtx_t gtx;
	err = ScanGTX (&gtx, raw, (uint)raw_size);
	if (err || !gtx.n_shaders)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	static const ccp names[] = { "vertex", "pixel", "geometry", "compute" };
	uint written = 0;
	for (uint i = 0; i < gtx.n_shaders; i++)
	{
		const gtx_shader_t *s = gtx.shaders + i;
		if (!s->program || !s->program->data_size)
			continue;
		char *text = 0;
		if (DisassembleLatteCF (&text, s->program->data, s->program->data_size))
			continue;
		// Refuse to emit a listing unless the bundled assembler proves that it
		// reproduces the complete program byte-for-byte.
		u8 *check = 0;
		uint check_size = 0;
		if (AssembleLatteCF (&check, &check_size, text) || check_size != s->program->data_size
			|| memcmp (check, s->program->data, check_size))
		{
			FREE (check);
			FREE (text);
			err = ERR_INVALID_DATA;
			continue;
		}
		FREE (check);
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.%s-%u.latte", arg, names[s->stage], i);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%sEXTRACT GSH:%s -> LATTE:%s\n", testmode ? "WOULD " : "", arg, dest);
		if (!testmode)
		{
			File_t F;
			InitializeFile (&F);
			enumError ferr = CreateFileOpt (&F, true, dest, false, arg);
			if (!ferr && F.f && fwrite (text, 1, strlen (text), F.f) != strlen (text))
				ferr = ERR_WRITE_FAILED;
			ResetFile (&F, false);
			if (ferr && err < ferr)
				err = ferr;
			else
				written++;
		}
		else
			written++;
		FREE (text);
	}
	if (!testmode)
	{
		char manifest_dest[PATH_MAX];
		snprintf (manifest_dest, sizeof (manifest_dest), "%s.txt", arg);
		File_t MF;
		InitializeFile (&MF);
		enumError mferr = CreateFileOpt (&MF, true, manifest_dest, false, arg);
		if (!mferr && MF.f)
		{
			DecodeGSH_Text (MF.f, raw, raw_size);
			written++;
		}
		ResetFile (&MF, false);
	}
	ResetGTX (&gtx);
	FREE (raw);
	return written ? err : ERR_NOTHING_TO_DO;
}

// "STRT" string table (One Piece Unlimited Adventure/Cruise ".bin"/".str"):
// {"STRT", u32 ?, char lang[4], u32 offsets[count]} with absolute offsets
// to NUL-terminated strings; count = (offsets[0] - 12) / 4. Written to
// "<name>.txt" as "[index]" headed blocks (decode mode: wszst xx).
static enumError extract_strt_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (export_count <= 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	#define STRT_BE32(p) ((uint)(p)[0] << 24 | (uint)(p)[1] << 16 | (uint)(p)[2] << 8 | (p)[3])
	if (raw_size < 0x14 || memcmp (raw, "STRT", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint first = STRT_BE32 (raw + 12);
	if (first < 0x10 || first > raw_size || ((first - 12) & 3))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = (first - 12) / 4;
	for (uint i = 0, prev = 0; i < count; i++)
	{
		const uint o = STRT_BE32 (raw + 12 + 4 * i);
		if (o < prev || o > raw_size)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		prev = o;
	}

	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE STRT:%s (%u strings) -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);
	if (testmode)
	{
		FREE (raw);
		return ERR_OK;
	}

	enumError err = ERR_OK;
	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	for (uint i = 0; !err && i < count; i++)
	{
		const uint o = STRT_BE32 (raw + 12 + 4 * i);
		uint e = o;
		while (e < raw_size && raw[e])
			e++;
		fprintf (F.f, "[%u]\n", i);
		if (e > o && fwrite (raw + o, 1, e - o, F.f) != e - o)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing failed: %s\n", dest);
		fputc ('\n', F.f);
	}
	ResetFile (&F, opt_preserve);
	#undef STRT_BE32
	FREE (raw);
	return err;
}

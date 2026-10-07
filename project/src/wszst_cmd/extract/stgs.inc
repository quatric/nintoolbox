// "STGS" string table (MySims Agents, "Text/*.str"): {"STGS", u32 version (5),
// u32 count, u32 ?, {u32 key_hash, u32 offset}[count]} followed by a pool of
// NUL-terminated UTF-8 strings (offsets are relative to the pool). Written to
// "<name>.txt" as "[hash]" headed blocks (decode mode: wszst xx).
static enumError extract_stgs_file (ccp arg, ccp basedir, uint depth)
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
	#define STGS_BE32(p) ((uint)(p)[0] << 24 | (uint)(p)[1] << 16 | (uint)(p)[2] << 8 | (p)[3])
	if (raw_size < 0x10 || memcmp (raw, "STGS", 4) || STGS_BE32 (raw + 4) != 5)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = STGS_BE32 (raw + 8);
	const size_t pool = 0x10 + 8ull * count;
	if (!count || count > 0x100000 || pool >= raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	for (uint i = 0, prev = 0; i < count; i++)
	{
		const uint o = STGS_BE32 (raw + 0x14 + 8 * i);
		if (o < prev || pool + o >= raw_size)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		prev = o;
	}

	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE STGS:%s (%u strings) -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);
	if (testmode)
	{
		FREE (raw);
		return ERR_OK;
	}

	File_t F;
	enumError err = CreateFileOpt (&F, true, dest, false, arg);
	for (uint i = 0; !err && i < count; i++)
	{
		const size_t o = pool + STGS_BE32 (raw + 0x14 + 8 * i);
		size_t e = o;
		while (e < raw_size && raw[e])
			e++;
		fprintf (F.f, "[%08x]\n", STGS_BE32 (raw + 0x10 + 8 * i));
		if (e > o && fwrite (raw + o, 1, e - o, F.f) != e - o)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing failed: %s\n", dest);
		fputc ('\n', F.f);
	}
	ResetFile (&F, opt_preserve);
	#undef STGS_BE32
	FREE (raw);
	return err;
}

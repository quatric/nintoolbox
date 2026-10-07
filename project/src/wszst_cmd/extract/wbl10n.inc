// "WB-L10n" string table (So Blonde: Back to the Island, "Text/*.dat"):
// {"WB-L10n\0", u32 version, char name[16], u32 count, u32 ?, u32 offsets[count]}
// little-endian, with absolute offsets to NUL-terminated UTF-8 strings.
// Written to "<name>.txt" as "[index]" headed blocks (decode mode: wszst xx).
static enumError extract_wbl10n_file (ccp arg, ccp basedir, uint depth)
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
	#define WBL_LE32(p) ((uint)(p)[0] | (uint)(p)[1] << 8 | (uint)(p)[2] << 16 | (uint)(p)[3] << 24)
	if (raw_size < 0x28 || memcmp (raw, "WB-L10n\0", 8))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = WBL_LE32 (raw + 0x1c);
	if (!count || count > 0x100000 || 0x24 + 4ull * count > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	for (uint i = 0, prev = 0; i < count; i++)
	{
		const uint o = WBL_LE32 (raw + 0x24 + 4 * i);
		if (o < prev || o >= raw_size)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		prev = o;
	}

	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE WB-L10n:%s (%u strings) -> %s\n", verbose > 0 ? "\n" : "",
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
		const uint o = WBL_LE32 (raw + 0x24 + 4 * i);
		uint e = o;
		while (e < raw_size && raw[e])
			e++;
		fprintf (F.f, "[%u]\n", i);
		if (e > o && fwrite (raw + o, 1, e - o, F.f) != e - o)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing failed: %s\n", dest);
		fputc ('\n', F.f);
	}
	ResetFile (&F, opt_preserve);
	#undef WBL_LE32
	FREE (raw);
	return err;
}

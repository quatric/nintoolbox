// "FUAX" string table (Rebel Raiders: Operation Nighthawk, "text/<lang>/*.bin"):
// {"FUAX", u32 hash, char lang[4], u32 ?, u32 count, u32 offsets[count]} with
// byte offsets (relative to the end of the table) to NUL-terminated UTF-16BE
// strings. Written to "<name>.txt" as UTF-8 "[index]" blocks (decode mode).
static enumError extract_fuax_file (ccp arg, ccp basedir, uint depth)
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
	#define FUAX_BE32(p) ((uint)(p)[0] << 24 | (uint)(p)[1] << 16 | (uint)(p)[2] << 8 | (p)[3])
	if (raw_size < 0x18 || memcmp (raw, "FUAX", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = FUAX_BE32 (raw + 0x10);
	const size_t base = 0x14 + 4ull * count;
	if (!count || count > 0x100000 || base > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	for (uint i = 0, prev = 0; i < count; i++)
	{
		const uint o = FUAX_BE32 (raw + 0x14 + 4 * i);
		if (o < prev || (o & 1) || base + o > raw_size)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		prev = o;
	}

	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE FUAX:%s (%u strings) -> %s\n", verbose > 0 ? "\n" : "",
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
		size_t p = base + FUAX_BE32 (raw + 0x14 + 4 * i);
		fprintf (F.f, "[%u]\n", i);
		while (p + 2 <= raw_size)
		{
			uint c = raw[p] << 8 | raw[p + 1];
			p += 2;
			if (!c)
				break;
			if (c >= 0xd800 && c < 0xdc00 && p + 2 <= raw_size)
			{
				const uint lo = raw[p] << 8 | raw[p + 1];
				if (lo >= 0xdc00 && lo < 0xe000)
				{
					c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
					p += 2;
				}
			}
			if (c < 0x80)
				fputc (c, F.f);
			else if (c < 0x800)
				fprintf (F.f, "%c%c", 0xc0 | c >> 6, 0x80 | (c & 0x3f));
			else if (c < 0x10000)
				fprintf (F.f, "%c%c%c", 0xe0 | c >> 12, 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f));
			else
				fprintf (F.f, "%c%c%c%c", 0xf0 | c >> 18, 0x80 | ((c >> 12) & 0x3f),
					0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f));
		}
		fputc ('\n', F.f);
	}
	ResetFile (&F, opt_preserve);
	#undef FUAX_BE32
	FREE (raw);
	return err;
}

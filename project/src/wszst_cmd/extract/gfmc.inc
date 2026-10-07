// "GFMC" message file (Teolsil Kirby Iyagi / Kirby's Return to Dream Land,
// "message/<lang>/*.bin"): big-endian {"GFMC", u32 version, u32 file_size,
// u32 sec1 (= 0x1c), u32 sec2, u32 sec3, u32 sec4}. sec2 holds
// {u32 label_hash, u32 count, u32 sec3_offset}[], sec3 holds u32 offsets into
// the string pool at sec4. A string slot is some control words that end in
// "TX\0\0", then a u16 length and that many UTF-16BE characters. Written to
// "<name>.txt" as "[hash]" blocks in UTF-8 (decode mode: wszst xx).
static void gfmc_put_utf8 (FILE *f, uint c)
{
	if (c < 0x80)
		fputc (c, f);
	else if (c < 0x800)
		fprintf (f, "%c%c", 0xc0 | c >> 6, 0x80 | (c & 0x3f));
	else
		fprintf (f, "%c%c%c", 0xe0 | c >> 12, 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f));
}

static enumError extract_gfmc_file (ccp arg, ccp basedir, uint depth)
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
	#define GF_BE32(p) ((uint)(p)[0] << 24 | (uint)(p)[1] << 16 | (uint)(p)[2] << 8 | (p)[3])
	if (raw_size < 0x20 || memcmp (raw, "GFMC", 4) || GF_BE32 (raw + 8) != raw_size
		|| GF_BE32 (raw + 12) != 0x1c)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint sec2 = GF_BE32 (raw + 16), sec3 = GF_BE32 (raw + 20), sec4 = GF_BE32 (raw + 24);
	if (!(0x1c < sec2 && sec2 <= sec3 && sec3 < sec4 && sec4 <= raw_size) || (sec3 - sec2) % 12
		|| (sec4 - sec3) & 3)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = (sec3 - sec2) / 12, n_off = (sec4 - sec3) / 4;

	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE GFMC:%s (%u messages) -> %s\n", verbose > 0 ? "\n" : "",
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
		const u8 *r = raw + sec2 + 12 * i;
		const uint idx = GF_BE32 (r + 8) / 4;
		fprintf (F.f, "[%08x]\n", GF_BE32 (r));
		if (idx < n_off)
		{
			size_t p = (size_t)sec4 + GF_BE32 (raw + sec3 + 4 * idx), e = p;
			while (e + 4 <= raw_size && e - p < 200 && memcmp (raw + e, "TX\0\0", 4))
				e += 2;
			if (e + 6 <= raw_size && !memcmp (raw + e, "TX\0\0", 4))
			{
				const uint len = raw[e + 4] << 8 | raw[e + 5];
				p = e + 6;
				for (uint k = 0; k < len && p + 2 <= raw_size; k++, p += 2)
				{
					const uint c = raw[p] << 8 | raw[p + 1];
					if (c == '\n' || c >= 0x20)
						gfmc_put_utf8 (F.f, c);
				}
			}
		}
		fputc ('\n', F.f);
	}
	ResetFile (&F, opt_preserve);
	#undef GF_BE32
	FREE (raw);
	return err;
}

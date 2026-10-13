// Muramasa - The Demon Blade ".nms" message sheets (Wii): an "FCMP" LZSS container (lib-muramasa.c) around an "NMSB"
// block, little-endian: "NMSB", u32 payload size, u32 header size (0x20), u32 0, u32 0, u32 string count @0x14,
// then count u32 slots (zero on disc, filled in at load time) followed by the NUL-terminated strings, in index
// order (the pool can hold a few extra trailing strings). Strings are Shift-JIS in the Japanese sheets and
// Latin-1 in the European ones; each string is tried as Shift-JIS first. Decoded to "<file>.txt" as
// "index<TAB>text" lines with backslash escapes for newlines.
static enumError extract_muranms_file (ccp arg)
{
	if (!is_ext (arg, ".nms") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	u8 *pl = 0;
	enumError err = ERR_NOTHING_TO_DO;
	const u32 dsz = GetDecompressedSizeMuramasaFcmp (raw, sz);
	if (!dsz || dsz > 0x10000000 || !IsMuramasaFcmp (raw, sz, sz))
		goto done;
	pl = MALLOC (dsz);
	if (!pl || DecompressMuramasaFcmp (raw, sz, pl, dsz, 0, arg, true) != ERR_OK)
		goto done;
	if (dsz < 0x20 || memcmp (pl, "NMSB", 4) || rd_le32 (pl + 8) != 0x20)
		goto done;
	const uint count = rd_le32 (pl + 0x14);
	const size_t start = 0x20 + 4 * (size_t)count;
	if (!count || count > 0x100000 || start > dsz)
		goto done;
	{
		char out[PATH_MAX];
		snprintf (out, sizeof (out), "%s.txt", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE NMS:%s (%u strings) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg,
				count, out);
		if (testmode)
		{
			err = ERR_OK;
			goto done;
		}
		File_t F;
		err = CreateFileOpt (&F, true, out, false, arg);
		if (!F.f)
			goto done;
		size_t p = start;
		for (uint i = 0; i < count && p < dsz; i++)
		{
			cucp s = pl + p, e = s + strnlen ((ccp)s, dsz - p);
			p += (size_t)(e - s) + 1;
			// Shift-JIS when the whole string is valid, otherwise Latin-1
			bool sjis = true;
			for (cucp q = s; q < e && sjis;)
				sjis = ScanShiftJISCharE (&q, e) > 0;
			fprintf (F.f, "%u\t", i);
			for (cucp q = s; q < e;)
			{
				const int code = sjis ? ScanShiftJISCharE (&q, e) : *q++;
				if (code == '\n')
					fputs ("\\n", F.f);
				else if (code == '\\')
					fputs ("\\\\", F.f);
				else
				{
					char buf[8], *w = PrintUTF8Char (buf, code);
					fwrite (buf, 1, (size_t)(w - buf), F.f);
				}
			}
			fputc ('\n', F.f);
		}
		err = ResetFile (&F, opt_preserve);
	}
done:
	FREE (pl);
	FREE (raw);
	return err;
}

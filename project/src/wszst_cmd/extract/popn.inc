// pop'n music (Wii) formats.
//
// ".sen" sentence tables, big-endian: "SEN\0", u32 string count, u32 file size, count u32 offsets (the first equals
// the end of the table), then NUL-terminated UTF-16BE strings. Decoded to "<file>.txt", one "index<TAB>text" line
// per string with backslash escapes for newlines.
static enumError extract_popn_sen_file (ccp arg)
{
	if (!is_ext (arg, ".sen") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 12 ? rd_be32 (raw + 4) : 0;
	bool ok = count && count <= 0x10000 && !memcmp (raw, "SEN", 4) && rd_be32 (raw + 8) == sz && 12 + 4 * (size_t)count <= sz;
	for (uint i = 0; ok && i < count; i++)
	{
		const size_t o = rd_be32 (raw + 12 + 4 * (size_t)i);
		ok = o >= 12 + 4 * (size_t)count && o < sz && !(o & 1) && (!i || o >= rd_be32 (raw + 8 + 4 * (size_t)i));
	}
	if (!ok)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char out[PATH_MAX];
	snprintf (out, sizeof (out), "%s.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE SEN:%s (%u strings) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			arg, count, out);
	enumError err = ERR_OK;
	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, out, false, arg);
		for (uint i = 0; F.f && i < count; i++)
		{
			fprintf (F.f, "%u\t", i);
			for (size_t p = rd_be32 (raw + 12 + 4 * (size_t)i); p + 2 <= sz;)
			{
				uint c = rd_be16 (raw + p);
				p += 2;
				if (!c)
					break;
				if (c >= 0xd800 && c < 0xdc00 && p + 2 <= sz && rd_be16 (raw + p) >= 0xdc00 && rd_be16 (raw + p) < 0xe000)
				{
					c = 0x10000 + ((c - 0xd800) << 10) + (rd_be16 (raw + p) - 0xdc00);
					p += 2;
				}
				if (c == '\n')
					fputs ("\\n", F.f);
				else if (c == '\\')
					fputs ("\\\\", F.f);
				else
				{
					char buf[8], *e = PrintUTF8Char (buf, (int)c);
					fwrite (buf, 1, (size_t)(e - buf), F.f);
				}
			}
			fputc ('\n', F.f);
		}
		if (F.f)
			err = ResetFile (&F, opt_preserve);
	}
	FREE (raw);
	return err;
}

// ".aept" / ".aepc" (LZ11-compressed, so seen as ".bin" after decompression) layout + texture files: "AEPT", u32 2,
// then a sprite-layout description (0x18-byte part records {id, name offset, width, height, ...} and a name
// table) followed by a standard TPL on a 16-byte boundary. The TPL is written as "<file>.tpl" and decodes to PNG;
// the layout tables are not interpreted.
static enumError extract_popn_aept_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".aept") && !is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 hdr[8];
	FILE *f = fopen (arg, "rb");
	const bool magic = f && fread (hdr, 1, 8, f) == 8 && !memcmp (hdr, "AEPT", 4) && rd_be32 (hdr + 4) == 2;
	if (f)
		fclose (f);
	if (!magic)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t tpl = 0;
	for (size_t p = 0x20; p + 0x10 <= sz && !tpl; p += 0x10)
		if (rd_be32 (raw + p) == 0x0020af30 && rd_be32 (raw + p + 4) && rd_be32 (raw + p + 4) <= 4096 && rd_be32 (raw + p + 8) == 12)
			tpl = p;
	enumError err = ERR_NOTHING_TO_DO;
	if (tpl)
	{
		char name[PATH_MAX];
		ccp slash = strrchr (arg, '/'), base = slash ? slash + 1 : arg;
		snprintf (name, sizeof (name), "%s", base);
		char *dot = strrchr (name, '.');
		if (dot && dot != name)
			*dot = 0;
		if (!is_ext (arg, ".aept") && (dot = strrchr (name, '.')) && dot != name)
			*dot = 0;
		strncat (name, ".tpl", sizeof (name) - strlen (name) - 1);
		nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
		if (!entries)
			err = ERR_OUT_OF_MEMORY;
		else if (OwnedEntryAdd (entries, 0, name, raw + tpl, (uint)(sz - tpl)))
			err = write_owned_entries (arg, basedir, depth, "AEPT", entries, 1);
		else
			ResetOwnedEntries (entries, 0);
	}
	FREE (raw);
	return err;
}

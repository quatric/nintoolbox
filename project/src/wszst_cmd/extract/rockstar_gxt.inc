// Rockstar Leeds "GXT" text tables (".gxt": Manhunt 2, Wii; sizes big endian, strings UTF-16BE): "TKEY" {payload size @4, then 20-byte entries from 0xc: name[12], u32 attribute,
// u32 end offset of the entry's string inside the TDAT block}, followed at 8+size by "TDAT" {size, UTF-16BE NUL-terminated strings back to back}. The strings are not stored in key order, so a string starts after the
// previous NUL before its end offset. Written as "<name>.txt" with "KEY=text" lines in UTF-8.
static enumError extract_rockstar_gxt_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".gxt") || !file_has_magic (arg, "TKEY", 4))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const size_t tk = sz >= 0x10 ? rd_be32 (raw + 4) : 0, td = 8 + tk;
	if (tk >= 12 && td + 8 <= sz && !memcmp (raw + td, "TDAT", 4))
	{
		const size_t dsz = rd_be32 (raw + td + 4), dat = td + 8, dlen = dsz < sz - dat ? dsz : sz - dat;
		const size_t n = (td - 0xc) / 20;
		char *txt = CALLOC (dlen * 3 + n * 24 + 16, 1);
		size_t o = 0, prev;
		bool ok = txt != 0 && n && n < 0x100000;
		for (size_t i = 0; ok && i < n; i++)
		{
			const u8 *e = raw + 0xc + 20 * i;
			const size_t end = rd_be32 (e + 16);
			if (end < 2 || end > dlen || (end & 1))
			{
				ok = false;
				break;
			}
			// The strings are not stored in key order, so the start is found by walking back from the terminator to the previous one.
			prev = end - 2;
			while (prev >= 2 && (raw[dat + prev - 2] || raw[dat + prev - 1]))
				prev -= 2;
			o += snprintf (txt + o, 13, "%.12s", (ccp)e);
			txt[o++] = '=';
			for (size_t p = prev; p + 2 <= end; p += 2)
			{
				uint c = raw[dat + p] << 8 | raw[dat + p + 1];
				if (!c)
					break;
				if (c >= 0xd800 && c < 0xdc00 && p + 4 <= end)
				{
					const uint c2 = raw[dat + p + 2] << 8 | raw[dat + p + 3];
					if (c2 >= 0xdc00 && c2 < 0xe000)
					{
						c = 0x10000 + ((c - 0xd800) << 10) + (c2 - 0xdc00);
						p += 2;
					}
				}
				if (c == '\n')
				{
					txt[o++] = '\\';
					txt[o++] = 'n';
				}
				else if (c < 0x80)
					txt[o++] = (char)c;
				else if (c < 0x800)
				{
					txt[o++] = (char)(0xc0 | c >> 6);
					txt[o++] = (char)(0x80 | (c & 0x3f));
				}
				else if (c < 0x10000)
				{
					txt[o++] = (char)(0xe0 | c >> 12);
					txt[o++] = (char)(0x80 | ((c >> 6) & 0x3f));
					txt[o++] = (char)(0x80 | (c & 0x3f));
				}
				else
				{
					txt[o++] = (char)(0xf0 | c >> 18);
					txt[o++] = (char)(0x80 | ((c >> 12) & 0x3f));
					txt[o++] = (char)(0x80 | ((c >> 6) & 0x3f));
					txt[o++] = (char)(0x80 | (c & 0x3f));
				}
			}
			txt[o++] = '\n';
		}
		if (ok)
		{
			char dest[PATH_MAX], name[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (name, '.');
			if (dot) strcpy (dot, ".txt");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT GXT:%s (%zu strings) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest, name);
			err = shade_write (arg, dest, basedir, name, (u8 *)txt, (uint)o);
		}
		FREE (txt);
	}
	FREE (raw);
	return err;
}

// Capcom "CSVT" data tables (Resident Evil Umbrella / Darkside Chronicles, ".aitbl .atk .vop .csvtbl").
// Big-endian: "CSVT", u32 version, u32 table count @8, u32 descriptor offset @0xc (0x10). Descriptors are
// 0x20 bytes: name[0x10], u32 columns, u32 rows, u32 data offset, u32 0. A table is rows x columns cells of
// 8 bytes {u32 type, u32 value}: 0 empty, 1 integer, 2 float, 3 string (value = offset of a NUL-terminated
// string). Every table is written as "<name>.csv" into "<file>.d/".
static enumError extract_csvt_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (export_count <= 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x30 || memcmp (raw, "CSVT", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint nt = rd_be32 (raw + 8), doff = rd_be32 (raw + 12);
	if (!nt || nt > 256 || doff > sz || (size_t)nt * 0x20 > sz - doff)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	enumError err = ERR_OK;
	uint done = 0;
	for (uint t = 0; t < nt && !err; t++)
	{
		const u8 *d = raw + doff + t * 0x20;
		const uint cols = rd_be32 (d + 0x10), rows = rd_be32 (d + 0x14), off = rd_be32 (d + 0x18);
		if (!cols || cols > 1024 || rows > 0x100000 || off > sz || (size_t)cols * rows * 8 > sz - off)
			continue;
		char name[0x11], fn[0x40];
		memcpy (name, d, 0x10);
		name[0x10] = 0;
		for (char *p = name; *p; p++)
			if (*p == '/' || *p == '\\')
				*p = '_';
		snprintf (fn, sizeof (fn), "%s.csv", name[0] ? name : "table");
		size_t cap = (size_t)cols * rows * 12 + 64, len = 0;
		char *out = MALLOC (cap);
		if (!out)
		{
			err = ERR_OUT_OF_MEMORY;
			break;
		}
		for (uint r = 0; r < rows; r++)
			for (uint c = 0; c < cols; c++)
			{
				const u8 *cell = raw + off + ((size_t)r * cols + c) * 8;
				const uint ty = rd_be32 (cell), v = rd_be32 (cell + 4);
				char tmp[64];
				const char *s = tmp;
				tmp[0] = 0;
				if (ty == 1)
					snprintf (tmp, sizeof (tmp), "%d", (int)v);
				else if (ty == 2)
				{
					float f;
					memcpy (&f, &v, 4);
					snprintf (tmp, sizeof (tmp), "%g", (double)f);
				}
				else if (ty == 3 && v < sz)
					s = (const char *)raw + v;
				size_t n = strnlen (s, ty == 3 ? sz - v : sizeof (tmp));
				int q = memchr (s, ',', n) || memchr (s, '"', n) || memchr (s, '\n', n);
				if (len + n * 2 + 4 > cap)
				{
					cap = cap * 2 + n * 2 + 4;
					char *nb = REALLOC (out, cap);
					if (!nb)
					{
						err = ERR_OUT_OF_MEMORY;
						break;
					}
					out = nb;
				}
				if (q)
					out[len++] = '"';
				for (size_t k = 0; k < n; k++)
				{
					if (s[k] == '"')
						out[len++] = '"';
					out[len++] = s[k];
				}
				if (q)
					out[len++] = '"';
				out[len++] = c + 1 < cols ? ',' : '\n';
			}
		if (!err)
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT CSVT:%s %s (%u x %u) -> %s/%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", arg, name, cols, rows, dest, fn);
			if (!testmode)
				err = shade_write (arg, dest, basedir, fn, out, (uint)len);
			done++;
		}
		FREE (out);
	}
	FREE (raw);
	return err ? err : done ? ERR_OK : ERR_NOTHING_TO_DO;
}

// Ubisoft ".loc" string tables ("Localisation/*/*.loc": Sleepover Party; little endian): u32 count @0, then count NUL-terminated ASCII names starting with the column title
// "String ID", then count UTF-16LE NUL-terminated strings starting with that column's title. Written as "<name>.txt" with "ID=text" lines in UTF-8.
static size_t ubi_loc_utf8 (char *out, uint c)
{
	if (c == '\n')
		return (size_t)sprintf (out, "\\n");
	if (c == '\r')
		return 0;
	if (c < 0x80)
		out[0] = (char)c;
	else if (c < 0x800)
	{
		out[0] = (char)(0xc0 | c >> 6);
		out[1] = (char)(0x80 | (c & 0x3f));
		return 2;
	}
	else if (c < 0x10000)
	{
		out[0] = (char)(0xe0 | c >> 12);
		out[1] = (char)(0x80 | ((c >> 6) & 0x3f));
		out[2] = (char)(0x80 | (c & 0x3f));
		return 3;
	}
	else
	{
		out[0] = (char)(0xf0 | c >> 18);
		out[1] = (char)(0x80 | ((c >> 12) & 0x3f));
		out[2] = (char)(0x80 | ((c >> 6) & 0x3f));
		out[3] = (char)(0x80 | (c & 0x3f));
		return 4;
	}
	return 1;
}

static enumError extract_ubi_loc_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".loc"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const size_t n = sz > 16 ? rd_le32 (raw) : 0;
	if (n > 1 && n < 0x100000 && !memcmp (raw + 4, "String ID\0", 10))
	{
		const u8 **key = CALLOC (n, sizeof (u8 *));
		char *txt = CALLOC (sz * 4 + n * 2 + 64, 1);
		size_t p = 4, o = 0;
		bool ok = key && txt;
		for (size_t i = 0; ok && i < n; i++)
		{
			const u8 *z = memchr (raw + p, 0, sz - p);
			if (!z)
				ok = false;
			else
			{
				key[i] = raw + p;
				p = (size_t)(z - raw) + 1;
			}
		}
		// skip the value column's title, then the n-1 values pair with the keys after "String ID"
		for (size_t i = 0; ok && i < n; i++)
		{
			if (p + 2 > sz)
			{
				ok = false;
				break;
			}
			const size_t start = o;
			if (i)
				o += (size_t)sprintf (txt + o, "%s=", (ccp)key[i]);
			for (;; p += 2)
			{
				if (p + 2 > sz)
				{
					ok = i == n - 1;
					break;
				}
				uint c = raw[p] | raw[p + 1] << 8;
				if (!c)
				{
					p += 2;
					break;
				}
				if (c >= 0xd800 && c < 0xdc00 && p + 4 <= sz)
				{
					const uint c2 = raw[p + 2] | raw[p + 3] << 8;
					if (c2 >= 0xdc00 && c2 < 0xe000)
					{
						c = 0x10000 + ((c - 0xd800) << 10) + (c2 - 0xdc00);
						p += 2;
					}
				}
				if (i)
					o += ubi_loc_utf8 (txt + o, c);
			}
			if (!i)
				o = start;
			else
				txt[o++] = '\n';
		}
		if (ok && o)
		{
			char dest[PATH_MAX], name[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (name, '.');
			if (dot) strcpy (dot, ".txt");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT UBI-LOC:%s (%zu strings) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n - 1, dest, name);
			err = shade_write (arg, dest, basedir, name, (u8 *)txt, (uint)o);
		}
		FREE (key);
		FREE (txt);
	}
	FREE (raw);
	return err;
}

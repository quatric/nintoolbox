// Localised string table ".str" (Freddi Fish: Kelp Seed Mystery `lang_en.str` ... and other Humongous / Atari Wii ports): big
// endian {u32 tag (varies), u32 version (bytes 01 00 00 00), u32 count, count * {u32 offset, u32 byte length}} then UTF-16BE
// strings stored back to back (the first offset is 12 + 8 * count and every string starts where the previous one ended).
// Written as "strings.tsv" ("index<TAB>text", UTF-8, \n \r \t \\ escaped) in the "<file>.d" directory.
static enumError extract_lang_str_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".str"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 20 || sz > 0x4000000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 n = rd_be32 (raw + 8);
	if (n == 0 || n > 100000 || 12 + 8 * (u64)n > sz || rd_be32 (raw + 12) != 12 + 8 * n)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	u64 next = 12 + 8 * (u64)n;
	for (uint i = 0; i < n; i++)
	{
		const u32 o = rd_be32 (raw + 12 + 8 * i), l = rd_be32 (raw + 16 + 8 * i);
		if (o != next || (l & 1) || (u64)o + l > sz)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		next += l;
	}
	if (next != sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	njm_out_t o = { MALLOC (1 << 16), 0, 1 << 16 };
	if (!o.b)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	for (uint i = 0; i < n; i++)
	{
		const u32 off = rd_be32 (raw + 12 + 8 * i), l = rd_be32 (raw + 16 + 8 * i);
		njm_put (&o, "%u\t", i);
		for (u32 k = 0; k + 1 < l; k += 2)
		{
			u32 c = rd_be16 (raw + off + k);
			if (c >= 0xd800 && c < 0xdc00 && k + 3 < l)
			{
				const u32 c2 = rd_be16 (raw + off + k + 2);
				if (c2 >= 0xdc00 && c2 < 0xe000)
				{
					c = 0x10000 + ((c - 0xd800) << 10) + (c2 - 0xdc00);
					k += 2;
				}
			}
			if (!c)
				continue;
			if (c == '\n')
				njm_put (&o, "\\n");
			else if (c == '\r')
				njm_put (&o, "\\r");
			else if (c == '\t')
				njm_put (&o, "\\t");
			else if (c == '\\')
				njm_put (&o, "\\\\");
			else if (c < 0x80)
				njm_put (&o, "%c", (int)c);
			else if (c < 0x800)
				njm_put (&o, "%c%c", (int)(0xc0 | c >> 6), (int)(0x80 | (c & 63)));
			else if (c < 0x10000)
				njm_put (&o, "%c%c%c", (int)(0xe0 | c >> 12), (int)(0x80 | ((c >> 6) & 63)), (int)(0x80 | (c & 63)));
			else
				njm_put (&o, "%c%c%c%c", (int)(0xf0 | c >> 18), (int)(0x80 | ((c >> 12) & 63)), (int)(0x80 | ((c >> 6) & 63)),
					(int)(0x80 | (c & 63)));
		}
		njm_put (&o, "\n");
	}
	enumError err = ERR_OK;
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT LANG-STR:%s (%u strings) -> %s/strings.tsv\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg,
			n, dest);
	if (!testmode)
		err = o.b ? shade_write (arg, dest, basedir, "strings.tsv", (u8 *)o.b, (uint)o.used) : ERR_OUT_OF_MEMORY;
	FREE (o.b);
	FREE (raw);
	return err;
}

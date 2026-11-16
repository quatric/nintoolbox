// Konami / Mahjong Fight Club "kwtp02" texture packs (".kwtp", Wii, big-endian): "kwtp02", a head {..., u32 sprite count at 0x1c, u32 TPL offset at 0x18, ...} followed by a
// table of 0x18 byte sprite records and an ordinary TPL ("00 20 af 30") at the stored offset (0x140 in the fonts). The TPL is written as "<name>.tpl" below "<file>.d/"
// and decoded to PNG by the TPL extractor; the sprite table is not decoded.
static enumError extract_kwtp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".kwtp"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x180 || memcmp (raw, "kwtp02", 6))
		goto done;
	{
		const u32 off = rd_be32 (raw + 0x18);
		if (off < 0x40 || off + 0x20 > sz || rd_be32 (raw + off) != 0x0020af30)
			goto done;
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			*dot = 0;
		strncat (name, ".tpl", sizeof (name) - strlen (name) - 1);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT KWTP:%s -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, name);
		err = ERR_OK;
		if (!testmode)
		{
			err = shade_write (arg, dest, basedir, name, raw + off, (uint)(sz - off));
			if (!err)
			{
				enumError sub = extract_tree_complete (dest, depth + 1);
				if (err < sub)
					err = sub;
			}
		}
	}
done:
	FREE (raw);
	return err;
}

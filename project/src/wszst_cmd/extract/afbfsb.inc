// "HVSIAFB" audio file bank (".AFB"; The Grim Adventures of Billy & Mandy, Wii), big-endian header {8 bytes "HVSIAFB\0" (replaced by random bytes in some files), u32 file size at 0x0c,
// u32 0x14, small descriptor words, zero padding up to 0x800}, followed by one complete FMOD sample bank ("FSB3" ...) that runs to the end of the file. The bank is written as
// "<name>.fsb" and extracted further by the FSB decoder (together with the sibling ".AOS" / ".aob" / ".aod" / ".GON" text descriptors, which need no conversion).
static enumError extract_afbfsb_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".afb"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x900 || rd_be32 (raw + 0x0c) != sz || rd_be32 (raw + 0x10) != 0x14 || memcmp (raw + 0x800, "FSB", 3))
		goto done;
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".fsb");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT AFB:%s -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, name);
		err = ERR_OK;
		if (!testmode)
		{
			err = shade_write (arg, dest, basedir, name, raw + 0x800, (uint)(sz - 0x800));
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

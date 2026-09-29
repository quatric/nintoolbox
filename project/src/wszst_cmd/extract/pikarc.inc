// Littlest Pet Shop's BIGF members include engine RPK resource packs.  Their
// 0x15fb/STRM signature is not the unrelated Retro Studios RPAK format, and
// they have no decoder yet.  Mark them as opaque leaves so XX preserves the
// extracted, named packs instead of handing them to the generic SZS reader,
// which otherwise creates a bogus .d directory and reports invalid data.
static bool is_littlest_pet_shop_rpk (ccp arg)
{
	if (!is_ext (arg, ".rpk"))
		return false;
	u8 head[12];
	FILE *f = fopen (arg, "rb");
	const size_t got = f ? fread (head, 1, sizeof (head), f) : 0;
	if (f)
		fclose (f);
	return got == sizeof (head) && head[0] == 0x15 && head[1] == 0xfb
		&& !memcmp (head + 7, "STRM\0", 5);
}

// Pikmin pairs use a separate .dir index; .arc itself has no magic.
static enumError extract_pikarc_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".arc") || strlen (arg) >= PATH_MAX)
		return ERR_NOTHING_TO_DO;
	char index_path[PATH_MAX];
	snprintf (index_path, sizeof (index_path), "%s", arg);
	strcpy (index_path + strlen (index_path) - 4, ".dir");
	struct stat st;
	if (stat (index_path, &st) || !S_ISREG (st.st_mode) || st.st_size < 8 || st.st_size > UINT_MAX)
		return ERR_NOTHING_TO_DO;
	u8 header[8];
	FILE *index = fopen (index_path, "rb");
	const size_t got = index ? fread (header, 1, sizeof (header), index) : 0;
	if (index)
		fclose (index);
	if (got != sizeof (header) || be32 (header) != st.st_size || !be32 (header + 4))
		return ERR_NOTHING_TO_DO;
	u8 *dir_data = 0, *arc_data = 0;
	size_t dir_size = 0, arc_size = 0;
	enumError err = LoadFileAlloc (index_path, 0, 0, &dir_data, &dir_size, 0, 0, 0, false);
	if (!err)
		err = LoadFileAlloc (arg, 0, 0, &arc_data, &arc_size, 0, 0, 0, false);
	pikarc_entry_t *entries = 0;
	uint count = 0;
	if (!err && (dir_size > UINT_MAX || arc_size > UINT_MAX))
		err = ERR_FILE_TOO_BIG;
	if (!err)
		err = ScanPIKARC (&entries, &count, dir_data, dir_size, arc_data, arc_size);
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	for (uint i = 0; !err && i < count; i++)
	{
		// Preserve UTF-8 names and transcode retail Shift-JIS names for the host.
		if (!valid_utf8 (entries[i].name))
		{
			const size_t len = strlen (entries[i].name);
			char *converted = MALLOC (len * 4 + 1);
			if (!converted)
			{
				err = ERR_OUT_OF_MEMORY;
				break;
			}
			char *dst = converted;
			cucp src = (cucp)entries[i].name, end = src + len;
			while (src < end)
			{
				const int code = ScanShiftJISCharE (&src, end);
				if (code <= 0)
				{
					err = ERR_INVALID_DATA;
					break;
				}
				dst = PrintUTF8Char (dst, code);
			}
			*dst = 0;
			FREE (entries[i].name);
			entries[i].name = converted;
		}
		if (!valid_sarc_path (entries[i].name)
			|| strlen (dest) + 1 + (basedir ? strlen (basedir) : 0) + strlen (entries[i].name)
				>= PATH_MAX)
			err = ERR_INVALID_DATA;
	}
	if (!err && (verbose >= 0 || testmode))
		fprintf (stdlog, "%sEXTRACT PIKARC:%s (%u files) -> %s/\n", testmode ? "WOULD " : "", arg,
			count, dest);
	for (uint i = 0; !err && !testmode && i < count; i++)
	{
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", entries[i].name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && entries[i].size
			&& fwrite (entries[i].data, 1, entries[i].size, F.f) != entries[i].size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing Pikmin member failed: %s\n", path);
		ResetFile (&F, opt_preserve);
	}
	FreePIKARC (entries, count);
	FREE (dir_data);
	FREE (arc_data);
	if (!err && !testmode && (opt_recurse < 0 || depth < (uint)opt_recurse))
		err = extract_tree_complete (dest, depth + 1);
	return err;
}

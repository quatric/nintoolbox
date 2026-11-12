// Extract a NUP container (decrypted Switch / Switch 2 base + update + add-on trees, "NUP\x01").
static enumError extract_nup_file (ccp arg, ccp basedir, uint depth)
{
	FILE *fp = fopen (arg, "rb");
	if (!fp)
		return ERR_NOTHING_TO_DO;
	u8 sb[NUP_SUPERBLOCK_SIZE];
	const bool ok = fread (sb, 1, sizeof (sb), fp) == sizeof (sb) && IsNUP (sb, sizeof (sb));
	fclose (fp);
	if (!ok)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	enumError err = ExtractNUPArchive (arg, basedir, depth);
	if (err == ERR_OK && !testmode)
		return extract_tree_complete (dest, depth + 1);
	return err;
}

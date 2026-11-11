// The romfs root a file lives in: the nearest ancestor folder named "romfs", or the folder that holds
// "System/Resource/<file>". Empty when neither applies.
static void romfs_root_of (char *root, uint root_size, ccp arg)
{
	*root = 0;
	char path[PATH_MAX];
	if (!realpath (arg, path))
		snprintf (path, sizeof (path), "%s", arg);
	for (char *p = path + strlen (path); p > path; p--)
		if (p[-1] == '/' && !strncasecmp (p, "romfs/", 6))
		{
			snprintf (root, root_size, "%.*s", (int)(p - path + 5), path);
			return;
		}
	char *sl = strrchr (path, '/');
	if (sl && sl - path >= 16 && !strncmp (sl - 16, "/System/Resource", 16))
		snprintf (root, root_size, "%.*s", (int)(sl - 16 - path), path);
}

static enumError extract_rstb_manifest (ccp arg)
{
	const bool restbl = is_ext (arg, ".rsizetable");
	if (!is_ext (arg, ".rstb") && !restbl)
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (restbl ? !IsRESTBL (raw, raw_size) : !IsRSTB (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".txt", false);
	else
		snprintf (dest, sizeof (dest), "%s.txt", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE %s:%s -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			restbl ? "RESTBL" : "RSTB", arg, dest);

	if (testmode)
	{
		FREE (raw);
		return ERR_OK;
	}

	char root[PATH_MAX] = "";
	if (restbl)
		romfs_root_of (root, sizeof (root), arg);

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f)
		err = restbl ? DecodeRESTBL_Text (F.f, raw, raw_size, root) : DecodeRSTB_Text (F.f, raw, raw_size);
	ResetFile (&F, opt_preserve);
	FREE (raw);
	return err;
}

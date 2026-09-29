// Locate the .wtp payload sibling for a .wta index. Retail trees split
// pairs across sibling directories (<name>.dat.d/wta vs .../wtp), so
// search same-dir first, then the parent tree (bounded).
static bool wta_find_sibling (const char *wta_path, char *out, size_t out_size)
{
	const char *slash = strrchr (wta_path, '/');
	const char *base = slash ? slash + 1 : wta_path;
	const char *dot = strrchr (base, '.');
	const size_t stem = dot ? (size_t)(dot - base) : strlen (base);
	char dir[PATH_MAX];
	if (slash)
	{
		size_t dl = (size_t)(slash - wta_path);
		if (dl >= sizeof (dir))
			return false;
		memcpy (dir, wta_path, dl);
		dir[dl] = 0;
	}
	else
	{
		dir[0] = '.';
		dir[1] = 0;
	}
	snprintf (out, out_size, "%s/%.*s.wtp", dir, (int)stem, base);
	struct stat st;
	if (!stat (out, &st) && S_ISREG (st.st_mode))
		return true;
	// Parent tree, one level of subdirectories, bounded entry count.
	char parent[PATH_MAX];
	snprintf (parent, sizeof (parent), "%s", dir);
	char *pslash = strrchr (parent, '/');
	if (!pslash)
		return false;
	*pslash = 0;
	DIR *dp = opendir (parent);
	if (!dp)
		return false;
	uint seen = 0;
	bool found = false;
	for (const struct dirent *de; (de = readdir (dp)) && !found;)
	{
		if (++seen > 512)
			break;
		if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, ".."))
			continue;
		char cand[PATH_MAX];
		snprintf (cand, sizeof (cand), "%s/%s", parent, de->d_name);
		if (!stat (cand, &st) && S_ISREG (st.st_mode))
		{
			const char *cb = strrchr (cand, '/') + 1;
			const size_t cbl = strlen (cb);
			if (cbl == stem + 4 && !strncasecmp (cb, base, stem) && !strcasecmp (cb + stem, ".wtp"))
			{
				snprintf (out, out_size, "%s", cand);
				found = true;
			}
		}
		else if (S_ISDIR (st.st_mode))
		{
			DIR *sub = opendir (cand);
			if (!sub)
				continue;
			uint sseen = 0;
			for (const struct dirent *se; (se = readdir (sub)) && !found;)
			{
				if (++sseen > 512)
					break;
				if (strlen (se->d_name) == stem + 4 && !strncasecmp (se->d_name, base, stem)
					&& !strcasecmp (se->d_name + stem, ".wtp"))
				{
					snprintf (out, out_size, "%s/%s", cand, se->d_name);
					struct stat st2;
					if (!stat (out, &st2) && S_ISREG (st2.st_mode))
						found = true;
				}
			}
			closedir (sub);
		}
	}
	closedir (dp);
	return found;
}

static enumError extract_wta_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".wta"))
		return ERR_NOTHING_TO_DO;

	u8 *wta_raw = 0;
	size_t wta_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &wta_raw, &wta_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (wta_size > UINT_MAX || wta_size < 32 || memcmp (wta_raw, "\0BTW", 4))
	{
		FREE (wta_raw);
		return ERR_NOTHING_TO_DO;
	}

	char wtp_path[PATH_MAX];
	u8 *wtp_raw = 0;
	size_t wtp_size = 0;
	if (!wta_find_sibling (arg, wtp_path, sizeof (wtp_path)))
	{
		FREE (wta_raw);
		return ERR_NOTHING_TO_DO;
	}
	err = LoadFileAlloc (wtp_path, 0, 0, &wtp_raw, &wtp_size, 0, 0, 0, false);
	if (err)
	{
		FREE (wta_raw);
		return ERR_NOTHING_TO_DO;
	}
	if (wtp_size > UINT_MAX)
	{
		FREE (wta_raw);
		FREE (wtp_raw);
		return ERR_FILE_TOO_BIG;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = ScanWTA (&entries, &n_entries, wta_raw, (uint)wta_size, wtp_raw, (uint)wtp_size);
	FREE (wta_raw);
	FREE (wtp_raw);
	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "WTA", entries, n_entries);
}

// Heatseeker (Wii) "HS.dt3": a flat archive of 0x200-aligned entries {char name[0x7c] (backslash paths, zero padded), ASCII decimal number at 0x7c, zero padding to 0x200} each followed by its data, which is padded to the next 0x200 boundary where the next entry header starts.
// The decimal number does not match the stored length, so an entry runs to the next header; PNG members are trimmed at IEND and text members (.csv/.xml/.met/.txt/.ini) of trailing zero padding, everything else keeps its padding. Members are written to "<file>.d/<path>".
static bool dt3_is_header (const u8 *h)
{
	if (h[0] < 0x21 || h[0] > 0x7e || h[0x7b])
		return false;
	uint n = 0;
	while (n < 0x7c && h[n])
	{
		if (h[n] < 0x20 || h[n] > 0x7e)
			return false;
		n++;
	}
	if (n < 3)
		return false;
	for (uint i = n; i < 0x7c; i++)
		if (h[i])
			return false;
	uint d = 0;
	while (0x7c + d < 0x90 && h[0x7c + d] >= '0' && h[0x7c + d] <= '9')
		d++;
	if (!d)
		return false;
	for (uint i = 0x7c + d; i < 0x200; i++)
		if (h[i])
			return false;
	return true;
}

static enumError extract_dt3_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".dt3") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u32 *offs = 0;
	if (sz < 0x400 || !dt3_is_header (raw))
		goto done;
	{
		uint n = 0, cap = 1024;
		offs = MALLOC (cap * sizeof (u32));
		for (size_t o = 0; o + 0x200 <= sz && sz <= 0xffffffffu; o += 0x200)
			if (dt3_is_header (raw + o))
			{
				if (n == cap)
				{
					u32 *no = MALLOC (cap * 2 * sizeof (u32));
					memcpy (no, offs, n * sizeof (u32));
					FREE (offs);
					offs = no;
					cap *= 2;
				}
				offs[n++] = (u32)o;
			}
		if (n < 2)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT DT3:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *h = raw + offs[i];
			char name[0x80];
			uint k = 0;
			for (; k < 0x7c && h[k]; k++)
				name[k] = h[k] == '\\' ? '/' : h[k];
			name[k] = 0;
			if (name[0] == '/' || strstr (name, "..") || strstr (name, "//"))
				continue;
			const size_t st = (size_t)offs[i] + 0x200, en = i + 1 < n ? offs[i + 1] : sz;
			size_t len = en - st;
			if (k > 4 && !strcasecmp (name + k - 4, ".png") && len > 20)
			{
				size_t q = 8;
				while (q + 12 <= len)
				{
					const u32 cl = rd_be32 (raw + st + q);
					const bool iend = !memcmp (raw + st + q + 4, "IEND", 4);
					q += 12 + (size_t)cl;
					if (iend || q > len)
						break;
				}
				if (q <= len)
					len = q;
			}
			else if (k > 4 && (!strcasecmp (name + k - 4, ".csv") || !strcasecmp (name + k - 4, ".xml") || !strcasecmp (name + k - 4, ".met") || !strcasecmp (name + k - 4, ".txt") || !strcasecmp (name + k - 4, ".ini")))
				while (len && !raw[st + len - 1])
					len--;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s", dest, name);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && len && fwrite (raw + st, 1, len, F.f) != len)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", len, path);
			ResetFile (&F, opt_preserve);
		}
		if (!err && !testmode)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (offs);
	FREE (raw);
	return err;
}

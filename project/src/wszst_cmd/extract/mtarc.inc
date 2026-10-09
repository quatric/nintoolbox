// Capcom MT Framework "ARC" archives, big-endian Wii variant (Sengoku Basara 3 ".arc"):
// "\0CRA", u16 version (7), u16 count, then count * 0x50-byte records
// {char name[0x40] (backslash separated, no extension), u32 type hash, u32 compressed size,
//  u32 (size << 3 | flags), u32 offset}. Members are zlib streams (compressed size ==
// 0 or equal to the size means stored). The extension comes from the decoded magic
// (reversed four-character code such as "\0XET" = .tex), else from the type hash.
static enumError extract_mtarc_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 8 ? rd_be16 (raw + 6) : 0;
	if (sz < 8 || memcmp (raw, "\0CRA", 4) || !count || rd_be16 (raw + 4) > 0x20 || 8 + 0x50 * (size_t)count > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT MT-ARC:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);
	enumError err = ERR_OK;
	uint done = 0;
	char (*used)[PATH_MAX] = CALLOC (count, PATH_MAX);
	if (!used)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	for (uint i = 0; i < count && !err; i++)
	{
		const u8 *e = raw + 8 + 0x50 * (size_t)i;
		const u32 hash = rd_be32 (e + 0x40), csize = rd_be32 (e + 0x44), osize = rd_be32 (e + 0x48) >> 3;
		const size_t off = rd_be32 (e + 0x4c);
		if (off > sz || csize > sz - off || osize > ((size_t)512 << 20))
			continue;
		u8 *out = MALLOC (osize ? osize : 1);
		if (!out)
		{
			err = ERR_OUT_OF_MEMORY;
			break;
		}
		size_t n = osize;
		if (csize == osize)
			memcpy (out, raw + off, osize);
		else
		{
			uLongf dsz = osize;
			if (uncompress (out, &dsz, raw + off, csize) != Z_OK)
			{
				FREE (out);
				continue;
			}
			n = dsz;
		}
		char name[PATH_MAX], ext[16];
		const char *p = (const char *)e;
		size_t o = 0;
		for (size_t k = 0; k < 0x40 && p[k] && o < sizeof (name) - 24; k++)
			name[o++] = p[k] == '\\' ? '/' : p[k];
		name[o] = 0;
		snprintf (ext, sizeof (ext), "%08x", hash);
		if (n >= 4)
		{
			char rev[5] = {0};
			uint m = 0;
			for (int k = 3; k >= 0; k--)
				if (out[k])
					rev[m++] = (char)(out[k] >= 'A' && out[k] <= 'Z' ? out[k] + 32 : out[k]);
			bool ok = m >= 2;
			for (uint k = 0; k < m; k++)
				if (!((rev[k] >= 'a' && rev[k] <= 'z') || (rev[k] >= '0' && rev[k] <= '9')))
					ok = false;
			if (ok && !out[0] && rev[0] >= 'a')
				snprintf (ext, sizeof (ext), "%s", rev);
		}
		char stem[PATH_MAX];
		snprintf (stem, sizeof (stem), "%s", name);
		snprintf (name + o, sizeof (name) - o, ".%s", ext);
		for (uint k = 0; k < i; k++)
			if (!strcmp (used[k], name))
			{
				snprintf (name, sizeof (name), "%.*s_%u.%s", (int)(sizeof (name) - 40), stem, i, ext);
				break;
			}
		snprintf (used[i], PATH_MAX, "%s", name);
		// refuse path traversal in member names
		if (strstr (name, "..") || name[0] == '/')
		{
			FREE (out);
			continue;
		}
		err = shade_write (arg, dest, basedir, name, out, (uint)n);
		FREE (out);
		done++;
	}
	FREE (used);
	FREE (raw);
	if (!err && !done)
		return ERR_NOTHING_TO_DO;
	return err;
}

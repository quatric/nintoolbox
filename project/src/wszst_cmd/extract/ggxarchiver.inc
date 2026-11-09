// Cave/Milestone "GGXArchiver" container (Radirgy Noa, Wii; the sc_*.bin / stage*.bin / player*.bin files; the original
// GGXArchiver.exe and its .ini ship on the disc): "GGXArchiver1.00\0", u32 LE count (twice), pad to 0x20, count x 32 byte
// NUL padded names (Windows "\\" separators become "/"), count x 24 byte records {u32 index, 1, u32 decompressed size, u32 compressed size, 1, u32 offset
// (from the end of the record table)}, then the members. Each member is Gage byte-pair-encoding blocks (256 entry pair
// table in the usual run-length-coded form, u16 BE block size, then the packed bytes) whose output is exactly the loose
// file format of the disc (Illvelo-style "XIBG" PVR textures, "LTJN"/"MCJN"/"MDMN" Ninja files); PVR members are also
// written as <name>.png (see segapvr_png.inc).
static bool ggx_bpe (const u8 *src, size_t n, u8 *out, size_t osz, size_t *olen)
{
	size_t p = 0, o = 0;
	while (p < n)
	{
		u8 left[256], right[256];
		for (uint i = 0; i < 256; i++)
			left[i] = (u8)i, right[i] = 0;
		uint c = 0;
		while (c < 256)
		{
			if (p >= n)
				return false;
			uint count = src[p++];
			if (count > 127)
			{
				c += count - 127;
				count = 0;
			}
			if (c >= 256)
				break;
			for (uint i = 0; i <= count && c < 256; i++, c++)
			{
				if (p >= n)
					return false;
				left[c] = src[p++];
				if (c != left[c])
				{
					if (p >= n)
						return false;
					right[c] = src[p++];
				}
			}
		}
		if (p + 2 > n)
			break;
		size_t bsz = ((size_t)src[p] << 8) | src[p + 1];
		p += 2;
		u8 st[512];
		uint sp = 0;
		for (;;)
		{
			u8 ch;
			if (sp)
				ch = st[--sp];
			else
			{
				if (!bsz)
					break;
				if (p >= n)
					return false;
				bsz--;
				ch = src[p++];
			}
			if (ch == left[ch])
			{
				if (o >= osz)
					return false;
				out[o++] = ch;
			}
			else
			{
				if (sp + 2 > sizeof (st))
					return false;
				st[sp++] = right[ch];
				st[sp++] = left[ch];
			}
		}
	}
	*olen = o;
	return true;
}

static enumError extract_ggxarchiver_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x40 || memcmp (raw, "GGXArchiver1.00", 15))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 n = rd_le32 (raw + 0x10);
	const size_t rec = 0x20 + 32 * (size_t)n, base = rec + 24 * (size_t)n;
	if (!n || n > 100000 || base > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT GGX:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n,
			dest);
	enumError err = ERR_OK;
	for (uint i = 0; !err && i < n && !testmode; i++)
	{
		char name[33];
		memcpy (name, raw + 0x20 + 32 * i, 32);
		name[32] = 0;
		for (char *c = name; *c; c++)
			if (*c == '\\')
				*c = '/';
		const u8 *r = raw + rec + 24 * i;
		const u32 dsz = rd_le32 (r + 8), csz = rd_le32 (r + 12), off = rd_le32 (r + 20);
		if (!dsz || dsz > 0x10000000 || base + off > sz || csz > sz - base - off)
			continue;
		u8 *out = MALLOC (dsz);
		size_t olen = 0;
		if (!out)
		{
			err = ERR_OUT_OF_MEMORY;
			break;
		}
		if (!ggx_bpe (raw + base + off, csz, out, dsz, &olen) || olen != dsz)
		{
			FREE (out);
			continue;
		}
		if (!name[0] || !valid_sarc_path (name))
			snprintf (name, sizeof (name), "entry_%04u.bin", i);
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (!err && F.f && fwrite (out, 1, olen, F.f) != olen)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", olen, path);
		const enumError ce = ResetFile (&F, opt_preserve);
		if (ce > err)
			err = ce;
		if (!err && olen >= 0x20 && !memcmp (out, "XIBG", 4))
		{
			char png[PATH_MAX];
			snprintf (png, sizeof (png), "%s", path);
			char *dot = strrchr (png, '.');
			if (dot && !strchr (dot, '/'))
				*dot = 0;
			snprintf (png + strlen (png), sizeof (png) - strlen (png), ".png");
			segapvr_png (out, olen, png);
		}
		FREE (out);
	}
	FREE (raw);
	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

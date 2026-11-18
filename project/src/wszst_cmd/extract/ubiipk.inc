// Ubisoft UbiArt bundles "*.ipk" (Just Dance, version 5 / Wii): big-endian {0x50EC12BA, u32 version, u32 platform, u32 base offset, u32 file count, ...}; the entries start at 0x2c and each is
// {u32 id, u32 offset count, u32 size, u32 compressed size, u64 timestamp, u64 offsets[], u32 name length, name, u32 path length, path, u32 checksum}; the table ends at the base offset and data offsets are absolute.
// Members are streamed to "<file>.d/<path><name>". Stored (compressed size 0) members are written as they are; members with a compressed size are not zlib and are written raw as "<name>.cmp".
static enumError extract_ubiipk_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".ipk"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *tab = 0, *buf = 0;
	u8 hdr[0x2c];
	if (fread (hdr, 1, sizeof (hdr), f) != sizeof (hdr) || rd_be32 (hdr) != 0x50ec12ba || fseeko (f, 0, SEEK_END))
		goto done;
	{
		const u64 fsz = (u64)ftello (f);
		const u32 base = rd_be32 (hdr + 12);
		const uint n = rd_be32 (hdr + 16);
		if (!n || n > 1000000 || base < 0x40 || base > fsz || base > 0x10000000)
			goto done;
		tab = MALLOC (base);
		if (fseeko (f, 0, SEEK_SET) || fread (tab, 1, base, f) != base)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT UBIIPK:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		size_t p = 0x2c, cap = 0;
		for (uint i = 0; i < n && !err; i++)
		{
			if (p + 24 > base)
				break;
			const uint no = rd_be32 (tab + p + 4);
			if (no < 1 || no > 8 || p + 24 + 8 * (size_t)no + 4 > base)
				break;
			const u32 size = rd_be32 (tab + p + 8), comp = rd_be32 (tab + p + 12);
			const u64 off = ((u64)rd_be32 (tab + p + 24) << 32) | rd_be32 (tab + p + 28);
			size_t q = p + 24 + 8 * (size_t)no;
			const u32 nl = rd_be32 (tab + q);
			q += 4;
			if (nl > 255 || q + nl + 4 > base)
				break;
			char name[300], path[300];
			memcpy (name, tab + q, nl);
			name[nl] = 0;
			q += nl;
			const u32 pl = rd_be32 (tab + q);
			q += 4;
			if (pl > 255 || q + pl + 4 > base)
				break;
			memcpy (path, tab + q, pl);
			path[pl] = 0;
			q += pl + 4;
			p = q;
			if (strstr (name, "..") || strstr (path, "..") || name[0] == '/' || path[0] == '/')
				continue;
			const u32 len = comp ? comp : size;
			if (!len || off + len > fsz || testmode)
				continue;
			if (len > cap)
			{
				FREE (buf);
				cap = len;
				buf = MALLOC (cap);
			}
			if (fseeko (f, (off_t)off, SEEK_SET) || fread (buf, 1, len, f) != len)
				continue;
			char out[PATH_MAX];
			snprintf (out, sizeof (out), "%s/%s%s%s", dest, path, name, comp ? ".cmp" : "");
			File_t F;
			err = CreateFileOpt (&F, true, out, false, arg);
			if (F.f && fwrite (buf, 1, len, F.f) != len)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", len, out);
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
	FREE (tab);
	FREE (buf);
	fclose (f);
	return err;
}

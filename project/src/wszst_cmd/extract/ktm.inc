// Cursed Mountain (Wii) ".dat" asset bundles, big-endian: bytes d2 f1 7e 00, char name[0x1c], u32 0, u32 entry
// count @0x24, u32 1 @0x28, u32 entry table size @0x2c (entry count * entry size, padded to 0x20), u32 entry size @0x30 (0x30 or 0x3c), u32 directory blob
// size @0x34. The blob (NUL-separated, backslash-separated paths such as "wii\hud\textures\") starts at 0x40, the
// entry table right behind it and the member data behind the table. An entry is {u32 hash, u32 offset of its
// directory string inside the blob, u32 data offset, u32 stored size, u32 unpacked size, u32 zlib flag,
// char name[]}. Members are zlib streams when the flag is set (stored size and unpacked size then differ or not)
// and are written to "<file>.d/<directory>/<name>". The bundles are up to 100 MB, so members are streamed.
static enumError extract_ktm_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".dat") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	FILE *in = fopen (arg, "rb");
	if (!in)
		return ERR_NOTHING_TO_DO;
	u8 hdr[0x40];
	if (!read_abe_at (in, 0, hdr, sizeof (hdr)) || memcmp (hdr, "\xd2\xf1\x7e\x00", 4) || fseek (in, 0, SEEK_END))
	{
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}
	const long end = ftell (in);
	const u64 fsz = end < 0 ? 0 : (u64)end;
	const uint count = rd_be32 (hdr + 0x24), tsz = rd_be32 (hdr + 0x2c), es = rd_be32 (hdr + 0x30), dsz = rd_be32 (hdr + 0x34);
	if (!count || count > 0x100000 || es < 0x1c || es > 0x100 || (u64)es * count > tsz || (u64)es * count + 0x20 <= tsz || dsz > 0x100000
		|| 0x40 + (u64)dsz + tsz > fsz)
	{
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}
	u8 *blob = MALLOC ((size_t)dsz + tsz + 1);
	if (!blob || !read_abe_at (in, 0x40, blob, (size_t)dsz + tsz))
	{
		FREE (blob);
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}
	blob[dsz + tsz] = 0;
	const u8 *tab = blob + dsz;
	const u64 base = 0x40 + (u64)dsz + tsz;
	bool ok = true;
	for (uint i = 0; ok && i < count; i++)
	{
		const u8 *e = tab + (size_t)es * i;
		const u64 off = base + rd_be32 (e + 8), cs = rd_be32 (e + 12);
		ok = rd_be32 (e + 4) < dsz && off + cs <= fsz && memchr (e + 24, 0, es - 24) && e[24]
			&& valid_sarc_path ((ccp)e + 24);
	}
	if (!ok)
	{
		FREE (blob);
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT KTM:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg,
			count, dest);
	enumError err = ERR_OK;
	for (uint i = 0; i < count && !err && !testmode; i++)
	{
		const u8 *e = tab + (size_t)es * i;
		const u64 off = base + rd_be32 (e + 8);
		const u32 cs = rd_be32 (e + 12), us = rd_be32 (e + 16);
		char sub[PATH_MAX];
		snprintf (sub, sizeof (sub), "%s", (ccp)blob + rd_be32 (e + 4));
		for (char *q = sub; *q; q++)
			if (*q == '\\')
				*q = '/';
		const size_t sl = strlen (sub);
		if (sl && sub[sl - 1] != '/' && sl + 1 < sizeof (sub))
			strcpy (sub + sl, "/");
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, sub, (ccp)e + 24);
		if ((sl && !valid_sarc_path (sub)) || strlen (path) >= PATH_MAX - 1)
		{
			err = ERR_INVALID_DATA;
			break;
		}
		if (!rd_be32 (e + 20))
		{
			err = write_abe_member (in, off, cs, path, arg);
			continue;
		}
		u8 *zb = MALLOC (cs ? cs : 1), *out = MALLOC (us ? us : 1);
		uLongf dl = us;
		if (!zb || !out || !read_abe_at (in, off, zb, cs))
			err = ERR_READ_FAILED;
		else if (uncompress (out, &dl, zb, cs) != Z_OK || dl != us)
			err = ERR_INVALID_DATA;
		else
		{
			File_t of;
			err = CreateFileOpt (&of, true, path, false, arg);
			if (!err && of.f && us && fwrite (out, 1, us, of.f) != us)
				err = ERR_WRITE_FAILED;
			if (of.f)
				ResetFile (&of, opt_preserve);
		}
		FREE (zb);
		FREE (out);
	}
	FREE (blob);
	fclose (in);
	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

// Climax "data.arc" / "igc.arc" archives (Silent Hill: Shattered Memories, Wii; not the Nintendo U8 ".arc").
// Little-endian header {u32 tag, u32 entry count, u32 first data offset (0x8000), u32 0}, then count 16-byte
// entries {u32 name hash, u32 offset, u32 stored size, u32 unpacked size} with the first member at the data
// offset and every offset 0x800 aligned. A member is a zlib stream when its unpacked size is non-zero and it
// starts with 0x78, otherwise it is stored as is. The names are hashes, so members are written as
// "<hash>.<ext>" with an extension taken from the content (jpg, afx, else bin). The archives are over a
// gigabyte, so extraction streams from the file.
static enumError extract_climaxarc_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".arc") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	FILE *in = fopen (arg, "rb");
	if (!in)
		return ERR_NOTHING_TO_DO;
	u8 hdr[16];
	if (!read_abe_at (in, 0, hdr, sizeof (hdr)) || !memcmp (hdr, "U\xaa" "8-", 4))
	{
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = get_le32 (hdr + 4), first = get_le32 (hdr + 8);
	if (!count || count > 0x100000 || first < 16 + 16 * (u64)count || first & 0x7ff || get_le32 (hdr + 12)
		|| fseek (in, 0, SEEK_END))
	{
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}
	const long end = ftell (in);
	const u64 fsz = end < 0 ? 0 : (u64)end;
	u8 *tab = MALLOC (16 * (size_t)count);
	if (!tab || !read_abe_at (in, 16, tab, 16 * (size_t)count) || get_le32 (tab + 4) != first)
	{
		FREE (tab);
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}
	for (uint i = 0; i < count; i++)
	{
		const u64 off = get_le32 (tab + 16 * i + 4), cs = get_le32 (tab + 16 * i + 8);
		if ((off & 0x7ff) || off + cs > fsz)
		{
			FREE (tab);
			fclose (in);
			return ERR_NOTHING_TO_DO;
		}
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CLIMAX ARC:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);
	enumError err = ERR_OK;
	for (uint i = 0; i < count && !err && !testmode; i++)
	{
		const u8 *e = tab + 16 * i;
		const u32 hash = get_le32 (e), off = get_le32 (e + 4), cs = get_le32 (e + 8), us = get_le32 (e + 12);
		u8 *buf = MALLOC (cs ? cs : 1);
		if (!buf || !read_abe_at (in, off, buf, cs))
		{
			FREE (buf);
			err = ERR_READ_FAILED;
			break;
		}
		u8 *out = buf;
		size_t osz = cs;
		u8 *unz = 0;
		if (us && cs && buf[0] == 0x78)
		{
			unz = MALLOC (us);
			uLongf dl = us;
			if (!unz || uncompress (unz, &dl, buf, cs) != Z_OK || dl != us)
			{
				FREE (unz);
				FREE (buf);
				err = ERR_INVALID_DATA;
				break;
			}
			out = unz;
			osz = us;
		}
		ccp ext = osz >= 4 && out[0] == 0xff && out[1] == 0xd8 ? "jpg" : osz >= 4 && !memcmp (out, "AFX_", 4) ? "afx" : "bin";
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%08x.%s", dest, hash, ext);
		File_t of;
		err = CreateFileOpt (&of, true, path, false, arg);
		if (!err && of.f && osz && fwrite (out, 1, osz, of.f) != osz)
			err = ERR_WRITE_FAILED;
		if (!err && of.f)
			ResetFile (&of, opt_preserve);
		FREE (unz);
		FREE (buf);
	}
	FREE (tab);
	fclose (in);
	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

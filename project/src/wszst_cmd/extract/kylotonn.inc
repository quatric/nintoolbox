// Kylotonn "FVRT" data archive (My Body Coach, My Body Coach 2: Fitness & Dance, Cocoto Festival; "BodyCoaching.dat",
// "cocoto.dat"), little-endian: "FVRT", u32 hash, "VERS" {u32 12, u32 0x1900}, ... "FILE" at 0x14, u32 payload size
// (the member data, starting at 0x1c, stored back to back; the payload ends with the 8-byte "SARR" index header), the "SARR" index. Chunks are {tag, u32 LE size}; "VERS" and "NAME"
// come first. The "TEXT" chunk holds {u32 count, u32 0} then count x 32-byte entries {-1, -1, u32 hash, -1,
// u32 (flags), u32 size, u32 offset (from 0x1c), u32 (name pool offset)}; the "TBUF" chunk (at TEXT + its size) is the NUL separated name pool
// (entry i has name i). Members are streamed to disk (the archive is ~490 MB).
static enumError extract_kylotonn_file (ccp arg, ccp basedir, uint depth)
{
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 hd[0x1c];
	if (fread (hd, 1, sizeof (hd), f) != sizeof (hd) || memcmp (hd, "FVRT", 4) || memcmp (hd + 0x14, "FILE", 4)
		|| fseeko (f, 0, SEEK_END))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	const u64 fsize = (u64)ftello (f);
	const u64 base = 0x1c, end = base + rd_le32 (hd + 0x18) - 8; // the "SARR" index starts 8 bytes before the end of the FILE payload
	if (end + 0x40 > fsize || fsize - end > 0x4000000 || fseeko (f, (off_t)end, SEEK_SET))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	const size_t tl = (size_t)(fsize - end);
	u8 *tail = MALLOC (tl);
	if (!tail || fread (tail, 1, tl, f) != tl || memcmp (tail, "SARR", 4))
	{
		FREE (tail);
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	size_t t = 0;
	for (size_t o = 8; o + 8 <= 0x80 && o + 8 <= tl; o++)
		if (!memcmp (tail + o, "TEXT", 4))
		{
			t = o;
			break;
		}
	if (!t || t + 16 > tl)
	{
		FREE (tail);
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	const u32 tsz = rd_le32 (tail + t + 4), n = rd_le32 (tail + t + 8);
	const size_t ents = t + 16, tb = t + tsz;
	if (!n || n > 1000000 || ents + 32 * (size_t)n > tl || tb + 8 > tl || memcmp (tail + tb, "TBUF", 4))
	{
		FREE (tail);
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	const size_t pool = tb + 8, poolend = (size_t)tl;
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT FVRT:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n,
			dest);
	enumError err = ERR_OK;
	size_t np = pool;
	u8 *buf = MALLOC (1 << 20);
	if (!buf)
		err = ERR_OUT_OF_MEMORY;
	for (uint i = 0; !err && i < n && np < poolend; i++)
	{
		ccp nm = (ccp)tail + np;
		const size_t nl = strnlen (nm, poolend - np);
		np += nl + 1;
		const u8 *e = tail + ents + 32 * (size_t)i;
		const u32 size = rd_le32 (e + 20), off = rd_le32 (e + 24);
		if (base + off + (u64)size > end || testmode)
			continue;
		char name[PATH_MAX];
		if (nl && valid_sarc_path (nm))
			snprintf (name, sizeof (name), "%s", nm);
		else
			snprintf (name, sizeof (name), "%04u_%08x.bin", i, rd_le32 (e + 8));
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (!err && F.f && size)
		{
			if (fseeko (f, (off_t)(base + off), SEEK_SET))
				err = ERR_READ_FAILED;
			for (u32 left = size; !err && left;)
			{
				const size_t r = fread (buf, 1, left < (1 << 20) ? left : (1 << 20), f);
				if (!r || fwrite (buf, 1, r, F.f) != r)
				{
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
					break;
				}
				left -= (u32)r;
			}
		}
		const enumError ce = ResetFile (&F, opt_preserve);
		if (ce > err)
			err = ce;
	}
	FREE (buf);
	FREE (tail);
	fclose (f);
	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

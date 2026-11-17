// Disaster: Day of Crisis (Wii) "map/*.ppz" / "*.epz" / "*.saz" zlib containers: {"SZ00", u32 LE uncompressed size, 0x18 zero bytes} followed by one zlib stream at 0x20. The inflated data is a "\x01ARC" archive
// (0x20-byte header, possibly repeated per member, then records {u32 BE size, u32 flags, char name[24]}): a ".red" record is a folder (its children follow, records inside it count their size including the 0x20-byte header,
// and a record of size 0 carrying the folder name ends it); every other record is a file whose data follow the header. Files are written to "<file>.d/<folder>/<name>" and extracted recursively; data
// that is not an ARC archive is written to "<file>.d/<name>.bin".
static uint sz00_walk (ccp arg, ccp dest, ccp basedir, const u8 *b, size_t len, size_t p, int depth, int top, enumError *err)
{
	uint count = 0;
	while (p + 8 <= len && !*err && depth < 8)
	{
		if (b[p] == 1 && !memcmp (b + p + 1, "ARC", 3) && p + 0x20 <= len)
		{
			p += 0x20;
			continue;
		}
		if (p + 0x20 > len)
			break;
		const u32 size = rd_be32 (b + p);
		char name[25];
		memcpy (name, b + p + 8, 24);
		name[24] = 0;
		if (!size && !top)
			return count;
		if (!name[0] || strchr (name, '/') || strchr (name, '\\') || !strcmp (name, "..") || (!size && top))
			break;
		const size_t nl = strlen (name);
		if (nl > 4 && !strcmp (name + nl - 4, ".red"))
		{
			char sub[PATH_MAX];
			snprintf (sub, sizeof (sub), "%s/%s", dest, name);
			const size_t start = p + 0x20;
			size_t q = start;
			// children until the zero-size footer record
			uint n2 = 0;
			{
				size_t r = start;
				while (r + 0x20 <= len)
				{
					const u32 sz2 = rd_be32 (b + r);
					if (!sz2)
					{
						q = r + 0x20;
						break;
					}
					if (sz2 < 0x20 || r + sz2 > len)
					{
						q = len;
						break;
					}
					r += sz2;
					q = r;
				}
			}
			n2 = sz00_walk (arg, sub, basedir, b, len, start, depth + 1, 0, err);
			count += n2;
			p = q;
			continue;
		}
		const size_t dl = top ? size : (size >= 0x20 ? size - 0x20 : 0);
		if (p + 0x20 + dl > len)
			break;
		count++;
		if (!testmode)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
			File_t F;
			*err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && dl && fwrite (b + p + 0x20, 1, dl, F.f) != dl)
				*err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)dl, path);
			ResetFile (&F, opt_preserve);
		}
		p += top ? 0x20 + dl : size;
	}
	return count;
}

static enumError extract_sz00zlib_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".ppz") && !is_ext (arg, ".epz") && !is_ext (arg, ".saz"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x24 || memcmp (raw, "SZ00", 4))
		goto done;
	{
		uint osz = rd_le32 (raw + 4);
		size_t used = 0;
		u8 *out = osz && osz < 0x10000000 ? fst_inflate (raw + 0x20, sz - 0x20, &osz, &used) : 0;
		if (!out)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SZ00:%s (%u bytes) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, osz, dest);
		err = ERR_OK;
		uint nf = 0;
		if (osz > 0x40 && out[0] == 1 && !memcmp (out + 1, "ARC", 3))
			nf = sz00_walk (arg, dest, basedir, out, osz, 0x20, 0, 0, &err);
		if (nf)
		{
			if (!err && !testmode)
			{
				const enumError sub = extract_tree_complete (dest, depth + 1);
				if (err < sub)
					err = sub;
			}
		}
		else if (!testmode)
		{
			ccp b = strrchr (arg, '/');
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s.bin", dest, basedir ? basedir : "", b ? b + 1 : arg);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (out, 1, osz, F.f) != osz)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", osz, path);
			ResetFile (&F, opt_preserve);
			if (!err)
			{
				const enumError sub = extract_tree_complete (dest, depth + 1);
				if (err < sub)
					err = sub;
			}
		}
		FREE (out);
	}
done:
	FREE (raw);
	return err;
}

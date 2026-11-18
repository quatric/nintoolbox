// Monster Jam: Path of Destruction "MJ.bft" BigFile index. Sibling files: "MJ.bff" (be32 count, be32 0, count be32 name lengths/offsets, then NUL-separated paths in index order), "MJ.bfd" (data) and "MJ.bfh" (name hashes, unused here). "MJ.bft" is be32 count followed by count {be32 offset, be32 size}; if bit 31 of the offset is set the low bits point to {be32 n, n * be32 offset} inside "MJ.bft": the file is stored n times (DVD seek optimisation) and the first copy is used. Files are written to "<file>.d/<path>".
static bool mjbf_read_all (ccp path, u8 **out, size_t *size)
{
	FILE *f = fopen (path, "rb");
	if (!f)
		return false;
	bool ok = false;
	if (!fseeko (f, 0, SEEK_END))
	{
		const long sz = ftell (f);
		if (sz > 0 && sz < 0x4000000 && !fseeko (f, 0, SEEK_SET))
		{
			u8 *d = MALLOC ((size_t)sz);
			if (fread (d, 1, (size_t)sz, f) == (size_t)sz)
			{
				*out = d;
				*size = (size_t)sz;
				ok = true;
			}
			else
				FREE (d);
		}
	}
	fclose (f);
	return ok;
}

static enumError extract_mjbf_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".bft"))
		return ERR_NOTHING_TO_DO;
	const size_t alen = strlen (arg);
	char sib[PATH_MAX];
	if (alen < 5 || alen + 1 > sizeof (sib))
		return ERR_NOTHING_TO_DO;
	u8 *t = 0, *nm = 0;
	size_t tsz = 0, nsz = 0;
	FILE *fd = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (!mjbf_read_all (arg, &t, &tsz))
		goto done;
	memcpy (sib, arg, alen + 1);
	memcpy (sib + alen - 4, ".bff", 4);
	if (!mjbf_read_all (sib, &nm, &nsz))
		goto done;
	memcpy (sib + alen - 4, ".bfd", 4);
	fd = fopen (sib, "rb");
	if (!fd || tsz < 8 || nsz < 8)
		goto done;
	{
		const uint n = be32 (t);
		if (!n || n > 1000000 || 4 + 8 * (size_t)n > tsz || be32 (nm) != n || 8 + 4 * (size_t)n > nsz)
			goto done;
		const u8 *name = nm + 8 + 4 * (size_t)n;
		const u8 *nend = nm + nsz;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT MJBF:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *z = memchr (name, 0, nend - name);
			if (!z)
				break;
			const size_t nl = z - name;
			const u32 off = be32 (t + 4 + 8 * i), size = be32 (t + 8 + 8 * i);
			ccp nmz = (ccp)name;
			name = z + 1;
			if (!nl || nl > 400 || size > 0x20000000 || nmz[0] == '/' || strstr (nmz, ".."))
				continue;
			u8 *buf = MALLOC (size ? size : 1);
			bool ok = true;
			u32 doff = off;
			if (off >> 31)
			{
				const u32 p = off & 0x7fffffff;
				const u32 cn = p + 4 <= tsz ? be32 (t + p) : 0;
				if (!cn || p + 4 + 4 * (size_t)cn > tsz)
					ok = false;
				else
					doff = be32 (t + p + 4);
			}
			if (ok)
				ok = !fseeko (fd, (off_t)doff, SEEK_SET) && fread (buf, 1, size, fd) == size;
			if (ok)
			{
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%s", dest, nmz);
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (F.f && size && fwrite (buf, 1, size, F.f) != size)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
				ResetFile (&F, opt_preserve);
			}
			FREE (buf);
		}
	}
done:
	if (fd)
		fclose (fd);
	FREE (t);
	FREE (nm);
	return err;
}

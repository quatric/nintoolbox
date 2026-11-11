// Unix archives written to "<file>.d/": ar (".a", ".ar", ".deb": "!<arch>\n", 60 byte member headers, GNU "//" long name table and "/NNN" references, BSD
// "#1/N" names) and cpio (".cpio": "newc" 070701 / 070702 and portable "odc" 070707; regular files only, "TRAILER!!!" ends the archive).
static bool unixarch_safe (const char *name)
{
	if (!*name)
		return false;
	for (const char *p = name; *p; p++)
		if ((p == name || p[-1] == '/') && p[0] == '.' && p[1] == '.' && (p[2] == '/' || !p[2]))
			return false;
	return true;
}

static uint unixarch_num (const u8 *p, uint n, uint base)
{
	uint v = 0;
	for (uint i = 0; i < n; i++)
	{
		int c = p[i];
		if (c >= '0' && c <= '9') c -= '0';
		else if (base == 16 && c >= 'a' && c <= 'f') c = c - 'a' + 10;
		else if (base == 16 && c >= 'A' && c <= 'F') c = c - 'A' + 10;
		else if (c == ' ' && base == 10) break;
		else return 0xffffffffu;
		v = v * base + c;
	}
	return v;
}

static enumError extract_unixarch_file (ccp arg, ccp basedir, uint depth)
{
	const bool ar = is_ext (arg, ".a") || is_ext (arg, ".ar") || is_ext (arg, ".deb"), cpio = is_ext (arg, ".cpio");
	if ((!ar && !cpio) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	uint count = 0;
	if (ar && sz >= 8 && !memcmp (raw, "!<arch>\n", 8))
	{
		const char *lnames = 0;
		size_t lnames_n = 0;
		err = ERR_OK;
		for (size_t p = 8; p + 60 <= sz && err == ERR_OK;)
		{
			const u8 *h = raw + p;
			if (h[58] != '`' || h[59] != '\n')
				break;
			const uint size = unixarch_num (h + 48, 10, 10);
			if (size == 0xffffffffu)
				break;
			char name[PATH_MAX];
			size_t dpos = p + 60, dlen = size;
			memcpy (name, h, 16);
			name[16] = 0;
			for (int i = 15; i >= 0 && (name[i] == ' ' || name[i] == '/'); i--)
				name[i] = 0;
			if (!memcmp (h, "//", 2) && h[2] == ' ')
			{
				if (dpos + dlen <= sz) { lnames = (const char *)raw + dpos; lnames_n = dlen; }
				name[0] = 0;
			}
			else if (h[0] == '/' && h[1] == ' ')
				name[0] = 0; // symbol table
			else if (h[0] == '/' && h[1] >= '0' && h[1] <= '9' && lnames)
			{
				const uint off = unixarch_num (h + 1, 15, 10);
				if (off < lnames_n)
				{
					size_t k = 0;
					while (off + k < lnames_n && lnames[off + k] != '/' && lnames[off + k] != '\n' && k < sizeof (name) - 1)
						{ name[k] = lnames[off + k]; k++; }
					name[k] = 0;
				}
			}
			else if (!memcmp (h, "#1/", 3))
			{
				const uint nl = unixarch_num (h + 3, 13, 10);
				if (nl < sizeof (name) && nl <= dlen && dpos + nl <= sz)
				{
					memcpy (name, raw + dpos, nl);
					name[nl] = 0;
					dpos += nl; dlen -= nl;
				}
			}
			if (*name && strncmp (name, "__.SYMDEF", 9) && dpos + dlen <= sz && unixarch_safe (name))
			{
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sEXTRACT AR:%s -> %s/%s%s (%zu bytes)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, basedir ? basedir : "", name, dlen);
				err = shade_write (arg, dest, basedir, name, raw + dpos, (uint)dlen);
				count++;
			}
			p += 60 + size + (size & 1);
		}
	}
	else if (cpio && sz >= 110)
	{
		err = ERR_OK;
		for (size_t p = 0; p + 76 <= sz && err == ERR_OK;)
		{
			uint mode, fsize, nlen;
			size_t hdr;
			if (!memcmp (raw + p, "070701", 6) || !memcmp (raw + p, "070702", 6))
			{
				if (p + 110 > sz) break;
				mode = unixarch_num (raw + p + 14, 8, 16);
				fsize = unixarch_num (raw + p + 54, 8, 16);
				nlen = unixarch_num (raw + p + 94, 8, 16);
				hdr = 110;
			}
			else if (!memcmp (raw + p, "070707", 6))
			{
				mode = unixarch_num (raw + p + 18, 6, 8);
				nlen = unixarch_num (raw + p + 59, 6, 8);
				fsize = unixarch_num (raw + p + 65, 11, 8);
				hdr = 76;
			}
			else
				break;
			if (mode == 0xffffffffu || fsize == 0xffffffffu || nlen == 0xffffffffu || !nlen || nlen > 4096 || p + hdr + nlen > sz)
				break;
			const bool newc = hdr == 110;
			const char *name = (const char *)raw + p + hdr;
			size_t dpos = p + hdr + nlen;
			if (newc)
				dpos = (dpos + 3) & ~(size_t)3;
			if (!strcmp (name, "TRAILER!!!"))
				break;
			if (dpos + fsize > sz)
				break;
			char nm[PATH_MAX];
			snprintf (nm, sizeof (nm), "%s", name[0] == '.' && name[1] == '/' ? name + 2 : name);
			if ((mode & 0170000) == 0100000 && unixarch_safe (nm))
			{
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sEXTRACT CPIO:%s -> %s/%s%s (%u bytes)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, basedir ? basedir : "", nm, fsize);
				err = shade_write (arg, dest, basedir, nm, raw + dpos, fsize);
				count++;
			}
			p = dpos + fsize;
			if (newc)
				p = (p + 3) & ~(size_t)3;
		}
	}
	if (!count && err == ERR_OK)
		err = ERR_NOTHING_TO_DO;
	FREE (raw);
	return err;
}

// "ZAP\0" archive (Thrillville: Off the Rails, Atari / Frontier; little endian): {"ZAP\0", u32 alignment (0x8000), u32 entry count, u32 ?}, then from 0x10
// `count` entries {"\r\n", u32 size, u16 name length + 1, name NUL}; names ending in "/" open a directory, ".." closes one (size 0). File data starts at
// the alignment, members follow in entry order each rounded up to the alignment (the file ends exactly there). Members are streamed to "<file>.d/".
static enumError extract_zap_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".zap"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *hd = 0, *buf = 0;
	u8 hh[16];
	if (fread (hh, 1, 16, f) != 16 || memcmp (hh, "ZAP", 4))
		goto done;
	{
		const u32 al = rd_le32 (hh + 4), n = rd_le32 (hh + 8);
		if (al < 0x800 || al > 0x100000 || (al & (al - 1)) || !n || n > 100000)
			goto done;
		hd = MALLOC (al);
		memcpy (hd, hh, 16);
		if (fread (hd + 16, 1, al - 16, f) != al - 16)
			goto done;
		fseeko (f, 0, SEEK_END);
		const u64 fsz = (u64)ftello (f);
		u64 p = 0x10, o = al;
		for (uint i = 0; i < n; i++)
		{
			if (p + 8 > al || hd[p] != '\r' || hd[p + 1] != '\n')
				goto done;
			const u32 sz = rd_le32 (hd + p + 2), nl = hd[p + 6] | hd[p + 7] << 8;
			if (!nl || p + 8 + nl > al || hd[p + 7 + nl])
				goto done;
			o += ((u64)sz + al - 1) / al * al;
			p += 8 + nl;
		}
		if (o != fsz)
			goto done;
		char dest[PATH_MAX], dir[PATH_MAX] = "";
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT ZAP:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		p = 0x10, o = al;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u32 sz = rd_le32 (hd + p + 2), nl = hd[p + 6] | hd[p + 7] << 8;
			char name[PATH_MAX];
			snprintf (name, sizeof (name), "%s", (ccp)hd + p + 8);
			p += 8 + nl;
			const size_t dl = strlen (dir);
			if (!strcmp (name, ".."))
			{
				char *s = dir + (dl ? dl - 1 : 0);
				if (dl)
					*s = 0;
				char *sl = strrchr (dir, '/');
				dir[sl ? sl - dir + 1 : 0] = 0;
				continue;
			}
			for (char *q = name; *q; q++)
				if (*q == '\\' || *q == ':')
					*q = '/';
			const size_t ln = strlen (name);
			if (ln && name[ln - 1] == '/')
			{
				if (!strstr (name, "..") && dl + ln < sizeof (dir) - 2 && name[0] != '/')
					strcat (dir, name);
				continue;
			}
			char full[PATH_MAX];
			snprintf (full, sizeof (full), "%s%s", dir, name);
			const char *safe = full;
			while (*safe == '/')
				safe++;
			if (!*safe || strstr (safe, "../"))
			{
				o += ((u64)sz + al - 1) / al * al;
				continue;
			}
			if (sz > 0x40000000)
			{
				err = ERR_WRONG_FILE_TYPE;
				break;
			}
			FREE (buf);
			buf = sz ? MALLOC (sz) : 0;
			if (sz && (fseeko (f, (off_t)o, SEEK_SET) || fread (buf, 1, sz, f) != sz))
			{
				err = ERR_READ_FAILED;
				break;
			}
			err = shade_write (arg, dest, basedir, safe, buf, sz);
			o += ((u64)sz + al - 1) / al * al;
		}
	}
done:
	fclose (f);
	FREE (hd);
	FREE (buf);
	return err;
}

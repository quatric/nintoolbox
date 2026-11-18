// Unreal Engine UMOD packages (Open Season "wiigame.umd" and the Xbox/PC "*.umd"): the file ends with a 20 byte footer {le32 0x9fe3c5a3, le32 directory offset, le32 directory size, le32 version, le32 crc}. The directory is a compact index entry count followed by entries {compact index name length (with NUL), name, le32 offset, le32 size, le32 flags, le32 filetime lo, le32 filetime hi}. Only entries with a unique 16-byte aligned non-zero offset inside the file are real members ("compiled" console entries share one dummy offset and are skipped, see the ResHax thread on Open Season UMD). Members are written to "<file>.d/<name>" with '\' turned into '/'.
static bool umod_ci (const u8 *d, size_t n, size_t *p, u32 *v)
{
	if (*p >= n)
		return false;
	u8 b = d[(*p)++];
	u32 r = b & 0x3f;
	if (b & 0x40)
	{
		uint s = 6;
		for (;;)
		{
			if (*p >= n || s > 28)
				return false;
			const u8 c = d[(*p)++];
			r |= (u32)(c & 0x7f) << s;
			s += 7;
			if (!(c & 0x80))
				break;
		}
	}
	*v = r;
	return true;
}

// Open Season "*.lin" / "*.liv" linear packages: a chain of chunks {le32 inflated size, le32 compressed size, zlib stream} that ends exactly at the end of the file. The inflated data (an Unreal package) is written to "<file>.pkg".
static enumError extract_linz_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".lin") && !is_ext (arg, ".liv"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *d = 0, *out = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const long sz = ftell (f);
		if (sz < 16 || sz > 0x30000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)sz);
		if (fread (d, 1, (size_t)sz, f) != (size_t)sz)
			goto done;
		size_t p = 0, total = 0;
		while (p + 8 <= (size_t)sz)
		{
			const u32 us = rd_le32 (d + p), cs = rd_le32 (d + p + 4);
			if (!us || !cs || us > 0x10000000 || p + 8 + cs > (size_t)sz || d[p + 8] != 0x78)
				goto done;
			total += us;
			p += 8 + cs;
			if (total > 0x40000000)
				goto done;
		}
		if (p != (size_t)sz)
			goto done;
		out = MALLOC (total);
		size_t o = 0;
		for (p = 0; p < (size_t)sz;)
		{
			const u32 us = rd_le32 (d + p), cs = rd_le32 (d + p + 4);
			uLongf dl = us;
			if (uncompress (out + o, &dl, d + p + 8, cs) != Z_OK || dl != us)
				goto done;
			o += us;
			p += 8 + cs;
		}
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s.pkg", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECOMPRESS LINZ:%s -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, path);
		err = ERR_OK;
		if (!testmode)
		{
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (out, 1, total, F.f) != total)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", total, path);
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (d);
	FREE (out);
	fclose (f);
	return err;
}

typedef struct umod_ent_t
{
	char name[256];
	u32 off, size;
} umod_ent_t;

static enumError extract_umod_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *dir = 0, *buf = 0;
	umod_ent_t *ents = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const off_t fsz = ftello (f);
		u8 foot[20];
		if (fsz < 0x100 || fseeko (f, fsz - 20, SEEK_SET) || fread (foot, 1, 20, f) != 20 || rd_le32 (foot) != 0x9fe3c5a3)
			goto done;
		const u32 doff = rd_le32 (foot + 4);
		if (doff < 0x10 || (off_t)doff >= fsz - 20 || fsz - 20 - (off_t)doff > 0x4000000)
			goto done;
		const size_t dl = (size_t)(fsz - 20 - doff);
		dir = MALLOC (dl);
		if (fseeko (f, doff, SEEK_SET) || fread (dir, 1, dl, f) != dl)
			goto done;
		size_t p = 0;
		u32 cnt;
		if (!umod_ci (dir, dl, &p, &cnt) || !cnt || cnt > 200000)
			goto done;
		ents = CALLOC (cnt, sizeof (*ents));
		for (uint i = 0; i < cnt; i++)
		{
			u32 ln;
			if (!umod_ci (dir, dl, &p, &ln) || ln < 2 || ln > 255 || p + ln + 20 > dl)
				goto done;
			memcpy (ents[i].name, dir + p, ln);
			ents[i].name[ln] = 0;
			p += ln;
			ents[i].off = rd_le32 (dir + p);
			ents[i].size = rd_le32 (dir + p + 4);
			p += 20;
		}
		// count how often each offset occurs
		uint valid = 0;
		bool *use = CALLOC (cnt, sizeof (bool));
		for (uint i = 0; i < cnt; i++)
		{
			umod_ent_t *e = ents + i;
			if (!e->off || (e->off & 0xf) || !e->size || (u64)e->off + e->size > doff)
				continue;
			uint same = 0;
			for (uint j = 0; j < cnt && same < 2; j++)
				same += ents[j].off == e->off;
			if (same == 1)
			{
				use[i] = true;
				valid++;
			}
		}
		if (!valid)
		{
			FREE (use);
			goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT UMOD:%s (%u of %u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, valid, cnt, dest);
		err = ERR_OK;
		const size_t BS = 1 << 20;
		buf = MALLOC (BS);
		for (uint i = 0; i < cnt && !err && !testmode; i++)
		{
			if (!use[i])
				continue;
			umod_ent_t *e = ents + i;
			char rel[300];
			uint k = 0;
			for (ccp c = e->name; *c && k < sizeof (rel) - 1; c++)
				rel[k++] = *c == '\\' ? '/' : *c;
			rel[k] = 0;
			if (!k || rel[0] == '/' || strstr (rel, ".."))
				snprintf (rel, sizeof (rel), "entry%04u", i);
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s", dest, rel);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f)
			{
				fseeko (f, e->off, SEEK_SET);
				for (u32 left = e->size; left && !err;)
				{
					const size_t n = left < BS ? left : BS;
					if (fread (buf, 1, n, f) != n || fwrite (buf, 1, n, F.f) != n)
						err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Copying %zu bytes failed: %s\n", n, path);
					left -= (u32)n;
				}
			}
			ResetFile (&F, opt_preserve);
			if (!err)
				extract_linz_file (path, 0, 0);
		}
		FREE (use);
	}
done:
	FREE (dir);
	FREE (buf);
	FREE (ents);
	fclose (f);
	return err;
}

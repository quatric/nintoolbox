// Champion Jockey: G1 Jockey & Gallop Racer (Wii) "DATA.UNH" + "DATA.UNB" UnitedData archive: the header "UNTH" {u32 BE magic, u32 count, ...} is followed from 0x10 by 24-byte entries {u32 offset high, u32 offset
// low, u32 hash, u32 0, u32 unpacked size, u32 packed size} into DATA.UNB (zlib when packed < unpacked, otherwise stored). Names come from the "HASH : offset : base : comp(..) : index : name" listing of
// "DATA.UNH.txt" when present (else "<hash>.bin"). Members are written to "DATA.UNH.d/<name>" and extracted recursively.
static enumError extract_unitedata_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".unh"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	char *txt = 0;
	FILE *unb = 0;
	if (sz < 0x30 || memcmp (raw, "UNTH", 4))
		goto done;
	const u32 n = rd_be32 (raw + 4);
	if (!n || n > 1000000 || 0x10 + 24 * (u64)n > sz)
		goto done;
	{
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s", arg);
		const size_t al = strlen (path);
		memcpy (path + al - 1, "B", 1);
		unb = fopen (path, "rb");
		if (!unb)
		{
			memcpy (path + al - 1, "b", 1);
			unb = fopen (path, "rb");
		}
		if (!unb)
			goto done;
		size_t tsz = 0;
		u8 *tdata = 0;
		snprintf (path, sizeof (path), "%s.txt", arg);
		if (!LoadFileAlloc (path, 0, 0, &tdata, &tsz, 0, 0, 0, false) && tdata)
		{
			txt = MALLOC (tsz + 1);
			if (txt)
				memcpy (txt, tdata, tsz), txt[tsz] = 0;
			FREE (tdata);
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT UNITEDATA:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (u32 i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = raw + 0x10 + 24 * (size_t)i;
			const u64 off = (u64)rd_be32 (e) << 32 | rd_be32 (e + 4);
			const u32 hash = rd_be32 (e + 8), base = rd_be32 (e + 16), comp = rd_be32 (e + 20);
			if (!base || comp > base || base > 0x20000000)
				continue;
			u8 *buf = MALLOC (comp ? comp : 1);
			if (!buf)
				break;
			if (fseeko (unb, (off_t)off, SEEK_SET) || fread (buf, 1, comp, unb) != comp)
			{
				FREE (buf);
				continue;
			}
			u8 *out = buf;
			uint osz = comp;
			if (comp < base)
			{
				size_t used = 0;
				osz = base;
				out = fst_inflate (buf, comp, &osz, &used);
				FREE (buf);
				if (!out)
					continue;
			}
			char name[PATH_MAX];
			snprintf (name, sizeof (name), "%08X.bin", hash);
			if (txt)
			{
				char key[16];
				snprintf (key, sizeof (key), "\n%08X :", hash);
				ccp l = strstr (txt, key);
				if (l)
				{
					ccp q = l + 1;
					for (uint k = 0; k < 5 && q; k++)
					{
						q = strchr (q, ':');
						if (q)
							q++;
					}
					while (q && *q == ' ')
						q++;
					if (q && *q)
					{
						size_t k = 0;
						while (q[k] && q[k] != '\r' && q[k] != '\n' && k < sizeof (name) - 1)
							name[k] = q[k], k++;
						name[k] = 0;
					}
				}
			}
			char rel[PATH_MAX];
			snprintf (rel, sizeof (rel), "%s", name);
			for (char *c = rel; *c; c++)
				if (*c == '\\')
					*c = '/';
			ccp r = rel;
			while (*r == '/')
				r++;
			if (*r && !strstr (r, ".."))
			{
				char path2[PATH_MAX];
				snprintf (path2, sizeof (path2), "%s/%s%s", dest, basedir ? basedir : "", r);
				if (!access (path2, F_OK))
					snprintf (path2, sizeof (path2), "%s/%s%s.%u", dest, basedir ? basedir : "", r, i);
				File_t F;
				err = CreateFileOpt (&F, true, path2, false, arg);
				if (F.f && osz && fwrite (out, 1, osz, F.f) != osz)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", osz, path2);
				ResetFile (&F, opt_preserve);
			}
			FREE (out);
		}
		if (!err && !testmode)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	if (unb)
		fclose (unb);
	FREE (txt);
	FREE (raw);
	return err;
}

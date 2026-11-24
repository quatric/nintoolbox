// "FIGA" resource packs ("game.dat" + "game.fat"; Ubisoft Petz DSiWare titles and other games of the Battle of Giants engine, about 33 DS/DSi games; little endian): "FIGA", then
// members laid end to end, each {u32 size, size bytes}; a member is a tagged resource {char tag[4] reversed ("DCEM" = MECD, " PMB" = BMP, "LXET" = TEXL, "TLTP" = PLTT, "HSEM" =
// MESH, ...), u32 size, data}, often with a Nintendo LZ11 stream inside. "game.fat" holds one {u32 name hash, u32 member offset} record per member, sorted by hash; the hash is
// h = h * 33 + c over the lower-case path with h = 0 (found by the +1 / 33 / 1089 steps between hashes of related names and checked against the game's own strings). The paths
// are not stored, so they are recovered by hashing every tail of every printable string in game.dat and in the ROM's ../arm9.bin and ../overlay/* (1405 of 2534 in Petz Hamsterz).
// Members are written as "<file>.d/<path>" (".<tag>" added when the path has no extension) or, unnamed, "<file>.d/unnamed/<hash>.<tag>"; without a game.fat as "<file>.d/NNNN.<tag>".

typedef struct
{
	u32 hash, off;
	char *name;
} figa_ent_t;

static int figa_cmp_hash (const void *a, const void *b)
{
	const u32 x = ((const figa_ent_t *)a)->hash, y = ((const figa_ent_t *)b)->hash;
	return x < y ? -1 : x > y;
}

// Hashes every tail of every printable run (>= 3 bytes) of BUF and names the entries it hits.
static void figa_harvest (const u8 *buf, size_t n, figa_ent_t *e, uint ne)
{
	for (size_t i = 0; i < n;)
	{
		size_t j = i;
		while (j < n && buf[j] >= 0x20 && buf[j] < 0x7f)
			j++;
		if (j - i >= 3 && j - i < 256)
			for (size_t k = i; k + 3 <= j; k++)
			{
				u32 h = 0;
				for (size_t m = k; m < j; m++)
					h = h * 33 + (u8)tolower (buf[m]);
				figa_ent_t key = { .hash = h }, *hit = bsearch (&key, e, ne, sizeof (*e), figa_cmp_hash);
				if (hit && !hit->name)
				{
					hit->name = MALLOC (j - k + 1);
					for (size_t m = k; m < j; m++)
						hit->name[m - k] = (char)tolower (buf[m]);
					hit->name[j - k] = 0;
					if (!valid_sarc_path (hit->name))
						FREE (hit->name), hit->name = 0;
				}
			}
		i = j + 1;
	}
}

static void figa_harvest_file (ccp path, figa_ent_t *e, uint ne)
{
	u8 *b = 0;
	size_t n = 0;
	struct stat st;
	if (stat (path, &st) || !S_ISREG (st.st_mode))
		return;
	if (!LoadFileAlloc (path, 0, 0, &b, &n, 0, 0, 0, false) && b)
		figa_harvest (b, n, e, ne);
	FREE (b);
}

static enumError extract_figa_file (ccp arg, ccp basedir, uint depth)
{
	if (!file_has_magic (arg, "FIGA", 4))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *fat = 0;
	size_t sz = 0, fsz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	// the members must tile the file exactly
	uint nm = 0;
	size_t o = 4;
	while (o + 4 <= sz && rd_le32 (raw + o) <= sz - o - 4 && rd_le32 (raw + o) >= 8)
		o += 4 + rd_le32 (raw + o), nm++;
	if (o != sz || !nm)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	// optional index beside it: same name, ".fat"
	char fp[PATH_MAX];
	snprintf (fp, sizeof (fp), "%s", arg);
	char *dot = strrchr (fp, '.'), *sl = strrchr (fp, '/');
	figa_ent_t *ent = 0;
	if (dot && (!sl || dot > sl))
	{
		strcpy (dot, ".fat");
		if (!LoadFileAlloc (fp, 0, 0, &fat, &fsz, 0, 0, 0, false) && fat && fsz == (size_t)nm * 8)
		{
			ent = CALLOC (nm, sizeof (*ent));
			for (uint i = 0; i < nm; i++)
			{
				ent[i].hash = rd_le32 (fat + 8 * i);
				ent[i].off = rd_le32 (fat + 8 * i + 4);
			}
			qsort (ent, nm, sizeof (*ent), figa_cmp_hash);
		}
	}
	FREE (fat);
	if (ent && !testmode)
	{
		figa_harvest (raw, sz, ent, nm);
		char rp[PATH_MAX];
		{
			const int dl = sl ? (int)(sl - fp) : 1;
			ccp dir = sl ? fp : ".";
			snprintf (rp, sizeof (rp), "%.*s/../arm9.bin", dl, dir);
			figa_harvest_file (rp, ent, nm);
			snprintf (rp, sizeof (rp), "%.*s/../overlay", dl, dir);
			DIR *dh = opendir (rp);
			struct dirent *de;
			while (dh && (de = readdir (dh)))
				if (de->d_name[0] != '.')
				{
					char op[PATH_MAX];
					snprintf (op, sizeof (op), "%s/%s", rp, de->d_name);
					figa_harvest_file (op, ent, nm);
				}
			if (dh)
				closedir (dh);
		}
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	uint named = 0;
	for (uint i = 0; ent && i < nm; i++)
		named += ent[i].name != 0;
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT FIGA:%s (%u members, %u named) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nm, named, dest);
	enumError err = ERR_OK;
	o = 4;
	for (uint i = 0; !err && !testmode && i < nm; i++)
	{
		const size_t s = rd_le32 (raw + o);
		const u8 *p = raw + o + 4;
		char tag[5];
		for (uint k = 0; k < 4; k++)
		{
			const u8 c = p[3 - k];
			tag[k] = isalnum (c) ? (char)tolower (c) : 0;
		}
		tag[4] = 0;
		if (!*tag)
			strcpy (tag, "bin");
		const figa_ent_t *e = 0;
		for (uint k = 0; ent && k < nm && !e; k++) // shortcut: linear, nm is a few thousand
			if (ent[k].off == o)
				e = ent + k;
		char path[PATH_MAX];
		if (e && e->name) // keep a stored extension (".dso", ".dsb"), else add the tag
		{
			ccp base = strrchr (e->name, '/');
			const bool has_ext = strchr (base ? base : e->name, '.') != 0;
			snprintf (path, sizeof (path), "%s/%s%s%s%s", dest, basedir ? basedir : "", e->name, has_ext ? "" : ".", has_ext ? "" : tag);
		}
		else if (e)
			snprintf (path, sizeof (path), "%s/%sunnamed/%08x.%s", dest, basedir ? basedir : "", e->hash, tag);
		else
			snprintf (path, sizeof (path), "%s/%s%04u.%s", dest, basedir ? basedir : "", i, tag);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (p, 1, s, F.f) != s)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", s, path);
		ResetFile (&F, opt_preserve);
		o += 4 + s;
	}
	for (uint i = 0; ent && i < nm; i++)
		FREE (ent[i].name);
	FREE (ent);
	FREE (raw);
	(void)depth;
	return err;
}

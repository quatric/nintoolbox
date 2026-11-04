// "FBND" bundle (Sega Bass Fishing, Uchi Tsuri! Sega Bass Fishing; Wii): "FBND", u32 0x630, then a
// zeroed file table in retail dumps; the members sit at 0x800-aligned offsets starting at 0x800:
//   U8 archives ("U\xaa8-", size = end of the last file in the node table) and "BCV0" resources
//   (a sectioned name index BCV0/STRS/KIND/CROW/EOCK followed by raw payload up to the next member).
// Members are carved by streaming (the file is far above the in-memory limit) into
// "NNNNN_<hex offset>.arc" / ".bcv"; the U8 archives are then extracted recursively.
typedef struct fbnd_member_t
{
	u64 off;
	u64 len;
	bool u8;
} fbnd_member_t;

static enumError extract_fbnd_file (ccp arg, ccp basedir, uint depth)
{
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 hd[8];
	if (fread (hd, 1, 8, f) != 8 || memcmp (hd, "FBND", 4) || rd_be32 (hd + 4) != 0x630
	    || fseeko (f, 0, SEEK_END))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	const u64 size = (u64)ftello (f);
	uint cap = 1024, n = 0;
	fbnd_member_t *mem = MALLOC (cap * sizeof (*mem));
	if (!mem)
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	u64 resume = 0x800;
	for (u64 off = 0x800; off + 0x20 <= size && n < 200000; off += 0x800)
	{
		if (off < resume)
			continue;
		u8 h[0x20];
		if (fseeko (f, (off_t)off, SEEK_SET) || fread (h, 1, 0x20, f) != 0x20)
			break;
		bool u8m = !memcmp (h, "U\xaa" "8-", 4), bcv = !memcmp (h, "BCV0", 4);
		if (!u8m && !bcv)
			continue;
		u64 len = 0;
		if (u8m)
		{
			const u32 ro = rd_be32 (h + 4), dof = rd_be32 (h + 12);
			if (ro != 0x20 || dof < ro + 12 || dof > 0x1000000 || off + dof > size)
				continue;
			u8 *nodes = MALLOC (dof - ro);
			if (!nodes || fseeko (f, (off_t)(off + ro), SEEK_SET) || fread (nodes, 1, dof - ro, f) != dof - ro)
			{
				FREE (nodes);
				break;
			}
			const u32 cnt = rd_be32 (nodes + 8);
			if (cnt < 1 || (u64)cnt * 12 > dof - ro)
			{
				FREE (nodes);
				continue;
			}
			for (u32 i = 0; i < cnt; i++)
				if (!nodes[i * 12])
				{
					const u64 e = (u64)rd_be32 (nodes + i * 12 + 4) + rd_be32 (nodes + i * 12 + 8);
					if (e > len)
						len = e;
				}
			FREE (nodes);
			if (!len || off + len > size)
				continue;
		}
		if (n == cap)
		{
			cap *= 2;
			fbnd_member_t *t = REALLOC (mem, cap * sizeof (*mem));
			if (!t)
				break;
			mem = t;
		}
		mem[n].off = off;
		mem[n].len = len;
		mem[n].u8 = u8m;
		n++;
		if (u8m)
			resume = (off + len + 0x7ff) & ~(u64)0x7ff;
	}
	for (uint i = 0; i < n; i++)
		if (!mem[i].u8)
			mem[i].len = (i + 1 < n ? mem[i + 1].off : size) - mem[i].off;
	if (!n)
	{
		FREE (mem);
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT FBND:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n, dest);
	enumError err = ERR_OK;
	u8 *buf = MALLOC (1 << 20);
	for (uint i = 0; !err && !testmode && i < n && buf; i++)
	{
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%05u_%llx.%s", dest, basedir ? basedir : "", i,
			(unsigned long long)mem[i].off, mem[i].u8 ? "arc" : "bcv");
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && !fseeko (f, (off_t)mem[i].off, SEEK_SET))
			for (u64 left = mem[i].len; left && !err;)
			{
				const size_t k = left < (1 << 20) ? (size_t)left : (1 << 20);
				if (fread (buf, 1, k, f) != k || fwrite (buf, 1, k, F.f) != k)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Copying member failed: %s\n", path);
				left -= k;
			}
		ResetFile (&F, opt_preserve);
	}
	FREE (buf);
	FREE (mem);
	fclose (f);
	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

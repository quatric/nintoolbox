// Cooking / Babysitting Mama (Wii, Office Create) "pack/*.pkb" resource packs, big-endian:
//   8 identical u32 counts (the member count), then count * 0x40-byte records {u32 offset, u32 size, u32 kind (1 = U8 archive, 2 = BRRES), u32 0,
//   char name[48]}; the first payload starts after the table (aligned) and the members follow back to back (with alignment gaps).
// Names may contain a sub folder ("stage/st0009"). Members are written as "<name>.arc" / "<name>.brres" ("~<n>" before the extension for repeated names) and extracted recursively.
static enumError extract_mamapkb_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pkb"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const u32 n = sz >= 32 ? rd_be32 (raw) : 0;
	bool good = n > 0 && n < 0x10000 && 0x20 + (u64)n * 0x40 <= sz;
	for (uint i = 1; good && i < 8; i++)
		if (rd_be32 (raw + 4 * i) != n)
			good = false;
	for (u32 i = 0; good && i < n; i++)
	{
		const u8 *r = raw + 0x20 + (size_t)i * 0x40;
		const u64 o = rd_be32 (r), s = rd_be32 (r + 4);
		const u32 kind = rd_be32 (r + 8);
		if (o < 0x20 + (u64)n * 0x40 || o + s > sz || (kind != 1 && kind != 2) || rd_be32 (r + 12) || !r[16] || r[63])
			good = false;
		for (uint k = 16; good && k < 64 && r[k]; k++)
			if (r[k] < 0x20 || r[k] > 0x7e || r[k] == '\\' || (r[k] == '.' && r[k + 1] == '.') || (r[k] == '/' && (k == 16 || r[k + 1] == '/')))
				good = false;
	}
	if (good)
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT MAMAPKB:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (u32 i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *r = raw + 0x20 + (size_t)i * 0x40;
			const u32 o = rd_be32 (r), s = rd_be32 (r + 4), kind = rd_be32 (r + 8);
			uint dup = 0;
			for (u32 j = 0; j < i; j++)
				if (!strcmp ((ccp)r + 16, (ccp)raw + 0x20 + (size_t)j * 0x40 + 16))
					dup++;
			char path[PATH_MAX];
			if (dup)
				snprintf (path, sizeof (path), "%s/%s%s~%u.%s", dest, basedir ? basedir : "", (ccp)r + 16, dup, kind == 1 ? "arc" : "brres");
			else
				snprintf (path, sizeof (path), "%s/%s%s.%s", dest, basedir ? basedir : "", (ccp)r + 16, kind == 1 ? "arc" : "brres");
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && s && fwrite (raw + o, 1, s, F.f) != s)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
			ResetFile (&F, opt_preserve);
		}
		if (!err && !testmode)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
	FREE (raw);
	return err;
}

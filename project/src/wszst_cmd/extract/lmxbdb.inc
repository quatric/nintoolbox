// Deadly Creatures "*.wii.database" level databases: "LMXB" blocks (big-endian header {"LMXB", 0x03ec0001, 5 counts, id, 0, zlib size} + zlib payload, sometimes followed by size-prefixed Lua 5.1 bytecode) and, after the tables, embedded little-endian FMOD "FSB4" sound banks stored back to back with other data. The FSB4 banks (total size = 0x30 + header size + data size) are carved to "<file>.d/NNNN.fsb" and handed to the FSB handler. The remaining blocks are not decoded.
static enumError extract_lmxbdb_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".database"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *d = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const long sz = ftell (f);
		if (sz < 0x100 || sz > 0x20000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)sz);
		if (fread (d, 1, (size_t)sz, f) != (size_t)sz || memcmp (d, "LMXB", 4))
			goto done;
		// first pass: count banks
		uint cnt = 0;
		for (size_t p = 0; p + 0x30 <= (size_t)sz;)
		{
			const u8 *q = memchr (d + p, 'F', (size_t)sz - 0x30 - p + 1);
			if (!q)
				break;
			p = q - d;
			if (!memcmp (d + p, "FSB4", 4))
			{
				const u64 t = 0x30 + (u64)rd_le32 (d + p + 8) + rd_le32 (d + p + 12);
				if (rd_le32 (d + p + 4) && rd_le32 (d + p + 4) < 20000 && p + t <= (size_t)sz)
				{
					cnt++;
					p += t;
					continue;
				}
			}
			p++;
		}
		if (!cnt)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT LMXBDB:%s (%u FSB4 banks) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, cnt, dest);
		err = ERR_OK;
		uint idx = 0;
		for (size_t p = 0; p + 0x30 <= (size_t)sz && !err && !testmode;)
		{
			const u8 *q = memchr (d + p, 'F', (size_t)sz - 0x30 - p + 1);
			if (!q)
				break;
			p = q - d;
			if (!memcmp (d + p, "FSB4", 4))
			{
				const u64 t = 0x30 + (u64)rd_le32 (d + p + 8) + rd_le32 (d + p + 12);
				if (rd_le32 (d + p + 4) && rd_le32 (d + p + 4) < 20000 && p + t <= (size_t)sz)
				{
					char path[PATH_MAX];
					snprintf (path, sizeof (path), "%s/%04u.fsb", dest, idx++);
					File_t F;
					err = CreateFileOpt (&F, true, path, false, arg);
					if (F.f && fwrite (d + p, 1, (size_t)t, F.f) != (size_t)t)
						err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", (size_t)t, path);
					ResetFile (&F, opt_preserve);
					if (!err)
					{
						const enumError sub = extract_one_file (path, 0, depth + 1);
						if (sub != ERR_NOTHING_TO_DO && err < sub)
							err = sub;
					}
					p += t;
					continue;
				}
			}
			p++;
		}
	}
done:
	FREE (d);
	fclose (f);
	return err;
}

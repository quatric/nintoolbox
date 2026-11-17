// Capcom "MARC" archive (Biohazard: The Darkside Chronicles / Resident Evil: The Darkside Chronicles, Wii; "Data/bg/*.marc"): big-endian {u32 0x2d38aa55, u32 0x20, u32 ?, u32 0x1000 alignment, ..., u32 count at
// 0x28} then 12-byte entries {u32 name offset, u32 file offset, u32 size} from 0x2c, followed by NUL-terminated names (offsets relative to the end of the entry table; the stored count includes one more
// slot than there are valid entries, and the name of the first member points one byte into its string). Members ("*.cenv", "*.clmt", "*.mczm", "*.2mc", ...) are written to "<file>.d/<name>"; members without
// a usable name get "NNNN.<magic>".
static enumError extract_marcpack_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".marc"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0x60 || rd_be32 (raw) != 0x55aa382d || rd_be32 (raw + 4) != 0x20)
		goto done;
	{
		const u32 count = rd_be32 (raw + 0x28);
		if (!count || count > 20000 || 0x2c + (u64)count * 12 > sz)
			goto done;
		const size_t base = 0x2c + (size_t)count * 12;
		uint n = 0;
		for (uint i = 0; i < count; i++)
		{
			const u8 *e = raw + 0x2c + (size_t)i * 12;
			if ((u64)rd_be32 (e + 4) + rd_be32 (e + 8) <= sz && rd_be32 (e + 4) >= 0x20)
				n++;
		}
		if (!n)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT MARC:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		char (*used)[160] = CALLOC (count, 160); // names already written; duplicates get an index prefix
		for (uint i = 0; i < count && !err && !testmode && used; i++)
		{
			const u8 *e = raw + 0x2c + (size_t)i * 12;
			const u32 no = rd_be32 (e), off = rd_be32 (e + 4), size = rd_be32 (e + 8);
			if ((u64)off + size > sz || off < 0x20)
				continue;
			char name[160];
			name[0] = 0;
			if (base + no < sz)
			{
				const char *s = (const char *)raw + base + no;
				const size_t room = sz - base - no;
				size_t l = 0;
				while (l < room && l < 120 && s[l] && (uchar)s[l] >= 0x20 && (uchar)s[l] < 0x7f && s[l] != '/' && s[l] != '\\')
					l++;
				if (l >= 3 && l < room && !s[l] && memchr (s, '.', l))
					snprintf (name, sizeof (name), "%.*s", (int)l, s);
			}
			if (!name[0])
			{
				const u8 *m = raw + off;
				snprintf (name, sizeof (name), "%04u.%c%c%c%c", i, size > 0 && m[0] > 0x20 && m[0] < 0x7f ? m[0] : '_', size > 1 && m[1] > 0x20 && m[1] < 0x7f ? m[1] : '_',
					size > 2 && m[2] > 0x20 && m[2] < 0x7f ? m[2] : '_', size > 3 && m[3] > 0x20 && m[3] < 0x7f ? m[3] : '_');
			}
			for (uint j = 0; j < i; j++)
				if (!strcmp (used[j], name))
				{
					char tmp[160];
					snprintf (tmp, sizeof (tmp), "%04u_%.150s", i, name);
					snprintf (name, sizeof (name), "%s", tmp);
					break;
				}
			snprintf (used[i], 160, "%s", name);
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && size && fwrite (raw + off, 1, size, F.f) != size)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
			ResetFile (&F, opt_preserve);
		}
		FREE (used);
	}
done:
	FREE (raw);
	return err;
}

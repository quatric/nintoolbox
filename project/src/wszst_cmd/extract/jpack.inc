// Coresoft "JPack" .pak (Fantasy Aquarium World "loaddata.pak" / "fishdata.pak", other Coresoft/Destineer
// Wii titles). Big-endian: u32 1, u32 count, 0x18-byte copyright string "(C)2008 Coresoft, Inc.", then count
// 20-byte records {u64 name hash (low 32 bits = the hash listed in the "jpack_<pak>.log" build log), u32 flags
// (alignment << 16 | 0x20), u32 stored size, u32 offset}. Members are zlib streams (stored when the stream
// does not start with a zlib header); names come from "jpack_<pak>.log" ("Hash: xxxxxxxx;  File: a\b.wtx; ...")
// when that file sits next to the archive, otherwise "<hash>.bin".
static enumError extract_jpack_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pak"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x40 || sz > 0x7fffffff)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = rd_be32 (raw + 4);
	if (rd_be32 (raw) != 1 || memcmp (raw + 8, "(C)", 3) || !count || count > 100000 || 0x20 + 20ull * count > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// optional build log with the real names
	char logpath[PATH_MAX];
	ccp slash = strrchr (arg, '/');
	snprintf (logpath, sizeof (logpath), "%.*sjpack_%s", slash ? (int)(slash + 1 - arg) : 0, arg, slash ? slash + 1 : arg);
	char *dot = strrchr (logpath, '.');
	if (dot)
		strcpy (dot, ".log");
	u8 *log = 0;
	size_t log_sz = 0;
	if (LoadFileAlloc (logpath, 0, 0, &log, &log_sz, 0, 0, 0, false))
		log = 0;

	nintendo_sarc_entry_t *entries = CALLOC (count, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		FREE (log);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *r = raw + 0x20 + 20 * (size_t)i;
		const u32 hash = rd_be32 (r + 4), size = rd_be32 (r + 12), off = rd_be32 (r + 16);
		if (off > sz || size > sz - off || !size)
			continue;
		u8 *body = 0;
		size_t blen = 0;
		if (size >= 2 && raw[off] == 0x78 && ((raw[off] << 8 | raw[off + 1]) % 31) == 0)
		{
			size_t cap = (size_t)size * 4 + 4096;
			for (;;)
			{
				body = REALLOC (body, cap);
				if (!body)
					break;
				uLongf dl = cap;
				const int zr = uncompress (body, &dl, raw + off, size);
				if (zr == Z_OK)
				{
					blen = dl;
					break;
				}
				if (zr != Z_BUF_ERROR || cap > ((size_t)1 << 30))
				{
					FREE (body);
					body = 0;
					break;
				}
				cap *= 2;
			}
		}
		else
		{
			body = MALLOC (size);
			if (body)
			{
				memcpy (body, raw + off, size);
				blen = size;
			}
		}
		if (!body)
			continue;
		char name[PATH_MAX];
		snprintf (name, sizeof (name), "%08x.bin", hash);
		if (log)
		{
			char key[24];
			snprintf (key, sizeof (key), "Hash: %08x;  File: ", hash);
			const size_t kl = strlen (key);
			for (size_t k = 0; k + kl < log_sz; k++)
				if (!memcmp (log + k, key, kl))
				{
					size_t e = k + kl, m = 0;
					while (e < log_sz && log[e] != ';' && log[e] != '\n' && m + 1 < sizeof (name))
					{
						const char c = (char)log[e++];
						name[m++] = c == '\\' ? '/' : c;
					}
					name[m] = 0;
					break;
				}
		}
		if (!OwnedEntryAdd (entries, n, name, body, (uint)blen))
		{
			FREE (body);
			break;
		}
		FREE (body);
		n++;
	}
	FREE (raw);
	FREE (log);
	if (!n)
	{
		FREE (entries);
		return ERR_NOTHING_TO_DO;
	}
	return write_owned_entries (arg, basedir, depth, "JPACK", entries, n);
}

// Junction Point / Gamebryo ".pak" resource packs (Disney Epic Mickey, Wii; big endian, magic bytes
// "20 4b 41 50"): header {magic, u32 2, 0, u32 0x20, u32 table size, 0, 0, 0}; 24-byte entries from 0x20
// {u32 file name offset, u32 size, u32 compressed size, u32 compressed size rounded to 0x20, u32 directory
// name offset, char type[4] or 0}, then a u32 and the NUL-separated name pool (names are "<directory>/<file>") and, at 0x20 + table size, the members back to back (each padded to
// 0x20). Members whose size differs from their compressed size are zlib streams. The entry count is found
// by summing the padded sizes until they fill the file.
static enumError extract_jppak_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pak"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x60 || rd_be32 (raw) != 0x204b4150 || rd_be32 (raw + 4) != 2 || rd_be32 (raw + 0xc) != 0x20)
		goto bad;
	{
		const size_t data = 0x20 + (size_t)rd_be32 (raw + 0x10);
		if (data >= sz || data + 0x20 > sz + 0x20)
			goto bad;
		uint n = 0;
		size_t total = 0;
		while (total < sz - data && 0x20 + 24 * (size_t)(n + 1) <= data)
			total += rd_be32 (raw + 0x20 + 24 * (size_t)n + 12), n++;
		if (!n || total != sz - data)
			goto bad;
		const size_t pool = 0x20 + 24 * (size_t)n + 4; // a u32 (pool size) precedes the names
		nintendo_sarc_entry_t *entries = CALLOC (n, sizeof (*entries));
		if (!entries)
		{
			FREE (raw);
			return ERR_OUT_OF_MEMORY;
		}
		uint cnt = 0;
		size_t pos = data;
		bool ok = true;
		for (uint i = 0; i < n && ok; i++)
		{
			const u8 *e = raw + 0x20 + 24 * (size_t)i;
			const size_t noff = rd_be32 (e), usz = rd_be32 (e + 4), csz = rd_be32 (e + 8), pad = rd_be32 (e + 12);
			if (pool + noff >= data || csz > pad || pos + csz > sz)
			{
				ok = false;
				break;
			}
			const size_t doff = rd_be32 (e + 16);
			if (doff >= data - pool)
			{
				ok = false;
				break;
			}
			char name[PATH_MAX];
			snprintf (name, sizeof (name), "%.200s/%.200s", (ccp)raw + pool + doff, (ccp)raw + pool + noff);
			for (char *p = name; *p; p++)
				if (*p == '\\' || (u8)*p < 0x20 || (*p == '.' && p[1] == '.'))
					*p = '_';
			char *nm = name;
			while (*nm == '/')
				nm++;
			if (!*nm)
				snprintf (name, sizeof (name), "entry%04u", i), nm = name;
			u8 *out = 0;
			const u8 *src = raw + pos;
			size_t osz = csz;
			if (usz != csz)
			{
				uLongf dsz = usz;
				out = MALLOC (usz ? usz : 1);
				if (!out || uncompress (out, &dsz, raw + pos, csz) != Z_OK || dsz != usz)
				{
					FREE (out);
					ok = false;
					break;
				}
				src = out;
				osz = usz;
			}
			ok = OwnedEntryAdd (entries, cnt, nm, src, (uint)osz);
			FREE (out);
			if (ok)
				cnt++;
			pos += pad;
		}
		enumError err = ERR_NOTHING_TO_DO;
		if (ok && cnt)
			err = write_owned_entries (arg, basedir, depth, "JP-PAK", entries, cnt);
		else
			FREE (entries);
		FREE (raw);
		return err;
	}
bad:
	FREE (raw);
	return ERR_NOTHING_TO_DO;
}

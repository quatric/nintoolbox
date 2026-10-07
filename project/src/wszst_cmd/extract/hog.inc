// "HOG" archives (Ultimate Band, Disney Hannah Montana; all little-endian):
//   u16 version = 1, u16 = 2, u32 header_size = 0x20, u32 0, u32 hash,
//   u32 n_entries, u32 names_size, u32 pad;
//   n_entries * {u32 name_offset (absolute), u32 data_offset (absolute),
//                u32 size, u32 name_hash}; NUL-terminated names with '\' paths.
// Members are stored uncompressed.

// EA Sports (Tiger Woods PGA Tour) ".hog": big-endian chunk stream
// {tag, u32 size incl. header, ...}: CTRL, FILL (padding) and SHOC chunks.
// A SHOC is {u32 0, u32 0, tag, ...}: SHDR {u32 ver, char type[4], u32 id,
// u32 length, ...} opens a resource; the Zdat ({u32 len, zlib bytes}) and SDAT
// ({u32 len, raw bytes}) chunks that follow up to the next SHDR carry its
// data. All Zdat parts of one resource form a single zlib stream. Members are
// written as "<type>_<id>.bin".
static enumError extract_hog_shoc (ccp arg, ccp basedir, uint depth, u8 *raw, size_t raw_size)
{
	nintendo_sarc_entry_t *entries = 0;
	uint n = 0, cap = 0;
	u8 *z = 0, *rawbuf = 0;
	size_t zn = 0, rn = 0, zc = 0, rc = 0;
	char type[8] = "";
	uint id = 0;
	bool open = false;
	enumError err = ERR_OK;

	for (size_t p = 0; p <= raw_size && !err;)
	{
		const bool tail = p + 8 > raw_size;
		const size_t sz = tail ? 0 : rd_be32 (raw + p + 4);
		const bool shoc = !tail && !memcmp (raw + p, "SHOC", 4);
		const bool end = tail || p + sz > raw_size || sz < 8;
		const u8 *t = raw + p + 16;
		const bool hdr = !end && shoc && sz >= 36 && !memcmp (t, "SHDR", 4);
		if (open && (end || hdr))
		{
			// finish the open resource
			u8 *data = rawbuf;
			size_t dsz = rn;
			u8 *tmp = 0;
			if (zn)
			{
				z_stream zs;
				memset (&zs, 0, sizeof (zs));
				size_t cap_out = 1 << 16, got = 0;
				tmp = MALLOC (cap_out);
				if (tmp && inflateInit (&zs) == Z_OK)
				{
					zs.next_in = z;
					zs.avail_in = (uInt)zn;
					int r;
					do
					{
						if (got == cap_out)
						{
							u8 *nt = REALLOC (tmp, cap_out *= 2);
							if (!nt)
								break;
							tmp = nt;
						}
						zs.next_out = tmp + got;
						zs.avail_out = (uInt)(cap_out - got);
						r = inflate (&zs, Z_NO_FLUSH);
						got = cap_out - zs.avail_out;
					} while (r == Z_OK);
					inflateEnd (&zs);
					data = tmp;
					dsz = got;
				}
			}
			if (data && dsz)
			{
				if (n == cap)
				{
					nintendo_sarc_entry_t *ne = REALLOC (entries, (cap = cap ? cap * 2 : 64) * sizeof (*ne));
					if (!ne)
						err = ERR_OUT_OF_MEMORY;
					else
						entries = ne;
				}
				char name[64];
				char *ty = type;
				snprintf (name, sizeof (name), "%s_%u.bin", ty, id);
				if (!err && OwnedEntryAdd (entries, n, name, data, (uint)dsz))
					n++;
			}
			FREE (tmp);
			zn = rn = 0;
			open = false;
		}
		if (end)
			break;
		if (hdr)
		{
			memcpy (type, t + 8, 4);
			type[4] = 0;
			for (int k = 3; k >= 0 && type[k] == ' '; k--)
				type[k] = 0;
			for (int k = 0; k < 4; k++)
				if (type[k] && (type[k] < 0x21 || type[k] > 0x7e || type[k] == '/'))
					type[k] = '_';
			id = rd_be32 (t + 12);
			open = true;
		}
		else if (open && shoc && sz >= 24)
		{
			const bool zd = !memcmp (t, "Zdat", 4), sd = !memcmp (t, "SDAT", 4);
			size_t len = rd_be32 (t + 4);
			if ((zd || sd) && len <= sz - 24)
			{
				u8 **buf = zd ? &z : &rawbuf;
				size_t *used = zd ? &zn : &rn, *capb = zd ? &zc : &rc;
				if (*used + len > *capb)
				{
					u8 *nb = REALLOC (*buf, *capb = (*used + len) * 2 + 64);
					if (!nb)
						err = ERR_OUT_OF_MEMORY;
					else
						*buf = nb;
				}
				if (!err)
				{
					memcpy (*buf + *used, t + 8, len);
					*used += len;
				}
			}
		}
		p += sz;
	}
	FREE (z);
	FREE (rawbuf);
	if (!err && n)
		err = write_owned_entries (arg, basedir, depth, "HOG", entries, n);
	else
	{
		if (entries)
			ResetOwnedEntries (entries, n), FREE (entries);
		if (!err)
			err = ERR_NOTHING_TO_DO;
	}
	FREE (raw);
	return err;
}

static enumError extract_hog_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".hog"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > 0x30 && raw_size <= UINT_MAX && !memcmp (raw, "CTRL", 4))
		return extract_hog_shoc (arg, basedir, depth, raw, raw_size);
	if (raw_size < 0x30 || raw_size > UINT_MAX || raw[0] != 1 || raw[1] || raw[2] != 2 || raw[3]
		|| rd_le32 (raw + 4) != 0x20)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = rd_le32 (raw + 0x10);
	if (!count || count > 100000 || 0x20 + 16ull * count > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = CALLOC (count, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = raw + 0x20 + 16 * (size_t)i;
		const u32 no = rd_le32 (e), off = rd_le32 (e + 4), sz = rd_le32 (e + 8);
		if (no >= raw_size || (u64)off + sz > raw_size || !memchr (raw + no, 0, raw_size - no))
			continue;
		char name[260];
		snprintf (name, sizeof (name), "%s", (ccp)raw + no);
		for (char *c = name; *c; c++)
			if (*c == '\\')
				*c = '/';
		if (!OwnedNameOk (name))
			snprintf (name, sizeof (name), "%04u.bin", i);
		if (!OwnedEntryAdd (entries, n, name, raw + off, sz))
			break;
		n++;
	}
	if (!n)
	{
		FREE (entries);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	err = write_owned_entries (arg, basedir, depth, "HOG", entries, n);
	FREE (raw);
	return err;
}

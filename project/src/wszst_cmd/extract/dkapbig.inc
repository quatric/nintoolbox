// "BIGFILEn.TOC" + "BIGFILEn.DAT" (Totally Spies! Totally Party, Wii; Ubisoft "vdb_NGC" repository).
// TOC (little-endian): u32 count, u32 1, then the first record without its leading word:
//   {u32 count, u32 version=1, u32 type_hash, u32 size}, zero padding up to 0x24 bytes. Then (count - 1)
//   36-byte records: {u32 group, u32 id, u32 type_hash, u32 size, u32 offset x4 (equal), u32 flag}.
//   The very first member is id 1 at offset 0 with the size from the head; the rest follow back to back.
//   A trailing 0xAAAAAAAA fill pads the TOC. One member (the biggest "name" blob, starting {n, 1,
//   {u32 id, u8 len, path[len]}...}) maps ids to the original "D:\TSPIES\PROD\GAME\VDB_REPOSITORY\..." paths.
// Members are written under their repository path (flattened, prefixed by id). "DKAP" members are LZO1X packed
// ("DKAP", u32 LE unpacked size, stream at +8) and are unpacked; "_NGCBM01" textures get ".ngcbm", "_NGCSD02"
// streams ".ngcsd" so the payload type stays visible.
typedef struct dkapbig_name_t
{
	u32 id;
	const u8 *str;
	uint len;
} dkapbig_name_t;

static enumError extract_dkapbig_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".toc"))
		return ERR_NOTHING_TO_DO;
	u8 *toc = 0, *dat = 0;
	size_t toc_sz = 0, dat_sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &toc, &toc_sz, 0, 0, 0, false) || !toc)
	{
		FREE (toc);
		return ERR_NOTHING_TO_DO;
	}
	const u32 count = toc_sz >= 0x24 ? rd_le32 (toc) : 0;
	if (count < 2 || count > 0x100000 || rd_le32 (toc + 4) != 1 || 0x24 + (size_t)(count - 1) * 36 > toc_sz)
	{
		FREE (toc);
		return ERR_NOTHING_TO_DO;
	}
	char *datp = STRDUP (arg);
	if (!datp)
	{
		FREE (toc);
		return ERR_NOTHING_TO_DO;
	}
	memcpy (datp + strlen (datp) - 3, "DAT", 3);
	if (LoadFileAlloc (datp, 0, 0, &dat, &dat_sz, 0, 0, 0, false) || !dat)
	{
		memcpy (datp + strlen (datp) - 3, "dat", 3);
		if (LoadFileAlloc (datp, 0, 0, &dat, &dat_sz, 0, 0, 0, false) || !dat)
		{
			FREE (dat);
			FREE (datp);
			FREE (toc);
			return ERR_NOTHING_TO_DO;
		}
	}
	FREE (datp);

	// records (head member first), validated against the DAT
	const uint nrec = count - 1;
	const u8 *rec = toc + 0x24;
	// locate the id -> path table: first u32 is a count, then {id, len, path, NUL}, 4-byte aligned
	dkapbig_name_t *names = 0;
	uint nnames = 0;
	for (uint i = 0; i < nrec && !names; i++)
	{
		const u8 *r = rec + (size_t)i * 36;
		const size_t off = rd_le32 (r + 16), sz = rd_le32 (r + 12);
		if (rd_le32 (r + 4) == 0xaaaaaaaa || off > dat_sz || sz > dat_sz - off || sz < 16)
			continue;
		const u8 *p = dat + off;
		const u32 n = rd_le32 (p);
		if (n < 2 || n > 0x100000 || rd_le32 (p + 4) != 1 || p[8] < 3 || p[8] > 250 || p[9] != 'D' || p[10] != ':')
			continue;
		dkapbig_name_t *tmp = CALLOC (n + 1, sizeof (*tmp));
		if (!tmp)
			break;
		size_t q = 4;
		uint k = 0;
		while (k < n && q + 5 <= sz)
		{
			const uint l = p[q + 4];
			if (q + 6 + l > sz)
				break;
			tmp[k].id = rd_le32 (p + q);
			tmp[k].str = p + q + 5;
			tmp[k].len = l;
			k++;
			q = (q + 6 + l + 3) & ~(size_t)3;
		}
		names = tmp;
		nnames = k;
	}

	nintendo_sarc_entry_t *entries = CALLOC (count + 1, sizeof (*entries));
	uint n = 0;
	bool good = entries != 0;
	for (uint i = 0; good && i < count; i++)
	{
		u32 id, hash, size, off;
		if (i == 0)
			id = 1, hash = rd_le32 (toc + 8), size = rd_le32 (toc + 12), off = 0;
		else
		{
			const u8 *r = rec + (size_t)(i - 1) * 36;
			id = rd_le32 (r + 4), hash = rd_le32 (r + 8), size = rd_le32 (r + 12), off = rd_le32 (r + 16);
			if (id == 0xaaaaaaaa)
				break;
		}
		if (off > dat_sz || size > dat_sz - off)
			continue;
		char name[PATH_MAX];
		ccp ext = "";
		const u8 *payload = dat + off;
		u8 *dec = 0;
		uint payload_sz = size;
		if (size > 8 && !memcmp (dat + off, "DKAP", 4))
		{
			// "DKAP", u32 LE unpacked size, then an LZO1X stream
			uint dsz = 0;
			if (!DecodeLZO1XGrow (&dec, &dsz, dat + off + 8, size - 8) && dec && dsz == rd_le32 (dat + off + 4))
			{
				payload = dec;
				payload_sz = dsz;
				ext = dsz >= 8 && !memcmp (dec, "_NGCBM01", 8) ? ".ngcbm" : "";
			}
			else
			{
				FREE (dec);
				dec = 0;
				ext = ".dkap";
			}
		}
		else if (size >= 4 && !memcmp (dat + off, "_NGC", 4))
			ext = ".ngcsd";
		const dkapbig_name_t *nm = 0;
		for (uint k = 0; k < nnames; k++)
			if (names[k].id == id)
			{
				nm = names + k;
				break;
			}
		if (nm)
		{
			// strip the build-machine prefix, flip separators, never allow ".." or absolute paths
			ccp s = (ccp)nm->str;
			uint l = nm->len;
			while (l && !s[l - 1])
				l--;
			for (uint k = 0; k + 15 <= l; k++)
				if (!strncasecmp (s + k, "VDB_REPOSITORY\\", 15))
				{
					s += k + 15;
					l -= k + 15;
					break;
				}
			if (l > 2 && s[1] == ':')
				s += 3, l -= 3;
			uint o = 0;
			name[o++] = 'i';
			for (; l && o < sizeof (name) - 40; l--, s++)
			{
				char c = *s == '\\' ? '/' : *s;
				name[o++] = c;
			}
			name[o] = 0;
			// "i<path>" -> "<path>" under a per-id prefix keeps duplicates apart and blocks traversal
			char tmp[PATH_MAX];
			snprintf (tmp, sizeof (tmp), "%05u_%s%s", id, name + 1, ext);
			for (char *c = tmp; *c; c++)
				if (c > tmp && c[0] == '.' && c[1] == '.')
					c[0] = '_';
			for (char *c = tmp; *c; c++)
				if (*c == '/')
					*c = '_';
			snprintf (name, sizeof (name), "%s", tmp);
		}
		else
			snprintf (name, sizeof (name), "%05u_%08x%s", id, hash, ext);
		good = OwnedEntryAdd (entries, n, name, payload, payload_sz);
		FREE (dec);
		if (good)
			n++;
	}
	FREE (names);
	enumError err = ERR_NOTHING_TO_DO;
	if (good && n)
		err = write_owned_entries (arg, basedir, depth, "DKAPBIG", entries, n);
	else
		FREE (entries);
	FREE (dat);
	FREE (toc);
	return err;
}

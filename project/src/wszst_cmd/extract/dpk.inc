// Simple Wii Series ".dpk" packages (data.dpk), little-endian: u32 table end, u32 node count, u32 file count, then
// directory nodes (u32 words, then the NUL terminated backslash path of the node) and, up to the table end, one record
// per file {u32 offset of the part inside its bundle, u32 unpacked size, NUL terminated name}. A node that ends in
// {u32 bundle offset (0x800 aligned), u32 bundle size, u32 parent node, u32 0 or 8, u32 file count (nonzero)} is a bundle: files are assigned to the
// bundle nodes in order. A bundle is "Wpbb", u32 part count, then {u32 offset, u32 size} per part; a part is either a
// "Wp16" stream (u32 unpacked size; 32 flag bits then 32 words; flag 1 = literal word, flag 0 = codeword
// dddddddddddccccc: copy c+2 words from d words back, history zero filled) or stored data.
static bool dpk_wp16 (const u8 *in, size_t n, u8 *out, size_t outsz)
{
	size_t ip = 0, op = 0;
	while (op < outsz)
	{
		if (ip + 4 > n)
			return false;
		u32 fl = rd_le32 (in + ip);
		ip += 4;
		for (uint k = 0; k < 32 && op < outsz; k++, fl >>= 1)
		{
			if (ip + 2 > n)
				return false;
			const uint t = rd_le16 (in + ip);
			ip += 2;
			if (fl & 1)
			{
				out[op++] = (u8)t;
				if (op < outsz)
					out[op++] = (u8)(t >> 8);
				continue;
			}
			const size_t len = ((t & 31) + 2) * 2, d = (size_t)(t >> 5) * 2;
			if (!d)
				return false;
			for (size_t i = 0; i < len && op < outsz; i++, op++)
				out[op] = d > op ? 0 : out[op - d];
		}
	}
	return true;
}

static enumError extract_dpk_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".dpk"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	nintendo_sarc_entry_t *entries = 0;
	if (sz < 0x40)
		goto done;
	const size_t tend = rd_le32 (raw);
	const uint nfiles = rd_le32 (raw + 8);
	if (tend < 0x20 || tend > sz || !nfiles || nfiles > 100000 || rd_le32 (raw + 4) > 100000)
		goto done;

	typedef struct { size_t off, size; uint cnt; char path[256]; } bundle_t;
	bundle_t *bundles = CALLOC (rd_le32 (raw + 4) + 1, sizeof (*bundles));
	if (!bundles)
		goto done;
	uint nb = 0, nodes_seen = 0;
	size_t pos = 12, rootlen = 0;
	char root[64];
	for (;;)
	{
		// every node path starts with the root node's name; the first node has exactly five words before it
		size_t nm = pos + 20;
		if (nb == 0 && !rootlen)
		{
			const u8 *re = nm < tend ? memchr (raw + nm, 0, tend - nm) : 0;
			if (!re || re - (raw + nm) < 2 || re - (raw + nm) > 60)
				break;
			rootlen = re - (raw + nm);
			memcpy (root, raw + nm, rootlen);
			nodes_seen = 1;
		}
		else
		{
			while (nm + rootlen < tend && nm < pos + 0x1000 && memcmp (raw + nm, root, rootlen))
				nm += 4;
			if (nm + rootlen >= tend || nm >= pos + 0x1000)
				break;
		}
		const u8 *e = memchr (raw + nm, 0, tend - nm);
		if (!e || e - (raw + nm) >= 255)
			break;
		const size_t nw = (nm - pos) / 4;
		if (nw >= 5)
		{
			const size_t o = rd_le32 (raw + nm - 20), s = rd_le32 (raw + nm - 16);
			const uint c = rd_le32 (raw + nm - 4);
			if (c && c <= nfiles && !(o & 0x7ff) && s && o <= sz && s <= sz - o)
			{
				bundles[nb].off = o;
				bundles[nb].size = s;
				bundles[nb].cnt = c;
				memcpy (bundles[nb].path, raw + nm, e - (raw + nm) + 1);
				for (char *p = bundles[nb].path; *p; p++)
					if (*p == '\\')
						*p = '/';
				nb++;
			}
		}
		pos = (e - raw) + 1;
	}
	(void)nodes_seen;
	if (!nb)
	{
		FREE (bundles);
		goto done;
	}
	// file records start at "pos" (the loop stopped at the first one)
	entries = CALLOC (nfiles, sizeof (*entries));
	uint k = 0, bi = 0, in_b = 0;
	bool good = entries != 0;
	// the node list may be followed by a few more words: take the start where nfiles records end exactly at the table end
	size_t rp = 0;
	for (size_t st = pos; st <= pos + 0x80 && st < tend && !rp; st++)
	{
		size_t q = st;
		uint c = 0;
		while (c < nfiles && q + 9 <= tend)
		{
			const u8 *z = memchr (raw + q + 8, 0, tend - q - 8);
			if (!z || z == raw + q + 8)
				break;
			q = (z - raw) + 1;
			c++;
		}
		if (c == nfiles && q == tend)
			rp = st;
	}
	if (!rp)
		good = false;
	while (good && k < nfiles && rp + 9 <= tend && bi < nb)
	{
		const size_t X = rd_le32 (raw + rp), Y = rd_le32 (raw + rp + 4);
		const u8 *e = memchr (raw + rp + 8, 0, tend - rp - 8);
		if (!e)
			break;
		const char *fname = (ccp)raw + rp + 8;
		rp = (e - raw) + 1;
		const bundle_t *b = bundles + bi;
		// locate the part: X is its offset inside the bundle
		const u8 *bp = raw + b->off;
		bool okp = b->size >= 8 + 8 && !memcmp (bp, "Wpbb", 4);
		size_t po = 0, ps = 0;
		if (okp)
		{
			const uint pc = rd_le32 (bp + 4);
			okp = false;
			for (uint i = 0; i < pc && 8 + 8 * (size_t)(i + 1) <= b->size; i++)
				if (rd_le32 (bp + 8 + 8 * i) == X)
				{
					po = X;
					ps = rd_le32 (bp + 12 + 8 * i);
					okp = po <= b->size && ps <= b->size - po;
				}
		}
		char name[300];
		snprintf (name, sizeof (name), "%s/%s", b->path, fname);
		for (uint j = 0; j < k; j++)
			if (!strcasecmp (entries[j].name, name))
			{
				snprintf (name, sizeof (name), "%s/%u_%s", b->path, k, fname);
				break;
			}
		if (!OwnedNameOk (name))
			snprintf (name, sizeof (name), "%04u.bin", k);
		if (okp && ps >= 8 && !memcmp (bp + po, "Wp16", 4) && Y == rd_le32 (bp + po + 4) && Y < (256u << 20))
		{
			u8 *buf = MALLOC (Y ? Y : 1);
			if (buf && dpk_wp16 (bp + po + 8, ps - 8, buf, Y))
				good = OwnedEntryAdd (entries, k, name, buf, (uint)Y);
			else
				good = buf && OwnedEntryAdd (entries, k, name, bp + po, (uint)ps);
			FREE (buf);
		}
		else if (okp)
			good = OwnedEntryAdd (entries, k, name, bp + po, (uint)ps);
		else
			good = OwnedEntryAdd (entries, k, name, raw, 0);
		k++;
		if (++in_b >= b->cnt)
			in_b = 0, bi++;
	}
	FREE (bundles);
	if (good && k == nfiles)
		err = write_owned_entries (arg, basedir, depth, "DPK", entries, k);
	else
		ResetOwnedEntries (entries, nfiles);
	entries = 0;
done:
	FREE (raw);
	return err;
}

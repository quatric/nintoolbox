// Pipeworks Software "bundle v1.03 (big endian)" archives (.bdg; Merv Griffin's
// Crosswords, ...). 0x26-byte banner, then big-endian:
//   +0x30 u32 n_types, +0x34 u32 type_table_off, +0x38 u32 type_table_size
//   (NUL-terminated type names), +0x3c u32 n_entries, +0x40 u32 entry_off;
//   entries 12 bytes {u32 file_offset, u32 (type << 24 | size), u32 name_hash}.
// Names are only stored as hashes, so members are written as
// "<hash>_<type>.bin" (".txt" when the content is plain text). Entries whose
// data lies outside the file (stored in a sibling bundle) are skipped; zlib
// members ("Ifc") are inflated.
// inflate a zlib stream of unknown output size; returns a malloc'ed buffer
static u8 *pwbundle_inflate (const u8 *src, uint src_size, uint *out_size)
{
	z_stream zs;
	memset (&zs, 0, sizeof (zs));
	if (inflateInit (&zs) != Z_OK)
		return 0;
	size_t cap = src_size * 4 + 1024, got = 0;
	u8 *out = MALLOC (cap);
	int r = Z_OK;
	zs.next_in = (Bytef *)src;
	zs.avail_in = src_size;
	while (out && r == Z_OK)
	{
		if (got == cap)
		{
			u8 *nb = REALLOC (out, cap *= 2);
			if (!nb)
			{
				FREE (out);
				out = 0;
				break;
			}
			out = nb;
		}
		zs.next_out = out + got;
		zs.avail_out = (uInt)(cap - got);
		r = inflate (&zs, Z_NO_FLUSH);
		got = cap - zs.avail_out;
	}
	inflateEnd (&zs);
	if (r != Z_STREAM_END || !got || got > 0x4000000)
	{
		FREE (out);
		return 0;
	}
	*out_size = (uint)got;
	return out;
}

static enumError extract_pwbundle_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bdg"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 0x80 || raw_size > UINT_MAX || memcmp (raw, "Pipeworks bundle ", 17))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint n_types = rd_be32 (raw + 0x30), tt_off = rd_be32 (raw + 0x34),
			   tt_size = rd_be32 (raw + 0x38), count = rd_be32 (raw + 0x3c),
			   ent_off = rd_be32 (raw + 0x40);
	if (!count || count > 100000 || (u64)ent_off + 12ull * count > raw_size || (u64)tt_off + tt_size > raw_size
		|| n_types > 32)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// type names: printable ASCII runs in the type table, in order
	char types[32][32];
	uint nt = 0;
	for (uint p = tt_off; p < tt_off + tt_size && nt < 32;)
	{
		uint q = p;
		while (q < tt_off + tt_size && raw[q] >= 0x20 && raw[q] < 0x7f)
			q++;
		if (q - p >= 2 && q < raw_size && !raw[q])
		{
			snprintf (types[nt++], sizeof (types[0]), "%.*s", (int)(q - p), raw + p);
			p = q + 1;
		}
		else
			p++;
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
		const u8 *e = raw + ent_off + 12 * (size_t)i;
		const u32 off = rd_be32 (e), ts = rd_be32 (e + 4), hash = rd_be32 (e + 8);
		const u32 size = ts & 0xffffff, type = ts >> 24;
		if (!size || (u64)off + size > raw_size || off < tt_off)
			continue;
		bool text = true;
		for (uint k = 0; k < size && k < 256 && text; k++)
			text = raw[off + k] == '\r' || raw[off + k] == '\n' || raw[off + k] == '\t'
				|| (raw[off + k] >= 0x20 && raw[off + k] < 0x7f) || raw[off + k] == 0xff;
		const u8 *data = raw + off;
		uint dsize = size;
		u8 *inflated = 0;
		if (size > 6 && raw[off] == 0x78 && (raw[off + 1] == 0xda || raw[off + 1] == 0x9c || raw[off + 1] == 0x01))
		{
			inflated = pwbundle_inflate (raw + off, size, &dsize);
			if (inflated)
			{
				data = inflated;
				text = false;
			}
			else
				dsize = size;
		}
		char name[80];
		snprintf (name, sizeof (name), "%08x_%s.%s", hash, type < nt ? types[type] : "data",
			text ? "txt" : "bin");
		const bool added = OwnedEntryAdd (entries, n, name, data, dsize);
		FREE (inflated);
		if (!added)
			break;
		n++;
	}
	if (!n)
	{
		FREE (entries);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	err = write_owned_entries (arg, basedir, depth, "BDG", entries, n);
	FREE (raw);
	return err;
}

// Banpresto/Now Production tagged resource table (Crayon Shin-chan:
// Saikyou Kazoku Kasukabe King, Wii): an LE u32 slot count, 12 reserved
// bytes, then that many 16-byte slots (u32 offset, u32 size, 8 reserved
// bytes). An all-zero slot is unused; every other slot must land in-bounds
// and start with a 4-upper-case-letter tag. The tag names the payload kind
// -- "HGPK" for UI/widget scenes (COLLECTION, DEBUG_MINIGAME_SELECT, ...),
// "HCML" for rhythm-minigame note charts (PETIT) -- but the outer table
// layout is shared, so the check below is tag-agnostic. Reached two ways in
// practice: directly as a ".pak" file on disc, and as the ".hgpk" file that
// decompress_nintendo_file3()'s MTXZ branch produces from a zlib-wrapped
// ".mz" asset.
static bool is_res_table_tag (const u8 *p)
{
	for (uint i = 0; i < 4; i++)
		if (p[i] < 'A' || p[i] > 'Z')
			return false;
	return true;
}

static bool IsBanprestoResTable (const u8 *data, uint size)
{
	if (size < 16)
		return false;
	const u32 count = rd_le32 (data);
	if (!count || count > 256 || 16 + (u64)count * 16 > size)
		return false;

	bool found_one = false;
	for (u32 i = 0; i < count; i++)
	{
		const u8 *e = data + 16 + (size_t)i * 16;
		const u32 off = rd_le32 (e);
		const u32 sz = rd_le32 (e + 4);
		if (!off && !sz)
			continue;
		if (!off || !sz || (u64)off + sz > size || (u64)off + 4 > size
			|| !is_res_table_tag (data + off))
			return false;
		found_one = true;
	}
	return found_one;
}

// Each slot's 4-letter tag is followed by a fixed 0x20-byte NUL-padded
// resource name and further engine-internal fields (a name hash, sub-object
// counts, and for "HGPK" a nested "LHGWG..."-tagged widget/scene graph) that
// are not decoded here -- same spirit as extract_xtd_file() leaving its
// RESOURCE:GX geometry block opaque. Every slot is dumped byte-exact as
// <name>.<tag>res (tag lower-cased) so the raw bytes stay available for
// further study.
static enumError extract_hgpk_pak_file (ccp arg, ccp basedir, uint depth)
{
	u8 *data = 0;
	size_t size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &data, &size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (size > UINT_MAX || !IsBanprestoResTable (data, (uint)size))
	{
		FREE (data);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT RESTAB:%s -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);
	if (!testmode)
		CreatePath (dest, true);

	const u32 count = rd_le32 (data);
	uint written = 0;
	for (u32 i = 0; i < count; i++)
	{
		const u8 *e = data + 16 + (size_t)i * 16;
		const u32 off = rd_le32 (e);
		const u32 sz = rd_le32 (e + 4);
		if (!off || !sz)
			continue;

		char tag[5];
		for (uint t = 0; t < 4; t++)
			tag[t] = (char)(data[off + t] - 'A' + 'a');
		tag[4] = 0;

		char name[0x21];
		memcpy (name, data + off + 4, 0x20);
		name[0x20] = 0;
		name[strnlen (name, 0x20)] = 0;

		char out[PATH_MAX];
		if (name[0])
			snprintf (out, sizeof (out), "%s/%s.%sres", dest, name, tag);
		else
			snprintf (out, sizeof (out), "%s/slot_%02u.%sres", dest, i, tag);

		if (!testmode)
		{
			if (!SaveFile (out, 0, 0, data + off, sz, 0))
				written++;
		}
		else
			written++;
		if (verbose > 0)
			fprintf (stdlog, "  %-40s %8u bytes\n", name[0] ? name : out, sz);
	}

	FREE (data);
	(void)depth;
	if (!written)
		return ERR_INVALID_DATA;
	return ERR_OK;
}

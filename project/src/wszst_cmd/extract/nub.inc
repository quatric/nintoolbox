// Namco "NUB" sound banks (Family Ski & Snowboard, Family Jockey, ...), big-endian:
//   +0x00 u32 0x00020100 (version), +0x04 u32 0, +0x08 u32 ?, +0x0c u32 count,
//   +0x10 u32 header_region_size (data region starts here),
//   +0x14 u32 data_region_size, +0x18 u32 0x20, +0x1c u32 table[count] with
//   table[i] + 0xbc = address of a 0x160-byte stream header block.
// A block starts with a Namco "IDSP" header: +0x08 channels, +0x0c rate,
// +0x10 samples, +0x2c stream size; the standard 0x60-byte GameCube DSP header
// of channel 0 sits at +0x40 and the block's +0xbc is the END offset of the
// stream in the data region. Mono streams are written as standalone ".dsp".
static enumError extract_nub_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".nub"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	const uint count = raw_size > 0x20 ? rd_be32 (raw + 0xc) : 0;
	const u32 hdr = raw_size > 0x20 ? rd_be32 (raw + 0x10) : 0;
	if (raw_size < 0x100 || raw_size > UINT_MAX || rd_be32 (raw) != 0x20100 || !count || count > 4096
		|| hdr >= raw_size || 0x1c + 4ull * count > hdr)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = CALLOC (count, sizeof (*entries));
	u8 *tmp = MALLOC (0x60 + 0x400000);
	if (!entries || !tmp)
	{
		FREE (entries);
		FREE (tmp);
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	u32 seen[4096];
	for (uint i = 0; i < count; i++)
	{
		const u64 b = (u64)rd_be32 (raw + 0x1c + 4 * i) + 0xbc;
		if (b + 0xc0 > hdr || memcmp (raw + b, "IDSP", 4) || rd_be32 (raw + b + 8) != 1)
			continue;
		bool dup = false;
		for (uint k = 0; k < n; k++)
			dup |= seen[k] == b;
		if (dup)
			continue;
		const u32 size = rd_be32 (raw + b + 0x2c), end = rd_be32 (raw + b + 0xbc);
		if (!size || size > 0x400000 || end < size || (u64)hdr + end > raw_size)
			continue;
		memcpy (tmp, raw + b + 0x40, 0x60);
		memcpy (tmp + 0x60, raw + hdr + end - size, size);
		char name[32];
		snprintf (name, sizeof (name), "%03u.dsp", i);
		if (!OwnedEntryAdd (entries, n, name, tmp, 0x60 + size))
			break;
		seen[n++] = (u32)b;
	}
	FREE (tmp);
	if (!n)
	{
		FREE (entries);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	err = write_owned_entries (arg, basedir, depth, "NUB", entries, n);
	FREE (raw);
	return err;
}

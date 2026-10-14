// Terminal Reality "Infernal Engine" ".smp" samples, small-header variant (Guilty Party, Wii), little-endian:
// u32 version 8, 16 bytes guid, u32 0 @0x14, samples @0x18, data offset @0x1c (0x80), data size @0x20 (offset +
// size = file size), codec @0x24 (2 = DSP-ADPCM), channels u8 @0x28 (1), bits u8 @0x29 (4), rate u16 @0x2a and the
// sixteen coefficients as little-endian s16 at 0x2c. Converted to "<file>.d/<file>.dsp" (zero history).
static enumError extract_smp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".smp"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t ns = sz > 0x80 ? rd_le32 (raw + 0x18) : 0, off = sz > 0x80 ? rd_le32 (raw + 0x1c) : 0,
		     dsz = sz > 0x80 ? rd_le32 (raw + 0x20) : 0;
	const uint rate = sz > 0x80 ? rd_le16 (raw + 0x2a) : 0;
	const size_t nibbles = ns / 14 * 16 + (ns % 14 ? ns % 14 + 2 : 0);
	if (sz <= 0x80 || rd_le32 (raw) != 8 || rd_le32 (raw + 0x14) || off != 0x80 || off + dsz != sz || rd_le32 (raw + 0x24) != 2
		|| raw[0x28] != 1 || raw[0x29] != 4 || !rate || !ns || nibbles > dsz * 2)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t nb = (nibbles + 1) / 2;
	u8 *out = CALLOC (1, 0x60 + nb);
	nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
	if (!out || !entries)
	{
		FREE (out);
		FREE (entries);
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	wr_be32 (out, (u32)ns);
	wr_be32 (out + 4, (u32)nibbles);
	wr_be32 (out + 8, rate);
	wr_be32 (out + 0x10, 2);
	wr_be32 (out + 0x14, (u32)nibbles - 1);
	wr_be32 (out + 0x18, 2);
	for (uint i = 0; i < 16; i++)
		wr_be16 (out + 0x1c + 2 * i, rd_le16 (raw + 0x2c + 2 * i));
	memcpy (out + 0x60, raw + off, nb);
	out[0x3f] = out[0x60];
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	char name[PATH_MAX];
	snprintf (name, sizeof (name), "%s", base);
	name[strlen (name) - 4] = 0;
	StringCat2S (name, sizeof (name), name, ".dsp");
	const bool good = OwnedEntryAdd (entries, 0, name, out, 0x60 + nb);
	FREE (out);
	FREE (raw);
	if (!good)
	{
		FREE (entries);
		return ERR_OUT_OF_MEMORY;
	}
	return write_owned_entries (arg, basedir, depth, "SMP", entries, 1);
}

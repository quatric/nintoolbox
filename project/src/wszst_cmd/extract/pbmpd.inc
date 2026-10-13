// Nippon Ichi Phantom Brave (Wii) battle map data (".mpd": mp*.mpd). Big-endian, laid out like the game's
// loader copies it: 0x40-byte header {u16 count A @0, u16 count B @4, 16 bytes @8, ..., u32 byte size of the
// trailing u16 table @0x30}, a 64x64 grid of 4-byte cells (stored column by column), A records of 0x40
// bytes, B records of 0x24 bytes, the u16 table, and up to 4 KiB of further data (listed as 16-byte records plus a short tail). "<name>.txt" lists
// the header, the four grid byte planes (64 rows of 64 hex bytes each) and every record as hex; the meaning
// of the individual fields is not decoded.
static enumError extract_pbmpd_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".mpd"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char *txt = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x4040)
		goto done;
	const size_t na = rd_be16 (raw), nb = rd_be16 (raw + 4), arr = rd_be32 (raw + 0x30);
	if (na > 4096 || nb > 4096 || arr > 0x10000 || arr % 2 || rd_be16 (raw + 2) || rd_be16 (raw + 6))
		goto done;
	const size_t grid = 0x40, recA = grid + 0x4000, recB = recA + na * 0x40, tab = recB + nb * 0x24, end = tab + arr;
	if (end > sz || sz - end > 0x1000)
		goto done;
	size_t cap = 0x10000 + sz * 4, used = 0;
	txt = MALLOC (cap);
	if (!txt)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
#define PBMPD_PRINT(...) used += snprintf (txt + used, cap - used, __VA_ARGS__)
#define PBMPD_HEX(p, n) \
	for (size_t k_ = 0; k_ < (n); k_++) \
		PBMPD_PRINT ("%02x", (p)[k_])
	PBMPD_PRINT ("Phantom Brave map: %zu A records, %zu B records, %zu table words, %zu extra records\n", na, nb,
		arr / 2, (sz - end) / 16);
	PBMPD_PRINT ("header: ");
	PBMPD_HEX (raw, 0x40);
	PBMPD_PRINT ("\n");
	for (uint pl = 0; pl < 4; pl++)
	{
		PBMPD_PRINT ("grid plane %u (row y, column x):\n", pl);
		for (uint y = 0; y < 64; y++)
		{
			for (uint x = 0; x < 64; x++)
				PBMPD_PRINT ("%02x", raw[grid + ((size_t)x * 64 + y) * 4 + pl]);
			PBMPD_PRINT ("\n");
		}
	}
	for (size_t i = 0; i < na; i++)
	{
		PBMPD_PRINT ("A %zu: ", i);
		PBMPD_HEX (raw + recA + i * 0x40, 0x40);
		PBMPD_PRINT ("\n");
	}
	for (size_t i = 0; i < nb; i++)
	{
		PBMPD_PRINT ("B %zu: ", i);
		PBMPD_HEX (raw + recB + i * 0x24, 0x24);
		PBMPD_PRINT ("\n");
	}
	PBMPD_PRINT ("table:");
	for (size_t i = 0; i < arr / 2; i++)
		PBMPD_PRINT ("%s%04x", i % 16 ? " " : "\n", rd_be16 (raw + tab + 2 * i));
	PBMPD_PRINT ("\n");
	for (size_t i = 0; i < (sz - end) / 16; i++)
	{
		PBMPD_PRINT ("C %zu: ", i);
		PBMPD_HEX (raw + end + i * 16, 16);
		PBMPD_PRINT ("\n");
	}
	if ((sz - end) % 16)
	{
		PBMPD_PRINT ("tail: ");
		PBMPD_HEX (raw + sz - (sz - end) % 16, (sz - end) % 16);
		PBMPD_PRINT ("\n");
	}
#undef PBMPD_HEX
#undef PBMPD_PRINT
	{
		ccp base = strrchr (arg, '/');
		base = base ? base + 1 : arg;
		char name[PATH_MAX];
		snprintf (name, sizeof (name), "%s", base);
		char *dot = strrchr (name, '.');
		if (dot)
			*dot = 0;
		snprintf (name + strlen (name), sizeof (name) - strlen (name), ".txt");
		nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
		if (!entries || !OwnedEntryAdd (entries, 0, name, (u8 *)txt, used))
		{
			FREE (entries);
			err = ERR_OUT_OF_MEMORY;
			goto done;
		}
		FREE (txt);
		txt = 0;
		FREE (raw);
		return write_owned_entries (arg, basedir, depth, "PB-MPD", entries, 1);
	}
done:
	FREE (txt);
	FREE (raw);
	return err;
}

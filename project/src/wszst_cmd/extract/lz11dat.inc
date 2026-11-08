// "resource.dat" + "resource.tbl" pairs of Deer Drive / Kid Adventures style budget Wii games: the .dat starts with a
// zero padded "dummy" header (0x20 bytes) followed by LZ11 compressed blocks; the sibling .tbl is itself LZ11 and, once
// unpacked, big-endian: u32 0, u32 size, ..., then from 0x20 one {u32 offset into the .dat, u32 compressed size,
// u32 unpacked size} triple per slot (all zero for unused slots). Blocks are LZ11 streams ("0x11" + 24 bit size) or
// stored. Every used slot is written as "<index>.bin" into "<file>.d"; the tbl has no names.
static enumError extract_lz11dat_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".dat"))
		return ERR_NOTHING_TO_DO;
	char tbl_path[PATH_MAX];
	snprintf (tbl_path, sizeof (tbl_path), "%s", arg);
	char *dot = strrchr (tbl_path, '.');
	if (!dot || strlen (arg) < 5)
		return ERR_NOTHING_TO_DO;
	strcpy (dot, ".tbl");
	if (access (tbl_path, R_OK))
		return ERR_NOTHING_TO_DO;
	u8 *tbl = 0, *dat = 0, *tbl_raw = 0;
	size_t tsz = 0, dsz = 0;
	uint tbl_n = 0;
	enumError err = ERR_NOTHING_TO_DO;
	nintendo_sarc_entry_t *entries = 0;
	uint cap = 0;
	if (LoadFileAlloc (tbl_path, 0, 0, &tbl_raw, &tsz, 0, 0, 0, false) || !tbl_raw)
		goto done;
	if (LoadFileAlloc (arg, 0, 0, &dat, &dsz, 0, 0, 0, false) || !dat)
		goto done;
	if (dsz < 0x30 || memcmp (dat, "dummy\0", 6) || tsz < 8 || tbl_raw[0] != 0x11)
		goto done;
	if (DecodeLZ10LZ11 (&tbl, &tbl_n, tbl_raw, (uint)tsz) || !tbl || tbl_n < 0x2c)
		goto done;
	cap = (tbl_n - 0x20) / 12;
	entries = CALLOC (cap ? cap : 1, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	uint k = 0;
	for (uint i = 0; i < cap; i++)
	{
		const u8 *e = tbl + 0x20 + 12 * (size_t)i;
		const size_t off = rd_be32 (e), cs = rd_be32 (e + 4), us = rd_be32 (e + 8);
		if (!off && !cs && !us)
			continue;
		if (off < 0x20 || cs == 0 || off > dsz || cs > dsz - off || us > (256u << 20))
			continue;
		char name[32];
		snprintf (name, sizeof (name), "%04u.bin", i);
		u8 *out = 0;
		uint on = 0;
		bool good = false;
		if (dat[off] == 0x11 && cs >= 4 && (u32)(dat[off + 1] | dat[off + 2] << 8 | dat[off + 3] << 16) == us
			&& !DecodeLZ10LZ11 (&out, &on, dat + off, (uint)cs) && out && on == us)
			good = OwnedEntryAdd (entries, k, name, out, on);
		else if (cs == us)
			good = OwnedEntryAdd (entries, k, name, dat + off, (uint)cs);
		FREE (out);
		if (good)
			k++;
	}
	if (k)
		err = write_owned_entries (arg, basedir, depth, "LZ11DAT", entries, k);
	else
		ResetOwnedEntries (entries, cap ? cap : 1);
	entries = 0;
done:
	FREE (tbl);
	FREE (tbl_raw);
	FREE (dat);
	return err;
}

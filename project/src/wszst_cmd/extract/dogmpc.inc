// Dog Island (Artlist Collection, Wii) "static.mpc" table pack, big-endian: {u32 table bytes, u32 data bytes, u32 0x20, u32 data start (0x20 + table bytes), u32 1, 3 zero words},
// table bytes / 0x28 records {u32 tag, char name[0x20], u32 offset}: the first records are directory headers (tag high byte 0), the leaf records (tag 0xff<count>, count = number of
// fixed-size rows in the block) give each data block's offset relative to the data start; a block ends where the next one begins (the last at the data size).
// Blocks are written as "<name>.bin"; the row contents are not decoded.
static enumError extract_dogmpc_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".mpc"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x60 || rd_be32 (raw + 8) != 0x20 || rd_be32 (raw + 16) != 1)
		goto done;
	{
		const u32 tb = rd_be32 (raw), db = rd_be32 (raw + 4), ds = rd_be32 (raw + 12);
		if (!tb || tb % 0x28 || ds != 0x20 + tb || (u64)ds + db > sz || ds + 0x400 + db < sz)
			goto done;
		const uint n = tb / 0x28;
		u32 prev = 0;
		uint leaves = 0;
		for (uint i = 0; i < n; i++)
		{
			const u8 *r = raw + 0x20 + 0x28 * (size_t)i;
			if (r[0] != 0xff)
				continue;
			const u32 o = rd_be32 (r + 0x24);
			if (o < prev || o > db || !r[4])
				goto done;
			prev = o;
			leaves++;
		}
		if (!leaves)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT DOGMPC:%s (%u blocks) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, leaves, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *r = raw + 0x20 + 0x28 * (size_t)i;
			if (r[0] != 0xff)
				continue;
			u32 end = db;
			for (uint j = i + 1; j < n; j++)
				if (raw[0x20 + 0x28 * (size_t)j] == 0xff)
				{
					end = rd_be32 (raw + 0x20 + 0x28 * (size_t)j + 0x24);
					break;
				}
			const u32 o = rd_be32 (r + 0x24);
			char name[0x30];
			snprintf (name, sizeof (name), "%.31s.bin", (ccp)r + 4);
			err = shade_write (arg, dest, basedir, name, raw + ds + o, end - o);
		}
	}
done:
	FREE (raw);
	return err;
}

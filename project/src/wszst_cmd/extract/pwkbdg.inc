// Pipeworks bundle (".bdg"; Monster High: Ghoul Spirit, Wii): the text "Pipeworks bundle v1.30 (big endian)   \x1a\0" + bundle name, 0xeb padding up to 0x4c, then big-endian
// {u32 file size, u32 type count, u32 type table offset, u32 type table size, u32 entry count, u32 entry table offset, ...}. The entry table holds 16-byte records
// {u32 offset, u8 flags + u24 size, u8 codec + u24 stored size, u32 id (sorted ascending)}; codec 0x24 = a zlib stream, 0x20 / 0x40 = stored. Entries that share an id come as a 16-byte
// stored head followed by the data. Members are written as "<index>_<id>.bin" (decoded); the names and types are kept in the sibling ".bdg.jnl" text journal and a trailing string table, which are
// not used here. Streamed from disk member by member (Monster.bdg is 59 MB).
static enumError extract_pwkbdg_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bdg"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 hd[0x68];
	if (fread (hd, 1, sizeof (hd), f) != sizeof (hd) || memcmp (hd, "Pipeworks bundle v", 18) || fseeko (f, 0, SEEK_END))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	const u64 fsz = (u64)ftello (f);
	const u32 tot = rd_be32 (hd + 0x4c), ne = rd_be32 (hd + 0x5c), eto = rd_be32 (hd + 0x60);
	if (tot != fsz || !ne || ne > 0x100000 || (u64)eto + 16 * (u64)ne > fsz || fseeko (f, eto, SEEK_SET))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	u8 *tab = MALLOC ((size_t)ne * 16);
	if (!tab || fread (tab, 1, (size_t)ne * 16, f) != (size_t)ne * 16)
	{
		FREE (tab);
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	for (u32 i = 0; i < ne; i++)
	{
		const u8 *r = tab + 16 * (size_t)i;
		const uint codec = r[8];
		if ((codec != 0x24 && codec != 0x20 && codec != 0x40) || (u64)rd_be32 (r) + (rd_be32 (r + 8) & 0xffffff) > fsz)
		{
			FREE (tab);
			fclose (f);
			return ERR_NOTHING_TO_DO;
		}
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PWKBDG:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ne, dest);
	enumError err = ERR_OK;
	for (u32 i = 0; i < ne && !err && !testmode; i++)
	{
		const u8 *r = tab + 16 * (size_t)i;
		const u32 off = rd_be32 (r), us = rd_be32 (r + 4) & 0xffffff, cs = rd_be32 (r + 8) & 0xffffff, id = rd_be32 (r + 12);
		u8 *buf = MALLOC (cs ? cs : 1);
		if (!buf || fseeko (f, off, SEEK_SET) || (cs && fread (buf, 1, cs, f) != cs))
		{
			FREE (buf);
			err = ERR_READ_FAILED;
			break;
		}
		u8 *out = buf;
		uint osz = cs;
		if (r[8] == 0x24)
		{
			size_t used = 0;
			out = fst_inflate (buf, cs, &osz, &used);
			if (!out || osz != us)
			{
				if (out)
					FREE (out);
				out = buf;		// keep the raw bytes if inflating fails
				osz = cs;
				buf = 0;
				if (out == 0)
					out = (u8 *)MALLOC (1);
			}
			else
				FREE (buf), buf = 0;
		}
		char name[48];
		snprintf (name, sizeof (name), "%04u_%08x.bin", i, id);
		err = shade_write (arg, dest, basedir, name, out, osz);
		FREE (out);
		if (buf && buf != out)
			FREE (buf);
	}
	FREE (tab);
	fclose (f);
	(void)depth;
	return err;
}

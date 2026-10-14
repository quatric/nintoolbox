// Good-Feel "BGST" background tile sets (".bgst3", Kirby's Epic Yarn), big-endian: "BGST", u32 version (0x10 or 0x11),
// u32 tile width, u32 tile height (512 each), u32 kind, u32 3, u32 tile count, ..., u32 table offset (0x40) @0x28
// and u32 start of the tile data @0x2c. The slot table at 0x40 holds 16-byte records {u16 used, u16 layer, u16 x,
// u16 y, u16 tile ids[4]} (0xffff / 0xfffe = none). The tile data is a plain run of count GX CMPR tiles of
// width * height / 2 bytes each, which ends the file. In decode mode every tile that is not entirely the empty
// block (00000001 ffffffff) is written to "<file>.NNN.png" and the slot table to "<file>.txt".
static enumError extract_bgst3_file (ccp arg)
{
	if ((export_count <= 0 && !opt_decode) || !is_ext (arg, ".bgst3"))
		return ERR_NOTHING_TO_DO;
	FILE *in = fopen (arg, "rb");
	if (!in)
		return ERR_NOTHING_TO_DO;
	u8 h[0x30];
	if (!read_abe_at (in, 0, h, sizeof (h)) || memcmp (h, "BGST", 4) || fseek (in, 0, SEEK_END))
	{
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}
	const long end = ftell (in);
	const u64 fsz = end < 0 ? 0 : (u64)end;
	const uint w = rd_be32 (h + 8), ht = rd_be32 (h + 12), count = rd_be32 (h + 0x18);
	const u64 tab = rd_be32 (h + 0x28), data = rd_be32 (h + 0x2c);
	const u64 tsz = (u64)w * ht / 2;
	if (w < 8 || w > 2048 || ht < 8 || ht > 2048 || (w | ht) & 7 || !count || count > 4096 || tab < 0x30 || data < tab
		|| data - tab > 0x10000 || data + count * tsz != fsz)
	{
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}
	char txt[PATH_MAX];
	snprintf (txt, sizeof (txt), "%s.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE BGST3:%s (%u tiles of %ux%u) -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, w, ht, txt);
	enumError err = ERR_OK;
	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, txt, false, arg);
		if (F.f)
		{
			u8 rec[16];
			fprintf (F.f, "# tiles=%u size=%ux%u\n# used layer x y tile0 tile1 tile2 tile3\n", count, w, ht);
			for (u64 p = tab; p + 16 <= data && read_abe_at (in, p, rec, sizeof (rec)); p += 16)
				fprintf (F.f, "%u %u %u %u %u %u %u %u\n", (rec[0] << 8) | rec[1], (rec[2] << 8) | rec[3],
					(rec[4] << 8) | rec[5], (rec[6] << 8) | rec[7], (rec[8] << 8) | rec[9], (rec[10] << 8) | rec[11],
					(rec[12] << 8) | rec[13], (rec[14] << 8) | rec[15]);
			const enumError rerr = ResetFile (&F, opt_preserve);
			if (!err)
				err = rerr;
		}
	}
	u8 *buf = err || testmode ? 0 : MALLOC (tsz);
	for (uint i = 0; buf && i < count && !err; i++)
	{
		if (!read_abe_at (in, data + i * tsz, buf, tsz))
			break;
		bool empty = true;
		for (u64 k = 0; empty && k < tsz; k += 8)
			empty = rd_be32 (buf + k) == 1 && rd_be32 (buf + k + 4) == 0xffffffff;
		if (empty)
			continue;
		u8 *rgba = eashpg_decode (14, buf, tsz, w, ht, 0, 0, 0);
		if (!rgba)
			continue;
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s.%03u.png", arg, i);
		err = SaveDecodedRGBAToPNG (rgba, w, ht, &be_func, png, 0, true);
	}
	FREE (buf);
	fclose (in);
	return err;
}

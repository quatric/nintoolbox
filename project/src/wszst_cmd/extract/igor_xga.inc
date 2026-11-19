// Igor: The Game skeletal animations (".xga", Exient "chkf" container of type "xga"; big endian) with "XgmDecompAnimChannel" channels. Layout as in igor_ggm.inc ({"chkf", u32 N}, N byte string
// table, then header words; the data size is at +0x1c of the header, the key count K at +0x20): {u32 channel count C at +0x24, u32 C again, C x {u32 0x14 (class name offset), u32 0x29 (empty
// string), u32 name offset (the skeleton node animated), u32 kind offset ("Joint"), u32 byte offset of the channel's keys}} followed by a table that is not decoded. The keys sit in the
// data block at the end of the file (size = C x K x 32): per channel K keys of {f32 quaternion x, y, z, w, f32 translation x, y, z, f32 scale (about 1)}, channel i starting at i x K x 32.
// Key times are not stored with the keys (written as frame indices). Other channel classes ("XgmMatrixAnimChannel", the 5 files with an empty first name) are left alone. Written as
// "<file>.json" (455 of 474 files).
static enumError extract_igor_xga_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".xga") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x40 || memcmp (raw, "chkf", 4) || memcmp (raw + 8, "xga", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 sl = rd_be32 (raw + 4);
	const size_t hb = 8 + (size_t)sl;
	if (sl < 16 || hb + 0x2c > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u8 *st = raw + 8;
	const u32 dsz = rd_be32 (raw + hb + 0x1c), kk = rd_be32 (raw + hb + 0x20), nc = rd_be32 (raw + hb + 0x24);
	bool ok = kk && kk <= 100000 && nc && nc <= 4096 && rd_be32 (raw + hb + 0x28) == nc && dsz <= sz - hb && (u64)nc * kk * 32 == dsz && hb + 0x2c + 20ull * nc <= sz - dsz;
	const size_t base = sz - dsz;
	for (u32 i = 0; ok && i < nc; i++)
	{
		const u8 *r = raw + hb + 0x2c + 20 * (size_t)i;
		ok = rd_be32 (r) < sl && rd_be32 (r + 8) < sl && rd_be32 (r + 12) < sl && rd_be32 (r + 16) == (u64)i * kk * 32 && !memcmp (st + rd_be32 (r), "XgmDecompAnimChannel", 21);
	}
	if (!ok)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "{\n\t\"format\": \"Igor XGA\",\n\t\"keys\": %u,\n\t\"channels\": [", kk);
	for (u32 i = 0; i < nc; i++)
	{
		const u8 *r = raw + hb + 0x2c + 20 * (size_t)i;
		sumo_printf (&b, "%s\n\t\t{\"name\": \"%s\", \"kind\": \"%s\", \"keys\": [", i ? "," : "", (ccp)st + rd_be32 (r + 8), (ccp)st + rd_be32 (r + 12));
		for (u32 k = 0; k < kk; k++)
		{
			const u8 *p = raw + base + (size_t)i * kk * 32 + 32 * (size_t)k;
			float f[8];
			for (uint v = 0; v < 8; v++)
			{
				const u32 u = rd_be32 (p + 4 * v);
				memcpy (&f[v], &u, 4);
			}
			sumo_printf (&b, "%s[%.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g]", k ? ", " : "", (double)f[0], (double)f[1], (double)f[2], (double)f[3], (double)f[4], (double)f[5], (double)f[6], (double)f[7]);
		}
		sumo_printf (&b, "]}");
	}
	sumo_printf (&b, "\n\t]\n}\n");
	FREE (raw);
	enumError err = ERR_NOTHING_TO_DO;
	if (b.ok && b.n && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl2 = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl2 ? sl2 + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".json");
		else
			strcat (name, ".json");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT IGOR-XGA:%s (%u channels, %u keys) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nc, kk, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}

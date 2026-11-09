// Bj engine track data ".btd" (Maximum Racing, little endian PC layout): u32 magic 0xb060ffed, u32 section mask (0x7ff = 11
// sections), then 11 entries {u32 offset, u32 count, u32 -1} from +8 (offsets are file offsets; the game relocates them and
// byte swaps every field per section, see its section fix-up table). Section 3 is a single structure at its offset:
// +8 = number of points per line (n), +0x5c + 8*i = {u32 offset of 0x20-byte point records, u32 offset of 12-byte records}
// (offsets relative to the section) for i < 19 lines; a point record is {float x, y, z, 5 floats of direction/side data}.
// Written as "lines.obj" (one OBJ polyline per line, positions only) and "sections.json" (offset / count of every section).
static enumError extract_btd_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".btd"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x90 || sz > 0x10000000
		|| rd_le32 (raw) != 0xb060ffed || rd_le32 (raw + 4) != 0x7ff)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	njm_out_t j = { MALLOC (4096), 0, 4096 }, o = { MALLOC (1 << 16), 0, 1 << 16 };
	if (!j.b || !o.b)
	{
		FREE (j.b);
		FREE (o.b);
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	njm_put (&j, "{\n\"sections\": [");
	for (uint i = 0; i < 11; i++)
		njm_put (&j, "%s\n {\"index\": %u, \"offset\": %u, \"count\": %u}", i ? "," : "", i, rd_le32 (raw + 8 + 12 * i),
			rd_le32 (raw + 12 + 12 * i));
	njm_put (&j, "\n]\n}\n");
	uint nl = 0;
	const u32 s3 = rd_le32 (raw + 8 + 12 * 3);
	if (s3 && (u64)s3 + 0x100 <= sz)
	{
		const u32 n = rd_le32 (raw + s3 + 8);
		if (n && n < 100000)
			for (uint i = 0; i < 19; i++)
			{
				const u32 po = rd_le32 (raw + s3 + 0x5c + 8 * i);
				if ((u64)s3 + po + 0x20 * (u64)n > sz)
					break;
				njm_put (&o, "o line%u\n", i);
				for (uint k = 0; k < n; k++)
				{
					const u8 *p = raw + s3 + po + 0x20 * (size_t)k;
					njm_put (&o, "v %g %g %g\n", njm_f_le (p), njm_f_le (p + 4), njm_f_le (p + 8));
				}
				njm_put (&o, "l");
				for (uint k = 0; k < n; k++)
					njm_put (&o, " %d", (int)k - (int)n);
				njm_put (&o, "\n");
				nl++;
			}
	}
	enumError err = ERR_OK;
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BJ-BTD:%s (%u lines) -> %s/lines.obj\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nl, dest);
	if (!testmode)
	{
		err = shade_write (arg, dest, basedir, "sections.json", (u8 *)j.b, (uint)j.used);
		if (!err && nl && o.b)
			err = shade_write (arg, dest, basedir, "lines.obj", (u8 *)o.b, (uint)o.used);
	}
	FREE (j.b);
	FREE (o.b);
	FREE (raw);
	return err;
}

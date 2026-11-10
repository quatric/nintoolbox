// "RC\0\2" fighter record (".chr": Power Punch, charaNNNNN.chr / Dummy.chr), always 32 bytes, little endian: {"RC\0\2", u32 1, s16 -1, u16 0,
// 5 floats}. The floats look like per-fighter body measurements (~175 / ~89 / 89 / ~175 / 89.34); Dummy.chr has all five = -1. Meanings
// are unconfirmed, so "chr.txt" lists them as f0..f4.
static enumError extract_rcchr_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz != 32 || memcmp (raw, "RC\0\2", 4) || rd_le32 (raw + 4) != 1 || rd_le32 (raw + 8) != 0x0000ffff)
		goto done;
	{
		char dest[PATH_MAX], t[256];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT RC-CHR:%s -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
		err = ERR_OK;
		if (!testmode)
		{
			uint tl = 0;
			for (uint i = 0; i < 5; i++)
			{
				const u32 v = rd_le32 (raw + 12 + 4 * i);
				float f;
				memcpy (&f, &v, 4);
				tl += snprintf (t + tl, sizeof (t) - tl, "f%u %g\n", i, f);
			}
			err = shade_write (arg, dest, basedir, "chr.txt", (const u8 *)t, tl);
		}
	}
done:
	FREE (raw);
	return err;
}

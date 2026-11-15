// Pikmin 1 "ghost/record.gst": a recorded controller stream (Kontroller::write in the decompilation), 12 bytes per
// frame: {u32 button mask, s8 main stick X/Y, s8 sub stick X/Y, u8 analog A/B, u8 trigger L/R}. Written as
// "<name>.gst.csv" (one row per frame; the all-zero tail of the fixed-size buffer is kept so frame numbers match).
static enumError extract_pikmin_gst_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".gst"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (!sz || sz % 12 || sz > 12 * 1000000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s.csv", arg);
	enumError err = ERR_OK;
	if (testmode)
		fprintf (stdlog, "WOULD EXTRACT GST:%s -> CSV:%s\n", arg, dest);
	else
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f)
		{
			fprintf (F.f, "frame,buttons,main_x,main_y,sub_x,sub_y,analog_a,analog_b,trigger_l,trigger_r\n");
			for (size_t i = 0; i < sz; i += 12)
			{
				const u8 *r = raw + i;
				fprintf (F.f, "%u,0x%08x,%d,%d,%d,%d,%u,%u,%u,%u\n", (uint)(i / 12), rd_be32 (r), (s8)r[4], (s8)r[5], (s8)r[6],
					(s8)r[7], r[8], r[9], r[10], r[11]);
			}
		}
		ResetFile (&F, opt_preserve);
		if (verbose >= 0 && !err)
			fprintf (stdlog, "%sEXTRACT GST:%s -> CSV:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	}
	FREE (raw);
	return err;
}

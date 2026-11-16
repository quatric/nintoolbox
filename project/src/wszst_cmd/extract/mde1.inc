// Ousama Monogatari / Little King's Story (Wii) event tables ("event/boot/MissionStartEventDB.bin"), big-endian:
//   "MDE1" and 12 zero bytes, then 16-byte records of eight u16 values (0xffff = unused). The field meanings are not documented;
//   written to "<file>.tsv" with columns f0..f7 (0xffff as -1).
static enumError extract_mde1_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 32 || memcmp (raw, "MDE1", 4) || (sz - 16) % 16)
		goto done;
	for (uint i = 4; i < 16; i++)
		if (raw[i])
			goto done;
	{
		pcr_ctx_t cx;
		memset (&cx, 0, sizeof (cx));
		cx.d = raw;
		cx.sz = sz;
		cx.ok = true;
		pcr_emit (&cx, "f0\tf1\tf2\tf3\tf4\tf5\tf6\tf7\n");
		for (size_t p = 16; p + 16 <= sz; p += 16)
			for (uint k = 0; k < 8; k++)
			{
				const uint v = raw[p + 2 * k] << 8 | raw[p + 2 * k + 1];
				if (v == 0xffff)
					pcr_emit (&cx, "-1%s", k == 7 ? "\n" : "\t");
				else
					pcr_emit (&cx, "%u%s", v, k == 7 ? "\n" : "\t");
			}
		if (cx.ok)
		{
			char dest[PATH_MAX];
			snprintf (dest, sizeof (dest), "%s.tsv", arg);
			if (testmode)
			{
				fprintf (stdlog, "WOULD EXTRACT MDE1:%s -> TSV:%s\n", arg, dest);
				err = ERR_OK;
			}
			else
			{
				File_t F;
				err = CreateFileOpt (&F, true, dest, false, arg);
				if (F.f && fwrite (cx.out, 1, cx.len, F.f) != cx.len)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)cx.len, dest);
				ResetFile (&F, opt_preserve);
				if (verbose >= 0 && !err)
					fprintf (stdlog, "%sEXTRACT MDE1:%s -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, dest);
			}
		}
		FREE (cx.out);
	}
done:
	FREE (raw);
	return err;
}

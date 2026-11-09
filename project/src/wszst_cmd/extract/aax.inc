// CRI AAX (".aax": Mario & Sonic at the London 2012 Olympic Games Sound/Synth/*): a big endian "@UTF" table named "AAX" with the two
// per-row columns "data" (u32 offset into the table's data area, u32 size) and "lpflg" (u8 loop flag), one row per segment (a
// single ADX for plain tracks, intro + loop body for looped ones). Every row's ADX is written as "NNN.adx" in "<file>.d" (the ADX
// extractor turns them into WAV with --decode / export).
static enumError extract_aax_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".aax") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 || memcmp (raw, "@UTF", 4) || (size_t)rd_be32 (raw + 4) + 8 != sz)
		goto done;
	{
		const u32 ro = rd_be16 (raw + 10) + 8u, so = rd_be32 (raw + 12) + 8u, dofs = rd_be32 (raw + 16) + 8u;
		const uint fc = rd_be16 (raw + 24), rs = rd_be16 (raw + 26), rc = rd_be32 (raw + 28);
		if (fc != 2 || rs != 9 || !rc || rc > 64 || raw[0x20] != 0x5b || raw[0x25] != 0x50 || ro + (size_t)rs * rc > sz || so > sz || dofs > sz)
			goto done;
		for (uint i = 0; i < rc; i++)
		{
			const u32 off = rd_be32 (raw + ro + rs * i), n = rd_be32 (raw + ro + rs * i + 4);
			if ((u64)dofs + off + n > sz || n < 0x20 || raw[dofs + off] != 0x80)
				goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT AAX:%s (%u segments) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, rc, dest);
		err = ERR_OK;
		for (uint i = 0; i < rc && !err && !testmode; i++)
		{
			const u32 off = rd_be32 (raw + ro + rs * i), n = rd_be32 (raw + ro + rs * i + 4);
			char name[32];
			snprintf (name, sizeof (name), "%03u.adx", i);
			err = shade_write (arg, dest, basedir, name, raw + dofs + off, n);
		}
	}
done:
	FREE (raw);
	return err;
}

// "TDXS" typed table ("BDD/**/*.bin" of Shawn Johnson Gymnastics, Wii), big-endian:
//   "TDXS", u16 version (1), u16 0, u32 0x18, u32 offset of the module name, u32 offset of the table name, u32 0x00010000, u32 row count, u32 0x20 (data offset);
//   row count * rows of equal size from 0x20 (all 32-bit words: ints, floats, 32-bit name hashes), one 0 byte, then the NUL terminated module ("Misc", "Player", "GameLocks"...)
//   and table ("MenuTimeList", "SkinColorList") names. The row size is (name offset - 1 - 0x20) / row count.
// The column types are not stored; they are guessed per column and written to "<file>.tsv": all values < 0x10000 -> decimal, all values plausible floats -> float, else hex.
static enumError extract_tdxs_file (ccp arg, ccp basedir, uint depth)
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
	if (sz < 0x30 || memcmp (raw, "TDXS", 4) || rd_be32 (raw + 8) != 0x18 || rd_be32 (raw + 0x1c) != 0x20)
		goto done;
	{
		const u32 mod = rd_be32 (raw + 0x0c), tab = rd_be32 (raw + 0x10), n = rd_be32 (raw + 0x18);
		if (!n || n > 0x100000 || mod <= 0x21 || mod >= tab || tab >= sz || raw[mod - 1] || raw[sz - 1] || (u64)sz < tab)
			goto done;
		const u32 dl = mod - 1 - 0x20;
		if (dl % n || dl / n < 4 || (dl / n) % 4)
			goto done;
		const uint cols = dl / n / 4;
		if (cols > 256)
			goto done;
		for (size_t i = mod; i < sz; i++)
			if (raw[i] && (raw[i] < 0x20 || raw[i] > 0x7e))
				goto done;
		// per-column type: 0 decimal, 1 float, 2 hex
		u8 kind[256];
		for (uint c = 0; c < cols; c++)
		{
			bool small = true, flt = true;
			for (uint r = 0; r < n; r++)
			{
				const u32 v = rd_be32 (raw + 0x20 + ((size_t)r * cols + c) * 4);
				if (v >= 0x10000)
					small = false;
				const uint ex = v >> 23 & 0xff;
				if (v && (ex < 100 || ex > 160))
					flt = false;
			}
			kind[c] = small ? 0 : flt ? 1 : 2;
		}
		pcr_ctx_t cx;
		memset (&cx, 0, sizeof (cx));
		cx.d = raw;
		cx.sz = sz;
		cx.ok = true;
		pcr_emit (&cx, "# module %s table %s rows %u columns %u\n", (ccp)raw + mod, (ccp)raw + tab, n, cols);
		for (uint r = 0; r < n; r++)
			for (uint c = 0; c < cols; c++)
			{
				const u32 v = rd_be32 (raw + 0x20 + ((size_t)r * cols + c) * 4);
				const ccp sep = c + 1 < cols ? "\t" : "\n";
				if (kind[c] == 0)
					pcr_emit (&cx, "%u%s", v, sep);
				else if (kind[c] == 1)
				{
					float f;
					memcpy (&f, &v, 4);
					pcr_emit (&cx, "%.9g%s", (double)f, sep);
				}
				else
					pcr_emit (&cx, "0x%08x%s", v, sep);
			}
		if (cx.ok)
		{
			char dest[PATH_MAX];
			snprintf (dest, sizeof (dest), "%s.tsv", arg);
			if (testmode)
			{
				fprintf (stdlog, "WOULD EXTRACT TDXS:%s -> TSV:%s\n", arg, dest);
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
					fprintf (stdlog, "%sEXTRACT TDXS:%s -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, dest);
			}
		}
		FREE (cx.out);
	}
done:
	FREE (raw);
	return err;
}

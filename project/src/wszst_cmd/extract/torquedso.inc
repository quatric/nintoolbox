// Compiled TorqueScript (".cs.dso" / ".gui.dso" / ".dso"; The Destiny of Zorro and other Torque Game Engine titles), little-endian: u32 version (33..36), u32 size + NUL separated global string
// table, u32 size + function string table, u32 count + f64 global floats, u32 count + f64 function floats, then u32 code size, u32 line break pair count and the code stream (8-bit opcodes with
// 8 / 32-bit operands) that is not decompiled. Written as "<file>.txt": the tables (strings as "<offset>\t<text>", floats by index).
static enumError extract_torquedso_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".dso"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	const u32 ver = sz >= 24 ? rd_le32 (raw) : 0;
	if (ver < 33 || ver > 36)
		goto done;
	{
		size_t p = 4;
		u32 gs = rd_le32 (raw + p);
		const size_t gso = p + 4;
		p = gso + gs;
		if (gs > sz || p + 4 > sz)
			goto done;
		const u32 fs = rd_le32 (raw + p);
		const size_t fso = p + 4;
		p = fso + fs;
		if (fs > sz || p + 4 > sz)
			goto done;
		const u32 nf = rd_le32 (raw + p);
		const size_t fo = p + 4;
		p = fo + 8 * (size_t)nf;
		if (nf > sz || p + 4 > sz)
			goto done;
		const u32 nf2 = rd_le32 (raw + p);
		const size_t f2o = p + 4;
		p = f2o + 8 * (size_t)nf2;
		if (nf2 > sz || p + 8 > sz)
			goto done;
		const u32 cs = rd_le32 (raw + p);
		if (cs > sz || p + 8 + cs > sz)
			goto done;
		c.d = raw;
		c.sz = sz;
		c.ok = true;
		pcr_emit (&c, "version %u, code words %u, line break pairs %u\n\n[global strings]\n", ver, cs, rd_le32 (raw + p + 4));
		for (size_t s = 0; s < gs;)
		{
			size_t e = s;
			while (e < gs && raw[gso + e])
				e++;
			pcr_emit (&c, "%zu\t%.*s\n", s, (int)(e - s), (ccp)raw + gso + s);
			s = e + 1;
		}
		pcr_emit (&c, "\n[function strings]\n");
		for (size_t s = 0; s < fs;)
		{
			size_t e = s;
			while (e < fs && raw[fso + e])
				e++;
			pcr_emit (&c, "%zu\t%.*s\n", s, (int)(e - s), (ccp)raw + fso + s);
			s = e + 1;
		}
		pcr_emit (&c, "\n[global floats]\n");
		for (u32 i = 0; i < nf; i++)
		{
			u64 b = (u64)rd_le32 (raw + fo + 8 * (size_t)i) | (u64)rd_le32 (raw + fo + 8 * (size_t)i + 4) << 32;
			double dv;
			memcpy (&dv, &b, 8);
			pcr_emit (&c, "%u\t%g\n", i, dv);
		}
		pcr_emit (&c, "\n[function floats]\n");
		for (u32 i = 0; i < nf2; i++)
		{
			u64 b = (u64)rd_le32 (raw + f2o + 8 * (size_t)i) | (u64)rd_le32 (raw + f2o + 8 * (size_t)i + 4) << 32;
			double dv;
			memcpy (&dv, &b, 8);
			pcr_emit (&c, "%u\t%g\n", i, dv);
		}
		if (c.ok)
		{
			char dest[PATH_MAX];
			snprintf (dest, sizeof (dest), "%s.txt", arg);
			if (testmode)
			{
				fprintf (stdlog, "WOULD EXTRACT TORQUEDSO:%s -> TXT:%s\n", arg, dest);
				err = ERR_OK;
			}
			else
			{
				File_t F;
				err = CreateFileOpt (&F, true, dest, false, arg);
				if (F.f && fwrite (c.out, 1, c.len, F.f) != c.len)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)c.len, dest);
				ResetFile (&F, opt_preserve);
				if (verbose >= 0 && !err)
					fprintf (stdlog, "%sEXTRACT TORQUEDSO:%s -> TXT:%s\n", verbose > 0 ? "\n" : "", arg, dest);
			}
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}

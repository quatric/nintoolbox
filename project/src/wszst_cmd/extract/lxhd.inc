// Genki "LXTools_FileCombine" archives (Spectrobes: Origins "DATA.LXHD" + "DATA.LXBD"): the header file starts "FMXL", be32 2, be32 file count n (at +8), further header words; n entries of 0x20 bytes from +0x40 {be32 flags (0x10000 = compressed, 0 = raw), be32 padded size, 0, be32 offset in the .LXBD, 0, be32 offset of the NUL terminated path in the header file, be32 inflated size, be32 stored size}. A compressed member is stored as "RPCL", be32 1, "LLZ7", be32 inflated size, 16 zero bytes and a Nintendo LZ11 stream from +0x20. Members are written to "<file>.d/<path>" (members behind the end of a partial .LXBD are skipped).
static enumError extract_lxhd_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".lxhd"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *h = 0, *buf = 0;
	FILE *b = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const long hs = ftell (f);
		if (hs < 0x100 || hs > 0x10000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		h = MALLOC ((size_t)hs);
		if (fread (h, 1, (size_t)hs, f) != (size_t)hs || memcmp (h, "FMXL", 4))
			goto done;
		const u32 n = be32 (h + 8);
		if (!n || n > 1000000 || 0x40 + 0x20 * (size_t)n > (size_t)hs)
			goto done;
		char bpath[PATH_MAX];
		snprintf (bpath, sizeof (bpath), "%s", arg);
		const size_t al = strlen (bpath);
		memcpy (bpath + al - 4, "LXBD", 4);
		b = fopen (bpath, "rb");
		if (!b)
		{
			memcpy (bpath + al - 4, "lxbd", 4);
			b = fopen (bpath, "rb");
		}
		if (!b || fseeko (b, 0, SEEK_END))
			goto done;
		const off_t bs = ftello (b);
		for (uint i = 0; i < n; i++)
		{
			const u8 *e = h + 0x40 + 0x20 * i;
			const u32 no = be32 (e + 20);
			if (no >= (u32)hs || !memchr (h + no, 0, (size_t)hs - no))
				goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT LXHD:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		uint skipped = 0;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = h + 0x40 + 0x20 * i;
			const u32 flags = be32 (e), off = be32 (e + 12), ino = be32 (e + 24), sto = be32 (e + 28);
			ccp nm = (ccp)h + be32 (e + 20);
			while (*nm == '/' || *nm == '\\')
				nm++;
			if (!*nm || strstr (nm, "..") || (off_t)off + sto > bs || sto > 0x20000000)
			{
				skipped++;
				continue;
			}
			buf = MALLOC (sto ? sto : 1);
			if (fseeko (b, off, SEEK_SET) || fread (buf, 1, sto, b) != sto)
			{
				FREE (buf);
				buf = 0;
				skipped++;
				continue;
			}
			u8 *out = buf;
			uint outsz = sto;
			u8 *dec = 0;
			if (flags && sto > 0x24 && !memcmp (buf, "RPCL", 4) && !memcmp (buf + 8, "LLZ7", 4))
			{
				uint ds = 0;
				if (!DecodeLZ10LZ11 (&dec, &ds, buf + 0x20, sto - 0x20) && dec)
				{
					out = dec;
					outsz = ds;
				}
			}
			(void)ino;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s", dest, nm);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && outsz && fwrite (out, 1, outsz, F.f) != outsz)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", outsz, path);
			ResetFile (&F, opt_preserve);
			FREE (dec);
			FREE (buf);
			buf = 0;
		}
		if (skipped && verbose >= 0)
			fprintf (stdlog, "LXHD: %u members skipped (not in the .LXBD)\n", skipped);
	}
done:
	FREE (h);
	FREE (buf);
	if (b)
		fclose (b);
	fclose (f);
	return err;
}

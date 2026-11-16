// Nodame Cantabile: Dream Orchestra (Wii) score containers ("msf/<song>_msf.bin"), little endian:
//   "_MSF", u32 end of the payload (= 0x1d + DATA size), 17 more header bytes, "DATA", u32 DATA size, then a packed, unaligned event stream
//   (the file is padded to 0x4000 bytes). Event records mix small integers with names stored as {u16 length, NUL terminated ASCII}
//   (effect / animation ids such as "na_mizuiro", "ne_kiraspot" and instrument cues such as "violin_miki01_4"). The record layout itself is not
//   decoded; "<file>.txt" lists the header values and every name with its offset, in stream order.
static enumError extract_nodamemsf_file (ccp arg, ccp basedir, uint depth)
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
	if (sz < 0x30 || memcmp (raw, "_MSF", 4) || memcmp (raw + 0x15, "DATA", 4))
		goto done;
	{
		const u32 end = rd_le32 (raw + 4), dl = rd_le32 (raw + 0x19);
		if (dl < 4 || (u64)dl + 0x1d != end || end > sz)
			goto done;
		pcr_ctx_t cx;
		memset (&cx, 0, sizeof (cx));
		cx.d = raw;
		cx.sz = sz;
		cx.ok = true;
		pcr_emit (&cx, "_MSF payload end 0x%x DATA size 0x%x\nheader bytes 8..20:", end, dl);
		for (uint i = 8; i < 0x15; i++)
			pcr_emit (&cx, " %02x", raw[i]);
		pcr_emit (&cx, "\n\noffset\tname\n");
		uint cnt = 0;
		for (size_t p = 0x1d; p + 3 < end;)
		{
			const uint L = raw[p] | raw[p + 1] << 8;
			bool nm = L >= 3 && L <= 40 && p + 2 + L <= end && !raw[p + 1 + L] && raw[p + 2] >= 'a' && raw[p + 2] <= 'z';
			uint k = 0;
			bool us = false;
			for (; nm && k + 1 < L; k++)
			{
				const u8 ch = raw[p + 2 + k];
				if (ch == '_')
					us = true;
				if (!isalnum (ch) && ch != '_')
					nm = false;
			}
			if (nm && us)
			{
				pcr_emit (&cx, "0x%zx\t%s\n", p + 2, (ccp)raw + p + 2);
				cnt++;
				p += 2 + L;
			}
			else
				p++;
		}
		if (cx.ok && cnt)
		{
			char dest[PATH_MAX];
			snprintf (dest, sizeof (dest), "%s.txt", arg);
			if (testmode)
			{
				fprintf (stdlog, "WOULD EXTRACT NODAME_MSF:%s -> TXT:%s\n", arg, dest);
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
					fprintf (stdlog, "%sEXTRACT NODAME_MSF:%s -> TXT:%s\n", verbose > 0 ? "\n" : "", arg, dest);
			}
		}
		FREE (cx.out);
	}
done:
	FREE (raw);
	return err;
}

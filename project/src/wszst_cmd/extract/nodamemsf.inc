// Nodame Cantabile: Dream Orchestra (Wii) score containers ("msf/<song>_msf.bin"), little endian:
//   "_MSF", u32 end of the payload (= 0x1d + DATA size), 17 more header bytes, "DATA", u32 DATA size, then a packed, unaligned event stream
//   (the file is padded to 0x4000 bytes). Event records mix small integers with names stored as {u16 length, NUL terminated ASCII}
//   (effect / animation ids such as "na_mizuiro", "ne_kiraspot" and instrument cues such as "violin_miki01_4"). The stream is a sequence of records
//   {u32 tag, payload}: fixed sizes (total bytes incl. the tag) 2 = 14 (note: u16 index, u32 tick, u32 value), 0x0a = 13, 0x0b = 11, 0x0c = 11, 0x0f = 12,
//   0x11 = 13, 0x12 = 11, 0x13 = 11, 0x16 = 9, 0x17 = 5, 0x18 = 13, 0x2c = 20, 0x2d = 11, and the named tags 8 / 0x15 = {u8 channel, u16 length, NUL
//   terminated name} + 6 bytes. All 56 files parse exactly to the DATA end with this table. "<file>.txt" lists the header, then every record
//   (offset, tag, size, payload bytes, name); the meaning of the tags other than the note records is not known. Files that do not parse fully fall
//   back to a list of the embedded names.
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
		// full record parse
		{
			static const struct { u32 tag; uint size; } fixed[] = { { 2, 14 }, { 0x0a, 13 }, { 0x0b, 11 }, { 0x0c, 11 }, { 0x0f, 12 }, { 0x11, 13 },
				{ 0x12, 11 }, { 0x13, 11 }, { 0x16, 9 }, { 0x17, 5 }, { 0x18, 13 }, { 0x2c, 20 }, { 0x2d, 11 } };
			size_t q = 0x1d;
			uint nrec = 0;
			bool all = true;
			while (q + 4 <= end)
			{
				const u32 tag = rd_le32 (raw + q);
				size_t n = 0;
				if (tag == 8 || tag == 0x15)
				{
					if (q + 7 <= end)
						n = 7 + (size_t)(raw[q + 5] | raw[q + 6] << 8) + 6;
				}
				else
					for (uint k = 0; k < sizeof (fixed) / sizeof (*fixed); k++)
						if (fixed[k].tag == tag)
							n = fixed[k].size;
				if (!n || q + n > end)
				{
					all = false;
					break;
				}
				q += n, nrec++;
			}
			if (all && q == end)
			{
				cx.len = 0;
				pcr_emit (&cx, "_MSF payload end 0x%x DATA size 0x%x records %u\nheader bytes 8..20:", end, dl, nrec);
				for (uint i = 8; i < 0x15; i++)
					pcr_emit (&cx, " %02x", raw[i]);
				pcr_emit (&cx, "\n\noffset\ttag\tsize\tpayload\tname\n");
				q = 0x1d;
				while (q + 4 <= end)
				{
					const u32 tag = rd_le32 (raw + q);
					size_t n = 0;
					ccp name = "";
					if (tag == 8 || tag == 0x15)
					{
						n = 7 + (size_t)(raw[q + 5] | raw[q + 6] << 8) + 6;
						name = (ccp)raw + q + 7;
					}
					else
						for (uint k = 0; k < sizeof (fixed) / sizeof (*fixed); k++)
							if (fixed[k].tag == tag)
								n = fixed[k].size;
					pcr_emit (&cx, "0x%zx\t0x%x\t%zu\t", q, tag, n);
					const size_t pl = tag == 8 || tag == 0x15 ? 7 : n;
					for (size_t i = 4; i < pl; i++)
						pcr_emit (&cx, "%s%02x", i > 4 ? " " : "", raw[q + i]);
					if (tag == 2)
						pcr_emit (&cx, " (index %u tick %u value %u)", raw[q + 4] | raw[q + 5] << 8, rd_le32 (raw + q + 6), rd_le32 (raw + q + 10));
					pcr_emit (&cx, "\t%s\n", name);
					q += n;
				}
				goto write_out;
			}
		}
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
		write_out:
		if (cx.ok && (cnt || cx.len > 200))
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

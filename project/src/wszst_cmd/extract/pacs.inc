// Konami "PACS" sound packs (".sep" Mahjong Fight Club, "SND_DATA" packs inside Pro Evolution Soccer ".afs" archives; big-endian): "PACS", u32 size, u16 count, name, a chunk offset
// table and tagged chunks (PHED, PRAM, TONE, SEQ, BANK, WHED, ...). The wave table is a run of 0x50 byte "WVHD" records {tag, u32 data offset, u32 data size, u16 rate, u16 0,
// u32 samples, u32 start nibble, u32 end nibble, u32 0, u32 end + 1, 16 DSP coefficients (s16), u16 predictor/scale, u16 history 1, u16 history 2, loop predictor / histories};
// "WDAT" follows with the DSP-ADPCM data of all waves back to back. Each wave is written as "<n>.dsp" (a standard 0x60 byte DSP header is built from the record) and then decoded to WAV.
static enumError extract_pacs_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x100 || memcmp (raw, "PACS", 4) || rd_be32 (raw + 4) > sz + 0x40)
		goto done;
	{
		size_t wv = 0, wd = 0;
		for (size_t p = 0x20; p + 4 <= sz && p < 0x4000; p += 4)
		{
			if (!wv && !memcmp (raw + p, "WVHD", 4))
				wv = p;
			if (!memcmp (raw + p, "WDAT", 4))
			{
				wd = p + 4;
				break;
			}
		}
		if (!wv || !wd || wd < wv)
			goto done;
		uint n = 0;
		while (wv + 0x50 * (size_t)(n + 1) <= wd && !memcmp (raw + wv + 0x50 * (size_t)n, "WVHD", 4))
			n++;
		if (!n)
			goto done;
		for (uint i = 0; i < n; i++)
		{
			const u8 *r = raw + wv + 0x50 * (size_t)i;
			const u64 o = rd_be32 (r + 4), l = rd_be32 (r + 8);
			if (wd + o + l > sz || !l || rd_be16 (r + 0xc) < 4000)
				goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT PACS:%s (%u waves) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *r = raw + wv + 0x50 * (size_t)i;
			const u32 o = rd_be32 (r + 4), l = rd_be32 (r + 8);
			u8 *buf = CALLOC (1, 0x60 + (size_t)l);
			if (!buf)
			{
				err = ERR_OUT_OF_MEMORY;
				break;
			}
			// DSP header: samples, nibbles, rate, loop flag, format, start / end / current address, coefficients, gain, predictor / histories
			memcpy (buf + 0x00, r + 0x10, 4);
			buf[4] = (u8)(2 * l >> 24), buf[5] = (u8)(2 * l >> 16), buf[6] = (u8)(2 * l >> 8), buf[7] = (u8)(2 * l);
			buf[10] = r[0xc], buf[11] = r[0xd];
			memcpy (buf + 0x10, r + 0x14, 4);
			memcpy (buf + 0x14, r + 0x18, 4);
			memcpy (buf + 0x18, r + 0x14, 4);
			memcpy (buf + 0x1c, r + 0x24, 32);
			memcpy (buf + 0x3e, r + 0x44, 6);
			memcpy (buf + 0x44, r + 0x4a, 6);
			memcpy (buf + 0x60, raw + wd + o, l);
			char name[64];
			snprintf (name, sizeof (name), "%04u.dsp", i);
			err = shade_write (arg, dest, basedir, name, buf, 0x60 + l);
			FREE (buf);
		}
		if (!err && !testmode)
		{
			enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (raw);
	return err;
}

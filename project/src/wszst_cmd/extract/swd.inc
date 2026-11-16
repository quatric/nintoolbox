// Procyon Studio "swdb" sound banks (".swd", Arc Rise Fantasia, Wii, big-endian): 0x40 byte head "swdb" (u32 BE file size at +8, name at +0x20), then chunks
// {char tag[4], u32, u32, u32 BE length} + data ("wavi" sample table, "prgi", "kgrp", "pcmd", "eod "). The "pcmd" chunk holds the samples back to back, each one a
// complete 0x60 byte Nintendo DSP-ADPCM header (nibble count at +4) followed by the nibble data padded to 8 bytes; they are written as "<file>_<n>.dsp".
static enumError extract_swd_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".swd"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x60 || memcmp (raw, "swdb", 4) || rd_be32 (raw + 8) != sz)
		goto done;
	{
		size_t p = 0x50, d = 0, dl = 0;
		while (p + 16 <= sz)
		{
			const u32 l = rd_be32 (raw + p + 12);
			if (!memcmp (raw + p, "pcmd", 4))
			{
				d = p + 16, dl = l;
				break;
			}
			if (!memcmp (raw + p, "eod ", 4))
				break;
			// other chunks: length is followed by the data, padded to 16
			p += 16 + (((size_t)l + 15) & ~(size_t)15);
		}
		if (!d || d + dl > sz)
			goto done;
		uint n = 0;
		for (size_t o = 0; o + 0x60 <= dl;)
		{
			const u32 nib = rd_be32 (raw + d + o + 4), rate = rd_be32 (raw + d + o + 8);
			const size_t tot = 0x60 + (((size_t)nib + 1) / 2 + 7 & ~(size_t)7);
			if (!nib || rate < 4000 || rate > 96000 || o + tot > dl)
				break;
			n++, o += tot;
		}
		if (!n)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SWD:%s (%u samples) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		uint i = 0;
		for (size_t o = 0; o + 0x60 <= dl && i < n && !err && !testmode; i++)
		{
			const size_t tot = 0x60 + (((size_t)rd_be32 (raw + d + o + 4) + 1) / 2 + 7 & ~(size_t)7);
			char name[64];
			snprintf (name, sizeof (name), "%04u.dsp", i);
			err = shade_write (arg, dest, basedir, name, raw + d + o, (uint)tot);
			o += tot;
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

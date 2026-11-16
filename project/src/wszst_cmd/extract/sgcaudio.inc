// Racquet Sports / Racket Sports Party (and Springdale) ".SGC" speech banks, big-endian: {u16 1, u16 id, u32 count, u32 0, u32 0} followed by count * 16-byte entries {u32 name hash, u16 sample rate,
// u16 0x0800 = 8 bit, u32 offset, u32 size}; samples are unsigned 8-bit mono PCM. Written as "<file>.d/NNNN_<hash>.wav".
static enumError extract_sgcaudio_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".sgc"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	const u32 n = sz >= 0x20 ? rd_be32 (raw + 4) : 0;
	if (sz < 0x20 || rd_be16 (raw) != 1 || !n || n > 4096 || 0x10 + 16 * (u64)n > sz)
		goto done;
	for (u32 i = 0; i < n; i++)
	{
		const u8 *e = raw + 0x10 + 16 * (size_t)i;
		const u32 rate = rd_be16 (e + 4), bits = rd_be16 (e + 6), o = rd_be32 (e + 8), s = rd_be32 (e + 12);
		if (rate < 4000 || rate > 48000 || bits != 0x800 || o < 0x10 + 16 * n || (u64)o + s > sz || !s)
			goto done;
	}
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SGC:%s (%u sounds) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (u32 i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = raw + 0x10 + 16 * (size_t)i;
			const u32 rate = rd_be16 (e + 4), o = rd_be32 (e + 8), s = rd_be32 (e + 12);
			u8 h[44];
			memcpy (h, "RIFF", 4);
			h[4] = (36 + s) & 255, h[5] = (36 + s) >> 8 & 255, h[6] = (36 + s) >> 16 & 255, h[7] = (36 + s) >> 24;
			memcpy (h + 8, "WAVEfmt ", 8);
			memcpy (h + 16, "\x10\0\0\0\1\0\1\0", 8);
			h[24] = rate & 255, h[25] = rate >> 8, h[26] = h[27] = 0;
			h[28] = rate & 255, h[29] = rate >> 8, h[30] = h[31] = 0;
			memcpy (h + 32, "\1\0\x08\0data", 8);
			h[40] = s & 255, h[41] = s >> 8 & 255, h[42] = s >> 16 & 255, h[43] = s >> 24;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%04u_%08x.wav", dest, basedir ? basedir : "", i, rd_be32 (e));
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && (fwrite (h, 1, 44, F.f) != 44 || fwrite (raw + o, 1, s, F.f) != s))
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (raw);
	return err;
}

// Humongous Entertainment (SCUMM HE, e.g. Freddi Fish for Wii) speech bank ".he2" "game/*.HE2": a "TLKB" block {"TLKB", u32 BE total size} holding "TALK" blocks {"TALK", u32 BE size incl. header}; each TALK has an
// "HSHD" header block (payload: u16 0, u16 flags, u16 0xffff, u16 LE sample rate, ...) and an "SDAT" block of unsigned 8-bit mono PCM. Written as "<file>.d/NNNN.wav".
static enumError extract_he2talk_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".he2"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x30 || memcmp (raw, "TLKB", 4) || rd_be32 (raw + 4) != sz)
		goto done;
	{
		uint n = 0;
		for (size_t p = 8; p + 8 <= sz;)
		{
			const u32 s = rd_be32 (raw + p + 4);
			if (memcmp (raw + p, "TALK", 4) || s < 0x28 || p + s > sz)
				goto done;
			p += s;
			n++;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT HE2:%s (%u sounds) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		uint idx = 0;
		for (size_t p = 8; p + 8 <= sz && !err && !testmode;)
		{
			const u32 s = rd_be32 (raw + p + 4);
			const u8 *t = raw + p + 8;
			const size_t tl = s - 8;
			p += s;
			if (tl < 0x28 - 8 || memcmp (t, "HSHD", 4))
			{
				idx++;
				continue;
			}
			const u32 hs = rd_be32 (t + 4);
			if (hs < 8 + 8 || hs + 8 > tl || memcmp (t + hs, "SDAT", 4))
			{
				idx++;
				continue;
			}
			const uint rate = rd_le16 (t + 8 + 6);
			const u32 ds = rd_be32 (t + hs + 4);
			if (ds < 8 || hs + ds > tl || rate < 4000 || rate > 48000)
			{
				idx++;
				continue;
			}
			const u32 len = ds - 8;
			u8 h[44];
			memcpy (h, "RIFF", 4);
			h[4] = (36 + len) & 255, h[5] = (36 + len) >> 8 & 255, h[6] = (36 + len) >> 16 & 255, h[7] = (36 + len) >> 24;
			memcpy (h + 8, "WAVEfmt ", 8);
			memcpy (h + 16, "\x10\0\0\0\1\0\1\0", 8);
			h[24] = rate & 255, h[25] = rate >> 8, h[26] = h[27] = 0;
			h[28] = rate & 255, h[29] = rate >> 8, h[30] = h[31] = 0;
			memcpy (h + 32, "\1\0\x08\0data", 8);
			h[40] = len & 255, h[41] = len >> 8 & 255, h[42] = len >> 16 & 255, h[43] = len >> 24;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%04u.wav", dest, basedir ? basedir : "", idx++);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && (fwrite (h, 1, 44, F.f) != 44 || fwrite (t + hs + 8, 1, len, F.f) != len))
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", len, path);
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (raw);
	return err;
}

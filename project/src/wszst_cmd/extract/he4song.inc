// Humongous Entertainment (Freddi Fish, Wii) "*.HE4" music files: {"SONG", u32 BE size, "SGHD" header block, one "SGEN" block per song {u32 BE block size 0x15, u32 LE id, u32 LE offset, u32 LE size, u8}}, followed by
// the songs, each a "DIGI" block (HSHD header + SDAT unsigned 8-bit mono PCM, rate = LE u16 at HSHD payload +6). Written as "<file>.d/song_<id>.wav".
static enumError extract_he4song_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".he4"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 || memcmp (raw, "SONG", 4) || memcmp (raw + 8, "SGHD", 4))
		goto done;
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		uint n = 0;
		err = ERR_OK;
		for (size_t p = 8; p + 8 <= sz && !err;)
		{
			const u32 bs = rd_be32 (raw + p + 4);
			if (bs < 8 || p + bs > sz)
				break;
			const bool sgen = !memcmp (raw + p, "SGEN", 4);
			if (!sgen && memcmp (raw + p, "SGHD", 4))
				break;
			const size_t q = p;
			p += bs;
			if (!sgen || bs < 8 + 12)
				continue;
			const u32 id = rd_le32 (raw + q + 8), off = rd_le32 (raw + q + 12), size = rd_le32 (raw + q + 16);
			if ((u64)off + size > sz || size < 0x30 || memcmp (raw + off, "DIGI", 4))
				continue;
			const u8 *t = raw + off + 8;
			const size_t tl = size - 8;
			if (memcmp (t, "HSHD", 4))
				continue;
			const u32 hs = rd_be32 (t + 4);
			if (hs < 16 || hs + 8 > tl || memcmp (t + hs, "SDAT", 4))
				continue;
			const uint rate = rd_le16 (t + 8 + 6);
			const u32 ds = rd_be32 (t + hs + 4);
			if (ds < 8 || hs + ds > tl || rate < 4000 || rate > 48000)
				continue;
			n++;
			if (testmode)
				continue;
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
			snprintf (path, sizeof (path), "%s/%ssong_%u.wav", dest, basedir ? basedir : "", id);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && (fwrite (h, 1, 44, F.f) != 44 || fwrite (t + hs + 8, 1, len, F.f) != len))
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", len, path);
			ResetFile (&F, opt_preserve);
		}
		if (!n && !err)
			err = ERR_NOTHING_TO_DO;
		else if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT HE4:%s (%u songs) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
	}
done:
	FREE (raw);
	return err;
}

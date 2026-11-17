// Humongous Entertainment (SCUMM HE) resource file "<GAME>.(a)" (Freddi Fish, ...): the whole file is XORed with 0x69; decoded it is a chain "LECF" > "LFLF" (one per room) > resource blocks {tag, u32 BE
// size incl. header}. The "DIGI" sound blocks (HSHD header + SDAT unsigned 8-bit PCM, rate = LE u16 at HSHD payload +6) are written as "<file>.d/room_NN/digi_NNNN.wav". Other resources (AWIZ, AKOS, SCRP,
// RMIM, CHAR, ...) are not extracted.
static enumError extract_hegame_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".(a)"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 || (raw[0] ^ 0x69) != 'L' || (raw[1] ^ 0x69) != 'E' || (raw[2] ^ 0x69) != 'C' || (raw[3] ^ 0x69) != 'F')
		goto done;
	for (size_t i = 0; i < sz; i++)
		raw[i] ^= 0x69;
	if (rd_be32 (raw + 4) != sz)
		goto done;
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		uint nrooms = 0, nsnd = 0;
		err = ERR_OK;
		for (size_t p = 8; p + 8 <= sz && !err; nrooms++)
		{
			const u32 s = rd_be32 (raw + p + 4);
			if (memcmp (raw + p, "LFLF", 4) || s < 8 || p + s > sz)
				break;
			uint ns = 0;
			for (size_t q = p + 8; q + 8 <= p + s && !err;)
			{
				const u32 s2 = rd_be32 (raw + q + 4);
				if (s2 < 8 || q + s2 > p + s)
					break;
				const u8 *t = raw + q + 8;
				const size_t tl = s2 - 8;
				q += s2;
				if (memcmp (raw + q - s2, "DIGI", 4) || tl < 0x20 || memcmp (t, "HSHD", 4))
					continue;
				const u32 hs = rd_be32 (t + 4);
				if (hs < 16 || hs + 8 > tl || memcmp (t + hs, "SDAT", 4))
					continue;
				const uint rate = rd_le16 (t + 8 + 6);
				const u32 ds = rd_be32 (t + hs + 4);
				if (ds < 8 || hs + ds > tl || rate < 4000 || rate > 48000)
					continue;
				nsnd++;
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
				snprintf (path, sizeof (path), "%s/%sroom_%02u/digi_%04u.wav", dest, basedir ? basedir : "", nrooms, ns++);
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (F.f && (fwrite (h, 1, 44, F.f) != 44 || fwrite (t + hs + 8, 1, len, F.f) != len))
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", len, path);
				ResetFile (&F, opt_preserve);
			}
			p += s;
		}
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT HE-GAME:%s (%u rooms, %u sounds) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nrooms, nsnd, dest);
	}
done:
	FREE (raw);
	return err;
}

// Indiana Jones and the Staff of Kings (Wii) "Fate/<lang>/MONSTER.SOU" (the bundled LucasArts "Fate of Atlantis" voice file, SCUMM v5 style): "SOU " + 0, a "VCTL" chunk (u32 BE size, offset table), then
// thousands of Creative Voice Files ("Creative Voice File\x1a", 26-byte header, blocks {u8 type, u24le size}) back to back. Type 1 blocks (u8 frequency divisor, u8 codec 0 = 8-bit unsigned PCM) and type 2 continuations
// are joined into a mono 8-bit WAV (rate = 1000000 / (256 - divisor)) written to "<file>.d/NNNNN.wav"; VOCs with other codecs are skipped.
static enumError extract_sousound_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".sou") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	static const char magic[] = "Creative Voice File\x1a";
	const size_t mlen = sizeof (magic) - 1;
	if (sz < 0x100 || memcmp (raw, "SOU ", 4))
		goto done;
	{
		uint n = 0;
		for (size_t p = 0; p + 26 < sz;)
		{
			const u8 *m = memmem (raw + p, sz - p, magic, mlen);
			if (!m)
				break;
			p = (size_t)(m - raw);
			size_t q = p + 26;
			u32 rate = 0;
			size_t total = 0;
			u8 *pcm = 0;
			size_t cap = 0;
			bool bad = false;
			size_t endq = q;
			for (uint pass = 0; pass < 2 && !bad; pass++)
			{
				q = p + 26;
				rate = 0;
				size_t w = 0;
				while (q + 4 <= sz)
				{
					const uint type = raw[q];
					if (!type)
						break;
					const size_t len = raw[q + 1] | (raw[q + 2] << 8) | (raw[q + 3] << 16);
					if (q + 4 + len > sz)
					{
						bad = true;
						break;
					}
					const u8 *b = raw + q + 4;
					const u8 *src = 0;
					size_t sl = 0;
					if (type == 1 && len >= 2)
					{
						if (b[1] || rate)
						{
							bad = true;
							break;
						}
						rate = 1000000 / (256 - b[0]);
						src = b + 2;
						sl = len - 2;
					}
					else if (type == 2 && rate)
					{
						src = b;
						sl = len;
					}
					if (src)
					{
						if (pass)
							memcpy (pcm + w, src, sl);
						w += sl;
					}
					q += 4 + len;
				}
				if (!pass && !bad)
				{
					cap = w;
					endq = q;
					pcm = MALLOC (cap + 1);
					if (!pcm)
						bad = true;
				}
			}
			q = endq;
			total = cap;
			p = q > p + 26 ? q : p + mlen;
			if (!bad && pcm && rate && total)
			{
				if (!n)
				{
					char dest0[PATH_MAX];
					beside_source_dest (dest0, sizeof (dest0), arg);
					if (verbose >= 0 || testmode)
						fprintf (stdlog, "%s%sEXTRACT SOU:%s -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest0);
					err = ERR_OK;
				}
				if (!testmode)
				{
					char dest[PATH_MAX], path[PATH_MAX];
					beside_source_dest (dest, sizeof (dest), arg);
					snprintf (path, sizeof (path), "%s/%s%05u.wav", dest, basedir ? basedir : "", n);
					u8 h[44];
					memcpy (h, "RIFF", 4);
					wr_le32 (h + 4, (u32)(36 + total));
					memcpy (h + 8, "WAVEfmt ", 8);
					wr_le32 (h + 16, 16);
					h[20] = 1; h[21] = 0; h[22] = 1; h[23] = 0;
					wr_le32 (h + 24, rate);
					wr_le32 (h + 28, rate);
					h[32] = 1; h[33] = 0; h[34] = 8; h[35] = 0;
					memcpy (h + 36, "data", 4);
					wr_le32 (h + 40, (u32)total);
					File_t F;
					const enumError e2 = CreateFileOpt (&F, true, path, false, arg);
					if (e2)
						err = e2;
					else if (F.f && (fwrite (h, 1, sizeof (h), F.f) != sizeof (h) || fwrite (pcm, 1, total, F.f) != total))
						err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", total, path);
					ResetFile (&F, opt_preserve);
				}
				n++;
			}
			FREE (pcm);
			if (err && err != ERR_NOTHING_TO_DO)
				break;
		}
	}
done:
	FREE (raw);
	return err;
}

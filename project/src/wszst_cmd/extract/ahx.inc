// CRI AHX (".ahx", about 200 DS files): a CRI ADX header (0x8000, u16 copyright offset, u8 encoding at 4, "(c)CRI" ending at offset + 4) whose encoding 0x10 or 0x11 means the payload is plain MPEG audio layer II frames. The frames are written as "<file>.d/<name>.mp2" (playable as is; verified with ffprobe: 22.05 kHz mono). Encoding 3 (ADX ADPCM, 4 bit, 18 byte frames per channel, high pass coefficients from the cutoff at 0x10) is decoded to 16 bit "<name>.wav".
static u8 *adx_wav (const u8 *raw, size_t sz, size_t o, size_t *outn)
{
	const uint ch = raw[7], bs = raw[5], sr = rd_be32 (raw + 8);
	size_t ns = rd_be32 (raw + 12);
	if (raw[4] != 3 || bs != 18 || raw[6] != 4 || !ch || ch > 8 || !sr || !ns)
		return 0;
	if (ns > (sz - o) / (ch * bs) * 32)
		ns = (sz - o) / (ch * bs) * 32; // truncated file
	if (!ns)
		return 0;
	const size_t frames = (ns + 31) / 32, dn = ns * ch * 2;
	u8 *w = CALLOC (1, 44 + dn);
	if (!w)
		return 0;
	const double x = (double)rd_be16 (raw + 0x10) / sr, a = M_SQRT2 - cos (2 * M_PI * x), b = M_SQRT2 - 1,
				 c = (a - sqrt ((a + b) * (a - b))) / b;
	const int c1 = (int)floor (c * 8192), c2 = (int)floor (-c * c * 4096);
	memcpy (w, "RIFF", 4);
	wr_le32 (w + 4, 36 + (u32)dn);
	memcpy (w + 8, "WAVEfmt ", 8);
	wr_le32 (w + 16, 16);
	wr_le16 (w + 20, 1);
	wr_le16 (w + 22, ch);
	wr_le32 (w + 24, sr);
	wr_le32 (w + 28, sr * ch * 2);
	wr_le16 (w + 32, ch * 2);
	wr_le16 (w + 34, 16);
	memcpy (w + 36, "data", 4);
	wr_le32 (w + 40, (u32)dn);
	int h1[8] = {0}, h2[8] = {0};
	for (size_t f = 0; f < frames; f++)
		for (uint k = 0; k < ch; k++)
		{
			const u8 *fr = raw + o + (f * ch + k) * bs;
			const int sc = (int)(rd_be16 (fr) & 0x1fff);
			for (uint i = 0; i < 32; i++)
			{
				int n = i & 1 ? fr[2 + i / 2] & 15 : fr[2 + i / 2] >> 4;
				if (n > 7)
					n -= 16;
				int v = n * sc + ((c1 * h1[k] + c2 * h2[k]) >> 12);
				v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
				h2[k] = h1[k];
				h1[k] = v;
				const size_t si = f * 32 + i;
				if (si < ns)
					wr_le16 (w + 44 + (si * ch + k) * 2, (u16)v);
			}
		}
	*outn = 44 + dn;
	return w;
}

static enumError extract_ahx_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0, *wav = 0;
	size_t sz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		goto done;
	if (sz < 0x24 || raw[0] != 0x80 || raw[1] || (raw[4] != 3 && (raw[4] < 0x10 || raw[4] > 0x11)))
		goto done;
	const size_t o = (size_t)rd_be16 (raw + 2) + 4;
	if (o < 0x18 || o + 4 > sz || memcmp (raw + o - 6, "(c)CRI", 6))
		goto done;
	size_t wn = 0;
	if (raw[4] == 3)
		wav = adx_wav (raw, sz, o, &wn);
	else if (raw[o] != 0xff || (raw[o + 1] & 0xe0) != 0xe0)
		goto done;
	if (raw[4] == 3 && !wav)
		goto done;
	nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	const char *s = strrchr (arg, '/');
	char name[PATH_MAX];
	snprintf (name, sizeof (name), wav ? "%s.wav" : "%s.mp2", s ? s + 1 : arg);
	if (OwnedEntryAdd (entries, 0, name, wav ? wav : raw + o, wav ? (uint)wn : (uint)(sz - o)))
		err = write_owned_entries (arg, basedir, depth, "AHX", entries, 1);
	else
		ResetOwnedEntries (entries, 1);
done:
	FREE (wav);
	FREE (raw);
	return err;
}

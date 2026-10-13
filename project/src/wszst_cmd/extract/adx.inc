// CRI ADX audio (".adx": Ghost Squad, Arc Rise Fantasia ...): 0x8000 signature, u16 copyright offset
// (sample data at offset + 4), u8 type (3 = standard; the exponential type 4 is not handled), u8 frame bytes (18), u8 bit
// depth (4), u8 channels, u32 rate, u32 samples, u16 highpass. A frame holds a 13 bit scale and 32
// 4-bit residuals per channel (channels alternate frame by frame); the predictor is a fixed 2-tap filter
// derived from the highpass frequency. Written as "audio.wav" (16-bit PCM, loops are not expanded).
static enumError extract_adx_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".adx") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint ctype = sz >= 0x20 ? raw[4] : 0, fb = sz >= 0x20 ? raw[5] : 0, bits = sz >= 0x20 ? raw[6] : 0,
		   ch = sz >= 0x20 ? raw[7] : 0, rate = sz >= 0x20 ? rd_be32 (raw + 8) : 0, hp = sz >= 0x20 ? rd_be16 (raw + 0x10) : 0,
		   flags = sz >= 0x20 ? raw[0x13] : 0;
	const size_t total = sz >= 0x20 ? rd_be32 (raw + 12) : 0, start = sz >= 0x20 ? (size_t)rd_be16 (raw + 2) + 4 : 0;
	if (sz < 0x20 || raw[0] != 0x80 || raw[1] || ctype != 3 || fb != 18 || bits != 4 || !ch || ch > 8
		|| rate < 4000 || rate > 192000 || !total || total > 0x10000000 || start < 0x14 || start >= sz || (flags & 8))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const double z = cos (2.0 * M_PI * hp / rate), a = M_SQRT2 - z, b = M_SQRT2 - 1.0;
	const double c = (a - sqrt ((a + b) * (a - b))) / b;
	const int c1 = (int)(c * 8192.0), c2 = (int)(-(c * c) * 4096.0);
	const size_t data = total * ch * 2;
	u8 *wav = MALLOC (44 + data);
	if (!wav)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	memcpy (wav, "RIFF", 4);
	wr_le32 (wav + 4, (u32)(36 + data));
	memcpy (wav + 8, "WAVEfmt ", 8);
	wr_le32 (wav + 16, 16);
	wr_le16 (wav + 20, 1);
	wr_le16 (wav + 22, ch);
	wr_le32 (wav + 24, rate);
	wr_le32 (wav + 28, rate * ch * 2);
	wr_le16 (wav + 32, ch * 2);
	wr_le16 (wav + 34, 16);
	memcpy (wav + 36, "data", 4);
	wr_le32 (wav + 40, (u32)data);
	memset (wav + 44, 0, data);
	int h1[8] = { 0 }, h2[8] = { 0 };
	const size_t fr = (total + 31) / 32;
	for (size_t f = 0; f < fr && start + (f + 1) * ch * 18 <= sz; f++)
		for (uint k = 0; k < ch; k++)
		{
			const u8 *p = raw + start + (f * ch + k) * 18;
			const int scale = ((p[0] << 8) | p[1]) & 0x1fff;
			for (uint i = 0; i < 32; i++)
			{
				const size_t pos = f * 32 + i;
				int v = (i & 1) ? p[2 + i / 2] & 15 : p[2 + i / 2] >> 4;
				if (v >= 8)
					v -= 16;
				int s = v * (scale + 1) + ((c1 * h1[k] + c2 * h2[k]) >> 12);
				s = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
				h2[k] = h1[k];
				h1[k] = s;
				if (pos < total)
					wr_le16 (wav + 44 + (pos * ch + k) * 2, (u16)(s16)s);
			}
		}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ADX:%s (%u ch, %u Hz) -> %s/audio.wav\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			arg, ch, rate, dest);
	enumError err = ERR_OK;
	if (!testmode)
		err = shade_write (arg, dest, basedir, "audio.wav", wav, (uint)(44 + data));
	FREE (wav);
	FREE (raw);
	return err;
}

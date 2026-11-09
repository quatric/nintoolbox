// Sony "SCEI" sound banks ported from the PS2 engine (Momotarou Dentetsu 2010 "data/sound.bin"; little endian, 0x800 sectors):
// the file starts "INIT"; every bank is a sector starting "BBNK" (BD offset u32 @0x1c from the bank start, 0x2800) or "BANK" (BD offset @0x14,
// 0x800), followed by "WAVE" and a PS2 ".hd" image at +0x40 ("SCEIVers", "SCEIHead" {u32 chunk size, u32 hd size,
// u32 bd size @0x10}, "SCEIVagi" {u32 chunk size, u32 count, count x u32 entry offsets from the chunk start}, "SCEIPrgm", ...).
// A Vagi entry is {u32 byte offset into the BD, u16 sample rate, u8 flag, u8 0xff}; the BD holds PS-ADPCM frames (16 bytes: shift |
// predictor << 4, flags (bit 0 = end), 14 data bytes = 28 samples, low nibble first). Every sample becomes "bNN_sNNN.wav" (mono 16 bit).
static bool sceibank_find (const u8 *d, size_t sz, size_t p, size_t *vagi, size_t *bd, size_t *bdsz)
{
	const bool big = p + 4 <= sz && !memcmp (d + p, "BBNK", 4);
	if (p + 0x60 > sz || (!big && memcmp (d + p, "BANK", 4)) || memcmp (d + p + 0x20, "WAVE", 4) || memcmp (d + p + 0x40, "IECSsreV", 8)
		|| memcmp (d + p + 0x50, "IECSdaeH", 8))
		return false;
	*bd = p + rd_le32 (d + p + (big ? 0x1c : 0x14));
	*bdsz = rd_le32 (d + p + 0x50 + 0x10);
	if (*bd < p + 0x100 || *bd > sz || *bdsz > sz - *bd)
		return false;
	for (size_t q = p + 0x60; q + 16 < *bd && q < p + 0x2800; q += 16)
		if (!memcmp (d + q, "IECSigaV", 8))
		{
			*vagi = q;
			return true;
		}
	return false;
}

static enumError extract_sceibank_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 hd[4];
	const size_t got = fread (hd, 1, 4, f);
	fclose (f);
	if (got != 4 || memcmp (hd, "INIT", 4))
		return ERR_NOTHING_TO_DO;
	u8 *d = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &d, &sz, 0, 0, 0, false) || !d || sz < 0x1000 || sz > 0x40000000 || (memcmp (d + 0x800, "BBNK", 4) && memcmp (d + 0x800, "BANK", 4)))
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	enumError err = ERR_OK;
	uint banks = 0, waves = 0;
	for (size_t p = 0x800; p + 0x800 <= sz && !err; p += 0x800)
	{
		size_t vagi, bd, bdsz;
		if (!sceibank_find (d, sz, p, &vagi, &bd, &bdsz))
			continue;
		const uint n = rd_le32 (d + vagi + 12);
		if (!n || n > 4096 || 16 + 4 * (size_t)n > rd_le32 (d + vagi + 8))
			continue;
		banks++;
		for (uint i = 0; i < n && !err; i++)
		{
			const size_t eo = rd_le32 (d + vagi + 16 + 4 * (size_t)i);
			if (vagi + eo + 8 > bd)
				continue;
			const u8 *e = d + vagi + eo;
			const size_t off = rd_le32 (e);
			const uint rate = rd_le16 (e + 4);
			// the stream ends at the next larger offset (or the BD end)
			size_t end = bdsz;
			for (uint k = 0; k < n; k++)
			{
				const size_t ko = rd_le32 (d + vagi + 16 + 4 * (size_t)k);
				if (vagi + ko + 8 > bd)
					continue;
				const size_t v = rd_le32 (d + vagi + ko);
				if (v > off && v < end)
					end = v;
			}
			if (off >= bdsz || end <= off || end - off < 16 || rate < 1000 || rate > 96000)
				continue;
			const size_t frames = (end - off) / 16;
			u8 *w = MALLOC (44 + frames * 56 + 4);
			if (!w)
			{
				err = ERR_OUT_OF_MEMORY;
				break;
			}
			static const int k0[5] = { 0, 60, 115, 98, 122 }, k1[5] = { 0, 0, -52, -55, -60 };
			int h1 = 0, h2 = 0;
			size_t ns = 0;
			for (size_t fr = 0; fr < frames; fr++)
			{
				const u8 *q = d + bd + off + 16 * fr;
				const uint sh = q[0] & 15, pr = (q[0] >> 4) > 4 ? 0 : q[0] >> 4;
				if (q[1] & 4 && fr == 0)
					h1 = h2 = 0;
				for (uint s = 0; s < 28; s++)
				{
					int nib = s & 1 ? q[2 + s / 2] >> 4 : q[2 + s / 2] & 15;
					if (nib >= 8)
						nib -= 16;
					int v = ((nib << 12) >> (sh > 12 ? 12 : sh)) + ((h1 * k0[pr] + h2 * k1[pr]) >> 6);
					v = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
					h2 = h1;
					h1 = v;
					w[44 + 2 * ns] = (u8)v, w[45 + 2 * ns] = (u8)(v >> 8);
					ns++;
				}
				if (q[1] & 1)
					break;
			}
			memcpy (w, "RIFF", 4);
			wr_le32 (w + 4, (u32)(36 + 2 * ns));
			memcpy (w + 8, "WAVEfmt ", 8);
			wr_le32 (w + 16, 16);
			wr_le16 (w + 20, 1);
			wr_le16 (w + 22, 1);
			wr_le32 (w + 24, rate);
			wr_le32 (w + 28, rate * 2);
			wr_le16 (w + 32, 2);
			wr_le16 (w + 34, 16);
			memcpy (w + 36, "data", 4);
			wr_le32 (w + 40, (u32)(2 * ns));
			char name[40];
			snprintf (name, sizeof (name), "b%02u_s%03u.wav", banks - 1, i);
			if (!testmode)
				err = shade_write (arg, dest, basedir, name, w, (uint)(44 + 2 * ns));
			waves++;
			FREE (w);
		}
	}
	FREE (d);
	if (!banks)
		return ERR_NOTHING_TO_DO;
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SCEI-BANK:%s (%u banks, %u samples) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, banks, waves, dest);
	return err;
}

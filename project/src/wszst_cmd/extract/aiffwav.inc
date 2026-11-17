// Plain AIFF / AIFF-C PCM audio (".aif", ".aiff", ".aifc"; All Star Cheerleader 2, Blob, ...): "FORM" <size> "AIFF"|"AIFC", a "COMM" chunk {s16 channels, u32 frames, s16 bits, 80-bit extended sample rate
// [, AIFC compression type "NONE" | "sowt"]} and an "SSND" chunk {u32 offset, u32 block size, data}. 8/16/24/32-bit big-endian PCM (little-endian for "sowt") is written as "<file>.wav". Compressed
// AIFC types (ima4, ulaw, ...) are left alone.
static enumError extract_aiffwav_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".aif") && !is_ext (arg, ".aiff") && !is_ext (arg, ".aifc"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *out = 0;
	size_t sz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0x30 || memcmp (raw, "FORM", 4) || (memcmp (raw + 8, "AIFF", 4) && memcmp (raw + 8, "AIFC", 4)))
		goto done;
	{
		const bool aifc = !memcmp (raw + 8, "AIFC", 4);
		uint ch = 0, bits = 0, rate = 0;
		u32 frames = 0;
		bool le = false;
		const u8 *data = 0;
		size_t dlen = 0;
		for (size_t o = 12; o + 8 <= sz;)
		{
			const u32 l = rd_be32 (raw + o + 4);
			const u8 *c = raw + o + 8;
			if (o + 8 + (u64)l > sz && memcmp (raw + o, "SSND", 4))
				break;
			if (!memcmp (raw + o, "COMM", 4) && l >= 18)
			{
				ch = rd_be16 (c);
				frames = rd_be32 (c + 2);
				bits = rd_be16 (c + 6);
				const int ex = ((c[8] & 0x7f) << 8 | c[9]) - 16383;
				const u64 mant = (u64)rd_be32 (c + 10) << 32 | rd_be32 (c + 14);
				rate = ex >= 0 && ex < 40 ? (uint)(mant >> (63 - ex)) : 0;
				if (aifc && l >= 22)
				{
					if (!memcmp (c + 18, "sowt", 4))
						le = true;
					else if (memcmp (c + 18, "NONE", 4) && memcmp (c + 18, "twos", 4))
						goto done;
				}
			}
			else if (!memcmp (raw + o, "SSND", 4) && l >= 8)
			{
				const u32 off = rd_be32 (c);
				if ((u64)o + 16 + off <= sz)
				{
					data = c + 8 + off;
					dlen = (size_t)l - 8 - off;
					if ((size_t)(data - raw) + dlen > sz)
						dlen = sz - (size_t)(data - raw);
				}
			}
			o += 8 + (size_t)l + (l & 1);
		}
		if (!ch || ch > 8 || !data || !rate || rate > 192000 || (bits != 8 && bits != 16 && bits != 24 && bits != 32))
			goto done;
		const uint bps = bits / 8;
		size_t len = (size_t)frames * ch * bps;
		if (len > dlen)
			len = dlen - dlen % (ch * bps);
		out = MALLOC (len + 1);
		if (!out)
			goto done;
		for (size_t i = 0; i + bps <= len; i += bps)
		{
			if (bps == 1)
				out[i] = data[i] ^ 0x80;
			else
				for (uint b = 0; b < bps; b++)
					out[i + b] = le ? data[i + b] : data[i + bps - 1 - b];
		}
		char wav[PATH_MAX];
		snprintf (wav, sizeof (wav), "%s.wav", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE AIFF:%s (%u ch, %u bit, %u Hz) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, bits, rate, wav);
		err = ERR_OK;
		if (!testmode)
		{
			u8 h[44];
			memcpy (h, "RIFF", 4);
			wr_le32 (h + 4, (u32)(36 + len));
			memcpy (h + 8, "WAVEfmt ", 8);
			wr_le32 (h + 16, 16);
			h[20] = 1, h[21] = 0, h[22] = (u8)ch, h[23] = 0;
			wr_le32 (h + 24, rate);
			wr_le32 (h + 28, rate * ch * bps);
			h[32] = (u8)(ch * bps), h[33] = 0, h[34] = (u8)bits, h[35] = 0;
			memcpy (h + 36, "data", 4);
			wr_le32 (h + 40, (u32)len);
			File_t F;
			err = CreateFileOpt (&F, true, wav, false, arg);
			if (F.f && (fwrite (h, 1, 44, F.f) != 44 || fwrite (out, 1, len, F.f) != len))
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)len, wav);
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (raw);
	FREE (out);
	return err;
}

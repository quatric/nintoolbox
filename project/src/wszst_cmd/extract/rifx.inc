// Big-endian "RIFX" WAVE files with integer PCM (".wax" sound effects of the 2 in 1 / Tierpension style games, all
// 16-bit mono 48 kHz; also ".rifx"): converted to a standard little-endian RIFF "<file>.wav" in decode mode.
static enumError extract_rifx_file (ccp arg)
{
	if ((export_count <= 0 && !opt_decode) || !(is_ext (arg, ".wax") || is_ext (arg, ".rifx")))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u8 *fmt = 0, *dat = 0;
	size_t dsz = 0;
	if (sz >= 12 && !memcmp (raw, "RIFX", 4) && !memcmp (raw + 8, "WAVE", 4))
		for (size_t p = 12; p + 8 <= sz;)
		{
			const size_t csz = rd_be32 (raw + p + 4);
			if (csz > sz - p - 8)
				break;
			if (!memcmp (raw + p, "fmt ", 4) && csz >= 16)
				fmt = raw + p + 8;
			else if (!memcmp (raw + p, "data", 4))
			{
				dat = raw + p + 8;
				dsz = csz;
			}
			p += 8 + csz + (csz & 1);
		}
	const uint ch = fmt ? rd_be16 (fmt + 2) : 0, bits = fmt ? rd_be16 (fmt + 14) : 0;
	if (!fmt || !dat || rd_be16 (fmt) != 1 || !ch || ch > 8 || (bits != 8 && bits != 16) || dsz < bits / 8)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	dsz -= dsz % (bits / 8 * ch);
	u8 *wav = MALLOC (44 + dsz);
	if (!wav)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	const u32 rate = rd_be32 (fmt + 4);
	memcpy (wav, "RIFF", 4);
	wr_le32 (wav + 4, (u32)(36 + dsz));
	memcpy (wav + 8, "WAVEfmt ", 8);
	wr_le32 (wav + 16, 16);
	wr_le16 (wav + 20, 1);
	wr_le16 (wav + 22, (u16)ch);
	wr_le32 (wav + 24, rate);
	wr_le32 (wav + 28, rate * ch * bits / 8);
	wr_le16 (wav + 32, (u16)(ch * bits / 8));
	wr_le16 (wav + 34, (u16)bits);
	memcpy (wav + 36, "data", 4);
	wr_le32 (wav + 40, (u32)dsz);
	if (bits == 16)
		for (size_t i = 0; i + 1 < dsz; i += 2)
		{
			wav[44 + i] = dat[i + 1];
			wav[45 + i] = dat[i];
		}
	else
		memcpy (wav + 44, dat, dsz);
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".wav");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE RIFX:%s (%u ch, %u Hz, %u bit) -> WAV:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, ch, rate, bits, dest);
	enumError err = ERR_OK;
	if (!testmode)
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f && fwrite (wav, 1, (uint)(44 + dsz), F.f) != (uint)(44 + dsz))
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing failed: %s\n", dest);
			ResetFile (&F, opt_preserve);
		}
	FREE (wav);
	FREE (raw);
	return err;
}

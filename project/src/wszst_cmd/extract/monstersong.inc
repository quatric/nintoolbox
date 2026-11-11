// Monster Games streamed music ".sng" / ".sn0" (ExciteBots): the music is split between this file and a ".sf0" next to it with the
// same name. Header: s32 LE sample rate (32000) @0, u32 0 @4, u32 LE size of the rest of every channel (held in the .sf0) @8, u32 LE
// interleave @0xc, u32 0 @0x10; the first blocks (2 channels x interleave bytes of big-endian PCM16) follow at 0x14, the rest of the
// audio is the .sf0. The stereo PCM is de-interleaved and written as "<name>.wav".
static enumError extract_monstersong_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if ((!is_ext (arg, ".sng") && !is_ext (arg, ".sn0")) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *rest = 0;
	size_t sz = 0, rsz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const u32 rate = rd_le32 (raw), chan = rd_le32 (raw + 8), il = rd_le32 (raw + 12);
	if (sz < 0x20 || rate != 32000 || rd_be32 (raw + 4) || rd_be32 (raw + 0x10) || !il || il & 1 || il > 0x100000
		|| (u64)il * 2 + 0x14 != sz)
		goto done;
	char sf0[PATH_MAX];
	snprintf (sf0, sizeof (sf0), "%s", arg);
	char *dot = strrchr (sf0, '.');
	if (!dot)
		goto done;
	strcpy (dot, ".sf0");
	if (LoadFileAlloc (sf0, 0, 0, &rest, &rsz, 0, 0, 0, false) || !rest || rsz < chan * 2u)
		goto done;
	const size_t il2 = il, total = (size_t)il2 * 2 + (size_t)chan * 2;
	u8 *pcm = MALLOC (total ? total : 1);
	u8 *wav = MALLOC (44 + total);
	if (!pcm || !wav)
	{
		FREE (pcm);
		FREE (wav);
		goto done;
	}
	// joined stream: .sng payload then .sf0 (only the first chan*2 bytes are audio)
	memcpy (pcm, raw + 0x14, il2 * 2);
	memcpy (pcm + il2 * 2, rest, (size_t)chan * 2);
	const size_t frames = total / 4;
	size_t pos = 0, o = 44;
	for (size_t f = 0; f < frames; )
	{
		// one interleave block per channel; the last one may be shorter
		size_t blk = il2 / 2; // frames per block
		if (blk > frames - f)
			blk = frames - f;
		for (size_t k = 0; k < blk; k++)
			for (uint c = 0; c < 2; c++)
			{
				const u8 *s = pcm + pos + c * (blk * 2) + k * 2;
				wav[o++] = s[1];
				wav[o++] = s[0];
			}
		pos += blk * 4;
		f += blk;
	}
	const u32 dsz = (u32)(o - 44);
	memcpy (wav, "RIFF", 4); wav[4] = (dsz + 36); wav[5] = (dsz + 36) >> 8; wav[6] = (dsz + 36) >> 16; wav[7] = (dsz + 36) >> 24;
	memcpy (wav + 8, "WAVEfmt ", 8);
	const u8 fmt[20] = {16, 0, 0, 0, 1, 0, 2, 0, 0x00, 0x7d, 0, 0, 0x00, 0xf4, 0x01, 0, 4, 0, 16, 0};
	memcpy (wav + 16, fmt, 20);
	memcpy (wav + 36, "data", 4); wav[40] = dsz; wav[41] = dsz >> 8; wav[42] = dsz >> 16; wav[43] = dsz >> 24;
	char dest[PATH_MAX], name[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	const char *sl = strrchr (arg, '/');
	snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
	dot = strrchr (name, '.');
	if (dot) *dot = 0;
	strncat (name, ".wav", sizeof (name) - strlen (name) - 1);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT Monster SNG:%s -> WAV:%s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, name);
	err = testmode ? ERR_OK : shade_write (arg, dest, basedir, name, wav, (uint)o);
	FREE (pcm);
	FREE (wav);
done:
	FREE (raw);
	FREE (rest);
	return err;
}

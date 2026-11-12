// Switch Opus wrappers decoded through switch_opus.inc's Ogg repacker:
//   Nihon Falcom "nxof" (".nxopus": Ys X -NORDICS-): channels u8 @5, rate @8, data start @0x18, data size @0x1c, samples @0x20, loop @0x30/0x34; Switch Opus packets
//     (u32 BE size + u32 final range + packet) from the data start; written as "<name>.ogg" / "<name>.wav" (pre-skip 312 samples).
//   Arika "NXMS" (".nxms": Tetris the Grand Master 4): segment count u16 @8 (intro + loop, or one), segment table @0x10 {offset, size, samples, 0, name[0x20]} 0x30 each;
//     every segment is a standard Switch Opus file written as "<name>_<NN>.opus" and decoded to "<name>_<NN>.wav" (after vgmstream nxof.c / nxms.c).
static enumError nxof_decode (const u8 *raw, size_t sz, size_t off, size_t size, uint ch, uint rate, uint skip, ccp arg, ccp basedir, ccp stem)
{
	if (off >= sz || ch < 1 || ch > 2)
		return ERR_NOTHING_TO_DO;
	if (size > sz - off)
		size = sz - off;
	swo_ogg_t og = { 0 };
	u8 oh[19] = { 'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, (u8)ch, (u8)skip, (u8)(skip >> 8), (u8)rate, (u8)(rate >> 8), (u8)(rate >> 16), (u8)(rate >> 24), 0, 0, 0 };
	og.serial = 0x4e494e54;
	swo_page (&og, oh, 19, 0, 2);
	static const u8 tags[] = { 'O', 'p', 'u', 's', 'T', 'a', 'g', 's', 5, 0, 0, 0, 'n', 'i', 'n', 't', 'o', 0, 0, 0, 0 };
	swo_page (&og, tags, sizeof (tags), 0, 0);
	const u8 *p = raw + off, *e = p + size;
	u64 gp = 0;
	uint npk = 0;
	while (p + 8 <= e)
	{
		const uint len = rd_be32 (p);
		if (!len || p + 8 + len > e)
			break;
		const uint ns = swo_packet_samples (p + 8, len);
		if (!ns)
			break;
		gp += ns;
		p += 8 + len;
		swo_page (&og, p - len, len, gp, p + 8 > e ? 4 : 0);
		npk++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (npk)
	{
		char dest[PATH_MAX], name[PATH_MAX], ogg[PATH_MAX], wav[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		snprintf (name, sizeof (name), "%s.ogg", stem);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE SWITCH-OPUS-WRAP:%s %s (%u ch, %u packets) -> OGG/WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, stem, ch, npk);
		if (testmode)
			err = ERR_OK;
		else if (!(err = shade_write (arg, dest, basedir, name, og.d, (uint)og.n)))
		{
			snprintf (ogg, sizeof (ogg), "%s/%s%s.ogg", dest, basedir ? basedir : "", stem);
			snprintf (wav, sizeof (wav), "%s/%s%s.wav", dest, basedir ? basedir : "", stem);
			PassthruDecodeAudio (ogg, wav);
		}
	}
	FREE (og.d);
	return err;
}

static enumError extract_nxof_nxms_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	const bool f = is_ext (arg, ".nxopus"), m = is_ext (arg, ".nxms");
	if ((!f && !m) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	char stem[PATH_MAX];
	const char *sl = strrchr (arg, '/');
	snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
	char *dot = strrchr (stem, '.');
	if (dot) *dot = 0;
	if (f && sz > 0x40 && !memcmp (raw, "nxof", 4))
		err = nxof_decode (raw, sz, rd_le32 (raw + 0x18), rd_le32 (raw + 0x1c), raw[5], rd_le32 (raw + 8), 312, arg, basedir, stem);
	else if (m && sz > 0x40 && !memcmp (raw, "NXMS", 4))
	{
		const uint nseg = rd_le16 (raw + 8);
		const size_t info = rd_le32 (raw + 0x10);
		if (nseg >= 1 && nseg <= 2 && info + 0x30 * (size_t)nseg <= sz)
		{
			char dest[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			for (uint i = 0; i < nseg; i++)
			{
				const size_t so = rd_le32 (raw + info + 0x30 * i), ss = rd_le32 (raw + info + 0x30 * i + 4);
				if (so + ss > sz || ss < 0x28 || rd_le32 (raw + so) != 0x80000001)
					continue;
				char name[PATH_MAX], seg[PATH_MAX];
				snprintf (seg, sizeof (seg), "%s_%02u", stem, i);
				snprintf (name, sizeof (name), "%s.opus", seg);
				if (testmode)
					err = ERR_OK;
				else if (!shade_write (arg, dest, basedir, name, raw + so, (uint)ss))
				{
					char path[PATH_MAX];
					snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
					const enumError e = extract_switch_opus_file (path, basedir, 0);
					err = e == ERR_NOTHING_TO_DO ? ERR_OK : e;
				}
			}
		}
	}
	FREE (raw);
	return err;
}

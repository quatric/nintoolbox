// Entergram "NXA1" Opus (".nxa": Higurashi no Naku Koro ni Hou, Gensou Rougoku no Kaleidoscope; Switch): type @4, end offset @8, rate @0xc, channels s16 @0x10, frame
// size u16 @0x12, skip s16 @0x16, data from 0x30. Type 1 is Switch Opus packets (u32 BE size + u32 final range + packet), type 2 is fixed-size packets without a
// prefix. The packets are repacked into "<name>.ogg" like switch_opus.inc and decoded to "<name>.wav" when ffmpeg/mobipeg is installed (after vgmstream nxa1.c).
static enumError extract_nxa1_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".nxa") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	swo_ogg_t og = { 0 };
	if (sz > 0x40 && !memcmp (raw, "NXA1", 4))
	{
		const uint type = rd_le32 (raw + 4), ch = raw[0x10] | raw[0x11] << 8, fsz = raw[0x12] | raw[0x13] << 8, skip = raw[0x16] | raw[0x17] << 8;
		size_t end = rd_le32 (raw + 8);
		if (end > sz || end < 0x30)
			end = sz;
		if ((type == 1 || type == 2) && ch >= 1 && ch <= 2 && (type == 1 || fsz))
		{
			u8 oh[19] = { 'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, (u8)ch, (u8)skip, (u8)(skip >> 8), 0x80, 0xbb, 0, 0, 0, 0, 0 };
			og.serial = 0x4e494e54;
			swo_page (&og, oh, 19, 0, 2);
			static const u8 tags[] = { 'O', 'p', 'u', 's', 'T', 'a', 'g', 's', 5, 0, 0, 0, 'n', 'i', 'n', 't', 'o', 0, 0, 0, 0 };
			swo_page (&og, tags, sizeof (tags), 0, 0);
			const u8 *p = raw + 0x30, *e = raw + end;
			u64 gp = 0;
			uint npk = 0;
			for (;;)
			{
				uint len, hdr = type == 1 ? 8 : 0;
				if (type == 1)
				{
					if (p + 8 > e) break;
					len = rd_be32 (p);
				}
				else
					len = fsz;
				if (!len || p + hdr + len > e)
					break;
				const uint ns = swo_packet_samples (p + hdr, len);
				if (!ns)
					break;
				gp += ns;
				p += hdr + len;
				swo_page (&og, p - len, len, gp, p + hdr + 1 > e ? 4 : 0);
				npk++;
			}
			if (npk)
			{
				char dest[PATH_MAX], name[PATH_MAX], wav[PATH_MAX], ogg[PATH_MAX];
				beside_source_dest (dest, sizeof (dest), arg);
				const char *sl = strrchr (arg, '/');
				snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
				char *dot = strrchr (name, '.');
				if (dot) *dot = 0;
				snprintf (ogg, sizeof (ogg), "%s.ogg", name);
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sDECODE NXA1:%s (%u ch, %u packets) -> OGG/WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, npk);
				if (testmode)
					err = ERR_OK;
				else
				{
					err = shade_write (arg, dest, basedir, ogg, og.d, (uint)og.n);
					if (!err)
					{
						snprintf (ogg, sizeof (ogg), "%s/%s%s.ogg", dest, basedir ? basedir : "", name);
						snprintf (wav, sizeof (wav), "%s/%s%s.wav", dest, basedir ? basedir : "", name);
						if (PassthruDecodeAudio (ogg, wav) == ERR_NOTHING_TO_DO && verbose >= 0)
							fprintf (stdlog, "  (no ffmpeg found, only the Ogg Opus file was written)\n");
					}
				}
			}
		}
	}
	FREE (og.d);
	FREE (raw);
	return err;
}

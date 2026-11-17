// Rockstar Games Presents Table Tennis (Wii) "pong/assets/zaudio/WiiBanks/*.spt" + "*.spd" sound banks: the big-endian ".spt" is {u32 count} + count * 28-byte records {u32 0, u32 rate, u32 0, u32 0, u32 end nibble
// address, u32 start nibble address (2 = first frame), u32 0} + count * 46-byte DSP-ADPCM parameter blocks {s16 coef[16], gain, ps, yn1, yn2, loop_ps, loop_yn1, loop_yn2} (the bytes 0x1c..0x4a of a DSP header).
// The sibling ".spd" (up to 830 MB, read per sample) is the concatenated ADPCM frame data; the frame data of a record starts at byte (start - 2) / 2. Records are written to "<file>.d/NNNN.dsp" and decoded to WAV.
static enumError extract_sptbank_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".spt"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	FILE *spd = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0x40)
		goto done;
	{
		const u32 count = rd_be32 (raw);
		if (!count || count > 100000 || 4 + (u64)count * (28 + 46) > sz)
			goto done;
		char sp[PATH_MAX];
		snprintf (sp, sizeof (sp), "%s", arg);
		char *dot = strrchr (sp, '.');
		if (!dot)
			goto done;
		strcpy (dot, ".spd");
		spd = fopen (sp, "rb");
		if (!spd)
			goto done;
		struct stat st;
		if (fstat (fileno (spd), &st) || st.st_size <= 0)
			goto done;
		const u64 dsz = (u64)st.st_size;
		const size_t ptab = 4 + (size_t)count * 28;
		uint ok = 0;
		for (uint i = 0; i < count; i++)
		{
			const u8 *e = raw + 4 + (size_t)i * 28;
			const u32 rate = rd_be32 (e + 4), ea = rd_be32 (e + 16), sa = rd_be32 (e + 20);
			if (!rd_be32 (e) && rate >= 4000 && rate <= 96000 && sa >= 2 && ea >= sa + 15 && !((sa - 2) & 15) && (u64)(sa - 2) / 2 + ((u64)ea - sa + 1 + 15) / 16 * 8 <= dsz)
				ok++;
		}
		if (!ok)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SPT:%s (%u of %u sounds) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ok, count, dest);
		err = ERR_OK;
		for (uint i = 0; i < count && !err && !testmode; i++)
		{
			const u8 *e = raw + 4 + (size_t)i * 28;
			const u32 rate = rd_be32 (e + 4), ea = rd_be32 (e + 16), sa = rd_be32 (e + 20);
			if (rd_be32 (e) || rate < 4000 || rate > 96000 || sa < 2 || ea < sa + 15 || ((sa - 2) & 15) || (u64)(sa - 2) / 2 + ((u64)ea - sa + 1 + 15) / 16 * 8 > dsz)
				continue;
			const u32 nib = ea - sa + 1, frames = (nib + 15) / 16, len = frames * 8, ns = nib / 16 * 14 + (nib % 16 > 2 ? nib % 16 - 2 : 0);
			const u8 *pb = raw + ptab + (size_t)i * 46;
			if (ptab + (size_t)(i + 1) * 46 > sz)
				break;
			u8 *buf = MALLOC (len);
			if (!buf || fseeko (spd, (off_t)((sa - 2) / 2), SEEK_SET) || fread (buf, 1, len, spd) != len)
			{
				FREE (buf);
				continue;
			}
			u8 h[0x60];
			memset (h, 0, sizeof (h));
			wr_be32 (h, ns);
			wr_be32 (h + 4, nib);
			wr_be32 (h + 8, rate);
			wr_be32 (h + 0x10, 2);
			wr_be32 (h + 0x14, nib - 1);
			wr_be32 (h + 0x18, 2);
			memcpy (h + 0x1c, pb, 46);
			h[0x3e] = 0;
			h[0x3f] = buf[0];
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%04u.dsp", dest, basedir ? basedir : "", i);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && (fwrite (h, 1, sizeof (h), F.f) != sizeof (h) || fwrite (buf, 1, len, F.f) != len))
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", len, path);
			ResetFile (&F, opt_preserve);
			FREE (buf);
		}
		if (!err && !testmode)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	if (spd)
		fclose (spd);
	FREE (raw);
	return err;
}

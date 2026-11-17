// Factor 5 MusyX sound banks on Wii (Biohazard 0 / Resident Evil 0 "bio0/sound/*.arc" + "*.sam"): the ".arc" is a big-endian chunk container {u32 0, u32 count, u32 0x20 table offset, ...} with 0x20-byte records
// {u32 offset, u32 size, char name[8], char type[16] ("pool", "proj", "sdir")}. The "sdir" chunk (sample directory) holds 0x20-byte entries {u32 id << 16, u32 sample byte offset into the sibling ".sam", u32 0,
// u8 pitch, u8 0, u16 rate, u32 (format << 24 | sample count), u32 loop start, u32 loop length, u32 offset of the ADPCM parameters inside the chunk}, terminated by 0xffffffff, followed by 0x28-byte ADPCM
// parameter blocks {u16 gain, u8 ps (+1 pad), s16 yn1, s16 yn2, s16 coef[16]}. DSP-ADPCM entries (format 0) are written as "<file>.d/NNNN_<id>.dsp" with a standard header and decoded to WAV. Other sample formats
// and the ".pool"/".proj" (instrument / macro) data are not converted.
static enumError extract_musyx_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".arc"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *sam = 0;
	size_t sz = 0, ssz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0x60 || rd_be32 (raw) || rd_be32 (raw + 8) != 0x20 || !rd_be32 (raw + 4) || rd_be32 (raw + 4) > 16)
		goto done;
	{
		const u32 nchunks = rd_be32 (raw + 4);
		u32 sdir = 0, sdir_size = 0;
		for (uint i = 0; i < nchunks; i++)
		{
			const u8 *r = raw + 0x20 + (size_t)i * 0x20;
			if (0x20 + (size_t)(i + 1) * 0x20 > sz)
				goto done;
			const u32 off = rd_be32 (r), size = rd_be32 (r + 4);
			if ((u64)off + size > sz)
				goto done;
			if (!memcmp (r + 16, "sdir", 4))
			{
				sdir = off;
				sdir_size = size;
			}
			else if (memcmp (r + 16, "pool", 4) && memcmp (r + 16, "proj", 4) && memcmp (r + 16, "samp", 4))
				goto done;
		}
		if (!sdir || sdir_size < 0x40)
			goto done;
		char sp[PATH_MAX];
		snprintf (sp, sizeof (sp), "%s", arg);
		char *dot = strrchr (sp, '.');
		if (!dot)
			goto done;
		strcpy (dot, ".sam");
		if (LoadFileAlloc (sp, 0, 0, &sam, &ssz, 0, 0, 0, false) || !sam)
			goto done;
		const u8 *sd = raw + sdir;
		uint n = 0;
		while ((size_t)(n + 1) * 0x20 <= sdir_size && rd_be32 (sd + (size_t)n * 0x20) != 0xffffffff)
			n++;
		if (!n)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		uint ok = 0;
		for (int pass = 0; pass < 2; pass++)
		{
			ok = 0;
			for (uint i = 0; i < n && (!pass || !err); i++)
			{
				const u8 *e = sd + (size_t)i * 0x20;
				const u32 id = rd_be32 (e) >> 16, so = rd_be32 (e + 4), rate = rd_be16 (e + 14), nsf = rd_be32 (e + 16), ap = rd_be32 (e + 28);
				const uint fmt = nsf >> 24, ns = nsf & 0xffffff;
				const u32 frames = (ns + 13) / 14, len = frames * 8;
				if (fmt || !ns || rate < 4000 || rate > 96000 || (u64)ap + 0x28 > sdir_size || (u64)so + len > ssz)
					continue;
				ok++;
				if (!pass || testmode)
					continue;
				const u8 *p = sd + ap;
				u8 h[0x60];
				memset (h, 0, sizeof (h));
				const u32 nib = frames * 16 - (ns % 14 ? 14 - ns % 14 : 0);
				wr_be32 (h, ns);
				wr_be32 (h + 4, nib);
				wr_be32 (h + 8, rate);
				wr_be32 (h + 0x10, 2);
				wr_be32 (h + 0x14, nib - 1);
				wr_be32 (h + 0x18, 2);
				memcpy (h + 0x1c, p + 8, 32);
				h[0x3f] = sam[so];
				memcpy (h + 0x40, p + 4, 4); // yn1, yn2
				h[0x40] = p[4], h[0x41] = p[5], h[0x42] = p[6], h[0x43] = p[7];
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%s%04u_%u.dsp", dest, basedir ? basedir : "", i, id);
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (F.f && (fwrite (h, 1, sizeof (h), F.f) != sizeof (h) || fwrite (sam + so, 1, len, F.f) != len))
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", len, path);
				ResetFile (&F, opt_preserve);
			}
			if (!pass)
			{
				if (!ok)
					goto done;
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sEXTRACT MUSYX:%s (%u samples) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ok, dest);
				err = ERR_OK;
				if (testmode)
					break;
			}
		}
		if (!err && !testmode)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (raw);
	FREE (sam);
	return err;
}

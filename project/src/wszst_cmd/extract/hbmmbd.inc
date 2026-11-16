// Fishing Master (Wii) "*.mbd" sound banks, little endian:
//   u32 table size, u32 table size, u32 version (1 = "HBMSND" Home-Button style bank, 3 = categorised bank), version 1: "HBMSND" + zeros; version 3: NUL
//   separated category names, then a u32 record count (at 0x38 for version 1, 0x4c for version 3) and count * 32-byte records
//   {u32 offset, u32 size, u32 16 (bits), u32 sample rate, u32 channels, u32 0, u32 samples, u32 extra}. The streams start at table size + 8 and
//   follow back to back; each one is {u32 size - 4, 16-bit little-endian interleaved PCM}. After the records: {u32 0, u32 0, u32 count} and count
//   entries {u32 length, name padded to 4, u32 record index}. Every stream is written as "<name>.wav" (or "<NNNN>.wav" without a name).
static enumError extract_hbmmbd_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".mbd"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 hd[0x60];
	enumError err = ERR_NOTHING_TO_DO;
	u8 *tab = 0;
	if (fread (hd, 1, sizeof (hd), f) != sizeof (hd) || fseeko (f, 0, SEEK_END))
		goto done;
	{
		const u64 size = (u64)ftello (f);
		const u32 t0 = rd_le32 (hd), ver = rd_le32 (hd + 8);
		if (t0 < 0x60 || t0 > 0x100000 || rd_le32 (hd + 4) != t0 || (ver != 1 && ver != 3) || (u64)t0 + 8 > size)
			goto done;
		if (ver == 1 && memcmp (hd + 12, "HBMSND", 6))
			goto done;
		const u32 base = t0 + 8, cpos = ver == 1 ? 0x38 : 0x4c;
		const u32 n = rd_le32 (hd + cpos);
		if (!n || n > 0x10000 || (u64)cpos + 4 + (u64)n * 32 > base)
			goto done;
		tab = MALLOC (base);
		if (!tab || fseeko (f, 0, SEEK_SET) || fread (tab, 1, base, f) != base)
			goto done;
		u64 expect = 0;
		for (u32 i = 0; i < n; i++)
		{
			const u8 *r = tab + cpos + 4 + (size_t)i * 32;
			const u64 o = rd_le32 (r), s = rd_le32 (r + 4);
			const u32 ch = rd_le32 (r + 16), rate = rd_le32 (r + 12);
			if (o != expect || s < 8 || base + o + s > size || rd_le32 (r + 8) != 16 || ch < 1 || ch > 2 || rate < 4000 || rate > 96000)
				goto done;
			expect = o + s;
		}
		if (base + expect != size)
			goto done;

		// names: {0, 0, count} then {len, name padded to 4, index}
		char **names = CALLOC (n, sizeof (*names));
		{
			size_t p = cpos + 4 + (size_t)n * 32;
			if (p + 12 <= base && rd_le32 (tab + p + 8) == n)
			{
				p += 12;
				for (u32 k = 0; k < n && p + 4 <= base; k++)
				{
					const u32 len = rd_le32 (tab + p);
					const size_t padded = (len + 3u) & ~3u;
					if (!len || len > 200 || p + 4 + padded + 4 > base)
						break;
					const u32 idx = rd_le32 (tab + p + 4 + padded);
					bool good = idx < n && !names[idx] && !tab[p + 4 + len - 1];
					for (u32 q = 0; good && q + 1 < len; q++)
						if (tab[p + 4 + q] < 0x20 || tab[p + 4 + q] > 0x7e || tab[p + 4 + q] == '/' || tab[p + 4 + q] == '\\')
							good = false;
					if (good)
						names[idx] = STRDUP ((ccp)tab + p + 4);
					p += 4 + padded + 4;
				}
			}
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT MBD:%s (%u streams) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (u32 i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *r = tab + cpos + 4 + (size_t)i * 32;
			const u32 o = rd_le32 (r), s = rd_le32 (r + 4) - 4, rate = rd_le32 (r + 12), ch = rd_le32 (r + 16);
			char path[PATH_MAX];
			if (names[i])
				snprintf (path, sizeof (path), "%s/%s%s.wav", dest, basedir ? basedir : "", names[i]);
			else
				snprintf (path, sizeof (path), "%s/%s%04u.wav", dest, basedir ? basedir : "", i);
			u8 *pcm = MALLOC (s ? s : 1);
			if (!pcm || fseeko (f, (off_t)((u64)base + o + 4), SEEK_SET) || fread (pcm, 1, s, f) != s)
			{
				FREE (pcm);
				err = ERR_READ_FAILED;
				break;
			}
			u8 w[44];
			memcpy (w, "RIFF", 4);
			w[4] = (36 + s) & 255, w[5] = (36 + s) >> 8 & 255, w[6] = (36 + s) >> 16 & 255, w[7] = (36 + s) >> 24;
			memcpy (w + 8, "WAVEfmt ", 8);
			const u32 fmt[4] = { 16, 1 | ch << 16, rate, rate * ch * 2 };
			for (uint k = 0; k < 4; k++)
				for (uint b = 0; b < 4; b++)
					w[16 + k * 4 + b] = fmt[k] >> (8 * b) & 255;
			w[32] = ch * 2, w[33] = 0, w[34] = 16, w[35] = 0;
			memcpy (w + 36, "data", 4);
			w[40] = s & 255, w[41] = s >> 8 & 255, w[42] = s >> 16 & 255, w[43] = s >> 24;
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && (fwrite (w, 1, 44, F.f) != 44 || fwrite (pcm, 1, s, F.f) != s))
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
			ResetFile (&F, opt_preserve);
			FREE (pcm);
		}
		for (u32 i = 0; i < n; i++)
			FREE (names[i]);
		FREE (names);
	}
done:
	FREE (tab);
	fclose (f);
	return err;
}

// Data East Arcade Classics (Wii) "index/<name>.jpi" + "sound/<name>.jps". The little-endian index is {u32 table bytes?, u32 first id, ...} followed (from 0x08) by 0x20-byte entries {u32 id, u32 offset, u32 size,
// u32 0, u32 loop start, u32 loop end (absolute .jps byte positions, or 0xffffffff), u64 0}; the sibling "../sound/<name>.jps" holds the members back to back. Short members are standalone big-endian
// DSP-ADPCM files (carved to "<file>.d/<id>.dsp" and decoded). The long music members (loop positions set, no DSP header) are headerless signed 8-bit PCM; they are written as mono 16384 Hz WAV (the rate
// is ASSUMED to be that of the DSP effects; it is not stored in the files).
static enumError extract_jpiindex_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".jpi"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *jps = 0;
	size_t sz = 0, jsz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0x28)
		goto done;
	{
		char sp[PATH_MAX];
		snprintf (sp, sizeof (sp), "%s", arg);
		char *dot = strrchr (sp, '.');
		if (!dot || strlen (sp) + 16 >= sizeof (sp))
			goto done;
		strcpy (dot, ".jps");
		char *sl = strrchr (sp, '/');
		if (!sl)
			goto done;
		char *sl2 = sl;
		while (sl2 > sp && sl2[-1] != '/')
			sl2--;
		if (sl2 == sp)
			goto done;
		char nm[PATH_MAX];
		snprintf (nm, sizeof (nm), "%s", sl + 1);
		snprintf (sl2, sizeof (sp) - (size_t)(sl2 - sp), "sound/%s", nm);
		if (LoadFileAlloc (sp, 0, 0, &jps, &jsz, 0, 0, 0, false) || !jps)
			goto done;
		uint n = 0;
		for (size_t o = 8; o + 32 <= sz; o += 32)
		{
			const u32 off = rd_le32 (raw + o + 4), size = rd_le32 (raw + o + 8);
			if (size && (u64)off + size <= jsz && !rd_le32 (raw + o + 12))
				n++;
			else
				break;
		}
		if (!n)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT JPI:%s (%u sounds) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = raw + 8 + i * 32;
			const u32 id = rd_le32 (e), off = rd_le32 (e + 4), size = rd_le32 (e + 8);
			const u8 *m = jps + off;
			const bool dsp = size > 0x60 && rd_be32 (m + 0x10) == 2 && rd_be32 (m + 0x18) == 2 && rd_be32 (m + 8) >= 4000 && rd_be32 (m + 8) <= 96000;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%04u_%u.%s", dest, basedir ? basedir : "", i, id, dsp ? "dsp" : "wav");
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (!F.f)
				continue;
			if (dsp)
			{
				if (fwrite (m, 1, size, F.f) != size)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
			}
			else
			{
				u8 h[44];
				memcpy (h, "RIFF", 4);
				wr_le32 (h + 4, 36 + size);
				memcpy (h + 8, "WAVEfmt ", 8);
				wr_le32 (h + 16, 16);
				h[20] = 1, h[21] = 0, h[22] = 1, h[23] = 0;
				wr_le32 (h + 24, 16384);
				wr_le32 (h + 28, 16384);
				h[32] = 1, h[33] = 0, h[34] = 8, h[35] = 0;
				memcpy (h + 36, "data", 4);
				wr_le32 (h + 40, size);
				u8 *pcm = MALLOC (size);
				if (pcm)
				{
					for (u32 k = 0; k < size; k++)
						pcm[k] = m[k] ^ 0x80;
				}
				if (!pcm || fwrite (h, 1, 44, F.f) != 44 || fwrite (pcm, 1, size, F.f) != size)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
				FREE (pcm);
			}
			ResetFile (&F, opt_preserve);
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
	FREE (jps);
	return err;
}

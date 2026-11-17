// Cosmic Family (Wii) "*/voices/<lang>/<name>_streams.snh" voice descriptors: 86 bytes, big-endian {u32 byte offset into the sibling "streams.sns", u32 byte length, 8 bytes 0, u32 rate, ..., s16 DSP-ADPCM
// coefficients[16] at 0x28}. "streams.sns" is the headerless mono DSP-ADPCM pool of the directory (members are consecutive). Each descriptor yields "<file>.d/<name>.dsp", decoded to WAV.
static enumError extract_snhstream_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".snh"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *pool = 0;
	size_t sz = 0, psz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0x48 || sz > 0x100)
		goto done;
	{
		const u32 off = rd_be32 (raw), len = rd_be32 (raw + 4), rate = rd_be32 (raw + 0x10);
		if (!len || rate < 4000 || rate > 96000 || rd_be32 (raw + 8) || rd_be32 (raw + 12))
			goto done;
		char sp[PATH_MAX];
		snprintf (sp, sizeof (sp), "%s", arg);
		char *sl = strrchr (sp, '/');
		snprintf (sl ? sl + 1 : sp, sizeof (sp) - (sl ? (size_t)(sl + 1 - sp) : 0), "streams.sns");
		if (LoadFileAlloc (sp, 0, 0, &pool, &psz, 0, 0, 0, false) || !pool || (u64)off + len > psz)
			goto done;
		const u32 nib = len * 2, ns = nib / 16 * 14 + (nib % 16 > 2 ? nib % 16 - 2 : 0);
		u8 h[0x60];
		memset (h, 0, sizeof (h));
		wr_be32 (h, ns);
		wr_be32 (h + 4, nib);
		wr_be32 (h + 8, rate);
		wr_be32 (h + 0x10, 2);
		wr_be32 (h + 0x14, nib - 1);
		wr_be32 (h + 0x18, 2);
		memcpy (h + 0x1c, raw + 0x28, 32);
		h[0x3f] = pool[off];
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SNH:%s (%u bytes @ 0x%x, %u Hz) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, len, off, rate, dest);
		err = ERR_OK;
		if (!testmode)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%svoice.dsp", dest, basedir ? basedir : "");
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && (fwrite (h, 1, sizeof (h), F.f) != sizeof (h) || fwrite (pool + off, 1, len, F.f) != len))
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", len, path);
			ResetFile (&F, opt_preserve);
			if (!err)
			{
				const enumError sub = extract_tree_complete (dest, depth + 1);
				if (err < sub)
					err = sub;
			}
		}
	}
done:
	FREE (raw);
	FREE (pool);
	return err;
}

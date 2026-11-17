// Hasbro Family Game Night 3 (Wii) "Resource/*.xpac" archives: {u32 0, u32 0, u32 0x800 (alignment), u32 count} + u32 0, u32 0 + count * 20-byte entries {u32 name hash, u32 offset, u32 size, u32 size, u32 0}; payloads are 0x800-aligned.
// Entries are written to "<file>.d/<hash>.<ext>": XML scenes as ".xml"; sound entries are runs of complete big-endian DSP-ADPCM files (0x60-byte header, each padded to 0x20), either bare or behind an "iSWA" header
// {"iSWA", hdr size 0x18, channels, stride, 0, 0} (DSP files start at 0x20) -> one "<hash>_NN.dsp" each (silent zero-coefficient placeholders skipped), decoded to WAV; everything else is stored as ".bin".
static bool xpac_dsp_ok (const u8 *h, size_t avail)
{
	if (avail < 0x60)
		return false;
	const u32 nib = rd_be32 (h + 4), rate = rd_be32 (h + 8);
	return nib >= 2 && nib < 0x10000000 && rate >= 4000 && rate <= 96000 && rd_be32 (h + 0x10) == 2 && rd_be32 (h + 0x14) == nib - 1 && rd_be32 (h + 0x18) == 2 && 0x60 + (size_t)((nib + 15) / 16) * 8 <= avail;
}

static enumError xpac_write (ccp arg, ccp dest, const char *name, const u8 *data, size_t len)
{
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/%s", dest, name);
	File_t F;
	enumError err = CreateFileOpt (&F, true, path, false, arg);
	if (F.f && fwrite (data, 1, len, F.f) != len)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", len, path);
	ResetFile (&F, opt_preserve);
	return err;
}

static enumError extract_xpac_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".xpac") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 || rd_be32 (raw) || rd_be32 (raw + 4) || rd_be32 (raw + 8) != 0x800)
		goto done;
	{
		const u32 count = rd_be32 (raw + 12);
		if (!count || count > 100000 || 0x18 + (u64)count * 20 > sz)
			goto done;
		for (uint i = 0; i < count; i++)
		{
			const u8 *e = raw + 0x18 + (size_t)i * 20;
			if (rd_be32 (e + 4) < 0x18 + (u64)count * 20 || (u64)rd_be32 (e + 4) + rd_be32 (e + 8) > sz || rd_be32 (e + 8) != rd_be32 (e + 12) || rd_be32 (e + 16))
				goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT XPAC:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, count, dest);
		err = ERR_OK;
		for (uint i = 0; i < count && !err && !testmode; i++)
		{
			const u8 *e = raw + 0x18 + (size_t)i * 20;
			const u32 hash = rd_be32 (e), off = rd_be32 (e + 4), len = rd_be32 (e + 8);
			const u8 *p = raw + off;
			char name[64];
			size_t base = 0;
			if (len >= 5 && !memcmp (p, "<?xml", 5))
			{
				snprintf (name, sizeof (name), "%08x.xml", hash);
				err = xpac_write (arg, dest, name, p, len);
				continue;
			}
			if (len >= 0x80 && !memcmp (p, "iSWA", 4) && rd_be32 (p + 4) == 0x18)
				base = 0x20;
			if (xpac_dsp_ok (p + base, len > base ? len - base : 0))
			{
				uint n = 0;
				size_t o = base;
				while (o + 0x60 <= len && xpac_dsp_ok (p + o, len - o) && !err)
				{
					const size_t dl = 0x60 + (size_t)((rd_be32 (p + o + 4) + 15) / 16) * 8;
					bool silent = true;
					for (uint k = 0x1c; k < 0x3c && silent; k++)
						silent = !p[o + k];
					if (!silent)
					{
						snprintf (name, sizeof (name), "%08x_%02u.dsp", hash, n);
						err = xpac_write (arg, dest, name, p + o, dl);
					}
					n++;
					o += (dl + 0x1f) & ~(size_t)0x1f;
				}
				continue;
			}
			snprintf (name, sizeof (name), "%08x.bin", hash);
			err = xpac_write (arg, dest, name, p, len);
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
	return err;
}

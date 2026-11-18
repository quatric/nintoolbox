// Sega "ABRS" packs (Sakura Wars: So Long, My Love "*.abr"), little-endian: {"ABRS", u32 payload size, u32 data start (table end, 16-byte aligned), 0}, {u32 crc, u32 count, u32 end offset, u32 data start}, count * {u32 0xffffffff, u32 chunk start offset or 0} (not needed: chunks are walked via their own headers {tag, u32 payload size, u32 header size}), chunk data from the data start, trailer "EOFC". Chunks (HMDL models, HTEX textures, HMOT motions, HMRP ...) are written with their own header to "<file>.d/NN.<tag>".
static enumError extract_abrs_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".abr"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *d = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const long sz = ftell (f);
		if (sz < 0x100 || sz > 0x10000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)sz);
		if (fread (d, 1, (size_t)sz, f) != (size_t)sz || memcmp (d, "ABRS", 4))
			goto done;
		const u32 start = rd_le32 (d + 8), n = rd_le32 (d + 0x14);
		const size_t end = (size_t)rd_le32 (d + 4) + start;
		if (!n || n > 4096 || start < 0x20 + 8 * (size_t)n || start > 0x20 + 8 * (size_t)n + 0x10 || end > (size_t)sz)
			goto done;
		// walk the self-describing chunks {tag[4], u32 payload size, u32 header size, ...}
		size_t pos = start;
		uint cnt = 0;
		while (pos < end)
		{
			if (pos + 12 > end)
				goto done;
			for (int k = 0; k < 4; k++)
				if (!((d[pos + k] >= 'A' && d[pos + k] <= 'Z') || (d[pos + k] >= '0' && d[pos + k] <= '9')))
					goto done;
			const size_t tot = (size_t)rd_le32 (d + pos + 4) + rd_le32 (d + pos + 8);
			if (rd_le32 (d + pos + 8) < 0x10 || rd_le32 (d + pos + 8) > 0x100 || pos + tot > end || ++cnt > 100000)
				goto done;
			pos += tot;
		}
		if (pos != end || !cnt)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT ABRS:%s (%u chunks) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, cnt, dest);
		err = ERR_OK;
		pos = start;
		for (uint i = 0; i < cnt && !err && !testmode; i++)
		{
			const size_t tot = (size_t)rd_le32 (d + pos + 4) + rd_le32 (d + pos + 8);
			char tag[5] = {0};
			for (int k = 0; k < 4; k++)
				tag[k] = d[pos + k] >= 'A' && d[pos + k] <= 'Z' ? d[pos + k] + 32 : d[pos + k];
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%03u.%s", dest, i, tag);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (d + pos, 1, tot, F.f) != tot)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", tot, path);
			ResetFile (&F, opt_preserve);
			pos += tot;
		}
	}
done:
	FREE (d);
	fclose (f);
	return err;
}

// Measure one XPCK/XPC2 archive's own total length from its header, without
// extracting it -- the highest (offset+size) among its own file entries, the
// same arithmetic ExtractXPCKArchive() already uses per entry. Returns 0 if
// 'raw' doesn't start with a plausible XPCK header.
static u32 measure_xpck_size (const u8 *raw, size_t raw_size)
{
	if (raw_size < 0x20 || (memcmp (raw, "XPCK", 4) && memcmp (raw, "XPC2", 4)))
		return 0;
	const u32 file_count = (uint)(rd_le16 (raw + 4) & 0xFFF);
	const u32 file_info_offset = (u32)rd_le16 (raw + 6) * 4;
	const u32 data_offset = (u32)rd_le16 (raw + 10) * 4;
	if (!file_count || (u64)file_info_offset + (u64)file_count * 12 > raw_size)
		return 0;

	u64 end = data_offset;
	for (uint i = 0; i < file_count; i++)
	{
		const u32 entry_off = file_info_offset + i * 12;
		u32 off = (u32)rd_le16 (raw + entry_off + 6);
		u32 sz = (u32)rd_le16 (raw + entry_off + 8);
		off |= (u32)raw[entry_off + 10] << 16;
		sz |= (u32)raw[entry_off + 11] << 16;
		off = off * 4 + data_offset;
		if ((u64)off + sz > end)
			end = (u64)off + sz;
	}
	return end <= raw_size ? (u32)end : 0;
}

// Level-5 "ARC0" flat archive (Yo-Kai Watch, 3DS). A 386 MB retail sample's
// header carries a handful of section offsets/counts this pass never fully
// solved (name/hash tables sized 22360 and 28244 that don't correspond to any
// clean stride through the bytes between them), but the actual payload is
// simply a sequence of ordinary sub-archives (XPCK confirmed; likely others)
// concatenated back to back starting at the offset the header's fifth u32
// names. Rather than block on the header, scan for known sub-archive magics
// directly and use each one's own self-reported length to find the next --
// this recovers the real, complete, individually-valid XPCK members without
// needing the still-unknown outer index at all.
static enumError extract_arc0_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (raw_size < 16 || raw_size > UINT_MAX || memcmp (raw, "ARC0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sSCAN ARC0:%s -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	enumError err = ERR_OK;
	uint emitted = 0;
	u64 pos = 4;
	while (pos + 0x20 <= raw_size && !err)
	{
		const u32 sz = measure_xpck_size (raw + pos, raw_size - pos);
		if (!sz)
		{
			pos++;
			continue;
		}

		if (verbose > 0 || testmode)
			fprintf (stdlog, "%sEXTRACT ARC0[%#llx]:%s -> XPCK:chunk_%06u.xpck\n",
				testmode ? "WOULD " : "", (unsigned long long)pos, arg, emitted);
		if (!testmode)
		{
			char path[PATH_MAX];
			snprintf (
				path, sizeof (path), "%s/%schunk_%06u.xpck", dest, basedir ? basedir : "", emitted);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (raw + pos, 1, sz, F.f) != sz)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", sz, path);
			const enumError close_err = ResetFile (&F, opt_preserve);
			if (close_err > err)
				err = close_err;
		}
		emitted++;
		pos += sz;
	}

	FREE (raw);
	if (!emitted)
		return ERR_NOTHING_TO_DO;
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ARC0:%s (%u chunks) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, emitted, dest);
	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

// "FRHD" frame bounds (Let's Dance with Mel B, Wii: ui_videos/*/frames.bin, the binary form of frames.xml): BE
//   {"FRHD", u32 0x40 (header size), u32 1, u32 total frames, u32 frames per set, zeros up to 0x40, u64 0}
//   "MAXB" u32 size (0 or 16), size bytes {s32 x, y, w, h}   - union of the frame rectangles
//   "FRMS" u32 size (0 or 16 * frames), size bytes of {s32 x, y, w, h} per frame
// i.e. the rectangle of the visible part of every Bink video frame. Written as "<name>.json".
static enumError extract_frhd_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x58 || memcmp (raw, "FRHD", 4) || rd_be32 (raw + 4) != 0x40 || memcmp (raw + 0x48, "MAXB", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 total = rd_be32 (raw + 12), per_set = rd_be32 (raw + 16), msz = rd_be32 (raw + 0x4c);
	size_t p = 0x50 + msz;
	if ((msz != 0 && msz != 16) || p + 8 > sz || memcmp (raw + p, "FRMS", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 fsz = rd_be32 (raw + p + 4);
	if (fsz % 16 || p + 8 + (size_t)fsz != sz || fsz / 16 > 10000000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "{\n  \"total_frames\": %u,\n  \"frames_per_set\": %u,\n  \"max_bounds\": ", total, per_set);
	if (msz)
		pcr_emit (&c, "[%d, %d, %d, %d]", (int)rd_be32 (raw + 0x50), (int)rd_be32 (raw + 0x54), (int)rd_be32 (raw + 0x58),
			(int)rd_be32 (raw + 0x5c));
	else
		pcr_emit (&c, "null");
	pcr_emit (&c, ",\n  \"frames\": [");
	for (u32 i = 0; i < fsz / 16; i++)
	{
		const u8 *r = raw + p + 8 + 16 * (size_t)i;
		pcr_emit (&c, "%s\n    [%d, %d, %d, %d]", i ? "," : "", (int)rd_be32 (r), (int)rd_be32 (r + 4), (int)rd_be32 (r + 8),
			(int)rd_be32 (r + 12));
	}
	pcr_emit (&c, "\n  ]\n}\n");
	enumError err = ERR_NOTHING_TO_DO;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.json", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT FRHD:%s -> JSON:%s\n", arg, dest);
			err = ERR_OK;
		}
		else
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f && fwrite (c.out, 1, c.len, F.f) != c.len)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)c.len, dest);
			ResetFile (&F, opt_preserve);
			if (verbose >= 0 && !err)
				fprintf (stdlog, "%sEXTRACT FRHD:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}

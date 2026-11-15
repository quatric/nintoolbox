// GUI image sets (Mary King's Riding School 2, Wii: data/gui/imagesets/*.imageset.bin), big-endian, raw dumps of a C
// structure (uninitialised bytes are 0xcc / 0xcd):
//   char name[0x40], char texture_path[0x100] (a ".dds" path), u16 texture width, u16 texture height, u8 flag,
//   u8 pad, u16 image count, then count * {char name[0x40], u16 x, u16 y, u16 w, u16 h}.
// The file size is exactly 0x148 + 0x48 * count (all 18 retail files). Written as "<file>.json".
static enumError extract_eqimgset_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	const size_t al = strlen (arg);
	if (al < 13 || strcasecmp (arg + al - 13, ".imageset.bin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t n = sz >= 0x148 ? rd_be16 (raw + 0x146) : 0;
	if (sz < 0x148 || !n || sz != 0x148 + 0x48 * n || !memchr (raw, 0, 0x40) || !memchr (raw + 0x40, 0, 0x100))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "{\n  \"name\": ");
	parm_emit_str (&c, raw, (uint)strnlen ((const char *)raw, 0x40));
	pcr_emit (&c, ",\n  \"texture\": ");
	parm_emit_str (&c, raw + 0x40, (uint)strnlen ((const char *)raw + 0x40, 0x100));
	pcr_emit (&c, ",\n  \"width\": %u,\n  \"height\": %u,\n  \"flag\": %u,\n  \"images\": [", rd_be16 (raw + 0x140),
		rd_be16 (raw + 0x142), raw[0x144]);
	for (size_t i = 0; i < n; i++)
	{
		const u8 *r = raw + 0x148 + 0x48 * i;
		pcr_emit (&c, "%s\n    {\"name\": ", i ? "," : "");
		parm_emit_str (&c, r, (uint)strnlen ((const char *)r, 0x40));
		pcr_emit (&c, ", \"x\": %u, \"y\": %u, \"w\": %u, \"h\": %u}", rd_be16 (r + 0x40), rd_be16 (r + 0x42),
			rd_be16 (r + 0x44), rd_be16 (r + 0x46));
	}
	pcr_emit (&c, "\n  ]\n}\n");
	enumError err = ERR_NOTHING_TO_DO;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.json", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT EQIMGSET:%s -> JSON:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT EQIMGSET:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
#pragma pop_macro("free")
#pragma pop_macro("calloc")

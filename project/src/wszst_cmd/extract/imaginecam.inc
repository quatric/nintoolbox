// Imagine engine camera path ".cam" (Party Babyz cut scenes; usually stored inside the Imagine LZSS wrapper, which is unpacked
// first): big endian u32 key count, then 48 bytes per key = a row-major 3x4 camera matrix (three rows {x, y, z, translation}
// of floats). Written as "camera.json" in the "<file>.d" directory with the matrix and the translation of every key.
static enumError extract_imaginecam_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".cam"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 52 || sz > 0x10000000
		|| sz != 4 + 48 * (size_t)rd_be32 (raw))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint n = rd_be32 (raw);
	njm_out_t o = { MALLOC (1 << 16), 0, 1 << 16 };
	if (!o.b)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	njm_put (&o, "{\n\"keys\": %u,\n\"matrices\": [", n);
	for (uint i = 0; i < n; i++)
	{
		const u8 *p = raw + 4 + 48 * (size_t)i;
		njm_put (&o, "%s\n[", i ? "," : "");
		for (uint k = 0; k < 12; k++)
			njm_put (&o, "%s%g", k ? "," : "", njm_f (p + 4 * k));
		njm_put (&o, "]");
	}
	njm_put (&o, "\n]\n}\n");
	enumError err = ERR_OK;
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT IMAGINE-CAM:%s (%u keys) -> %s/camera.json\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n, dest);
	if (!testmode)
	{
		if (!o.b)
			err = ERR_OUT_OF_MEMORY;
		else
			err = shade_write (arg, dest, basedir, "camera.json", (u8 *)o.b, (uint)o.used);
	}
	FREE (o.b);
	FREE (raw);
	return err;
}

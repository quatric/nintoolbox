// IO Interactive Glacier scene geometry (".gms" next to a Mini Ninjas scene ".prm"/".prp"): {u32 LE decompressed size, u32 LE file size (the whole file), u8 0, raw deflate stream}. The stream
// inflates to exactly the stated size (1,548,016 bytes for C15_FireCastle_Main.gms); the unpacked big endian object graph (a table of absolute offsets 0x0014a990, 0x00179cd0, ... followed by
// float data) is written as "<file>.d/<file>.bin" and not decoded further.
static enumError extract_glacier_gms_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".gms"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 16 || rd_le32 (raw + 4) != sz || !rd_le32 (raw) || rd_le32 (raw) > 0x20000000 || raw[8])
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 us = rd_le32 (raw);
	u8 *out = MALLOC (us);
	z_stream zs;
	memset (&zs, 0, sizeof zs);
	bool ok = out && inflateInit2 (&zs, -15) == Z_OK;
	if (ok)
	{
		zs.next_in = raw + 9;
		zs.avail_in = (uInt)(sz - 9);
		zs.next_out = out;
		zs.avail_out = us;
		const int r = inflate (&zs, Z_FINISH);
		ok = r == Z_STREAM_END && zs.total_out == us;
		inflateEnd (&zs);
	}
	FREE (raw);
	enumError err = ERR_NOTHING_TO_DO;
	if (ok)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".bin");
		else
			strcat (name, ".bin");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT GLACIER-GMS:%s (%u bytes) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, us, dest, name);
		err = shade_write (arg, dest, basedir, name, out, us);
	}
	FREE (out);
	return err;
}

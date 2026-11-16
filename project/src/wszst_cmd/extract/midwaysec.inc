// Midway "SEC " sectioned containers (Mortal Kombat: Armageddon, Wii: "*.ssf"), big-endian:
//   "SEC ", u32 4 (version), u32 0, u32 (flags), u32 entry count (word 4), 2 more header words, then count 16-byte entries
//   {u32 offset, u32 size, u32 tag (0x7c801a4f in every file), u32 kind}, 0x800 aligned, ascending, ending inside the file.
// The payloads are nested "SEC " blocks (models / textures / animations with relocation tables) or raw data; they are written unchanged as
// "<NN>_k<kind>.bin" (nested "SEC " blocks as ".sec"); the inner layout is not decoded.
static enumError extract_midwaysec_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".ssf") && !is_ext (arg, ".sec"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 hd[0x1020];
	enumError err = ERR_NOTHING_TO_DO;
	const size_t got = fread (hd, 1, sizeof (hd), f);
	if (got < 0x30 || memcmp (hd, "SEC ", 4) || rd_be32 (hd + 4) != 4 || fseeko (f, 0, SEEK_END))
		goto done;
	{
		const u64 size = (u64)ftello (f);
		const u32 n = rd_be32 (hd + 0x10);
		if (!n || n > 250 || 0x20 + (u64)n * 16 > got)
			goto done;
		u64 prev = 0;
		for (u32 i = 0; i < n; i++)
		{
			const u8 *e = hd + 0x20 + (size_t)i * 16;
			const u64 o = rd_be32 (e), s = rd_be32 (e + 4);
			if (o < 0x20 + (u64)n * 16 || o < prev || o + s > size)
				goto done;
			prev = o + s;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SEC:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (u32 i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = hd + 0x20 + (size_t)i * 16;
			const u32 o = rd_be32 (e), s = rd_be32 (e + 4), kind = rd_be32 (e + 12);
			u8 *buf = MALLOC (s ? s : 1);
			if (!buf || fseeko (f, (off_t)o, SEEK_SET) || (s && fread (buf, 1, s, f) != s))
			{
				FREE (buf);
				err = ERR_READ_FAILED;
				break;
			}
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%02u_k%u.%s", dest, basedir ? basedir : "", i, kind, s >= 4 && !memcmp (buf, "SEC ", 4) ? "sec" : "bin");
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && s && fwrite (buf, 1, s, F.f) != s)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
			ResetFile (&F, opt_preserve);
			FREE (buf);
		}
	}
done:
	fclose (f);
	return err;
}

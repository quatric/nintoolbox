// Neighborhood Games "*.dbreg": a 0x20-aligned run of BRRES files ("bres" + FE FF, size at +8) preceded by a pointer table. Each BRRES is carved to "<file>.d/NNNN.brres" and decoded in turn.
static enumError extract_dbreg_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".dbreg"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *d = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const off_t fsz = ftello (f);
		if (fsz < 0x100 || fsz > 0x20000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)fsz);
		if (fread (d, 1, (size_t)fsz, f) != (size_t)fsz)
			goto done;
		uint n = 0;
		for (u64 p = 0; p + 0x20 <= (u64)fsz; p += 0x20)
			if (!memcmp (d + p, "bres\xfe\xff", 6))
			{
				const u32 s = be32 (d + p + 8);
				if (s >= 0x20 && p + s <= (u64)fsz)
					n++;
			}
		if (!n)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT DBREG:%s (%u BRRES) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		uint i = 0;
		for (u64 p = 0; p + 0x20 <= (u64)fsz && !err && !testmode; p += 0x20)
		{
			if (memcmp (d + p, "bres\xfe\xff", 6))
				continue;
			const u32 s = be32 (d + p + 8);
			if (s < 0x20 || p + s > (u64)fsz)
				continue;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%04u.brres", dest, i++);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (d + p, 1, s, F.f) != s)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
			ResetFile (&F, opt_preserve);
			p += (s - 1) & ~(u64)0x1f;
		}
	}
done:
	FREE (d);
	fclose (f);
	(void)basedir;
	(void)depth;
	return err;
}

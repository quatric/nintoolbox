// Marvelous "pBin" resource packs (Harvest Moon: Animal Parade "*.dpt" and siblings): big-endian {"pBin", u32 1, u32 1, u32 0xffffffff, u32 table offset (0x20), u32 count, ...} followed by count 16-byte entries {char tag[4] ("FMS ", "TEX ", "UVM ", ...), u32 0, u32 size, u32 offset}. The last entry ends at the end of the file (up to 0x7ff bytes of padding); entries with size 0 are placeholders. Members are written to "<file>.d/NNN_<tag>.bin".
static enumError extract_mrvpbin_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *d = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const long sz = ftell (f);
		u8 hd[0x20];
		if (sz < 0x40 || sz > 0x20000000 || fseeko (f, 0, SEEK_SET) || fread (hd, 1, 0x20, f) != 0x20)
			goto done;
		if (memcmp (hd, "pBin", 4) || be32 (hd + 4) != 1 || be32 (hd + 12) != 0xffffffff || be32 (hd + 16) != 0x20)
			goto done;
		const uint n = be32 (hd + 20);
		if (!n || n > 100000 || 0x20 + 16 * (size_t)n > (size_t)sz)
			goto done;
		d = MALLOC ((size_t)sz);
		if (fseeko (f, 0, SEEK_SET) || fread (d, 1, (size_t)sz, f) != (size_t)sz)
			goto done;
		const u8 *e = d + 0x20;
		u64 end = 0;
		for (uint i = 0; i < n; i++)
		{
			const u32 s = be32 (e + 16 * i + 8), o = be32 (e + 16 * i + 12);
			if (s && ((u64)s + o > (u64)sz || o < 0x20 + 16 * (size_t)n))
				goto done;
			for (int k = 0; k < 4; k++)
				if (e[16 * i + k] < 0x20 || e[16 * i + k] > 0x7e)
					goto done;
			if (s && (u64)s + o > end)
				end = (u64)s + o;
		}
		if (end > (u64)sz || (u64)sz - end >= 0x800)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT MRVPBIN:%s (%u members) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u32 s = be32 (e + 16 * i + 8), o = be32 (e + 16 * i + 12);
			if (!s)
				continue;
			char tag[5] = { e[16 * i], e[16 * i + 1], e[16 * i + 2], e[16 * i + 3], 0 };
			for (int k = 3; k >= 0 && tag[k] == ' '; k--)
				tag[k] = 0;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%03u_%s.bin", dest, i, tag);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && s && fwrite (d + o, 1, s, F.f) != s)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (d);
	fclose (f);
	return err;
}

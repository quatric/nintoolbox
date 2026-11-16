// Konami "afs" containers (Pro Evolution Soccer 2012, Wii; not the CRI "AFS\0" archive): u8 type (4), 3 flag bytes, u32 BE total size, ..., u32 BE count at 0x20, then
// count + 1 u32 BE offsets relative to 0x20 (the last one is the end). A member is either stored as is (SDHS sound data, PACS packs, ...) or wrapped as
// {u8 4, 3 flags, u32 BE packed size, u32 BE unpacked size, 20 zero bytes} + a zlib stream. Members are streamed (the sound archives are 200-750 MB) and written as
// "<n>.<ext>" with the extension taken from the 4 character tag of the payload (".bin" otherwise), zlib members after inflating.
static enumError extract_konamiafs_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".afs"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 h[0x28];
	u32 *off = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (fread (h, 1, sizeof (h), f) != sizeof (h) || !memcmp (h, "AFS", 3) || h[0] == 0 || h[0] > 8 || fseeko (f, 0, SEEK_END))
		goto done;
	{
		const u64 size = (u64)ftello (f);
		const u32 n = rd_be32 (h + 0x20);
		if (rd_be32 (h + 4) > size || rd_be32 (h + 4) + 0x800 < size || !n || n > 0x10000 || 0x24 + 4 * (u64)(n + 1) > size || fseeko (f, 0x24, SEEK_SET))
			goto done;
		off = MALLOC (4 * ((size_t)n + 1));
		if (!off || fread (off, 4, n + 1, f) != n + 1)
			goto done;
		for (u32 i = 0; i <= n; i++)
		{
			off[i] = rd_be32 ((u8 *)&off[i]);
			if (0x20 + (u64)off[i] > size || (i && off[i] < off[i - 1]))
				goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT KONAMI-AFS:%s (%u members) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (u32 i = 0; i < n && !err && !testmode; i++)
		{
			const u64 o = 0x20 + (u64)off[i], l = off[i + 1] - off[i];
			if (!l)
				continue;
			u8 *buf = MALLOC (l);
			if (!buf || fseeko (f, (off_t)o, SEEK_SET) || fread (buf, 1, l, f) != l)
			{
				FREE (buf);
				err = ERR_READ_FAILED;
				break;
			}
			const u8 *data = buf;
			u8 *inf = 0;
			uint dsz = (uint)l;
			if (l > 0x22 && buf[0] == 4 && buf[0x20] == 0x78 && rd_be32 (buf + 4) + 0x20 <= l)
			{
				size_t used = 0;
				uint osz = 0;
				inf = fst_inflate (buf + 0x20, l - 0x20, &osz, &used);
				if (inf)
					data = inf, dsz = osz;
			}
			char ext[8] = "bin";
			if (dsz >= 4 && isupper (data[0]) && isupper (data[1]) && isalnum (data[2]) && isalnum (data[3]))
				for (uint k = 0; k < 4; k++)
					ext[k] = tolower (data[k]), ext[k + 1] = 0;
			char name[64];
			snprintf (name, sizeof (name), "%04u.%s", i, ext);
			err = shade_write (arg, dest, basedir, name, data, dsz);
			FREE (inf);
			FREE (buf);
		}
		if (!err && !testmode)
		{
			enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (off);
	fclose (f);
	return err;
}

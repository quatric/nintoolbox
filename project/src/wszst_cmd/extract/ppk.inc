// Luxor "PRPK" packs ("ASSETS_WII/*.PPK"): little-endian {"PRPK", u32 file size} at 0, 64-byte nodes from 0x30 {u32 crc, u32 flags (bit 16 = directory), u16 parent (0xffff = none), u16 link, u32 ..., u32 data offset @0x10 (0x80-aligned), u32 size @0x14, char name[0x28] @0x18}.
// Directory nodes form a tree through the parent index; files are written to "<file>.d/<path>" and recursed.
static enumError extract_ppk_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".ppk"))
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
		if (sz < 0x400 || sz > 0x20000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)sz);
		if (fread (d, 1, (size_t)sz, f) != (size_t)sz || memcmp (d, "PRPK", 4) || rd_le32 (d + 4) != (u32)sz)
			goto done;
		// find the end of the node table: the smallest file data offset
		u32 minoff = (u32)sz;
		uint nn = 0;
		for (uint i = 0; 0x30 + (u64)(i + 1) * 0x40 <= minoff && 0x30 + (u64)(i + 1) * 0x40 <= (u64)sz; i++)
		{
			const u8 *n = d + 0x30 + i * 0x40;
			nn = i + 1;
			if (!(rd_le32 (n + 4) & 0x10000) && n[0x18] && rd_le32 (n + 0x10) < minoff && rd_le32 (n + 0x14))
				minoff = rd_le32 (n + 0x10);
		}
		if (nn < 2)
			goto done;
		uint nfiles = 0;
		for (uint i = 0; i < nn; i++)
		{
			const u8 *n = d + 0x30 + i * 0x40;
			if (!(rd_le32 (n + 4) & 0x10000) && n[0x18])
			{
				if ((u64)rd_le32 (n + 0x10) + rd_le32 (n + 0x14) > (u64)sz)
					goto done;
				nfiles++;
			}
		}
		if (!nfiles)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT PPK:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nfiles, dest);
		err = ERR_OK;
		for (uint i = 0; i < nn && !err && !testmode; i++)
		{
			const u8 *n = d + 0x30 + i * 0x40;
			if ((rd_le32 (n + 4) & 0x10000) || !n[0x18])
				continue;
			char names[16][0x29];
			uint depthn = 0;
			char nm[0x29];
			memcpy (nm, n + 0x18, 0x28);
			nm[0x28] = 0;
			uint par = rd_le16 (n + 8);
			while (par != 0xffff && par < nn && depthn < 16)
			{
				const u8 *pn = d + 0x30 + par * 0x40;
				memcpy (names[depthn], pn + 0x18, 0x28);
				names[depthn][0x28] = 0;
				depthn++;
				par = rd_le16 (pn + 8);
			}
			char path[PATH_MAX];
			int w = snprintf (path, sizeof (path), "%s", dest);
			for (int k = (int)depthn - 1; k >= 0 && w < (int)sizeof (path) - 64; k--)
				w += snprintf (path + w, sizeof (path) - w, "/%s", names[k]);
			snprintf (path + w, sizeof (path) - w, "/%s", nm);
			if (strstr (path, ".."))
				continue;
			const u32 off = rd_le32 (n + 0x10), size = rd_le32 (n + 0x14);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (d + off, 1, size, F.f) != size)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
			ResetFile (&F, opt_preserve);
		}
		if (!err && !testmode)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (d);
	fclose (f);
	return err;
}

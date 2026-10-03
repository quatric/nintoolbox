// Treasure Co., Ltd. Nintendo DS Multi-Resource Archive (*.mrg;
// Bleach: The Blade of Fate, Bleach: Dark Souls, Bangai-O Spirits; DS);
// see lib-treasuremrg.h. Members are streamed directly from the archive to disk.

static enumError extract_treasuremrg_file (ccp arg, ccp basedir, uint depth)
{
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;

	fseeko (f, 0, SEEK_END);
	const u64 file_size = (u64)ftello (f);
	rewind (f);

	if (file_size < 12)
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}

	u8 head[0x1000];
	const size_t n_read = fread (head, 1, sizeof (head), f);
	if (n_read < 12)
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}

	const u32 count = (u32)head[0] | ((u32)head[1] << 8) | ((u32)head[2] << 16) | ((u32)head[3] << 24);
	if (!count || count > TREASURE_MRG_MAX_FILES)
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}

	const u64 table_size = 4 + (u64)count * 8;
	if (table_size > file_size)
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}

	u8 *full_tbl = 0;
	const u8 *tbl_ptr = head;
	if (table_size > n_read)
	{
		full_tbl = MALLOC (table_size);
		if (!full_tbl)
		{
			fclose (f);
			return ERR_OUT_OF_MEMORY;
		}
		rewind (f);
		if (fread (full_tbl, 1, table_size, f) != table_size)
		{
			FREE (full_tbl);
			fclose (f);
			return ERR_READ_FAILED;
		}
		tbl_ptr = full_tbl;
	}

	if (!IsTreasureMrg (tbl_ptr, (uint)table_size, file_size))
	{
		if (full_tbl)
			FREE (full_tbl);
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT TREASURE-MRG:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);

	enumError err = ERR_OK;

	for (u32 i = 0; i < count && !err; i++)
	{
		const u8 *p = tbl_ptr + 4 + i * 8;
		const u32 off = (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
		const u32 sz = (u32)p[4] | ((u32)p[5] << 8) | ((u32)p[6] << 16) | ((u32)p[7] << 24);

		if (fseeko (f, (off_t)off, SEEK_SET))
		{
			err = ERR_READ_FAILED;
			break;
		}

		u8 magic[4] = { 0 };
		const size_t m_read = fread (magic, 1, sz < 4 ? sz : 4, f);
		char ext[16] = ".bin";
		if (m_read == 4)
		{
			if (!memcmp (magic, "\x04\x00\x01\x01", 4))
				strcpy (ext, ".bg4");
			else if (!memcmp (magic, "\x08\x00\x01\x01", 4))
				strcpy (ext, ".bg8");
		}

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/file_%03u%s", dest, i, ext);

		if (verbose > 0 || testmode)
			fprintf (stdlog, "%s  -> %s (%u bytes)\n", testmode ? "WOULD EXTRACT" : "EXTRACT",
				out_path, sz);

		if (!testmode)
		{
			File_t F;
			err = CreateFileOpt (&F, true, out_path, false, arg);
			if (!err && F.f && !fseeko (f, (off_t)off, SEEK_SET))
			{
				u8 *buf = MALLOC (1 << 20);
				u32 left = sz;
				if (!buf)
					err = ERR_OUT_OF_MEMORY;
				while (buf && left)
				{
					const size_t n = fread (buf, 1, left < (1 << 20) ? left : (1 << 20), f);
					if (!n || fwrite (buf, 1, n, F.f) != n)
					{
						err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", sz, out_path);
						break;
					}
					left -= (u32)n;
				}
				FREE (buf);
			}
			else if (!err && F.f)
				err = ERR_READ_FAILED;
			const enumError close_err = ResetFile (&F, opt_preserve);
			if (close_err > err)
				err = close_err;
			if (err)
				break;
		}
	}

	if (full_tbl)
		FREE (full_tbl);
	fclose (f);

	if (!err && !testmode && count > 0)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err ? err : (count > 0 ? ERR_OK : ERR_NOTHING_TO_DO);
}

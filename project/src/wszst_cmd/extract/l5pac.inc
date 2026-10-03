// Level-5 / Armor Project PAC archive container (*.pac, *.dat; Dragon Quest IX;
// DS); see lib-l5pac.h. Members are streamed directly from the archive to disk.

static enumError extract_l5pac_file (ccp arg, ccp basedir, uint depth)
{
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;

	u8 head[0x200];
	const size_t n_read = fread (head, 1, sizeof (head), f);
	if (n_read < L5PAC_HDR_SIZE)
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}

	fseeko (f, 0, SEEK_END);
	const u64 file_size = (u64)ftello (f);
	rewind (f);

	if (!IsL5Pac (head, n_read, file_size))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT L5-PAC:%s -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	enumError err = ERR_OK;
	uint total_files = 0;
	u64 off = 0;

	while (off + L5PAC_HDR_SIZE <= file_size && !err)
	{
		if (fseeko (f, (off_t)off, SEEK_SET))
			break;

		u8 hdr[L5PAC_HDR_SIZE];
		if (fread (hdr, 1, L5PAC_HDR_SIZE, f) != L5PAC_HDR_SIZE)
			break;

		const u32 h_len = (u32)hdr[0x40] | ((u32)hdr[0x41] << 8) | ((u32)hdr[0x42] << 16) | ((u32)hdr[0x43] << 24);
		const u32 f_len = (u32)hdr[0x44] | ((u32)hdr[0x45] << 8) | ((u32)hdr[0x46] << 16) | ((u32)hdr[0x47] << 24);
		const u32 a_len = (u32)hdr[0x48] | ((u32)hdr[0x49] << 8) | ((u32)hdr[0x4a] << 16) | ((u32)hdr[0x4b] << 24);

		if (h_len != L5PAC_HDR_SIZE)
			break;
		if (a_len == 0 || a_len == 0xffffffff || f_len == 0xffffffff)
			break;
		if (a_len < L5PAC_HDR_SIZE || f_len > a_len - L5PAC_HDR_SIZE
			|| off + a_len > file_size)
		{
			err = ERR_INVALID_DATA;
			break;
		}

		// Entry filename in first 40 bytes
		char name[64];
		size_t nlen = 0;
		while (nlen < 40 && hdr[nlen] != 0)
		{
			name[nlen] = (char)hdr[nlen];
			nlen++;
		}
		name[nlen] = '\0';
		if (nlen == 0)
			break;
		if (nlen == 40 || !valid_sarc_path (name))
			snprintf (name, sizeof (name), "file_%04u.bin", total_files);

		if (!testmode)
		{
			char out_path[PATH_MAX];
			snprintf (out_path, sizeof (out_path), "%s/%s%s", dest, basedir ? basedir : "", name);

			File_t F;
			err = CreateFileOpt (&F, true, out_path, false, arg);
			if (!err && F.f && !fseeko (f, (off_t)(off + L5PAC_HDR_SIZE), SEEK_SET))
			{
				u8 *buf = MALLOC (1 << 20);
				u32 left = f_len;
				if (!buf)
					err = ERR_OUT_OF_MEMORY;
				while (buf && left)
				{
					const size_t n = fread (buf, 1, left < (1 << 20) ? left : (1 << 20), f);
					if (!n || fwrite (buf, 1, n, F.f) != n)
					{
						err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", f_len, out_path);
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
		}

		total_files++;
		off += a_len;
	}

	fclose (f);

	if (!err && !testmode && total_files > 0)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

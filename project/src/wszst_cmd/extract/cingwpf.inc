// CiNG Wish Pack File (*.wpf; Hotel Dusk: Room 215 / Wish Room; DS);
// see lib-cingwpf.h. Members are streamed directly from the archive to disk.

static enumError extract_cingwpf_file (ccp arg, ccp basedir, uint depth)
{
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;

	u8 head[0x200];
	const size_t n_read = fread (head, 1, sizeof (head), f);
	if (n_read < CINGWPF_HDR_SIZE)
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}

	fseeko (f, 0, SEEK_END);
	const u64 file_size = (u64)ftello (f);
	rewind (f);

	if (!IsCingWpf (head, n_read, file_size))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CING-WPF:%s -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	enumError err = ERR_OK;
	uint total_files = 0;
	u64 off = 0;

	while (off + CINGWPF_HDR_SIZE <= file_size && !err)
	{
		if (fseeko (f, (off_t)off, SEEK_SET))
			break;

		u8 hdr[CINGWPF_HDR_SIZE];
		if (fread (hdr, 1, CINGWPF_HDR_SIZE, f) != CINGWPF_HDR_SIZE)
			break;

		size_t nlen = 0;
		while (nlen < 24 && hdr[nlen] != 0)
			nlen++;
		if (nlen == 0)
			break;

		const u32 f_len = (u32)hdr[24] | ((u32)hdr[25] << 8) | ((u32)hdr[26] << 16) | ((u32)hdr[27] << 24);
		const u32 next_off = (u32)hdr[28] | ((u32)hdr[29] << 8) | ((u32)hdr[30] << 16) | ((u32)hdr[31] << 24);

		if (f_len == 0 || off + CINGWPF_HDR_SIZE + f_len > file_size)
			break;

		// Clean filename
		const u8 *name_start = hdr;
		size_t clean_len = nlen;
		if (clean_len > 0 && (name_start[0] == '\\' || name_start[0] == '/'))
		{
			name_start++;
			clean_len--;
		}

		char name[64];
		if (clean_len >= sizeof (name))
			clean_len = sizeof (name) - 1;
		memcpy (name, name_start, clean_len);
		name[clean_len] = '\0';

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

		if (verbose > 0 || testmode)
			fprintf (stdlog, "%s  -> %s (%u bytes)\n", testmode ? "WOULD EXTRACT" : "EXTRACT",
				name, f_len);

		if (!testmode)
		{
			File_t F;
			err = CreateFileOpt (&F, true, out_path, false, arg);
			if (!err && F.f && !fseeko (f, (off_t)(off + CINGWPF_HDR_SIZE), SEEK_SET))
			{
				u8 *buf = MALLOC (1 << 20);
				u32 left = f_len;
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
			ResetFile (&F, opt_preserve);
			if (err)
				break;
		}

		total_files++;
		if (next_off <= off)
			break;
		off = next_off;
	}

	fclose (f);

	if (!err && !testmode && total_files > 0)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err ? err : (total_files > 0 ? ERR_OK : ERR_NOTHING_TO_DO);
}

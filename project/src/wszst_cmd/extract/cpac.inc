// Capcom CPAC multi-section archive container (*.bin; Ghost Trick: Phantom
// Detective, Resident Evil: Deadly Silence; DS); see lib-cpac.h. Members are
// streamed directly from the archive to disk.

static enumError extract_cpac_file (ccp arg, ccp basedir, uint depth)
{
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;

	u8 first[0x100];
	const size_t n_read = fread (first, 1, sizeof (first), f);
	if (n_read < 0x20)
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}

	fseeko (f, 0, SEEK_END);
	const u64 file_size = (u64)ftello (f);
	rewind (f);

	if (!IsCpac (first, n_read, file_size))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}

	const u32 head_len = (u32)first[0] | ((u32)first[1] << 8) | ((u32)first[2] << 16) | ((u32)first[3] << 24);
	const uint n_sections = head_len / 8;
	if (!n_sections || n_sections > 32)
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}

	typedef struct sec_rec_t {
		u32 offset;
		u32 size;
	} sec_rec_t;

	sec_rec_t secs[32];
	memset (secs, 0, sizeof (secs));
	secs[0].offset = head_len;
	secs[0].size = (u32)first[4] | ((u32)first[5] << 8) | ((u32)first[6] << 16) | ((u32)first[7] << 24);

	for (uint i = 1; i < n_sections; i++)
	{
		secs[i].offset = (u32)first[i * 8] | ((u32)first[i * 8 + 1] << 8) | ((u32)first[i * 8 + 2] << 16) | ((u32)first[i * 8 + 3] << 24);
		secs[i].size = (u32)first[i * 8 + 4] | ((u32)first[i * 8 + 5] << 8) | ((u32)first[i * 8 + 6] << 16) | ((u32)first[i * 8 + 7] << 24);
	}

	// We need each section's header table to discover members.
	// We read section tables sequentially and extract files.
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CPAC:%s (%u sections) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_sections, dest);

	enumError err = ERR_OK;
	uint total_files = 0;

	for (uint s_idx = 0; s_idx < n_sections && !err; s_idx++)
	{
		const u32 soff = secs[s_idx].offset;
		const u32 ssz = secs[s_idx].size;
		if (ssz < 24 || soff >= file_size)
			continue;

		if (fseeko (f, (off_t)soff, SEEK_SET))
			continue;

		u8 tag_hdr[24];
		if (fread (tag_hdr, 1, 24, f) != 24)
			continue;

		const u32 key_tag = (u32)tag_hdr[8] | ((u32)tag_hdr[9] << 8) | ((u32)tag_hdr[10] << 16) | ((u32)tag_hdr[11] << 24);
		const u32 tbl_len = (u32)tag_hdr[20] | ((u32)tag_hdr[21] << 8) | ((u32)tag_hdr[22] << 16) | ((u32)tag_hdr[23] << 24);

		if (tbl_len < 32 || tbl_len > 0x1000000 || soff + tbl_len > file_size)
			continue;

		u8 *sec_tbl = MALLOC (tbl_len);
		if (!sec_tbl)
			continue;

		memcpy (sec_tbl, tag_hdr, 24);
		if (fread (sec_tbl + 24, 1, tbl_len - 24, f) != tbl_len - 24)
		{
			FREE (sec_tbl);
			continue;
		}

		const u32 payload_start = soff + tbl_len;

		if (key_tag == 0x424b4559) // 'BKEY'
		{
			const uint n_recs = (tbl_len - 32) / 16;
			uint sec_file = 0;
			for (uint r = 0; r < n_recs && !err && !testmode; r++)
			{
				const u8 *rec = sec_tbl + 32 + r * 16;
				const u32 o1_raw = (u32)rec[0] | ((u32)rec[1] << 8) | ((u32)rec[2] << 16) | ((u32)rec[3] << 24);
				const u32 s1_raw = (u32)rec[4] | ((u32)rec[5] << 8) | ((u32)rec[6] << 16) | ((u32)rec[7] << 24);
				const u32 o2_raw = (u32)rec[8] | ((u32)rec[9] << 8) | ((u32)rec[10] << 16) | ((u32)rec[11] << 24);
				const u32 s2_raw = (u32)rec[12] | ((u32)rec[13] << 8) | ((u32)rec[14] << 16) | ((u32)rec[15] << 24);

				const u32 parts[2][2] = { { o1_raw, s1_raw }, { o2_raw, s2_raw } };
				for (int p = 0; p < 2 && !err; p++)
				{
					const u32 raw_o = parts[p][0];
					const u32 raw_s = parts[p][1];
					const u32 sz = raw_s & 0x7FFFFFFF;
					if (sz == 0)
						continue;

					const u32 off = payload_start + (raw_o & 0x7FFFFFFF);
					if (off + sz > file_size)
						continue;

					// Peek magic from member
					char ext[16] = ".bin";
					if (!fseeko (f, (off_t)off, SEEK_SET))
					{
						u8 peek[8];
						if (fread (peek, 1, sizeof (peek), f) == sizeof (peek))
						{
							if (peek[0] == 0x11)
							{
								if (!memcmp (peek + 4, "\0sdt", 4))
									strcpy (ext, ".sdt");
								else if (!memcmp (peek + 4, "\0bfe", 4))
									strcpy (ext, ".bfe");
							}
							else if (!memcmp (peek, "RNAN", 4))
								strcpy (ext, ".nanr");
							else if (!memcmp (peek, "RECN", 4))
								strcpy (ext, ".ncer");
							else if (!memcmp (peek, "RGCN", 4))
								strcpy (ext, ".ncgr");
						}
					}

					char name[PATH_MAX];
					snprintf (name, sizeof (name), "sec%u_%04u_%04u%s", s_idx, r, sec_file++, ext);

					char out_path[PATH_MAX];
					snprintf (out_path, sizeof (out_path), "%s/%s%s", dest, basedir ? basedir : "", name);

					File_t F;
					err = CreateFileOpt (&F, true, out_path, false, arg);
					if (!err && F.f && !fseeko (f, (off_t)off, SEEK_SET))
					{
						u8 *buf = MALLOC (1 << 20);
						u32 left = sz;
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
					ResetFile (&F, opt_preserve);
					total_files++;
				}
			}
		}
		else if (key_tag == 0x504b4559) // 'PKEY'
		{
			const uint n_desc = (tbl_len - 32) / 4;
			for (uint d_idx = 0; d_idx < n_desc && !err && !testmode; d_idx++)
			{
				const u8 *rec = sec_tbl + 32 + d_idx * 4;
				const u16 w0 = (u16)rec[0] | ((u16)rec[1] << 8);
				const u16 w1 = (u16)rec[2] | ((u16)rec[3] << 8);
				const u32 pal_sz = (w0 == 0x0100) ? 512 : 32;
				const u32 off = payload_start + (u32)w1 * 32;

				if (off + pal_sz > file_size)
					continue;

				char name[PATH_MAX];
				snprintf (name, sizeof (name), "sec%u_pal_%04u.nclr", s_idx, d_idx);

				char out_path[PATH_MAX];
				snprintf (out_path, sizeof (out_path), "%s/%s%s", dest, basedir ? basedir : "", name);

				File_t F;
				err = CreateFileOpt (&F, true, out_path, false, arg);
				if (!err && F.f && !fseeko (f, (off_t)off, SEEK_SET))
				{
					u8 pal_buf[512];
					const size_t n = fread (pal_buf, 1, pal_sz, f);
					if (n == pal_sz)
						fwrite (pal_buf, 1, pal_sz, F.f);
				}
				ResetFile (&F, opt_preserve);
				total_files++;
			}
		}

		FREE (sec_tbl);
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

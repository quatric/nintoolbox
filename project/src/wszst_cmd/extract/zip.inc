static inline u32 zip_le32 (const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static enumError extract_zip_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".zip") && !is_ext (arg, ".tszip"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	// The end-of-central-directory record is followed only by its (at most
	// 64 KiB) comment.  Scanning backwards also permits self-extracting ZIPs.
	const size_t begin = raw_size > 0x10016 ? raw_size - 0x10016 : 0;
	const u8 *eocd = 0;
	for (size_t pos = raw_size >= 22 ? raw_size - 22 : 0; raw_size >= 22 && pos >= begin; pos--)
	{
		if (!memcmp (raw + pos, "PK\x05\x06", 4)
			&& pos + 22 + zip_le16 (raw + pos + 20) == raw_size)
		{
			eocd = raw + pos;
			break;
		}
		if (!pos)
			break;
	}
	if (!eocd || zip_le16 (eocd + 4) || zip_le16 (eocd + 6)
		|| zip_le16 (eocd + 8) != zip_le16 (eocd + 10))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const uint n_entries = zip_le16 (eocd + 10);
	const u32 cd_size = zip_le32 (eocd + 12), cd_off = zip_le32 (eocd + 16);
	if (cd_off == 0xffffffff || cd_size == 0xffffffff || cd_off > raw_size
		|| cd_size > raw_size - cd_off)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO; // ZIP64 is deliberately not guessed at.
	}

	const u8 *cd = raw + cd_off, *cd_end = cd + cd_size;
	const u8 *entry = cd;
	for (uint i = 0; i < n_entries; i++)
	{
		if (entry > cd_end || (size_t)(cd_end - entry) < 46 || memcmp (entry, "PK\x01\x02", 4))
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		const size_t entry_size
			= 46u + zip_le16 (entry + 28) + zip_le16 (entry + 30) + zip_le16 (entry + 32);
		if (entry_size > (size_t)(cd_end - entry))
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		entry += entry_size;
	}
	if (entry != cd_end)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ZIP:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_entries, dest);

	entry = cd;
	for (uint i = 0; !err && i < n_entries; i++)
	{
		const uint name_len = zip_le16 (entry + 28), extra_len = zip_le16 (entry + 30);
		const uint flags = zip_le16 (entry + 8), method = zip_le16 (entry + 10);
		const u32 compressed_size = zip_le32 (entry + 20),
				  uncompressed_size = zip_le32 (entry + 24);
		const u32 local_off = zip_le32 (entry + 42);
		const char *name = (ccp)(entry + 46);

		// Directories are implied by file paths. Reject encrypted, ZIP64 and
		// unsupported-compression members rather than emitting corrupt bytes.
		if (name_len >= PATH_MAX || !name_len || name[name_len - 1] == '/')
			goto next_zip_entry;
		char rel[PATH_MAX];
		memcpy (rel, name, name_len);
		rel[name_len] = 0;
		if (!valid_sarc_path (rel) || flags & 1 || local_off == 0xffffffff
			|| compressed_size == 0xffffffff || uncompressed_size == 0xffffffff
			|| (method != 0 && method != 8) || local_off > raw_size || raw_size - local_off < 30
			|| memcmp (raw + local_off, "PK\x03\x04", 4))
			goto next_zip_entry;

		const uint local_name_len = zip_le16 (raw + local_off + 26);
		const uint local_extra_len = zip_le16 (raw + local_off + 28);
		const size_t data_off = (size_t)local_off + 30 + local_name_len + local_extra_len;
		if (data_off > raw_size || compressed_size > raw_size - data_off)
			goto next_zip_entry;

		if (!testmode)
		{
			u8 *out = 0;
			if (method == 0)
			{
				if (compressed_size != uncompressed_size)
					goto next_zip_entry;
				out = MALLOC (uncompressed_size ? uncompressed_size : 1);
				if (uncompressed_size)
					memcpy (out, raw + data_off, uncompressed_size);
			}
			else
			{
				out = MALLOC (uncompressed_size ? uncompressed_size : 1);
				z_stream zs = { 0 };
				zs.next_in = raw + data_off;
				zs.avail_in = compressed_size;
				zs.next_out = out;
				zs.avail_out = uncompressed_size;
				if (inflateInit2 (&zs, -MAX_WBITS) != Z_OK
					|| inflate (&zs, Z_FINISH) != Z_STREAM_END || zs.total_out != uncompressed_size
					|| inflateEnd (&zs) != Z_OK)
				{
					FREE (out);
					goto next_zip_entry;
				}
			}

			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", rel);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && uncompressed_size
				&& fwrite (out, 1, uncompressed_size, F.f) != uncompressed_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", uncompressed_size, path);
			ResetFile (&F, opt_preserve);
			FREE (out);
		}

	next_zip_entry:
		entry += 46u + name_len + extra_len + zip_le16 (entry + 32);
	}
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

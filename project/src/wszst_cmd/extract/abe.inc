static enumError extract_abe_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	u8 header[0x40];
	FILE *in = fopen (arg, "rb");
	if (!in)
		return ERR_NOT_EXISTS;
	if (!read_abe_at (in, 0, header, sizeof (header)) || memcmp (header, "ABE\0", 4)
		|| get_le32 (header + 4) != 4)
	{
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}

	const u32 ntab = get_le32 (header + 0x0c);
	const u32 nfile = get_le32 (header + 0x2c);
	u64 tab = get_le32 (header + 0x18);
	if (!ntab || !nfile || ntab > 64 || !tab)
	{
		fclose (in);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ABE:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, nfile, dest);

	enumError err = ERR_OK;
	u32 emitted = 0;
	for (u32 ti = 0; !err && ti < ntab && emitted < nfile; ti++)
	{
		u8 th[12];
		if (!read_abe_at (in, tab, th, sizeof (th)))
		{
			err = ERR_INVALID_DATA;
			break;
		}
		const u32 count = get_le32 (th);
		const u32 link = get_le32 (th + 4);
		const u32 next = get_le32 (th + 8);
		if (count > nfile - emitted || tab > UINT_MAX - 12ULL - (u64)count * 0xc8)
		{
			err = ERR_INVALID_DATA;
			break;
		}
		for (u32 i = 0; !err && i < count; i++, emitted++)
		{
			u8 entry[0xc8], engine[0x20];
			if (!read_abe_at (in, tab + 12ULL + (u64)i * 0xc8, entry, sizeof (entry)))
			{
				err = ERR_INVALID_DATA;
				break;
			}
			// The 64-byte name area holds two NUL-terminated strings back to
			// back: the base name, then the extension (e.g. "fbf01f48\0bin\0").
			char base[65];
			memcpy (base, entry, 64);
			base[64] = 0;
			if (!valid_abe_name (base))
			{
				err = ERR_INVALID_DATA;
				break;
			}
			const size_t base_len = strlen (base);
			ccp ext = base_len < 63 ? base + base_len + 1 : "";
			char name[130];
			if (*ext && valid_abe_name (ext))
				snprintf (name, sizeof (name), "%s.%s", base, ext);
			else
				snprintf (name, sizeof (name), "%s", base);
			const u64 data_off = get_le32 (entry + 0x6c);
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s", dest, name);
			if (testmode)
				continue;
			if (!read_abe_at (in, data_off, engine, sizeof (engine)))
			{
				err = ERR_INVALID_DATA;
				break;
			}
			const u32 packed = get_le32 (engine);
			const u32 original = get_le32 (engine + 4);
			const u32 type = get_le32 (engine + 12);
			if (type == 0 && original && packed <= UINT_MAX - 32)
			{
				if (packed == original)
					err = write_abe_member (in, data_off + 32, original, path, arg);
				else
				{
					u8 *src = MALLOC (packed), *dec = 0;
					uint dec_size = 0;
					if (!src || !read_abe_at (in, data_off + 32, src, packed))
						err = ERR_INVALID_DATA;
					else
						err = DecodeLZO1XGrow (&dec, &dec_size, src, packed);
					if (!err && dec_size != original)
						err = ERR_INVALID_DATA;
					if (!err)
					{
						File_t out;
						err = CreateFileOpt (&out, true, path, false, arg);
						if (out.f)
						{
							if (fwrite (dec, 1, dec_size, out.f) != dec_size)
								err = ERR_WRITE_FAILED;
							ResetFile (&out, opt_preserve);
						}
					}
					FREE (src);
					FREE (dec);
				}
			}
			else if (type == 2 && original)
				err = write_abe_member (in, data_off + 32, original, path, arg);
			else if (!type && !original)
				err = ERR_OK;
			else if (type == 4 && original)
			{
				u8 count_buf[4];
				if (!read_abe_at (in, data_off + 32, count_buf, sizeof (count_buf)))
				{
					err = ERR_INVALID_DATA;
					break;
				}
				const u32 chunks = get_le32 (count_buf);
				if (!chunks || chunks > 0x10000)
				{
					err = ERR_INVALID_DATA;
					break;
				}
				u32 *sizes = MALLOC ((size_t)chunks * sizeof (*sizes));
				if (!sizes
					|| !read_abe_at (in, data_off + 36, sizes, (size_t)chunks * sizeof (*sizes)))
					err = ERR_INVALID_DATA;
				File_t out = { 0 };
				if (!err)
					err = CreateFileOpt (&out, true, path, false, arg);
				u64 pos = data_off + 36ULL + (u64)chunks * 4;
				u32 left = original;
				for (u32 j = 0; !err && j < chunks && left; j++)
				{
					const u32 packed_size = get_le32 ((u8 *)sizes + j * 4);
					const u32 want = left < 0x3fffc ? left : 0x3fffc;
					if (!packed_size)
					{
						u8 *raw = MALLOC (want);
						if (!raw || !read_abe_at (in, pos, raw, want)
							|| fwrite (raw, 1, want, out.f) != want)
							err = ERR_WRITE_FAILED;
						FREE (raw);
					}
					else
					{
						u8 *src = MALLOC (packed_size), *dec = 0;
						uint dec_size = 0;
						if (!src || !read_abe_at (in, pos, src, packed_size))
							err = ERR_INVALID_DATA;
						else
							err = DecodeLZO1XGrow (&dec, &dec_size, src, packed_size);
						if (!err
							&& (dec_size != want || fwrite (dec, 1, dec_size, out.f) != dec_size))
							err = ERR_WRITE_FAILED;
						FREE (src);
						FREE (dec);
					}
					pos += packed_size ? packed_size : want;
					left -= want;
				}
				if (!err && left)
					err = ERR_INVALID_DATA;
				if (out.f)
					ResetFile (&out, opt_preserve);
				FREE (sizes);
			}
			else
				// Type 3 is a cross-BigFile shortcut. Keep its engine record so
				// callers can resolve it against the companion archive.
				err = write_abe_member (in, data_off, packed, path, arg);
		}
		if (link == 0 && next != UINT_MAX)
			tab = next;
		else if (ti + 1 < ntab)
		{
			err = ERR_INVALID_DATA;
			break;
		}
	}
	fclose (in);
	if (!err && emitted != nfile)
		err = ERR_INVALID_DATA;
	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

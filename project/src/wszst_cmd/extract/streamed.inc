// Shared writer for archives scanned from an open file: each member is
// copied from the source in 1 MiB chunks, so multi-GB archives never have to
// fit in memory. Takes ownership of neither the entries nor the stream.
// Ubisoft Magma "ABE" LZO members: u32 n_blocks, n * u32 packed sizes, then the
// blocks back to back (LZO1X, each inflating to at most 256 KiB, the last one
// less). The stream is already positioned at the payload.
static u32 stream_rd_le32 (const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

static enumError stream_write_abe_lzo (File_t *F, FILE *src, ccp arg, ccp path, u32 unpacked)
{
	u8 nb_raw[4];
	if (fread (nb_raw, 1, 4, src) != 4)
		return FILEERROR1 (F, ERR_READ_FAILED, "Reading member failed: %s\n", arg);
	const u32 n_blocks = stream_rd_le32 (nb_raw);
	if (!n_blocks || n_blocks > (unpacked >> 10) + 2)
		return FILEERROR1 (F, ERR_INVALID_DATA, "Bad LZO block count: %s\n", arg);

	u8 *tab = MALLOC ((size_t)n_blocks * 4);
	if (!tab)
		return ERR_OUT_OF_MEMORY;
	if (fread (tab, 1, (size_t)n_blocks * 4, src) != (size_t)n_blocks * 4)
	{
		FREE (tab);
		return FILEERROR1 (F, ERR_READ_FAILED, "Reading member failed: %s\n", arg);
	}

	enumError err = ERR_OK;
	u8 *packed = 0;
	u32 packed_cap = 0, total = 0;
	for (u32 b = 0; !err && b < n_blocks; b++)
	{
		const u32 psz = stream_rd_le32 (tab + 4 * b);
		if (!psz || psz > 0x100000)
		{
			err = FILEERROR1 (F, ERR_INVALID_DATA, "Bad LZO block size: %s\n", arg);
			break;
		}
		if (psz + 3 > packed_cap)
		{
			u8 *np = REALLOC (packed, psz + 3);
			if (!np)
			{
				err = ERR_OUT_OF_MEMORY;
				break;
			}
			packed = np;
			packed_cap = psz + 3;
		}
		if (fread (packed, 1, psz, src) != psz)
		{
			err = FILEERROR1 (F, ERR_READ_FAILED, "Reading member failed: %s\n", arg);
			break;
		}
		u8 *plain = 0;
		uint plain_size = 0;
		// Blocks may end without the LZO end-of-stream marker; retry with one appended.
		enumError lzo_err = DecodeLZO1XGrow (&plain, &plain_size, packed, psz);
		if (lzo_err)
		{
			packed[psz] = 0x11;
			packed[psz + 1] = packed[psz + 2] = 0;
			lzo_err = DecodeLZO1XGrow (&plain, &plain_size, packed, psz + 3);
		}
		if (lzo_err)
			err = FILEERROR1 (F, ERR_INVALID_DATA, "LZO block failed: %s\n", arg);
		else if (fwrite (plain, 1, plain_size, F->f) != plain_size)
			err = FILEERROR1 (F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", plain_size, path);
		else
			total += plain_size;
		FREE (plain);
	}
	FREE (packed);
	FREE (tab);
	if (!err && total != unpacked)
		fprintf (stderr, "WARNING: %s: unpacked %u bytes, expected %u\n", path, total, unpacked);
	return err;
}

static enumError extract_streamed_entries (ccp arg, ccp basedir, uint depth, ccp label, FILE *src,
	const stream_entry_t *entries, uint n_entries)
{
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT %s:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", label, arg, n_entries, dest);

	enumError err = ERR_OK;
	u8 *buf = testmode ? 0 : MALLOC (0x100000);
	if (!testmode && !buf)
		return ERR_CANT_CREATE;
	for (uint i = 0; !err && !testmode && i < n_entries; i++)
	{
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", entries[i].name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fseeko (src, (off_t)entries[i].offset, SEEK_SET))
			err = FILEERROR1 (&F, ERR_READ_FAILED, "Seek failed: %s\n", arg);
		if (F.f && !err && entries[i].codec == STREAM_CODEC_ABE_LZO)
			err = stream_write_abe_lzo (&F, src, arg, path, entries[i].size);
		for (u32 left = entries[i].codec ? 0 : entries[i].size; F.f && !err && left;)
		{
			const size_t chunk = left < 0x100000 ? left : 0x100000;
			if (fread (buf, 1, chunk, src) != chunk)
				err = FILEERROR1 (&F, ERR_READ_FAILED, "Reading member failed: %s\n", arg);
			else if (fwrite (buf, 1, chunk, F.f) != chunk)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", chunk, path);
			left -= (u32)chunk;
		}
		ResetFile (&F, opt_preserve);
	}
	FREE (buf);

	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

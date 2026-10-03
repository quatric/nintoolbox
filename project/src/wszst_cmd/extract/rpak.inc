// RPAK has no magic at byte zero.  Its STRG/RSHD layout is sufficiently
// distinctive to recognize it cheaply without decoding every member.
static bool is_rpak_file (ccp arg)
{
	if (!is_ext (arg, ".pak"))
		return false;
	u8 head[0x84];
	FILE *f = fopen (arg, "rb");
	const size_t got = f ? fread (head, 1, sizeof (head), f) : 0;
	if (!f || got != sizeof (head))
	{
		if (f)
			fclose (f);
		return false;
	}
	const u32 strg_length = rd_be32 (head + 0x48);
	const u32 rshd_length = rd_be32 (head + 0x50);
	if (!strg_length && !rshd_length)
	{
		fclose (f);
		return false;
	}
	const u64 entries_off = (u64)0x80 + strg_length;
	if (entries_off > LONG_MAX || fseek (f, (long)entries_off, SEEK_SET))
	{
		fclose (f);
		return false;
	}
	u8 count[4];
	const bool valid = fread (count, 1, sizeof (count), f) == sizeof (count) && rd_be32 (count) > 0
		&& rd_be32 (count) <= 0x1000000;
	fclose (f);
	return valid;
}


//-----------------------------------------------------------------------------
// Bermuda Triangle .PKI extract

static enumError extract_bermuda_pki_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".PKI"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 36 || memcmp (raw, "IMAGE_WII_COMPACT_FILE_VERSION_1", 32))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u32 entry_count = rd_le32 (raw + 32);

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BERMUDA-PKI:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, entry_count, dest);

	size_t pos = 36;
	for (uint i = 0; !err && i < entry_count; i++)
	{
		if (pos + 4 > raw_size) break;
		u32 name_len = rd_le32 (raw + pos);
		pos += 4;
		if (pos + name_len + 8 > raw_size) break;
		const u8 *name = raw + pos;
		pos += name_len;
		u32 data_size = rd_le32 (raw + pos);
		pos += 4;
		u32 data_offset = rd_le32 (raw + pos);
		pos += 4;

		if (testmode) continue;

        // Path is Windows-style backslashes, e.g., DATA\CORALDATA\PLAYGAME\CORAL\03\01.MWT
		char e_path[PATH_MAX];
		snprintf (e_path, sizeof(e_path), "%s/%s", dest, basedir ? basedir : "");
		size_t e_len = strlen(e_path);
		for (u32 j = 0; j < name_len && e_len < sizeof(e_path)-1; j++)
		{
			char c = name[j];
			if (c == '\\') c = '/';
			e_path[e_len++] = c;
		}
		e_path[e_len] = 0;

		const u8 *out_data = 0;
		if (data_offset <= raw_size && data_offset + (u64)data_size <= raw_size)
			out_data = raw + data_offset;
		else
		    continue; // out of bounds

		File_t F;
		err = CreateFileOpt (&F, true, e_path, false, arg);
		if (F.f && data_size && fwrite (out_data, 1, data_size, F.f) != data_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", data_size, e_path);
		const enumError close_err = ResetFile (&F, opt_preserve);
		if (close_err > err)
			err = close_err;
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

static enumError extract_rpak_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pak"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}

	rpak_t pak;
	err = ScanRPAK (&pak, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT RPAK:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, pak.n_entries, dest);

	for (uint i = 0; !err && i < pak.n_entries; i++)
	{
		const rpak_entry_t *e = pak.entries + i;
		if (testmode)
			continue;

		// Real games embed non-ASCII "magic" bytes here as raw small integers
		// rather than a genuine 4-char tag (Disney Epic Mickey's own .pak
		// files, same Retro Studios engine as DKCR, have entries like this).
		// '/' (0x2F) is inside the naive printable-ASCII range but is a path
		// separator -- letting it through embeds an unintended subdirectory
		// in the destination path and corrupts extraction (confirmed: caused
		// a real "Can't create file ... Is a directory" failure on Epic
		// Mickey). Exclude it explicitly rather than only checking 0x20-0x7e.
		char ext[5];
		for (uint k = 0; k < 4; k++)
		{
			const u8 c = (u8)(e->magic >> (24 - 8 * k));
			ext[k] = c >= 0x20 && c < 0x7f && c != '/' ? (char)tolower (c) : '_';
		}
		ext[4] = 0;

		u8 *dec = 0;
		uint dec_size = 0;
		bool decompress_failed = false;
		if (e->compressed)
		{
			dec = DecompressRPAKEntry (e->data, e->size, &dec_size);
			decompress_failed = !dec;
		}
		const u8 *out_data = dec ? dec : e->data;
		const uint out_size = dec ? dec_size : e->size;

		// A block that fails to decompress (confirmed on a real Metroid Prime
		// 3 .pak: its CMPD blocks use some other, unidentified codec, not the
		// zlib DecompressRPAKEntry() expects -- the block header lies about
		// compression the same way as a genuinely zlib entry, but the payload
		// itself doesn't parse as zlib) is still raw, still-compressed data of
		// unknown real format -- writing it under the type-guessed extension
		// (e.g. ".cmdl") is actively wrong and dangerous: something downstream
		// keyed off that extension/magic can misidentify and mishandle it
		// (observed: routed to the Switch NCA tool 'hactool', which then fails
		// loudly on data that was never an NCA). Use a plain, inert extension
		// instead so nothing downstream tries to interpret the content.
		ccp out_ext = decompress_failed ? "bin" : ext;

		// (id_hi,id_lo) is not guaranteed unique across all .pak files -- a
		// real Disney Epic Mickey archive has 26 colliding pairs, all sharing
		// tiny non-ASCII "magic" values that fall back to the same "____"
		// extension. The entry index is always unique, so it's included
		// unconditionally rather than only as a collision fallback.
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%04u_%08x%08x.%s", dest, basedir ? basedir : "", i,
			e->id_hi, e->id_lo, out_ext);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && out_size && fwrite (out_data, 1, out_size, F.f) != out_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", out_size, path);
		const enumError close_err = ResetFile (&F, opt_preserve);
		if (close_err > err)
			err = close_err;
		FREE (dec);
	}

	ResetRPAK (&pak);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

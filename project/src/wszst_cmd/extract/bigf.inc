// EA's BIGF archives use a big-endian, variable-length table.  Littlest Pet
// Shop (Wii) keeps all of its game assets in these .big files; the largest
// one is well above the normal in-memory archive limit, so keep extraction
// streamed just like the ABE reader below.  Each table record is
// { u32 offset, u32 size, NUL-terminated path }, and the header's final u32
// marks the first payload byte (and therefore the end of the table).  FIFA/NBA
// Live-era banks use the same layout under the magic "BIG4".
static enumError extract_bigf_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	u8 header[16];
	FILE *in = fopen (arg, "rb");
	if (!in)
		return ERR_NOT_EXISTS;
	if (!read_abe_at (in, 0, header, sizeof (header)) || (memcmp (header, "BIGF", 4) && memcmp (header, "BIG4", 4)))
	{
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}

	if (fseek (in, 0, SEEK_END))
	{
		fclose (in);
		return ERR_READ_FAILED;
	}
	const long end = ftell (in);
	const u64 file_size = end < 0 ? 0 : (u64)end;
	// The total-size field is usually little-endian while the member table is
	// big-endian (Littlest Pet Shop's data.big is 947,545 bytes: header bytes
	// 59 75 0e 00); some SSX Blur BIG4 files (animdbg.big, nested scdat.big
	// members) store it big-endian.
	const u32 n_entries = be32 (header + 8);
	const u32 data_off = be32 (header + 12);
	if (!file_size || (get_le32 (header + 4) != file_size && be32 (header + 4) != file_size) || !n_entries || n_entries > 0x100000
		|| data_off < sizeof (header) || data_off > file_size)
	{
		fclose (in);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT EA BIGF:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_entries, dest);

	enumError err = ERR_OK;
	u64 table_pos = sizeof (header);
	for (u32 i = 0; !err && i < n_entries; i++)
	{
		u8 entry[8];
		if (table_pos > data_off - sizeof (entry)
			|| !read_abe_at (in, table_pos, entry, sizeof (entry)))
		{
			err = ERR_INVALID_DATA;
			break;
		}
		table_pos += sizeof (entry);
		const u64 member_off = be32 (entry);
		const u32 member_size = be32 (entry + 4);
		char name[PATH_MAX];
		uint name_len = 0;
		for (;;)
		{
			u8 ch;
			if (table_pos >= data_off || name_len + 1 >= sizeof (name)
				|| !read_abe_at (in, table_pos++, &ch, 1))
			{
				err = ERR_INVALID_DATA;
				break;
			}
			name[name_len++] = ch == '\\' ? '/' : ch; // SSX Blur's audio .big files use "data\audio\..."
			if (!ch)
				break;
		}
		if (err)
			break;
		if (!valid_sarc_path (name) || member_off < data_off || member_off > file_size
			|| member_size > file_size - member_off
			|| (opt_max_file_size && member_size > opt_max_file_size))
		{
			err = ERR_INVALID_DATA;
			break;
		}
		if (!testmode)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s", dest, name);
			err = write_abe_member (in, member_off, member_size, path, arg);
		}
	}
	// Some tool versions end the table with a four-byte "L2nn" marker (L234;
	// L231 and L222 in SSX Blur, followed by a u32 that is not always 0) and
	// DATA_OFF then points past them; newer SSX Blur files have DATA_OFF == end
	// of the table, a few (sounddbg.big) a zero-padded 4-byte aligned DATA_OFF.
	if (!err && table_pos != data_off)
	{
		u8 marker[4] = { 0 };
		const u64 gap = data_off - table_pos;
		if (!read_abe_at (in, table_pos, marker, gap < sizeof (marker) ? gap : sizeof (marker)))
			err = ERR_INVALID_DATA;
		else if (gap >= sizeof (marker) && marker[0] == 'L' && isdigit ((u8)marker[1]) && isdigit ((u8)marker[2])
			&& isdigit ((u8)marker[3]))
			;
		else if (gap >= sizeof (marker) || marker[0] || marker[1] || marker[2])
			err = ERR_INVALID_DATA;
	}
	fclose (in);
	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

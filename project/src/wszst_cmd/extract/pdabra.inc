// Messiah-engine ".bra" archives ("PDA\0", little-endian; Gummy Bears:
// Magical Medallion / MiniGolf):
//   u32 magic "PDA\0", u32 version = 2, u32 table_offset, u32 count;
//   count members back to back from 0x10, each {u32 size, u32 stored_size,
//   u32 crc, u32 flags} + a raw deflate stream of stored_size bytes;
//   at table_offset: count * {u32 time, u32 crc, u32 member_size
//   (stored_size + 16), u32 size, u16 name_len, u16 flags, u32 offset,
//   char name[name_len]} (names are NUL / 0xCC padded, '\' paths).
static enumError extract_pdabra_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bra"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size < 32 || memcmp (raw, "PDA\0", 4) || rd_le32 (raw + 4) != 2)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t tab = rd_le32 (raw + 8);
	const uint count = rd_le32 (raw + 12);
	if (!count || tab >= raw_size || count > (raw_size - tab) / 24)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BRA:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);

	enumError err = ERR_OK;
	uint n_files = 0;
	size_t p = tab;
	for (uint i = 0; i < count && !err; i++)
	{
		if (raw_size - p < 24)
			break;
		const u8 *e = raw + p;
		const uint size = rd_le32 (e + 12), nlen = rd_le16 (e + 16);
		const u64 off = rd_le32 (e + 20);
		p += 24;
		if (nlen > raw_size - p)
			break;
		char name[256];
		uint k = 0;
		while (k < nlen && k < sizeof (name) - 1 && raw[p + k] && raw[p + k] != 0xcc)
		{
			name[k] = raw[p + k] == '\\' ? '/' : raw[p + k];
			k++;
		}
		name[k] = 0;
		p += nlen;
		if (!OwnedNameOk (name))
			snprintf (name, sizeof (name), "%04u.bin", i);

		if (off + 16 > raw_size)
			continue;
		const uint stored = rd_le32 (raw + off + 4);
		if (stored > raw_size - off - 16 || size > (1u << 28))
			continue;
		u8 *dec = MALLOC (size ? size : 1);
		if (!dec)
		{
			err = ERR_OUT_OF_MEMORY;
			break;
		}
		z_stream zs;
		memset (&zs, 0, sizeof (zs));
		uint got = 0;
		if (inflateInit2 (&zs, -15) == Z_OK)
		{
			zs.next_in = raw + off + 16;
			zs.avail_in = stored;
			zs.next_out = dec;
			zs.avail_out = size;
			inflate (&zs, Z_FINISH);
			got = size - zs.avail_out;
			inflateEnd (&zs);
		}
		if (got != size)
		{
			FREE (dec);
			continue;
		}
		if (!testmode)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && size && fwrite (dec, 1, size, F.f) != size)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
			ResetFile (&F, opt_preserve);
		}
		n_files++;
		FREE (dec);
	}
	FREE (raw);
	if (!err && !n_files)
		return ERR_NOTHING_TO_DO;
	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

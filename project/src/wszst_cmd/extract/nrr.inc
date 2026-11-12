// Switch 2 NRR0 module-hash list (.nrr, e.g. Welcome Tour romfs/.nrr/tcn_Release.nrr):
// 'NRR0', a signed header, then a table {u32 hash_offset, u32 count, u64 0}
// followed by `count` SHA-256 digests of the whole .nro files the title may load.
// The table sits right after the signature area; it is found by checking each
// 16-byte slot for {offset == slot + 0x10, count that fits, zero tail} -- all 36
// digests of the Welcome Tour's nro/ files resolve this way.
// Written as `<file>.txt`: one digest per line.
static enumError extract_nrr_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".nrr"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, UINT_MAX, 2, 0, false))
		return ERR_NOTHING_TO_DO;
	if (raw_size < 0x40 || raw_size > (16 << 20) || memcmp (raw, "NRR0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	size_t table = 0;
	u32 count = 0;
	for (size_t o = 0x20; o + 16 <= raw_size; o += 16)
	{
		u32 hoff = rd_le32 (raw + o), n = rd_le32 (raw + o + 4);
		if (hoff != o + 0x10 || !n || n > 4096 || (u64)hoff + (u64)n * 32 > raw_size
			|| rd_le64 (raw + o + 8))
			continue;
		bool tail_zero = true;
		for (size_t i = hoff + (size_t)n * 32; i < raw_size && tail_zero; i++)
			tail_zero = !raw[i];
		if (tail_zero)
		{
			table = hoff;
			count = n;
			break;
		}
	}
	if (!table)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NRR:%s (%u module hashes) -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);

	enumError err = ERR_OK;
	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f)
		{
			fprintf (F.f, "# Switch 2 NRR0 module list: SHA-256 of each .nro\ncount\t%u\n", count);
			for (u32 i = 0; i < count; i++)
			{
				for (int b = 0; b < 32; b++)
					fprintf (F.f, "%02x", raw[table + (size_t)i * 32 + b]);
				fputc ('\n', F.f);
			}
			ResetFile (&F, opt_preserve);
		}
	}
	FREE (raw);
	return err;
}

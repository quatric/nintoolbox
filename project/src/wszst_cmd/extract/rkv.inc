// Krome "RK33" resource archives ".rkv" (Happy Feet Two, Legend of Spyro:
// The Eternal Night, Owls of Ga'Hoole; big-endian). A 0x100-byte header:
//   char name[0x40]; u64 toc_offset (0x40); u32 version = 6 (0x48);
//   u32 count (0x4c); u32 toc_size (0x50); "RK33" (0x54); ...
// The table sits at toc_offset, count * 0x60 bytes each:
//   char name[0x40]; u64 time; u64 data_offset (0x48); u32 crc (0x50);
//   u32 size (0x54); u32 stored_size (0x58); u32 0.
// stored_size == size means raw data; otherwise the block starts with a
// method byte 2 followed by an LZF stream.
static int rkv_lzf (const u8 *in, size_t in_size, u8 *out, size_t out_size)
{
	const u8 *ip = in, *ie = in + in_size;
	u8 *op = out, *oe = out + out_size;
	while (ip < ie)
	{
		uint ctrl = *ip++;
		if (ctrl < 0x20)
		{
			const size_t n = ctrl + 1;
			if ((size_t)(ie - ip) < n || (size_t)(oe - op) < n)
				return -1;
			memcpy (op, ip, n);
			ip += n;
			op += n;
		}
		else
		{
			size_t len = ctrl >> 5;
			if (len == 7)
			{
				if (ip >= ie)
					return -1;
				len += *ip++;
			}
			if (ip >= ie)
				return -1;
			const size_t back = ((size_t)(ctrl & 0x1f) << 8) + *ip++ + 1;
			len += 2;
			if (back > (size_t)(op - out) || (size_t)(oe - op) < len)
				return -1;
			for (size_t i = 0; i < len; i++, op++)
				*op = op[-(ptrdiff_t)back];
		}
	}
	return (int)(op - out);
}

static enumError extract_rkv_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".rkv"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size < 0x100 || memcmp (raw + 0x54, "RK33", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u64 toc = ((u64)rd_be32 (raw + 0x40) << 32) | rd_be32 (raw + 0x44);
	const uint count = rd_be32 (raw + 0x4c);
	if (!count || toc > raw_size || (raw_size - toc) / 0x60 < count)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT RKV:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);

	enumError err = ERR_OK;
	uint n_files = 0;
	for (uint i = 0; i < count && !err; i++)
	{
		const u8 *e = raw + toc + 0x60ull * i;
		char name[0x41];
		memcpy (name, e, 0x40);
		name[0x40] = 0;
		if (!OwnedNameOk (name))
			snprintf (name, sizeof (name), "%04u.bin", i);
		const u64 off = ((u64)rd_be32 (e + 0x48) << 32) | rd_be32 (e + 0x4c);
		const u32 size = rd_be32 (e + 0x54), csize = rd_be32 (e + 0x58);
		if (off > raw_size || csize > raw_size - off)
			continue;

		const u8 *data = raw + off;
		u8 *dec = 0;
		uint dsz = size;
		if (csize != size)
		{
			if (!csize || data[0] != 2 || size > (1u << 28))
				continue;
			dec = MALLOC (size ? size : 1);
			if (!dec)
			{
				err = ERR_OUT_OF_MEMORY;
				break;
			}
			const int got = rkv_lzf (data + 1, csize - 1, dec, size);
			if (got < 0)
			{
				FREE (dec);
				continue;
			}
			dsz = (uint)got;
			data = dec;
		}
		if (!testmode)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && dsz && fwrite (data, 1, dsz, F.f) != dsz)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", dsz, path);
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

// Messiah-engine "3df " level/model containers (Gummy Bears). The file is a
// chunk graph that embeds raw-deflate members using the same 16-byte header
// as ".bra" members {u32 size, u32 stored_size, u32 crc, u32 0x????0006}.
// Only those members are recovered, as "<index>_<offset>.bin".
static enumError extract_threedf_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".3df"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size < 0x100 || memcmp (raw, "3df\0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT 3DF:%s -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	enumError err = ERR_OK;
	uint n = 0;
	for (size_t o = 0x20; o + 16 < raw_size && !err;)
	{
		const uint size = rd_le32 (raw + o), stored = rd_le32 (raw + o + 4);
		if ((rd_le32 (raw + o + 12) & 0xffff) != 6 || !size || size > (1u << 28) || !stored
			|| stored > raw_size - o - 16)
		{
			o += 4;
			continue;
		}
		u8 *dec = MALLOC (size);
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
			zs.next_in = raw + o + 16;
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
			o += 4;
			continue;
		}
		if (!testmode)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%03u_%06zx.bin", dest, basedir ? basedir : "", n, o);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (dec, 1, size, F.f) != size)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
			ResetFile (&F, opt_preserve);
		}
		n++;
		FREE (dec);
		o += 16 + stored;
	}
	FREE (raw);
	if (!err && !n)
		return ERR_NOTHING_TO_DO;
	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

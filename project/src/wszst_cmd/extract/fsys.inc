// Extract a Genius Sonority FSYS archive (Pokémon Colosseum/XD/Battle
// Revolution).  Battle Revolution v2 archives commonly give every member the
// literal name "(null)", so stable ordinal filenames are intentional; the
// recursive dispatcher identifies children by their actual magic.
static uint fsys_be32 (const u8 *p)
{
	return (uint)p[0] << 24 | (uint)p[1] << 16 | (uint)p[2] << 8 | p[3];
}

static bool decode_fsys_lzss (u8 *out, uint out_size, const u8 *in, uint in_size)
{
	if (in_size < 16 || memcmp (in, "LZSS", 4) || fsys_be32 (in + 4) != out_size
		|| fsys_be32 (in + 8) < 16 || fsys_be32 (in + 8) > in_size)
		return false;
	const u8 *ip = in + 16, *end = in + fsys_be32 (in + 8);
	u8 ring[4096];
	memset (ring, 0, sizeof (ring));
	uint op = 0, rp = 4096 - 18, flags = 0;
	while (op < out_size)
	{
		if (!(flags & 0x100))
		{
			if (ip >= end)
				return false;
			flags = 0xff00 | *ip++;
		}
		if (flags & 1)
		{
			if (ip >= end)
				return false;
			ring[rp] = out[op++] = *ip++;
			rp = (rp + 1) & 4095;
		}
		else
		{
			if (end - ip < 2)
				return false;
			uint pos = *ip++;
			const uint b = *ip++;
			pos |= (b & 0xf0) << 4;
			uint count = (b & 15) + 3;
			while (count-- && op < out_size)
			{
				ring[rp] = out[op++] = ring[pos];
				pos = (pos + 1) & 4095;
				rp = (rp + 1) & 4095;
			}
		}
		flags >>= 1;
	}
	return true;
}

static enumError extract_fsys_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 0x40 || memcmp (raw, "FSYS", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint n_files = fsys_be32 (raw + 12), table = fsys_be32 (raw + 24);
	if (n_files > 65536 || table > raw_size || raw_size - table < 12)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint file_list = fsys_be32 (raw + table);
	if (file_list > raw_size || n_files > (raw_size - file_list) / 4)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT FSYS:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_files, dest);
	for (uint i = 0; !err && i < n_files; i++)
	{
		const uint ent = fsys_be32 (raw + file_list + i * 4);
		if (ent > raw_size || raw_size - ent < 40)
		{
			err = ERR_INVALID_DATA;
			break;
		}
		const uint offset = fsys_be32 (raw + ent + 4), size = fsys_be32 (raw + ent + 8);
		const uint flags = fsys_be32 (raw + ent + 12), csize = fsys_be32 (raw + ent + 20);
		const uint stored = flags & 0x80000000 ? csize : size;
		if (offset > raw_size || stored > raw_size - offset || size > 0x40000000)
		{
			err = ERR_INVALID_DATA;
			break;
		}
		if (testmode)
			continue;
		u8 *data = raw + offset;
		u8 *decoded = 0;
		if (flags & 0x80000000)
		{
			decoded = MALLOC (size);
			if (!decoded || !decode_fsys_lzss (decoded, size, data, stored))
			{
				FREE (decoded);
				err = ERR_INVALID_DATA;
				break;
			}
			data = decoded;
		}
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%sfile_%04u.bin", dest, basedir ? basedir : "", i);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && size && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
		ResetFile (&F, opt_preserve);
		FREE (decoded);
	}
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

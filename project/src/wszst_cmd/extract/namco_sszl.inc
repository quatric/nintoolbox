// Namco Museum Remix / Megamix (Wii) resources are commonly wrapped twice:
// an SSZL LZSS0 stream contains a VCRA archive.  The format is documented by
// aluigi's namco_museum.bms and, unlike the unrelated ARCV format above,
// VCRA retains a 56-byte name for each member.  Detect by magic rather than
// extension: the disc uses both .lzs files and extensionless resources.
static bool decode_namco_lzss0 (u8 *out, uint out_size, const u8 *in, uint in_size)
{
	u8 ring[4096];
	memset (ring, 0, sizeof (ring)); // LZSS0's init_chr is NUL, not space.
	uint r = 4096 - 18, ip = 0, op = 0;
	uint flags = 0;
	while (op < out_size)
	{
		if (!(flags & 0x100))
		{
			if (ip >= in_size)
				return false;
			flags = in[ip++] | 0xff00; // flags are consumed least-significant bit first.
		}
		if (flags & 1)
		{
			if (ip >= in_size)
				return false;
			out[op++] = ring[r] = in[ip++];
			r = (r + 1) & 0xfff;
		}
		else
		{
			if (ip + 1 >= in_size)
				return false;
			uint p = in[ip++];
			const uint b = in[ip++];
			p |= (b & 0xf0) << 4;
			uint n = (b & 0x0f) + 3;
			if (n > out_size - op)
				return false;
			while (n--)
			{
				out[op++] = ring[r] = ring[p++ & 0xfff];
				r = (r + 1) & 0xfff;
			}
		}
		flags >>= 1;
	}
	return true;
}

static enumError extract_namco_sszl_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (raw_size < 16 || raw_size > UINT_MAX || memcmp (raw, "SSZL", 4) || le32 (raw + 4)
		|| le32 (raw + 8) > raw_size - 16 || !le32 (raw + 12))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint zsize = le32 (raw + 8), usize = le32 (raw + 12);
	u8 *decoded = MALLOC (usize);
	if (!decoded || !decode_namco_lzss0 (decoded, usize, raw + 16, zsize))
	{
		FREE (decoded);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SSZL:%s (%u -> %u bytes) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, zsize, usize, dest);
	enumError err = ERR_OK;
	if (!testmode)
	{
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", "payload.vcra");
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (decoded, 1, usize, F.f) != usize)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", usize, path);
		ResetFile (&F, opt_preserve);
	}
	FREE (decoded);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

// Winning Post World (Wii) "*.datl": {be32 0x1000} then a table of {be32 compressed size, be32 decompressed size, be32 file offset} (ends at the first zero offset); each member is a headerless Okumura LZSS stream (LSB-first flags, zero-filled 4096-byte ring, start 0xfee) that inflates to a BRRES.
// Members are written to "<file>.d/NNNN.<ext>" (".brres" for "bres" data) and recursed.
static enumError extract_datl_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".datl") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 || rd_be32 (raw) != 0x1000)
		goto done;
	{
		uint n = 0;
		size_t p = 4;
		while (p + 12 <= sz)
		{
			const u32 cs = rd_be32 (raw + p), us = rd_be32 (raw + p + 4), off = rd_be32 (raw + p + 8);
			if (!off || !us || !cs || us > 0x4000000 || (u64)off + cs > sz)
				break;
			p += 12;
			n++;
		}
		if (n < 2)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT DATL:%s (%u members) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		static u8 ring[4096];
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = raw + 4 + (size_t)i * 12;
			const u32 cs = rd_be32 (e), us = rd_be32 (e + 4), off = rd_be32 (e + 8);
			u8 *out = MALLOC (us);
			memset (ring, 0, sizeof (ring));
			uint r = 0xfee;
			size_t ip = off, o = 0;
			const size_t ie = (size_t)off + cs;
			while (ip < ie && o < us)
			{
				const uint f = raw[ip++];
				for (uint b = 0; b < 8 && o < us; b++)
				{
					if (f >> b & 1)
					{
						if (ip >= ie)
							break;
						const u8 c = raw[ip++];
						out[o++] = c;
						ring[r] = c;
						r = (r + 1) & 0xfff;
					}
					else
					{
						if (ip + 1 >= ie)
							break;
						const uint pos = raw[ip] | (raw[ip + 1] & 0xf0) << 4, ln = (raw[ip + 1] & 15) + 3;
						ip += 2;
						for (uint k = 0; k < ln && o < us; k++)
						{
							const u8 c = ring[(pos + k) & 0xfff];
							out[o++] = c;
							ring[r] = c;
							r = (r + 1) & 0xfff;
						}
					}
				}
			}
			if (o == us)
			{
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%04u.%s", dest, i, us > 4 && !memcmp (out, "bres", 4) ? "brres" : "bin");
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (F.f && fwrite (out, 1, us, F.f) != us)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", us, path);
				ResetFile (&F, opt_preserve);
			}
			FREE (out);
		}
		if (!err && !testmode)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (raw);
	return err;
}

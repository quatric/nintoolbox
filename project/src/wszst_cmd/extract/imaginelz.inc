// Ubisoft "Imagine" engine compressed asset (Babysitting Party, Babyz, Beastly, ...; any extension: .wiz, .wsp, .wsn,
// .tan, .wan, .msk, .cam, .wik, .spt, .h, ...): bytes 21 43 65 87 (u32 LE 0x87654321), u32 LE compressed size (file
// size - 12), u32 LE decompressed size, then Okumura LZSS: 4096-byte ring filled with 0x20 and written from
// position 4078, groups of 8 tokens under a control byte (LSB first; 1 = literal byte, 0 = 2-byte match
// {b0, b1}: ring position b0 | (b1 & 0xf0) << 4, length (b1 & 15) + 3). The decompressed file is written to
// "<file>.d/<name>" and unpacked further when it is a known format.
static enumError extract_imaginelz_file (ccp arg, ccp basedir, uint depth)
{
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 hd[12];
	const size_t got = fread (hd, 1, 12, f);
	if (got != 12 || memcmp (hd, "\x21\x43\x65\x87", 4))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	const u32 cs = rd_le32 (hd + 4), ds = rd_le32 (hd + 8);
	if (fseeko (f, 0, SEEK_END))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	const u64 fsize = (u64)ftello (f);
	if (cs > fsize - 12 || !ds || ds > 0x20000000 || cs < 2 || fseeko (f, 12, SEEK_SET))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	u8 *src = MALLOC (cs), *out = MALLOC (ds);
	if (!src || !out || fread (src, 1, cs, f) != cs)
	{
		FREE (src);
		FREE (out);
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	fclose (f);
	u8 ring[4096];
	memset (ring, 0x20, sizeof (ring));
	uint r = 4078;
	size_t i = 0, o = 0;
	bool ok = true;
	while (o < ds && ok)
	{
		if (i >= cs)
		{
			ok = false;
			break;
		}
		const uint ctl = src[i++];
		for (uint k = 0; k < 8 && o < ds; k++)
		{
			if ((ctl >> k) & 1)
			{
				if (i >= cs)
				{
					ok = false;
					break;
				}
				const u8 b = src[i++];
				out[o++] = b;
				ring[r] = b;
				r = (r + 1) & 4095;
			}
			else
			{
				if (i + 1 >= cs)
				{
					ok = false;
					break;
				}
				const uint b0 = src[i], b1 = src[i + 1];
				i += 2;
				uint pos = b0 | ((b1 & 0xf0) << 4);
				for (uint n = (b1 & 15) + 3; n && o < ds; n--)
				{
					const u8 b = ring[pos];
					pos = (pos + 1) & 4095;
					out[o++] = b;
					ring[r] = b;
					r = (r + 1) & 4095;
				}
			}
		}
	}
	FREE (src);
	if (!ok)
	{
		FREE (out);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	ccp slash = strrchr (arg, '/');
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", slash ? slash + 1 : arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT IMAGINE-LZ:%s -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, path);
	enumError err = ERR_OK;
	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (out, 1, ds, F.f) != ds)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", ds, path);
		ResetFile (&F, opt_preserve);
	}
	FREE (out);
	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

// "*.vtb" vertex / geometry buffers (Batman: The Brave and the Bold, Wii), big-endian:
//   u32 header size, u32 A, u32 B, then A*B u32 offsets of 0x88-byte block descriptors (0 = unused; the table ends at 0xc + 4*A*B
//   for the small files, the descriptors follow it), each descriptor {u32 data offset, u32 data size, 16 slots of {u32 start, u32 size}}
//   (start is relative to the block's data offset, size 0 = unused slot). Blocks are back to back from the header size to the end of
//   the file, the slot sizes add up to the block size, and every slot is one Nintendo LZ11 stream (decoded size up to 64 KiB).
// Each slot is written decoded as "<block>_s<slot>.bin"; the contents are raw vertex / index data whose layout is not decoded.
static enumError extract_vtb_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".vtb"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *tab = 0;
	u8 hd[12];
	if (fread (hd, 1, 12, f) != 12 || fseeko (f, 0, SEEK_END))
		goto done;
	{
		const u64 size = (u64)ftello (f);
		const u32 hs = rd_be32 (hd), a = rd_be32 (hd + 4), b = rd_be32 (hd + 8);
		if (!a || !b || a > 256 || b > 256 || a * b > 8192 || hs < 12 + 4 * (u64)a * b || hs > size)
			goto done;
		const u32 n = a * b;
		tab = MALLOC ((size_t)hs);
		if (!tab || fseeko (f, 0, SEEK_SET) || fread (tab, 1, hs, f) != hs)
			goto done;
		u64 pos = hs;
		uint blocks = 0;
		for (u32 i = 0; i < n; i++)
		{
			const u32 t = rd_be32 (tab + 12 + 4 * (size_t)i);
			if (!t)
				continue;
			if (t < 12 + 4 * n || t + 0x88 > hs)
				goto done;
			const u8 *d = tab + t;
			const u64 off = rd_be32 (d), sz = rd_be32 (d + 4);
			u64 sum = 0;
			for (uint k = 0; k < 16; k++)
			{
				const u64 st = rd_be32 (d + 8 + 8 * k), ss = rd_be32 (d + 12 + 8 * k);
				if (ss && (st + ss > sz))
					goto done;
				sum += ss;
			}
			if (off != pos || sum != sz)
				goto done;
			pos += sz;
			blocks++;
		}
		if (!blocks || pos != size)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT VTB:%s (%u blocks) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, blocks, dest);
		err = ERR_OK;
		uint bi = 0;
		for (u32 i = 0; i < n && !err && !testmode; i++)
		{
			const u32 t = rd_be32 (tab + 12 + 4 * (size_t)i);
			if (!t)
				continue;
			const u8 *d = tab + t;
			const u64 off = rd_be32 (d);
			for (uint k = 0; k < 16 && !err; k++)
			{
				const u64 st = rd_be32 (d + 8 + 8 * k);
				const u32 ss = rd_be32 (d + 12 + 8 * k);
				if (!ss)
					continue;
				u8 *raw = MALLOC (ss);
				if (!raw || fseeko (f, (off_t)(off + st), SEEK_SET) || fread (raw, 1, ss, f) != ss)
				{
					FREE (raw);
					err = ERR_READ_FAILED;
					break;
				}
				u8 *dec = 0;
				uint dsz = 0;
				if (raw[0] != 0x11 || DecodeLZ10LZ11 (&dec, &dsz, raw, ss) || !dec)
				{
					// not a valid stream: keep the bytes as they are
					dec = raw;
					dsz = ss;
					raw = 0;
				}
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%s%02u_s%02u.bin", dest, basedir ? basedir : "", bi, k);
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (F.f && dsz && fwrite (dec, 1, dsz, F.f) != dsz)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", dsz, path);
				ResetFile (&F, opt_preserve);
				FREE (dec);
				FREE (raw);
			}
			bi++;
		}
	}
done:
	FREE (tab);
	fclose (f);
	return err;
}

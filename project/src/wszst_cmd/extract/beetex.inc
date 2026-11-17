// Beenox/Activision (DreamWorks Bee Movie Game, Wii) "*.Wiitex" textures, big-endian: {u32 version 2, u32 file size - 0x10, u32 0x10, u32 8, ...}, source file name ("de_strapA.tga") at 0x30, GX CMPR mip chain
// from 0x50 (every level at least one 32-byte 8x8 tile). The dimensions are not stored in an identified field: the largest square (else rectangular) power-of-two size whose CMPR mip chain, counted from
// 0x50, ends within 8 bytes of the end of the file is used. Level 0 is written to "<file>.png".
static enumError extract_beetex_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".wiitex"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x100 || rd_be32 (raw) != 2 || rd_be32 (raw + 4) != sz - 0x10 || rd_be32 (raw + 8) != 0x10 || rd_be32 (raw + 12) != 8)
		goto done;
	for (uint pass = 0; pass < 2 && err == ERR_NOTHING_TO_DO; pass++)
		for (uint w = 2048; w >= 8 && err == ERR_NOTHING_TO_DO; w >>= 1)
			for (uint h = 2048; h >= 8 && err == ERR_NOTHING_TO_DO; h >>= 1)
			{
				if (pass == 0 && w != h)
					continue;
				u64 sum = 0;
				uint lw = w, lh = h;
				for (uint levels = 1; levels <= 12 && err == ERR_NOTHING_TO_DO; levels++)
				{
					sum += (u64)((lw + 7) / 8) * ((lh + 7) / 8) * 32;
					lw = lw > 1 ? lw >> 1 : 1;
					lh = lh > 1 ? lh >> 1 : 1;
					if (0x50 + sum <= sz && sz - 0x50 - sum <= 8)
					{
						const uint l0 = (uint)((u64)((w + 7) / 8) * ((h + 7) / 8) * 32);
						char png[PATH_MAX];
						snprintf (png, sizeof (png), "%s.png", arg);
						if (verbose >= 0 || testmode)
							fprintf (stdlog, "%s%sDECODE WIITEX:%s (%ux%u CMPR, %u levels) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h, levels, png);
						err = testmode ? ERR_OK : wiitex_write_raw (png, w, h, 14, raw + 0x50, l0);
					}
				}
			}
done:
	FREE (raw);
	return err;
}

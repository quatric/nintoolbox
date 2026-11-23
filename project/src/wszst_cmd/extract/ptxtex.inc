// "PATX WII" textures (Around the World, WiiWare): "<name>.PTX" is raw GX texture data and "<name>.PTXH" is its 80-byte big-endian header: {char date[8], "PATX WII",
// u32 version, u32 data size, u16 width, u16 height, char format[4] ("1TXD" = CMPR, " 3A5" = RGB5A3, " 565" = RGB565, "ABGR" = RGBA32), u32 bits per pixel, u32 mip count, ...}. A PTX with a mip chain is larger than its base level; only the
// base level is used. The PTX is wrapped in a one-image TPL and written as "<file>.d/<file>.tpl" for the TPL decoder (PNG). A PTX without a usable PTXH is left alone.
static enumError extract_ptxtex_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".ptx"))
		return ERR_NOTHING_TO_DO;
	char hdr[PATH_MAX];
	snprintf (hdr, sizeof (hdr), "%sH", arg);
	if (access (hdr, R_OK))
	{
		snprintf (hdr, sizeof (hdr), "%sh", arg);
		if (access (hdr, R_OK))
			return ERR_NOTHING_TO_DO;
	}
	u8 *h = 0, *raw = 0;
	size_t hs = 0, sz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (hdr, 0, 0, &h, &hs, 0, 0, 0, false) || !h || hs < 0x28 || memcmp (h + 8, "PATX WII", 8))
		goto done;
	const uint w = (h[0x18] << 8) | h[0x19], ht = (h[0x1a] << 8) | h[0x1b];
	const u32 bpp = rd_be32 (h + 0x20);
	u32 fmt = 0;
	if (!memcmp (h + 0x1c, "1TXD", 4) && bpp == 4)
		fmt = 14;
	else if (!memcmp (h + 0x1c, " 3A5", 4) && bpp == 16)
		fmt = 5;
	else if (!memcmp (h + 0x1c, " 565", 4) && bpp == 16)
		fmt = 4;
	else if (!memcmp (h + 0x1c, "ABGR", 4) && bpp == 32)
		fmt = 6;
	if (!fmt || !w || !ht || w > 4096 || ht > 4096 || (w | ht) & 3)
		goto done;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < (size_t)w * ht * bpp / 8)
		goto done;
	{
		const size_t base = (size_t)w * ht * bpp / 8;
		u8 *tpl = CALLOC (1, 0x40 + base);
		nintendo_sarc_entry_t *entries = tpl ? CALLOC (1, sizeof (*entries)) : 0;
		if (!entries)
		{
			FREE (tpl);
			err = ERR_OUT_OF_MEMORY;
			goto done;
		}
		wr_be32 (tpl, 0x0020af30);
		wr_be32 (tpl + 4, 1);
		wr_be32 (tpl + 8, 0x0c);
		wr_be32 (tpl + 0x0c, 0x14);
		tpl[0x14] = ht >> 8;
		tpl[0x15] = ht;
		tpl[0x16] = w >> 8;
		tpl[0x17] = w;
		wr_be32 (tpl + 0x18, fmt);
		wr_be32 (tpl + 0x1c, 0x40);
		wr_be32 (tpl + 0x24, 0);
		wr_be32 (tpl + 0x28, 1);
		wr_be32 (tpl + 0x2c, 1);
		memcpy (tpl + 0x40, raw, base);
		const char *b = strrchr (arg, '/');
		char name[PATH_MAX];
		snprintf (name, sizeof (name), "%s.tpl", b ? b + 1 : arg);
		if (OwnedEntryAdd (entries, 0, name, tpl, (uint)(0x40 + base)))
		{
			err = write_owned_entries (arg, basedir, depth, "PTX", entries, 1);
			entries = 0;
		}
		if (entries)
			ResetOwnedEntries (entries, 1);
		FREE (tpl);
	}
done:
	FREE (h);
	FREE (raw);
	return err;
}

// Cars: Race-O-Rama (THQ / Incinerator, Wii) "*.gcn.res" resources, little-endian head: u32 3, u32 0x1c, u32 head size, ..., a 0x30 byte entry block with the
// original path ("e\ui\tex\Load_Screen_1.gct"), zero padding up to 0x800, then the payload as zlib streams (separated by 0xff pad bytes). The inflated payload of a texture starts with a
// big-endian head {u32 2, u32 flags, u32 0, u32 1, u32 w, u32 h, u32 w, u32 h, u32 data size} (0x24 bytes) followed by the GX data (CMPR; other sizes are kept raw). Textures are
// written as PNG, every other resource as "<name>.bin" (the inflated payload).
static enumError extract_racres_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".res"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *pl = 0, *rgba = 0;
	size_t sz = 0, plen = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x810 || rd_le32 (raw) != 3 || rd_le32 (raw + 4) != 0x1c || raw[0x800] != 0x78 || (raw[0x801] != 0x9c && raw[0x801] != 0xda && raw[0x801] != 0x01))
		goto done;
	{
		size_t pos = 0x800, cap = 0;
		for (;;)
		{
			while (pos < sz && raw[pos] == 0xff)	// single pad bytes between the streams
				pos++;
			if (pos + 2 > sz || raw[pos] != 0x78)
				break;
			uint osz = 0;
			size_t used = 0;
			u8 *o = fst_inflate (raw + pos, sz - pos, &osz, &used);
			if (!o || !used)
			{
				FREE (o);
				break;
			}
			if (plen + osz > cap)
			{
				cap = (plen + osz) * 2;
				u8 *nb = REALLOC (pl, cap);
				if (!nb)
				{
					FREE (o);
					FREE (pl);
					pl = 0;
					break;
				}
				pl = nb;
			}
			memcpy (pl + plen, o, osz);
			plen += osz;
			FREE (o);
			pos += used;
		}
		if (!pl || !plen)
			goto done;
		char name[PATH_MAX];
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			*dot = 0;
		const uint w = plen >= 0x24 ? rd_be32 (pl + 16) : 0, h = plen >= 0x24 ? rd_be32 (pl + 20) : 0;
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT RACRES:%s (%u bytes)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, (uint)plen);
		err = ERR_OK;
		if (testmode)
			goto done;
		if (plen > 0x24 && rd_be32 (pl) == 2 && w && h && w <= 4096 && h <= 4096 && rd_be32 (pl + 0x20) == w * h / 2 && 0x24 + (u64)w * h / 2 <= plen
			&& !DecodeGXTexture_RGBA (&rgba, w, h, 14, pl + 0x24, w * h / 2, 0, 0, 0) && rgba)
			err = kyl_write_rgba_png (arg, basedir, name, (uint)-1, w, h, rgba);
		else
		{
			char dest[PATH_MAX], fn[PATH_MAX + 8];
			beside_source_dest (dest, sizeof (dest), arg);
			snprintf (fn, sizeof (fn), "%s.bin", name);
			err = shade_write (arg, dest, basedir, fn, pl, (uint)plen);
		}
	}
done:
	FREE (rgba);
	FREE (pl);
	FREE (raw);
	return err;
}

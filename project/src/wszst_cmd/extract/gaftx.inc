// Green Lantern: Rise of the Manhunters ".gaftx" textures (inside the .glob packs, big endian). Header: u16 width, u16 height (twice, as two words), u32 0x10001, u32 format (18 = GX CMPR, 8 = RGBA8,
// 9 / 11 = 16 bit GX formats), u32 base level size, 0, u32 header size, u32 0x10000, 0x38, u32 data offset (at 0x24), u32 total size twice, 0, 0, u32 mip count (at 0x38), then one {u16 width,
// u16 height, u32 end offset of the level in the data, 0} per mip level; the GX tiled levels follow back to back at the data offset. The base level is written as "<file>.png".
static enumError extract_gaftx_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	(void)basedir;
	if (!is_ext (arg, ".gaftx"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x44)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint w = rd_be16 (raw), h = rd_be16 (raw + 2), fmt = rd_be32 (raw + 0xc);
	const size_t doff = rd_be32 (raw + 0x24);
	const uint gx = fmt == 18 ? 14 : fmt == 8 ? 6 : fmt == 9 ? 3 : fmt == 11 ? 4 : 99;
	if (rd_be16 (raw + 4) != w || rd_be16 (raw + 6) != h || rd_be32 (raw + 8) != 0x10001 || gx == 99 || !w || !h || w > 4096 || h > 4096 || doff < 0x40 || doff >= sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	u8 *rgba = 0;
	enumError err = DecodeGXTexture_RGBA (&rgba, w, h, gx, raw + doff, (uint)(sz - doff), 0, 0, 0);
	if (err || !rgba)
	{
		FREE (raw);
		FREE (rgba);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".png");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE GAFTX:%s (%ux%u fmt %u) -> PNG:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h, fmt, dest);
	if (testmode)
	{
		FREE (rgba);
		err = ERR_OK;
	}
	else
		err = SaveDecodedRGBAToPNG (rgba, w, h, &le_func, dest, 0, opt_overwrite); // takes ownership of rgba
	FREE (raw);
	return err;
}

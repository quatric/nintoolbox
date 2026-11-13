// Criterion RenderWare texture dictionaries for GameCube/Wii (".txd": Rebel Raiders: Operation Nighthawk; chunks little endian, pixel data GX-native big endian): chunk 0x16
// {struct 0x01 {u16 count, u16 device}, count x native texture 0x15, extension 0x03}. A native texture's struct (0x01) holds, relative to its payload: platform u32 BE @0 (6 = GC),
// name[32] @0x18, mask name[32] @0x38, RW raster format u32 BE @0x58 (0x0200 = 565 palette, 0x2000 = 8-bit paletted, 0x4000 = 4-bit), width/height u16 BE @0x5c, depth u8 @0x60,
// level count u8, GX texture format u8 @0x62 (9 = C8, 8 = C4, 0xe = CMPR, 5 = RGB5A3, ...), pixels from @0x6c: the 16-bit palette first for paletted formats, then the
// base image. Only the base image is decoded, to "NNNN_<name>.png".
static enumError extract_rw_txd_gc_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".txd") || !file_has_magic (arg, "\x16\0\0\0", 4))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x30 || rd_le32 (raw + 0xc) != 1 || rd_le32 (raw + 0x10) != 4 || rd_le32 (raw + 0x1c) != 0x15)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	uint wrote = 0;
	const uint count = rd_le16 (raw + 0x18);
	size_t p = 0x1c;
	for (uint k = 0; k < count && p + 0x24 < sz; k++)
	{
		const size_t tsz = rd_le32 (raw + p + 4);
		if (rd_le32 (raw + p) != 0x15 || p + 12 + tsz > sz)
			break;
		const u8 *st = raw + p + 12;
		const size_t ssz = rd_le32 (st + 4);
		const u8 *b = st + 12;
		const size_t next = p + 12 + tsz;
		p = next;
		if (rd_le32 (st) != 1 || ssz < 0x6c || (size_t)(b - raw) + ssz > sz || rd_be32 (b) != 6)
			continue;
		const uint rf = rd_be32 (b + 0x58), w = rd_be16 (b + 0x5c), h = rd_be16 (b + 0x5e), depth_bits = b[0x60], gx = b[0x62];
		if (!w || !h || w > 4096 || h > 4096)
			continue;
		uint isz = 0;
		if (!CalcImageGeometry ((image_format_t)gx, w, h, 0, 0, 0, 0, &isz) || !isz)
			continue;
		const bool pal = gx == IMG_C4 || gx == IMG_C8;
		const uint npal = pal ? (depth_bits == 4 ? 16u : 256u) : 0;
		if (0x6c + npal * 2 + isz > ssz)
			continue;
		Image_t img;
		InitializeIMG (&img);
		img.data = CALLOC (1, isz + 64);
		memcpy (img.data, b + 0x6c + npal * 2, isz);
		img.data_alloced = true;
		img.data_size = isz;
		img.width = w;
		img.height = h;
		img.iform = img.info_iform = (image_format_t)gx;
		img.info_fform = FF_PNG;
		img.info_n_image = 1;
		img.endian = &be_func;
		CalcImageGeometry (img.iform, img.width, img.height, &img.xwidth, &img.xheight, 0, 0, 0);
		if (npal)
		{
			img.pal = CALLOC (1, 4 * npal + 64);
			memcpy (img.pal, b + 0x6c, 2 * npal);
			img.pal_alloced = true;
			img.pal_size = 2 * npal;
			img.n_pal = npal;
			img.pform = img.info_pform = (rf & 0xf00) == 0x200 ? PAL_RGB565 : PAL_RGB5A3;
		}
		char nm[40], path[PATH_MAX];
		snprintf (nm, sizeof (nm), "%.32s", (ccp)b + 0x18);
		for (char *c = nm; *c; c++)
			if (!isalnum ((u8)*c) && *c != '_' && *c != '-' && *c != '.')
				*c = '_';
		snprintf (path, sizeof (path), "%s/%s%04u_%s.png", dest, basedir ? basedir : "", k, nm);
		if (!wrote)
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE RW-TXD:%s (%u textures) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, count, dest);
			if (!testmode)
				CreatePath (dest, true);
		}
		if (testmode || (!ConvertIMG (&img, false, 0, IMG_X_RGB, PAL_INVALID) && !SavePNG (&img, false, 0, path, 0, 0, true, 0)))
			wrote++;
		ResetIMG (&img);
	}
	if (wrote)
		err = ERR_OK;
	(void)depth;
	FREE (raw);
	return err;
}

// Midway "TIM" textures (".tim", members of TNA Impact! ".WAD" packs; big endian): 0x1c header size?, @0x20 width, height, bits per pixel (8 or 32), pixel bytes, @0x30 pixel data
// offset (0xc0), colour count (256 for 8 bpp), palette offset. 8 bpp is GX-tiled C8 (8x4 tiles) with a 256-entry palette stored as two 512-byte planes ({G,R} pairs then {A,B} pairs); 32 bpp is GX RGBA32. Written as "<name>.png".
static enumError extract_midway_tim_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".tim"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const uint w = sz > 0x40 ? rd_be32 (raw + 0x20) : 0, h = sz > 0x40 ? rd_be32 (raw + 0x24) : 0, bpp = sz > 0x40 ? rd_be32 (raw + 0x28) : 0;
	const size_t dsz = sz > 0x40 ? rd_be32 (raw + 0x2c) : 0, off = sz > 0x40 ? rd_be32 (raw + 0x30) : 0, npal = sz > 0x40 ? rd_be32 (raw + 0x34) : 0, poff = sz > 0x40 ? rd_be32 (raw + 0x38) : 0;
	if (w && h && w <= 4096 && h <= 4096 && (bpp == 8 || bpp == 32) && dsz == (size_t)w * h * bpp / 8 && off >= 0x40 && off + dsz <= sz
		&& (bpp == 32 || (npal == 256 && poff + 1024 <= sz)) && !(w & 7) && !(h & 3))
	{
		Image_t img;
		InitializeIMG (&img);
		bool ok = true;
		if (bpp == 32)
		{
			img.data = CALLOC (1, dsz + 64);
			memcpy (img.data, raw + off, dsz);
			img.data_alloced = true;
			img.data_size = (uint)dsz;
			img.iform = img.info_iform = IMG_RGBA32;
			img.endian = &be_func;
			CalcImageGeometry (img.iform, w, h, &img.xwidth, &img.xheight, 0, 0, 0);
		}
		else
		{
			u8 *rgba = CALLOC ((size_t)w * h, 4);
			ok = rgba != 0;
			const u8 *px = raw + off;
			// the palette is two 512-byte planes: {G, R} pairs, then {A, B} pairs
			u8 pal[1024];
			for (uint i = 0; i < 256; i++)
			{
				pal[4 * i] = raw[poff + 2 * i + 1];
				pal[4 * i + 1] = raw[poff + 2 * i];
				pal[4 * i + 2] = raw[poff + 512 + 2 * i + 1];
				pal[4 * i + 3] = raw[poff + 512 + 2 * i];
			}
			for (uint ty = 0; ok && ty < h; ty += 4)
				for (uint tx = 0; tx < w; tx += 8)
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 8; x++)
							memcpy (rgba + (((size_t)(ty + y) * w) + tx + x) * 4, pal + 4 * (size_t)*px++, 4);
			img.data = rgba;
			img.data_alloced = true;
			img.data_size = w * h * 4;
			img.iform = img.info_iform = IMG_X_RGB;
			img.endian = &le_func;
			img.xwidth = w;
			img.xheight = h;
		}
		if (ok)
		{
			img.width = w;
			img.height = h;
			img.info_fform = FF_PNG;
			img.info_n_image = 1;
			char dest[PATH_MAX];
			beside_source_image_dest (dest, sizeof (dest), arg);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE MIDWAY-TIM:%s (%ux%u, %u bpp) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h, bpp, dest);
			if (testmode)
				err = ERR_OK;
			else if (bpp == 32 ? (!ConvertIMG (&img, false, 0, IMG_X_RGB, PAL_INVALID) && !SavePNG (&img, false, 0, dest, 0, 0, true, 0)) : !SavePNG (&img, false, 0, dest, 0, 0, opt_overwrite > 0, 0))
				err = ERR_OK;
		}
		ResetIMG (&img);
	}
	FREE (raw);
	return err;
}

// EA "FntG" bitmap fonts (".gfn": Madden NFL 10 and other EA Wii titles; big endian except the LE file size @4): @0x0a u16 glyph count, @0x14 glyph table offset
// (0x80), @0x30 image header offset. Glyph records are 16 bytes {u16 char, u8 w, u8 h, u16 x, u16 y, u16 ?, s8 xoff, s8 yoff, u16 ?, u16 advance} in the atlas.
// The image header carries {.., data size @+0x0c, width @+0x18, height @+0x1c} with the GX I4 (8x8 tiles) atlas at +0x30. Written as "<name>.png" (grey
// intensity) next to a "<name>.txt" glyph list (char, box, offset and advance).
static enumError extract_ea_gfn_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".gfn") || !file_has_magic (arg, "FntG", 4))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const size_t n = sz > 0x80 ? rd_be16 (raw + 10) : 0, goff = sz > 0x80 ? rd_be32 (raw + 0x14) : 0, ioff = sz > 0x80 ? rd_be32 (raw + 0x30) : 0;
	const size_t dsz = ioff + 0x40 < sz ? rd_be32 (raw + ioff + 0x0c) : 0;
	const uint w = ioff + 0x40 < sz ? rd_be32 (raw + ioff + 0x18) : 0, h = ioff + 0x40 < sz ? rd_be32 (raw + ioff + 0x1c) : 0;
	if (n && goff + n * 16 <= sz && w && h && w <= 2048 && h <= 2048 && !(w & 7) && !(h & 7) && dsz == (size_t)w * h / 2 && ioff + 0x30 + dsz <= sz)
	{
		u8 *rgba = CALLOC ((size_t)w * h, 4);
		char *txt = CALLOC (n * 64 + 64, 1);
		if (rgba && txt)
		{
			const u8 *px = raw + ioff + 0x30;
			for (uint ty = 0; ty < h; ty += 8)
				for (uint tx = 0; tx < w; tx += 8)
					for (uint y = 0; y < 8; y++)
						for (uint x = 0; x < 8; x += 2)
						{
							const u8 b = *px++;
							for (uint k = 0; k < 2; k++)
							{
								u8 *o = rgba + (((size_t)(ty + y) * w) + tx + x + k) * 4;
								o[0] = o[1] = o[2] = (u8)((k ? b & 15 : b >> 4) * 17);
								o[3] = 255;
							}
						}
			size_t o = (size_t)sprintf (txt, "# char w h x y xoff yoff advance (atlas %ux%u)\n", w, h);
			for (size_t i = 0; i < n; i++)
			{
				const u8 *g = raw + goff + i * 16;
				o += (size_t)sprintf (txt + o, "%u %u %u %u %u %d %d %u\n", rd_be16 (g), g[2], g[3], rd_be16 (g + 4), rd_be16 (g + 6), (int)(signed char)g[10], (int)(signed char)g[11], rd_be16 (g + 14));
			}
			Image_t img;
			InitializeIMG (&img);
			img.data = rgba;
			img.data_alloced = true;
			img.data_size = w * h * 4;
			img.iform = img.info_iform = IMG_X_RGB;
			img.endian = &le_func;
			img.xwidth = w;
			img.xheight = h;
			img.width = w;
			img.height = h;
			img.info_fform = FF_PNG;
			img.info_n_image = 1;
			char dest[PATH_MAX], name[PATH_MAX];
			beside_source_image_dest (dest, sizeof (dest), arg);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE EA-GFN:%s (%zu glyphs, %ux%u) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, w, h, dest);
			if (testmode)
				err = ERR_OK;
			else if (!SavePNG (&img, false, 0, dest, 0, 0, opt_overwrite > 0, 0))
			{
				char dd[PATH_MAX];
				beside_source_dest (dd, sizeof (dd), arg);
				const char *sl = strrchr (arg, '/');
				snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
				char *dot = strrchr (name, '.');
				if (dot) strcpy (dot, ".txt");
				err = shade_write (arg, dd, basedir, name, (u8 *)txt, (uint)o);
			}
			ResetIMG (&img);
		}
		else
			FREE (rgba);
		FREE (txt);
	}
	FREE (raw);
	return err;
}

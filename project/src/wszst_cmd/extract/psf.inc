// PC Screen Font ".psf" (PSF1 "36 04": 256 / 512 glyphs, 8 pixel wide, height = charsize; PSF2 "72 b5 4a 86": header with glyph count, bytes per glyph, height, width):
// all glyphs are drawn into "<name>.png" (16 glyphs per row, 1 pixel gap, white on black) and "info.txt" gives the glyph size and count.
static enumError extract_psf_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".psf") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	uint count = 0, gw = 0, gh = 0, bpg = 0;
	size_t off = 0;
	if (sz > 4 && raw[0] == 0x36 && raw[1] == 0x04)
	{
		count = raw[2] & 1 ? 512 : 256;
		gw = 8; gh = bpg = raw[3];
		off = 4;
	}
	else if (sz > 32 && raw[0] == 0x72 && raw[1] == 0xb5 && raw[2] == 0x4a && raw[3] == 0x86)
	{
		off = raw[8] | raw[9] << 8 | raw[10] << 16 | raw[11] << 24;
		count = raw[16] | raw[17] << 8 | raw[18] << 16 | raw[19] << 24;
		bpg = raw[20] | raw[21] << 8 | raw[22] << 16 | raw[23] << 24;
		gh = raw[24] | raw[25] << 8 | raw[26] << 16 | raw[27] << 24;
		gw = raw[28] | raw[29] << 8 | raw[30] << 16 | raw[31] << 24;
	}
	if (count && count <= 65536 && gw && gh && gw <= 128 && gh <= 128 && bpg >= ((gw + 7) / 8) * gh && off + (size_t)count * bpg <= sz)
	{
		const uint cols = 16, rows = (count + cols - 1) / cols, pw = cols * (gw + 1), ph = rows * (gh + 1);
		u8 *rgba = CALLOC (1, (size_t)pw * ph * 4);
		if (rgba)
		{
			for (size_t i = 0; i < (size_t)pw * ph; i++)
				rgba[i * 4 + 3] = 255;
			const uint rb = (gw + 7) / 8;
			for (uint g = 0; g < count; g++)
				for (uint y = 0; y < gh; y++)
					for (uint x = 0; x < gw; x++)
						if (raw[off + (size_t)g * bpg + y * rb + x / 8] >> (7 - x % 8) & 1)
						{
							u8 *o = rgba + (((size_t)(g / cols) * (gh + 1) + y) * pw + (g % cols) * (gw + 1) + x) * 4;
							o[0] = o[1] = o[2] = 255;
						}
			char stem[PATH_MAX], dest[PATH_MAX], text[128];
			const char *sl = strrchr (arg, '/');
			snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (stem, '.');
			if (dot)
				*dot = 0;
			beside_source_dest (dest, sizeof (dest), arg);
			err = testmode ? ERR_OK : kyl_write_rgba_png (arg, basedir, stem, (uint)-1, pw, ph, rgba);
			const int n = snprintf (text, sizeof (text), "glyphs: %u\nsize: %ux%u\nbytes per glyph: %u\nformat: PSF%u\n", count, gw, gh, bpg, raw[0] == 0x36 ? 1 : 2);
			if (!err)
				err = shade_write (arg, dest, basedir, "info.txt", (u8 *)text, (uint)n);
			FREE (rgba);
		}
	}
	FREE (raw);
	return err;
}

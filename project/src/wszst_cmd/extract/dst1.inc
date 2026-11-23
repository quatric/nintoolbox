// "DST1" (".dst", 2D sprite sheets of about 70 DS games, e.g. FIFA 08 player numbers): "DST1", 4 bytes of flags (02 01 01 01), u16 width at 0x0c, u16 height at 0x0e, u16 palette size at 0x10, that many little endian RGB555 colors (bit 15 set, ignored; color 0 is the transparent key, magenta in the sample), then linear rows of width * bpp / 8 bytes. bpp is 2 up to 4 colors, 4 up to 16, else 8, low bits first in each byte; the file size must match exactly (checked on the one sample with the magic: a 16x256 sheet of digits, which render cleanly). Written as "<file>.d/<name>.png".
static enumError extract_dst1_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0, *rgba = 0;
	size_t sz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	(void)depth;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		goto done;
	if (sz < 0x14 || memcmp (raw, "DST1", 4))
		goto done;
	const uint w = rd_le16 (raw + 0xc), h = rd_le16 (raw + 0xe), nc = rd_le16 (raw + 0x10), bpp = nc <= 4 ? 2 : nc <= 16 ? 4 : 8;
	const size_t o = 0x12 + 2 * (size_t)nc;
	if (!w || !h || !nc || nc > 256 || w * bpp % 8 || o + (size_t)w * h * bpp / 8 != sz)
		goto done;
	rgba = CALLOC (4, (size_t)w * h);
	if (!rgba)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	for (uint y = 0; y < h; y++)
		for (uint x = 0; x < w; x++)
		{
			const size_t bit = ((size_t)y * w + x) * bpp;
			const uint i = raw[o + bit / 8] >> (bit & 7) & ((1u << bpp) - 1);
			if (!i || i >= nc)
				continue;
			const uint c = rd_le16 (raw + 0x12 + 2 * i);
			u8 *p = rgba + 4 * ((size_t)y * w + x);
			p[0] = (c & 31) * 255 / 31;
			p[1] = (c >> 5 & 31) * 255 / 31;
			p[2] = (c >> 10 & 31) * 255 / 31;
			p[3] = 255;
		}
	char name[PATH_MAX];
	const char *s = strrchr (arg, '/');
	snprintf (name, sizeof (name), "%s", s ? s + 1 : arg);
	char *e = strrchr (name, '.');
	if (e)
		*e = 0;
	err = kyl_write_rgba_png (arg, basedir, name, (uint)-1, w, h, rgba);
done:
	FREE (rgba);
	FREE (raw);
	return err;
}

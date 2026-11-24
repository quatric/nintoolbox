// "GRPH" (".dat" graphics of the Boogie engine, about 10,000 files): "GRPH", 0xfffe, u16 version 0x100, u32 file size, u16 header size 0x10, u16 block count, then blocks {tag, u32 size incl. header, ...}: "GPAL" {u16 colors at +8, 3 more u16, then 15 bit colors (bit 15 set, ignored; red in the high bits, i.e. the reverse of the usual DS order, chosen because the sample background then has a blue sky)} and "GIMG" {u16 width at +8, u16 height at +0xa, u8 flag at +0xc, 0x01 at +0xd, 0x02 at +0xe, u8 bits per pixel (4 or 8) at +0xf, pixels from +0x10, linear rows} (checked on the one sample that carries the magic: a 256x192 8 bit background, size matches exactly). Every GIMG uses the last GPAL before it; written as "<file>.d/<name>[_n].png", index 0 transparent.
// EA's later DS titles (Littlest Pet Shop, EA Playground, Football Academy) mostly carry sprites instead: a GIMG with 0x14 at +0xe is a frame {u32 offset of the pieces
// from +0x10, u32 width, u32 height, u32 piece count, then one {u16 OAM attr0, u16 OAM attr1, u16 tile, u16 ?} per piece} followed by one GIMG per piece (6 at +0xe: 8x8
// tiles, row stride in pixels at +0xc); a piece goes to x = attr1 & 0x1ff, y = attr0 & 0xff and the frame is clipped to width x height. GPAL then also gives the first
// palette index at +0xc (the colors fill indices base .. base + n - 1, n = (block size - 16) / 2).

// Palette index C of a GPAL at PAL (BASE, N colors) to RGBA at Q; index 0 and indices outside the palette stay transparent.
static void grph_color (const u8 *raw, size_t pal, uint base, uint n, uint c, u8 *q)
{
	if (!c || c < base || c - base >= n)
		return;
	const uint v = rd_le16 (raw + pal + 2 * (c - base));
	q[2] = (v & 31) * 255 / 31, q[1] = (v >> 5 & 31) * 255 / 31, q[0] = (v >> 10 & 31) * 255 / 31, q[3] = 255;
}

// Pixel I of a GIMG at G (linear rows, or 8x8 tiles when tiled).
static uint grph_pixel (const u8 *g, uint w, uint bpp, bool tiled, size_t x, size_t y)
{
	size_t i = y * w + x;
	if (tiled)
		i = ((y / 8) * (w / 8) + x / 8) * 64 + (y % 8) * 8 + x % 8;
	return bpp == 8 ? g[16 + i] : (g[16 + i / 2] >> (4 * (i & 1)) & 15);
}

// Is G (inside RAW of SZ) a plain GIMG image: returns its block size, or 0.
static size_t grph_image_ok (const u8 *raw, size_t sz, size_t g, uint *w, uint *h, uint *bpp, bool *tiled)
{
	if (g + 16 > sz || memcmp (raw + g, "GIMG", 4))
		return 0;
	const size_t bl = rd_le32 (raw + g + 4);
	*w = rd_le16 (raw + g + 8), *h = rd_le16 (raw + g + 10), *bpp = raw[g + 15], *tiled = raw[g + 14] == 6;
	if (bl < 16 || g + bl > sz || !*w || !*h || (*bpp != 4 && *bpp != 8) || (*tiled && (*w % 8 || *h % 8)) || (size_t)*w * *h * *bpp / 8 + 16 > bl)
		return 0;
	return bl;
}

static enumError extract_grph_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0, *rgba = 0;
	size_t sz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	(void)depth;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		goto done;
	if (sz < 0x30 || memcmp (raw, "GRPH", 4) || rd_le32 (raw + 8) != sz)
		goto done;
	size_t pal = 0, pn = 0, pbase = 0;
	uint idx = 0;
	for (size_t o = rd_le16 (raw + 0x0c) ? rd_le16 (raw + 0x0c) : 0x10; o + 16 <= sz;)
	{
		const size_t bl = rd_le32 (raw + o + 4);
		if (bl < 16 || o + bl > sz)
			break;
		if (!memcmp (raw + o, "GPAL", 4))
		{
			pn = (bl - 16) / 2;
			pbase = rd_le16 (raw + o + 12) < 256 ? rd_le16 (raw + o + 12) : 0;
			pal = o + 16;
		}
		else if (!memcmp (raw + o, "GIMG", 4) && pal)
		{
			uint w = 0, h = 0, bpp = 0;
			bool tiled = false;
			const bool sprite = raw[o + 14] == 0x14 && bl >= 32;
			if (sprite)
				w = rd_le32 (raw + o + 20), h = rd_le32 (raw + o + 24);
			if (sprite ? w && h && w <= 1024 && h <= 1024 : grph_image_ok (raw, sz, o, &w, &h, &bpp, &tiled) != 0)
			{
				rgba = CALLOC (4, (size_t)w * h);
				if (!rgba)
				{
					err = ERR_OUT_OF_MEMORY;
					goto done;
				}
				if (!sprite)
					for (size_t y = 0; y < h; y++)
						for (size_t x = 0; x < w; x++)
							grph_color (raw, pal, pbase, pn, grph_pixel (raw + o, w, bpp, tiled, x, y), rgba + 4 * (y * w + x));
				else
				{
					const uint np = rd_le32 (raw + o + 28);
					size_t g = o + 16 + rd_le32 (raw + o + 16);
					for (uint k = 0; k < np && o + 32 + 8 * k + 8 <= o + bl; k++)
					{
						uint pw, ph, pb;
						bool pt;
						const size_t gl = grph_image_ok (raw, sz, g, &pw, &ph, &pb, &pt);
						if (!gl || g + gl > o + bl)
							break;
						const uint px = rd_le16 (raw + o + 34 + 8 * k) & 0x1ff, py = rd_le16 (raw + o + 32 + 8 * k) & 0xff;
						for (size_t y = 0; y < ph && py + y < h; y++)
							for (size_t x = 0; x < pw && px + x < w; x++)
								grph_color (raw, pal, pbase, pn, grph_pixel (raw + g, pw, pb, pt, x, y), rgba + 4 * ((py + y) * w + px + x));
						g += gl;
					}
				}
				char name[PATH_MAX];
				const char *sl = strrchr (arg, '/');
				snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
				char *e = strrchr (name, '.');
				if (e)
					*e = 0;
				if (idx)
					snprintf (name + strlen (name), sizeof (name) - strlen (name), "_%u", idx);
				err = kyl_write_rgba_png (arg, basedir, name, (uint)-1, w, h, rgba);
				FREE (rgba);
				rgba = 0;
				idx++;
			}
		}
		o += bl;
	}
done:
	FREE (rgba);
	FREE (raw);
	return err;
}

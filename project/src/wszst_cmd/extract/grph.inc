// "GRPH" (".dat" graphics of the Boogie engine, about 10,000 files): "GRPH", 0xfffe, u16 version 0x100, u32 file size, u16 header size 0x10, u16 block count, then blocks {tag, u32 size incl. header, ...}: "GPAL" {u16 colors at +8, 3 more u16, then 15 bit colors (bit 15 set, ignored; red in the high bits, i.e. the reverse of the usual DS order, chosen because the sample background then has a blue sky)} and "GIMG" {u16 width at +8, u16 height at +0xa, u8 flag at +0xc, 0x01 at +0xd, 0x02 at +0xe, u8 bits per pixel (4 or 8) at +0xf, pixels from +0x10, linear rows} (checked on the one sample that carries the magic: a 256x192 8 bit background, size matches exactly). Every GIMG uses the last GPAL before it; written as "<file>.d/<name>[_n].png", index 0 transparent.
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
	size_t pal = 0, pn = 0;
	uint idx = 0;
	for (size_t o = rd_le16 (raw + 0x0c) ? rd_le16 (raw + 0x0c) : 0x10; o + 16 <= sz;)
	{
		const size_t bl = rd_le32 (raw + o + 4);
		if (bl < 16 || o + bl > sz)
			break;
		if (!memcmp (raw + o, "GPAL", 4))
		{
			pn = rd_le16 (raw + o + 8);
			pal = o + 16;
			if (pal + 2 * pn > o + bl)
				pal = pn = 0;
		}
		else if (!memcmp (raw + o, "GIMG", 4) && pal)
		{
			const uint w = rd_le16 (raw + o + 8), h = rd_le16 (raw + o + 10), bpp = raw[o + 15];
			if (w && h && (bpp == 4 || bpp == 8) && (size_t)w * h * bpp / 8 + 16 <= bl)
			{
				rgba = CALLOC (4, (size_t)w * h);
				if (!rgba)
				{
					err = ERR_OUT_OF_MEMORY;
					goto done;
				}
				for (size_t i = 0; i < (size_t)w * h; i++)
				{
					const uint c = bpp == 8 ? raw[o + 16 + i] : (raw[o + 16 + i / 2] >> (4 * (i & 1)) & 15);
					if (!c || c >= pn)
						continue;
					const uint v = rd_le16 (raw + pal + 2 * c);
					u8 *q = rgba + 4 * i;
					q[2] = (v & 31) * 255 / 31, q[1] = (v >> 5 & 31) * 255 / 31, q[0] = (v >> 10 & 31) * 255 / 31, q[3] = 255;
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

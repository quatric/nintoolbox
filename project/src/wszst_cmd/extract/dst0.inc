// "DST\0" version "1.7" / "1.00" image banks (".dat" of about 19 Japanese DS quiz/trivia games, e.g. Gakken M Bunko Presents: Monoshiri Edo Meijin, 3-Pun Tenka Touitsu; little endian):
// entries laid end to end, each a 0x40 byte header {char "DST\0", char "1.7\0" or "1.00", u32 entry size, u16 width, u16 height, u32 palette bytes, u32 0, u32 pixel bytes, u32 palette offset,
// u32 pixel offset, u32 pixel offset again, char name[24]} (offsets relative to the entry), RGB555 palette, linear pixels of pixel bytes * 8 / (width * height) bits (2, 4 or 8;
// low bits first). Colour 0 is transparent below 8 bits (fonts, sprites); 8 bit entries are full screen backgrounds. Written as "<file>.d/NNN_<name>.png".
static enumError extract_dst0_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!file_has_magic (arg, "DST\0001.", 6))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	size_t o = 0;
	uint n = 0;
	for (; o + 0x40 <= sz && !memcmp (raw + o, "DST\0001.", 6); n++)
	{
		const u8 *h = raw + o;
		const size_t es = rd_le32 (h + 8), w = rd_le16 (h + 12), hh = rd_le16 (h + 14), ps = rd_le32 (h + 16), xs = rd_le32 (h + 24), po = rd_le32 (h + 28),
			     xo = rd_le32 (h + 32);
		const size_t bpp = w && hh ? xs * 8 / (w * hh) : 0;
		if (es < 0x40 || es > sz - o || (bpp != 2 && bpp != 4 && bpp != 8) || xs * 8 != w * hh * bpp || ps > 512 || ps % 2 || po + ps > es || xo + xs > es)
			break;
		o += es;
	}
	if (o != sz || !n)
		goto done;
	o = 0;
	err = ERR_OK;
	for (uint i = 0; i < n && !err; i++)
	{
		const u8 *h = raw + o;
		const uint w = rd_le16 (h + 12), hh = rd_le16 (h + 14), nc = rd_le32 (h + 16) / 2;
		const size_t xs = rd_le32 (h + 24), bpp = xs * 8 / ((size_t)w * hh);
		const u8 *pal = h + rd_le32 (h + 28), *px = h + rd_le32 (h + 32);
		u8 *rgba = CALLOC (4, (size_t)w * hh);
		if (!rgba)
		{
			err = ERR_OUT_OF_MEMORY;
			break;
		}
		for (size_t p = 0; p < (size_t)w * hh; p++)
		{
			const size_t bit = p * bpp;
			const uint c = px[bit / 8] >> (bit % 8) & ((1u << bpp) - 1);
			if (c >= nc || (!c && bpp < 8))
				continue;
			const uint v = rd_le16 (pal + 2 * c);
			u8 *q = rgba + 4 * p;
			q[0] = (v & 31) * 255 / 31;
			q[1] = (v >> 5 & 31) * 255 / 31;
			q[2] = (v >> 10 & 31) * 255 / 31;
			q[3] = 255;
		}
		char name[64];
		snprintf (name, sizeof (name), "%03u_%.24s", i, (ccp)h + 0x28);
		err = kyl_write_rgba_png (arg, basedir, name, (uint)-1, w, hh, rgba);
		FREE (rgba);
		o += rd_le32 (h + 8);
	}
done:
	FREE (raw);
	return err;
}

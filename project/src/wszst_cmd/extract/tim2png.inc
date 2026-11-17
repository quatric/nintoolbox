// PS2 TIM2 textures (".tm2", e.g. GTI Club Supermini Festa "Sample/image/TM2"): "TIM2" header {u8 version, u8 alignment, u16 image count, ...}, then per image a 0x30-byte header {u32 total size, u32 CLUT size,
// u32 image size, u16 header size, u16 CLUT colours, u8 format, u8 mipmaps, u8 CLUT type, u8 image type (1 = 16 bit ABGR1555, 2 = 24 bit, 3 = 32 bit, 4 = 4 bit, 5 = 8 bit indexed), u16 width, u16 height, ...}.
// Linear pixel data is followed by the CLUT (16/24/32 bit colours; 8-bit palettes use the PS2 CSM1 block swizzle unless CLUT type bit 7 is set; PS2 alpha 0x80 = opaque). Only the first mip level of each
// image is decoded, to "<file>.png" ("<file>.NN.png" when there are several images). The result does not replace the manifest dump of the Zack & Wiki handler, which still runs afterwards.
static enumError extract_tim2png_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".tm2"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	const uint nimg = sz >= 0x40 && !memcmp (raw, "TIM2", 4) ? rd_le16 (raw + 6) : 0;
	size_t p = 0x10;
	for (uint im = 0; im < nimg && im < 64 && p + 0x30 <= sz; im++)
	{
		const u8 *h = raw + p;
		const u32 total = rd_le32 (h), clutsz = rd_le32 (h + 4), imgsz = rd_le32 (h + 8);
		const uint hs = rd_le16 (h + 12), ncol = rd_le16 (h + 14), ctype = h[18], itype = h[19], w = rd_le16 (h + 20), ht = rd_le16 (h + 22);
		if (total < hs || p + total > sz || hs < 0x30 || !w || !ht || w > 4096 || ht > 4096 || itype < 1 || itype > 5)
			break;
		const uint bpp = itype == 1 ? 16 : itype == 2 ? 24 : itype == 3 ? 32 : itype == 4 ? 4 : 8;
		if ((u64)w * ht * bpp / 8 > imgsz || hs + imgsz + clutsz > total)
			break;
		const u8 *pix = h + hs, *clut = pix + imgsz;
		const uint cbpp = (ctype & 3) == 1 ? 16 : (ctype & 3) == 2 ? 24 : (ctype & 3) == 3 ? 32 : 0;
		if (itype >= 4 && (!cbpp || (u64)ncol * cbpp / 8 > clutsz))
			break;
		u8 *rgba = MALLOC ((size_t)w * ht * 4);
		if (!rgba)
			break;
		for (uint i = 0; i < w * ht; i++)
		{
			const u8 *c = 0;
			uint cb = bpp;
			if (itype >= 4)
			{
				uint idx = itype == 4 ? (pix[i >> 1] >> ((i & 1) * 4)) & 15 : pix[i];
				if (itype == 5 && !(ctype & 0x80) && ncol >= 256)
					idx = (idx & 0x18) == 8 ? idx ^ 0x10 : (idx & 0x18) == 0x10 ? idx ^ 8 : idx;
				if (idx >= ncol)
					idx = 0;
				c = clut + (size_t)idx * cbpp / 8;
				cb = cbpp;
			}
			else
				c = pix + (size_t)i * bpp / 8;
			u8 *o = rgba + (size_t)i * 4;
			if (cb == 16)
			{
				const uint v = rd_le16 (c);
				o[0] = (v & 31) * 255 / 31, o[1] = (v >> 5 & 31) * 255 / 31, o[2] = (v >> 10 & 31) * 255 / 31, o[3] = v & 0x8000 ? 255 : 0;
			}
			else if (cb == 24)
				o[0] = c[0], o[1] = c[1], o[2] = c[2], o[3] = 255;
			else
			{
				o[0] = c[0], o[1] = c[1], o[2] = c[2];
				o[3] = c[3] >= 0x80 ? 255 : c[3] * 2;
			}
		}
		char png[PATH_MAX];
		if (nimg > 1)
			snprintf (png, sizeof (png), "%s.%02u.png", arg, im);
		else
			snprintf (png, sizeof (png), "%s.png", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE TIM2:%s (%ux%u, type %u) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, ht, itype, png);
		if (!testmode)
			SaveDecodedRGBAToPNG (rgba, w, ht, &be_func, png, 0, true);
		FREE (rgba);
		p += total;
	}
	FREE (raw);
	return ERR_NOTHING_TO_DO;
}

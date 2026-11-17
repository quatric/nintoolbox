// Truevision TGA images that carry another extension (Cocoto Kart Racer "*.bkg" 640x512 loading screens, ...). Recognised by content: an 18-byte header {id length, colour map type 0, image type 2/3/10/11,
// ..., u16le x/y origin, u16le width, height, bpp 8/24/32, descriptor} whose size matches exactly (raw types) or whose RLE stream decodes to width*height pixels. Decoded to "<file>.png".
static enumError extract_tgaimg_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (is_ext (arg, ".png") || is_ext (arg, ".tga") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *rgba = 0;
	if (sz < 0x20 || raw[1] || (raw[2] != 2 && raw[2] != 3 && raw[2] != 10 && raw[2] != 11) || raw[3] || raw[4] || raw[5] || raw[6] || raw[7])
		goto done;
	{
		const uint idl = raw[0], w = rd_le16 (raw + 12), h = rd_le16 (raw + 14), bpp = raw[16], bytes = bpp / 8, type = raw[2];
		if (!w || !h || w > 8192 || h > 8192 || (bpp != 8 && bpp != 24 && bpp != 32) || (type == 2 || type == 10 ? bpp == 8 : bpp != 8))
			goto done;
		const size_t npx = (size_t)w * h, off = 18 + idl;
		if (type < 9 && (off + npx * bytes > sz || sz - off - npx * bytes > 0x2000))
			goto done;
		rgba = MALLOC (npx * 4);
		u8 *px = MALLOC (npx * bytes);
		if (!rgba || !px)
		{
			FREE (px);
			goto done;
		}
		if (type < 9)
			memcpy (px, raw + off, npx * bytes);
		else
		{
			size_t p = off, n = 0;
			while (n < npx && p < sz)
			{
				const uint c = raw[p++], run = (c & 0x7f) + 1;
				if (n + run > npx)
					break;
				if (c & 0x80)
				{
					if (p + bytes > sz)
						break;
					for (uint i = 0; i < run; i++)
						memcpy (px + (n + i) * bytes, raw + p, bytes);
					p += bytes;
				}
				else
				{
					if (p + (size_t)run * bytes > sz)
						break;
					memcpy (px + n * bytes, raw + p, (size_t)run * bytes);
					p += (size_t)run * bytes;
				}
				n += run;
			}
			if (n != npx || sz - p > 0x2000)
			{
				FREE (px);
				goto done;
			}
		}
		for (uint y = 0; y < h; y++)
		{
			const uint sy = (raw[17] & 0x20) ? y : h - 1 - y;
			for (uint x = 0; x < w; x++)
			{
				const u8 *s = px + ((size_t)sy * w + x) * bytes;
				u8 *o = rgba + ((size_t)y * w + x) * 4;
				if (bytes == 1)
					o[0] = o[1] = o[2] = s[0], o[3] = 255;
				else
					o[0] = s[2], o[1] = s[1], o[2] = s[0], o[3] = bytes == 4 ? s[3] : 255;
			}
		}
		FREE (px);
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s.png", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE TGA:%s (%ux%u, %u bpp) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h, bpp, png);
		err = ERR_OK;
		if (!testmode)
		{
			SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png, 0, true); // takes ownership of rgba
			rgba = 0;
		}
	}
done:
	FREE (rgba);
	FREE (raw);
	return err;
}

// Plain raster images that the image loader does not read: QOI (".qoi"), ZSoft PCX (".pcx": RLE, 8 bit paletted or 24 bit planar) and Windows icons /
// cursors (".ico", ".cur": every entry as "<name>_N.png"; PNG entries are copied, DIB entries of 32, 24 and 8 bit depth are converted, AND mask applied).
// Output goes to "<file>.d/".
static u8 *rimg_qoi (const u8 *d, size_t n, uint *ow, uint *oh)
{
	if (n < 22 || memcmp (d, "qoif", 4))
		return 0;
	const uint w = rd_be32 (d + 4), h = rd_be32 (d + 8);
	if (!w || !h || w > 16384 || h > 16384 || d[12] < 3 || d[12] > 4)
		return 0;
	u8 *out = MALLOC ((size_t)w * h * 4);
	if (!out)
		return 0;
	u8 idx[64][4];
	memset (idx, 0, sizeof (idx));
	u8 px[4] = {0, 0, 0, 255};
	size_t p = 14, o = 0;
	const size_t total = (size_t)w * h;
	uint run = 0;
	for (size_t i = 0; i < total; i++)
	{
		if (run)
			run--;
		else if (p < n - 8)
		{
			const uint b = d[p++];
			if (b == 0xfe) { px[0] = d[p++]; px[1] = d[p++]; px[2] = d[p++]; }
			else if (b == 0xff) { px[0] = d[p++]; px[1] = d[p++]; px[2] = d[p++]; px[3] = d[p++]; }
			else if ((b & 0xc0) == 0) memcpy (px, idx[b], 4);
			else if ((b & 0xc0) == 0x40) { px[0] += ((b >> 4) & 3) - 2; px[1] += ((b >> 2) & 3) - 2; px[2] += (b & 3) - 2; }
			else if ((b & 0xc0) == 0x80)
			{
				const uint b2 = d[p++], dg = (b & 63) - 32;
				px[0] += dg - 8 + ((b2 >> 4) & 15); px[1] += dg; px[2] += dg - 8 + (b2 & 15);
			}
			else run = b & 63;
			const uint hsh = (px[0] * 3 + px[1] * 5 + px[2] * 7 + px[3] * 11) & 63;
			memcpy (idx[hsh], px, 4);
		}
		memcpy (out + o, px, 4);
		o += 4;
	}
	*ow = w; *oh = h;
	return out;
}

static u8 *rimg_pcx (const u8 *d, size_t n, uint *ow, uint *oh)
{
	if (n < 128 || d[0] != 10 || d[2] != 1)
		return 0;
	const uint bpp = d[3], x0 = d[4] | d[5] << 8, y0 = d[6] | d[7] << 8, x1 = d[8] | d[9] << 8, y1 = d[10] | d[11] << 8, planes = d[65], bpl = d[66] | d[67] << 8;
	if (x1 < x0 || y1 < y0 || bpp != 8 || (planes != 1 && planes != 3) || !bpl)
		return 0;
	const uint w = x1 - x0 + 1, h = y1 - y0 + 1;
	if (w > 16384 || h > 16384 || bpl < w)
		return 0;
	u8 *line = MALLOC ((size_t)bpl * planes);
	u8 *out = MALLOC ((size_t)w * h * 4);
	if (!line || !out)
	{
		FREE (line); FREE (out);
		return 0;
	}
	size_t p = 128;
	bool ok = true;
	const u8 *pal = n >= 769 && d[n - 769] == 12 ? d + n - 768 : 0;
	if (planes == 1 && !pal)
		ok = false;
	for (uint y = 0; y < h && ok; y++)
	{
		for (size_t k = 0; k < (size_t)bpl * planes;)
		{
			if (p >= n) { ok = false; break; }
			uint c = d[p++], cnt = 1;
			if ((c & 0xc0) == 0xc0)
			{
				cnt = c & 63;
				if (p >= n) { ok = false; break; }
				c = d[p++];
			}
			while (cnt-- && k < (size_t)bpl * planes)
				line[k++] = c;
		}
		for (uint x = 0; x < w && ok; x++)
		{
			u8 *o = out + ((size_t)y * w + x) * 4;
			if (planes == 3) { o[0] = line[x]; o[1] = line[bpl + x]; o[2] = line[2 * bpl + x]; }
			else { o[0] = pal[line[x] * 3]; o[1] = pal[line[x] * 3 + 1]; o[2] = pal[line[x] * 3 + 2]; }
			o[3] = 255;
		}
	}
	FREE (line);
	if (!ok)
	{
		FREE (out);
		return 0;
	}
	*ow = w; *oh = h;
	return out;
}

static enumError extract_rasterimg_file (ccp arg, ccp basedir, uint depth)
{
	const bool qoi = is_ext (arg, ".qoi"), pcx = is_ext (arg, ".pcx"), ico = is_ext (arg, ".ico") || is_ext (arg, ".cur");
	if ((!qoi && !pcx && !ico) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char stem[PATH_MAX];
	const char *sl = strrchr (arg, '/');
	snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
	char *dot = strrchr (stem, '.');
	if (dot)
		*dot = 0;
	enumError err = ERR_NOTHING_TO_DO;
	uint w = 0, h = 0;
	if (qoi || pcx)
	{
		u8 *rgba = qoi ? rimg_qoi (raw, sz, &w, &h) : rimg_pcx (raw, sz, &w, &h);
		if (rgba)
		{
			err = testmode ? ERR_OK : kyl_write_rgba_png (arg, basedir, stem, (uint)-1, w, h, rgba);
			FREE (rgba);
		}
	}
	else if (sz >= 22 && !raw[0] && !raw[1] && (raw[2] == 1 || raw[2] == 2) && !raw[3])
	{
		const uint cnt = raw[4] | raw[5] << 8;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		for (uint i = 0; i < cnt && 6 + (i + 1) * 16 <= sz; i++)
		{
			const u8 *e = raw + 6 + i * 16;
			const size_t len = e[8] | e[9] << 8 | e[10] << 16 | (size_t)e[11] << 24, off = e[12] | e[13] << 8 | e[14] << 16 | (size_t)e[15] << 24;
			if (off + len > sz || len < 16)
				continue;
			const u8 *b = raw + off;
			char name[PATH_MAX];
			snprintf (name, sizeof (name), "%s_%u", stem, i);
			if (!memcmp (b, "\x89PNG", 4))
			{
				char fn[PATH_MAX];
				snprintf (fn, sizeof (fn), "%s.png", name);
				err = testmode ? ERR_OK : shade_write (arg, dest, basedir, fn, (u8 *)b, (uint)len);
				continue;
			}
			const uint hs = b[0] | b[1] << 8, bw = b[4] | b[5] << 8 | b[6] << 16, bh2 = b[8] | b[9] << 8 | b[10] << 16, bits = b[14] | b[15] << 8;
			const uint ih = bh2 / 2;
			if (hs < 40 || !bw || !ih || bw > 1024 || ih > 1024 || (bits != 32 && bits != 24 && bits != 8))
				continue;
			const uint ncol = bits == 8 ? (b[32] | b[33] << 8 | b[34] << 16) ? (b[32] | b[33] << 8 | b[34] << 16) : 256 : 0;
			const u8 *pal = b + hs;
			const u8 *xor_ = pal + ncol * 4;
			const size_t xstride = ((size_t)bw * bits + 31) / 32 * 4, mstride = ((size_t)bw + 31) / 32 * 4;
			const u8 *mask = xor_ + xstride * ih;
			if ((size_t)(xor_ - raw) + xstride * ih > sz)
				continue;
			u8 *rgba = MALLOC ((size_t)bw * ih * 4);
			if (!rgba)
				continue;
			const bool have_mask = (size_t)(mask - raw) + mstride * ih <= sz;
			for (uint y = 0; y < ih; y++)
				for (uint x = 0; x < bw; x++)
				{
					const u8 *s = xor_ + (size_t)(ih - 1 - y) * xstride;
					u8 *o = rgba + ((size_t)y * bw + x) * 4;
					if (bits == 32) { o[0] = s[x * 4 + 2]; o[1] = s[x * 4 + 1]; o[2] = s[x * 4]; o[3] = s[x * 4 + 3]; }
					else if (bits == 24) { o[0] = s[x * 3 + 2]; o[1] = s[x * 3 + 1]; o[2] = s[x * 3]; o[3] = 255; }
					else { const u8 *c = pal + s[x] * 4; o[0] = c[2]; o[1] = c[1]; o[2] = c[0]; o[3] = 255; }
					if (bits != 32 && have_mask && (mask[(size_t)(ih - 1 - y) * mstride + x / 8] >> (7 - x % 8) & 1))
						o[3] = 0;
				}
			err = testmode ? ERR_OK : kyl_write_rgba_png (arg, basedir, name, (uint)-1, bw, ih, rgba);
			FREE (rgba);
		}
	}
	FREE (raw);
	return err;
}

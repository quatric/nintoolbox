// Plain raster images that the image loader does not read: Sun raster (".ras", ".sun"), SGI (".sgi", ".rgb", ".bw"), Netpbm P1-P6 (".pbm", ".pgm", ".ppm", ".pnm"), QOI (".qoi"), ZSoft PCX (".pcx": RLE, 8 bit paletted or 24 bit planar) and Windows icons /
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

static bool rimg_pnm_num (const u8 *d, size_t n, size_t *p, uint *v)
{
	for (;;)
	{
		while (*p < n && (d[*p] == ' ' || d[*p] == '\t' || d[*p] == '\r' || d[*p] == '\n'))
			(*p)++;
		if (*p < n && d[*p] == '#')
			while (*p < n && d[*p] != '\n')
				(*p)++;
		else
			break;
	}
	if (*p >= n || d[*p] < '0' || d[*p] > '9')
		return false;
	uint x = 0;
	while (*p < n && d[*p] >= '0' && d[*p] <= '9')
		x = x * 10 + d[(*p)++] - '0';
	*v = x;
	return true;
}

// Netpbm "P1".."P6" (".pbm", ".pgm", ".ppm", ".pnm")
static u8 *rimg_pnm (const u8 *d, size_t n, uint *ow, uint *oh)
{
	if (n < 8 || d[0] != 'P' || d[1] < '1' || d[1] > '6')
		return 0;
	const uint kind = d[1] - '0';
	size_t p = 2;
	uint w, h, mx = 1;
	if (!rimg_pnm_num (d, n, &p, &w) || !rimg_pnm_num (d, n, &p, &h) || !w || !h || w > 16384 || h > 16384)
		return 0;
	if (kind != 1 && kind != 4 && (!rimg_pnm_num (d, n, &p, &mx) || !mx || mx > 65535))
		return 0;
	const bool ascii = kind <= 3, rgb = kind == 3 || kind == 6, bit = kind == 1 || kind == 4;
	if (!ascii)
		p++; // single whitespace byte after the header
	u8 *out = MALLOC ((size_t)w * h * 4);
	if (!out)
		return 0;
	const uint wide = mx > 255 ? 2 : 1;
	for (size_t y = 0; y < h; y++)
		for (size_t x = 0; x < w; x++)
		{
			uint c[3] = {0, 0, 0};
			const uint nc = rgb ? 3 : 1;
			for (uint k = 0; k < nc; k++)
			{
				uint v = 0;
				if (bit && !ascii)
				{
					const size_t rb = p + y * ((w + 7) / 8) + x / 8;
					if (rb >= n) { FREE (out); return 0; }
					v = (d[rb] >> (7 - x % 8) & 1) ? 0 : 255;
				}
				else if (ascii)
				{
					if (!bit && false) {}
					if (bit)
					{
						while (p < n && (d[p] == ' ' || d[p] == '\n' || d[p] == '\r' || d[p] == '\t')) p++;
						if (p >= n) { FREE (out); return 0; }
						v = d[p++] == '1' ? 0 : 255;
					}
					else
					{
						if (!rimg_pnm_num (d, n, &p, &v)) { FREE (out); return 0; }
						v = v * 255 / mx;
					}
				}
				else
				{
					const size_t rp = p + ((y * w + x) * nc + k) * wide;
					if (rp + wide > n) { FREE (out); return 0; }
					v = wide == 2 ? (d[rp] << 8 | d[rp + 1]) * 255 / mx : d[rp] * 255 / mx;
				}
				c[k] = v > 255 ? 255 : v;
			}
			u8 *o = out + (y * w + x) * 4;
			o[0] = c[0]; o[1] = rgb ? c[1] : c[0]; o[2] = rgb ? c[2] : c[0]; o[3] = 255;
		}
	*ow = w; *oh = h;
	return out;
}

// Sun raster ".ras" / ".sun": 1 / 8 / 24 / 32 bit, standard and RLE, optional colour map
static u8 *rimg_sun (const u8 *d, size_t n, uint *ow, uint *oh)
{
	if (n < 32 || rd_be32 (d) != 0x59a66a95)
		return 0;
	const uint w = rd_be32 (d + 4), h = rd_be32 (d + 8), depth = rd_be32 (d + 12), type = rd_be32 (d + 20), mt = rd_be32 (d + 24), ml = rd_be32 (d + 28);
	if (!w || !h || w > 16384 || h > 16384 || (depth != 1 && depth != 8 && depth != 24 && depth != 32) || type > 2 || (mt && mt != 1) || 32 + (size_t)ml > n)
		return 0;
	const u8 *map = ml >= 3 ? d + 32 : 0;
	const uint mcount = ml / 3;
	const size_t stride = (((size_t)w * depth + 15) / 16) * 2, need = stride * h;
	u8 *data = MALLOC (need);
	if (!data)
		return 0;
	const u8 *src = d + 32 + ml;
	const size_t sn = n - 32 - ml;
	if (type != 2)
	{
		if (sn < need) { FREE (data); return 0; }
		memcpy (data, src, need);
	}
	else
	{
		size_t i = 0, o = 0;
		while (o < need && i < sn)
		{
			const u8 c = src[i++];
			if (c != 0x80) { data[o++] = c; continue; }
			if (i >= sn) break;
			const uint cnt = src[i++];
			if (!cnt) { data[o++] = 0x80; continue; }
			if (i >= sn) break;
			const u8 v = src[i++];
			for (uint k = 0; k <= cnt && o < need; k++)
				data[o++] = v;
		}
		if (o < need) { FREE (data); return 0; }
	}
	u8 *out = MALLOC ((size_t)w * h * 4);
	if (!out) { FREE (data); return 0; }
	for (uint y = 0; y < h; y++)
		for (uint x = 0; x < w; x++)
		{
			const u8 *row = data + y * stride;
			u8 *o = out + ((size_t)y * w + x) * 4;
			o[3] = 255;
			if (depth == 1) o[0] = o[1] = o[2] = (row[x / 8] >> (7 - x % 8) & 1) ? 0 : 255;
			else if (depth == 8)
			{
				const uint v = row[x];
				if (map && v < mcount) { o[0] = map[v]; o[1] = map[mcount + v]; o[2] = map[2 * mcount + v]; }
				else o[0] = o[1] = o[2] = v;
			}
			else
			{
				const u8 *px = row + x * (depth / 8) + (depth == 32 ? 1 : 0);
				{ o[0] = px[2]; o[1] = px[1]; o[2] = px[0]; } // BGR in standard and RLE files
			}
		}
	FREE (data);
	*ow = w; *oh = h;
	return out;
}

// SGI "RGB" ".sgi" / ".rgb" / ".bw": verbatim and RLE, 8 bit per channel, 1-4 channels, rows bottom-up
static u8 *rimg_sgi (const u8 *d, size_t n, uint *ow, uint *oh)
{
	if (n < 512 || d[0] != 0x01 || d[1] != 0xda || d[3] != 1)
		return 0;
	const uint rle = d[2], w = d[6] << 8 | d[7], h = d[8] << 8 | d[9], z = d[10] << 8 | d[11];
	if (rle > 1 || !w || !h || !z || z > 4 || w > 16384 || h > 16384)
		return 0;
	u8 *out = MALLOC ((size_t)w * h * 4);
	if (!out)
		return 0;
	memset (out, 255, (size_t)w * h * 4);
	for (uint c = 0; c < z; c++)
		for (uint y = 0; y < h; y++)
		{
			const uint oy = h - 1 - y;
			u8 *row = out + (size_t)oy * w * 4;
			if (!rle)
			{
				const size_t off = 512 + ((size_t)c * h + y) * w;
				if (off + w > n) { FREE (out); return 0; }
				for (uint x = 0; x < w; x++)
					row[x * 4 + (z == 2 && c == 1 ? 3 : c)] = d[off + x];
			}
			else
			{
				const size_t ti = 512 + ((size_t)c * h + y) * 4;
				if (ti + 4 > n) { FREE (out); return 0; }
				size_t p = rd_be32 (d + ti);
				uint x = 0;
				while (x < w)
				{
					if (p >= n) { FREE (out); return 0; }
					const uint cnt = d[p] & 0x7f, lit = d[p] & 0x80;
					p++;
					if (!cnt) break;
					for (uint k = 0; k < cnt && x < w; k++)
					{
						if (p >= n) { FREE (out); return 0; }
						row[x * 4 + (z == 2 && c == 1 ? 3 : c)] = d[p];
						x++;
						if (lit) p++;
					}
					if (!lit) p++;
				}
			}
		}
	if (z < 3)
		for (size_t i = 0; i < (size_t)w * h; i++)
			out[i * 4 + 1] = out[i * 4 + 2] = out[i * 4];
	*ow = w; *oh = h;
	return out;
}

static enumError extract_rasterimg_file (ccp arg, ccp basedir, uint depth)
{
	const bool sun = is_ext (arg, ".ras") || is_ext (arg, ".sun"), sgi = is_ext (arg, ".sgi") || is_ext (arg, ".rgb") || is_ext (arg, ".bw"), pnm = is_ext (arg, ".pbm") || is_ext (arg, ".pgm") || is_ext (arg, ".ppm") || is_ext (arg, ".pnm"), qoi = is_ext (arg, ".qoi"), pcx = is_ext (arg, ".pcx"), ico = is_ext (arg, ".ico") || is_ext (arg, ".cur");
	if ((!qoi && !pcx && !ico && !pnm && !sun && !sgi) || (export_count == 0 && !opt_decode))
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
	if (qoi || pcx || pnm || sun || sgi)
	{
		u8 *rgba = sun ? rimg_sun (raw, sz, &w, &h) : sgi ? rimg_sgi (raw, sz, &w, &h) : qoi ? rimg_qoi (raw, sz, &w, &h) : pnm ? rimg_pnm (raw, sz, &w, &h) : rimg_pcx (raw, sz, &w, &h);
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

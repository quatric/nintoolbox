// Plain raster images that the image loader does not read: XWD, PAM, DPX, FITS, TIFF (baseline strips), X11 XBM / XPM text bitmaps, Sun raster (".ras", ".sun"), SGI (".sgi", ".rgb", ".bw"), Netpbm P1-P6 (".pbm", ".pgm", ".ppm", ".pnm"), QOI (".qoi"), ZSoft PCX (".pcx": RLE, 8 bit paletted or 24 bit planar) and Windows icons /
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

// X11 bitmap ".xbm" (C source: "#define n_width W", "#define n_height H", LSB-first hex bytes)
static u8 *rimg_xbm (const u8 *d, size_t n, uint *ow, uint *oh)
{
	char *t = MALLOC (n + 1);
	if (!t)
		return 0;
	memcpy (t, d, n);
	t[n] = 0;
	uint w = 0, h = 0;
	const char *pw = strstr (t, "_width"), *ph = strstr (t, "_height");
	if (pw) w = strtoul (pw + 6, 0, 10);
	if (ph) h = strtoul (ph + 7, 0, 10);
	const char *br = strchr (t, '{');
	u8 *out = 0;
	if (w && h && w <= 16384 && h <= 16384 && br && (out = MALLOC ((size_t)w * h * 4)))
	{
		const uint rb = (w + 7) / 8;
		const char *q = br + 1;
		bool ok = true;
		for (size_t i = 0; i < (size_t)rb * h && ok; i++)
		{
			while (*q && !(q[0] == '0' && (q[1] == 'x' || q[1] == 'X')))
				q++;
			if (!*q) { ok = false; break; }
			const uint v = strtoul (q, (char **)&q, 16);
			const size_t y = i / rb, bx = i % rb;
			for (uint k = 0; k < 8 && bx * 8 + k < w; k++)
			{
				u8 *o = out + (y * w + bx * 8 + k) * 4;
				o[0] = o[1] = o[2] = (v >> k & 1) ? 0 : 255;
				o[3] = 255;
			}
		}
		if (!ok) { FREE (out); out = 0; }
	}
	FREE (t);
	if (out) { *ow = w; *oh = h; }
	return out;
}

// XPM version 3 ".xpm" (C source, characters per pixel 1-4, colours "c #RRGGBB" / "c None" / a few names)
static u8 *rimg_xpm (const u8 *d, size_t n, uint *ow, uint *oh)
{
	char *t = MALLOC (n + 1);
	if (!t)
		return 0;
	memcpy (t, d, n);
	t[n] = 0;
	// collect the quoted strings
	const char *strs[70000];
	size_t lens[70000], ns = 0;
	for (char *q = t; *q && ns < 69999;)
	{
		if (*q == '/' && q[1] == '*')
		{
			char *e = strstr (q + 2, "*/");
			if (!e) break;
			q = e + 2;
		}
		else if (*q == '"')
		{
			char *e = ++q;
			while (*e && *e != '"') e++;
			strs[ns] = q; lens[ns] = e - q; ns++;
			q = *e ? e + 1 : e;
		}
		else
			q++;
	}
	u8 *out = 0;
	uint w, h, nc, cpp;
	if (ns > 1 && sscanf (strs[0], "%u %u %u %u", &w, &h, &nc, &cpp) == 4 && w && h && w <= 16384 && h <= 16384 && nc && cpp && cpp <= 4 && ns >= 1 + (size_t)nc + h)
	{
		struct { char key[4]; u8 c[4]; } *tab = CALLOC (nc, 12);
		bool ok = tab != 0;
		for (uint i = 0; ok && i < nc; i++)
		{
			const char *l = strs[1 + i];
			if (lens[1 + i] < cpp + 3) { ok = false; break; }
			memcpy (tab[i].key, l, cpp);
			const char *cp = 0;
			for (const char *k = l + cpp; k + 2 < l + lens[1 + i]; k++)
				if ((k[0] == ' ' || k[0] == '\t') && k[1] == 'c' && (k[2] == ' ' || k[2] == '\t')) { cp = k + 3; break; }
			if (!cp) cp = l + cpp + 1;
			while (*cp == ' ' || *cp == '\t') cp++;
			tab[i].c[3] = 255;
			if (*cp == '#')
			{
				char hx[16] = {0};
				size_t k = 0;
				for (cp++; k < 12 && ((*cp >= '0' && *cp <= '9') || ((*cp | 32) >= 'a' && (*cp | 32) <= 'f')); cp++) hx[k++] = *cp;
				const uint dg = k / 3;
				if (dg == 2 || dg == 4 || dg == 1)
					for (uint c = 0; c < 3; c++)
					{
						char b[3] = {hx[c * dg], dg > 1 ? hx[c * dg + 1] : hx[c * dg], 0};
						tab[i].c[c] = strtoul (b, 0, 16);
					}
			}
			else if (!strncasecmp (cp, "None", 4)) tab[i].c[3] = 0;
			else if (!strncasecmp (cp, "white", 5)) tab[i].c[0] = tab[i].c[1] = tab[i].c[2] = 255;
			else if (!strncasecmp (cp, "red", 3)) tab[i].c[0] = 255;
			else if (!strncasecmp (cp, "green", 5)) tab[i].c[1] = 255;
			else if (!strncasecmp (cp, "blue", 4)) tab[i].c[2] = 255;
		}
		out = ok ? MALLOC ((size_t)w * h * 4) : 0;
		for (uint y = 0; out && y < h; y++)
		{
			if (lens[1 + nc + y] < (size_t)w * cpp) { FREE (out); out = 0; break; }
			for (uint x = 0; x < w; x++)
			{
				uint i;
				for (i = 0; i < nc; i++)
					if (!memcmp (tab[i].key, strs[1 + nc + y] + x * cpp, cpp)) break;
				memcpy (out + ((size_t)y * w + x) * 4, i < nc ? tab[i].c : (const u8[4]){0, 0, 0, 0}, 4);
			}
		}
		FREE (tab);
	}
	FREE (t);
	if (out) { *ow = w; *oh = h; }
	return out;
}

// TIFF (".tif", ".tiff"): first IFD, little / big endian, 1 / 8 / 16 bit samples, gray / RGB / RGBA / palette, strips or planar, compression none, LZW, Deflate and PackBits,
// horizontal predictor
static uint tif_u16 (const u8 *p, bool le) { return le ? p[0] | p[1] << 8 : p[0] << 8 | p[1]; }
static u32 tif_u32 (const u8 *p, bool le) { return le ? (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24 : rd_be32 (p); }

static bool tif_lzw (const u8 *src, size_t n, u8 *out, size_t on)
{
	static u16 prefix[4096];
	static u8 suffix[4096], stackbuf[4097];
	size_t o = 0, pos = 0;
	uint bits = 9, next = 258, bitbuf = 0, nb = 0;
	int old = -1;
	for (;;)
	{
		while (nb < bits)
		{
			if (pos >= n) return o == on;
			bitbuf = bitbuf << 8 | src[pos++];
			nb += 8;
		}
		const uint code = bitbuf >> (nb - bits) & ((1u << bits) - 1);
		nb -= bits;
		bitbuf &= (1u << nb) - 1;
		if (code == 257)
			break;
		if (code == 256)
		{
			bits = 9; next = 258; old = -1;
			continue;
		}
		uint cur = code;
		size_t sp = 0;
		if (code >= next)
		{
			if (old < 0 || code > next) return false;
			stackbuf[sp++] = 0; // patched below with the first byte
			cur = old;
		}
		while (cur >= 258)
		{
			if (sp >= 4096) return false;
			stackbuf[sp++] = suffix[cur];
			cur = prefix[cur];
		}
		stackbuf[sp++] = cur;
		const u8 first = stackbuf[sp - 1];
		if (code >= next)
			stackbuf[0] = first;
		while (sp)
		{
			if (o < on) out[o] = stackbuf[--sp]; else --sp;
			o++;
		}
		if (old >= 0 && next < 4096)
		{
			prefix[next] = old;
			suffix[next] = first;
			next++;
		}
		old = code;
		if (next + 1 >= (1u << bits) && bits < 12) bits++;
	}
	return o >= on;
}

static u8 *rimg_tiff (const u8 *d, size_t n, uint *ow, uint *oh)
{
	if (n < 16 || !((d[0] == 'I' && d[1] == 'I' && d[2] == 42) || (d[0] == 'M' && d[1] == 'M' && d[3] == 42)))
		return 0;
	const bool le = d[0] == 'I';
	size_t ifd = tif_u32 (d + 4, le);
	if (ifd + 2 > n)
		return 0;
	uint w = 0, h = 0, bps = 1, comp = 1, photo = 1, spp = 1, rps = 0xffffffffu, planar = 1, pred = 1, extra = 0;
	u32 *offs = 0, *cnts = 0;
	uint noff = 0, ncnt = 0;
	const u8 *cmap = 0;
	size_t cmap_n = 0;
	const uint ne = tif_u16 (d + ifd, le);
	for (uint i = 0; i < ne && ifd + 2 + (size_t)(i + 1) * 12 <= n; i++)
	{
		const u8 *e = d + ifd + 2 + i * 12;
		const uint tag = tif_u16 (e, le), type = tif_u16 (e + 2, le);
		const u32 cnt = tif_u32 (e + 4, le);
		const uint tsz = type == 3 ? 2 : type == 4 ? 4 : 1;
		const u8 *val = (size_t)cnt * tsz <= 4 ? e + 8 : (tif_u32 (e + 8, le) + (size_t)cnt * tsz <= n ? d + tif_u32 (e + 8, le) : 0);
		if (!val) continue;
		const uint v = type == 3 ? tif_u16 (val, le) : type == 4 ? tif_u32 (val, le) : val[0];
		switch (tag)
		{
			case 256: w = v; break;
			case 257: h = v; break;
			case 258: bps = v; break;
			case 259: comp = v; break;
			case 262: photo = v; break;
			case 273: case 279:
			{
				u32 *arr = MALLOC ((size_t)cnt * 4 + 4);
				if (!arr || cnt > 1000000) { FREE (arr); break; }
				for (u32 k = 0; k < cnt; k++)
					arr[k] = type == 3 ? tif_u16 (val + k * 2, le) : tif_u32 (val + k * 4, le);
				if (tag == 273) { FREE (offs); offs = arr; noff = cnt; } else { FREE (cnts); cnts = arr; ncnt = cnt; }
				break;
			}
			case 277: spp = v; break;
			case 278: rps = v; break;
			case 284: planar = v; break;
			case 317: pred = v; break;
			case 320: cmap = val; cmap_n = cnt; break;
			case 338: extra = v; break;
		}
	}
	u8 *out = 0;
	if (w && h && w <= 16384 && h <= 16384 && offs && cnts && noff == ncnt && noff && spp && spp <= 4 && (bps == 1 || bps == 8 || bps == 16) && (comp == 1 || comp == 5 || comp == 8 || comp == 32946 || comp == 32773)
		&& (planar == 1) && (pred == 1 || (pred == 2 && bps != 1)) && (photo <= 3) && (photo != 3 || (cmap && bps == 8 && cmap_n >= 768)))
	{
		if (rps == 0 || rps > h) rps = h;
		const size_t rowbytes = ((size_t)w * spp * bps + 7) / 8;
		u8 *img = CALLOC (1, rowbytes * h + 16);
		bool ok = img != 0;
		for (uint st = 0; ok && st < noff; st++)
		{
			const uint rows = (st * rps >= h) ? 0 : (h - st * rps < rps ? h - st * rps : rps);
			if (!rows) break;
			const size_t want = rowbytes * rows;
			if ((size_t)offs[st] + cnts[st] > n) { ok = false; break; }
			u8 *dst = img + (size_t)st * rps * rowbytes;
			const u8 *sp = d + offs[st];
			if (comp == 1)
			{
				if (cnts[st] < want) { ok = false; break; }
				memcpy (dst, sp, want);
			}
			else if (comp == 32773)
			{
				size_t i = 0, o = 0;
				while (o < want && i < cnts[st])
				{
					const int8_t c = (int8_t)sp[i++];
					if (c >= 0)
					{
						size_t k = (size_t)c + 1;
						if (i + k > cnts[st] || o + k > want) { ok = false; break; }
						memcpy (dst + o, sp + i, k); i += k; o += k;
					}
					else if (c != -128)
					{
						size_t k = (size_t)(1 - c);
						if (i >= cnts[st] || o + k > want) { ok = false; break; }
						memset (dst + o, sp[i++], k); o += k;
					}
				}
				if (o < want) ok = false;
			}
			else if (comp == 5)
				ok = tif_lzw (sp, cnts[st], dst, want);
			else
			{
				uLongf dl = want;
				ok = uncompress (dst, &dl, sp, cnts[st]) == Z_OK && dl >= want;
			}
			if (ok && pred == 2)
				for (uint r = 0; r < rows; r++)
				{
					u8 *row = dst + (size_t)r * rowbytes;
					if (bps == 8)
						for (size_t x = spp; x < rowbytes; x++) row[x] += row[x - spp];
					else
						for (size_t x = spp * 2; x + 1 < rowbytes; x += 2)
						{
							const uint a = tif_u16 (row + x - spp * 2, le), b = tif_u16 (row + x, le), r16 = (a + b) & 0xffff;
							if (le) { row[x] = r16 & 255; row[x + 1] = r16 >> 8; } else { row[x] = r16 >> 8; row[x + 1] = r16 & 255; }
						}
				}
		}
		out = ok ? MALLOC ((size_t)w * h * 4) : 0;
		for (uint y = 0; out && y < h; y++)
			for (uint x = 0; x < w; x++)
			{
				const u8 *row = img + (size_t)y * rowbytes;
				u8 *o = out + ((size_t)y * w + x) * 4;
				int c[4] = {0, 0, 0, 255};
				if (bps == 1)
					c[0] = (row[x / 8] >> (7 - x % 8) & 1) ? 255 : 0;
				else
					for (uint k = 0; k < spp; k++)
						c[k] = bps == 8 ? row[(size_t)x * spp + k] : (le ? row[((size_t)x * spp + k) * 2 + 1] : row[((size_t)x * spp + k) * 2]);
				if (photo == 3)
				{
					const uint idx = c[0], nc = cmap_n / 3;
					c[0] = tif_u16 (cmap + idx * 2, le) >> 8; c[1] = tif_u16 (cmap + (nc + idx) * 2, le) >> 8; c[2] = tif_u16 (cmap + (2 * nc + idx) * 2, le) >> 8;
				}
				else if (photo == 0)
				{
					c[0] = 255 - c[0];
					if (spp == 1 || (spp == 2 && extra)) {}
				}
				if (spp <= 2 && photo != 2 && photo != 3) { c[3] = spp == 2 ? c[1] : 255; c[1] = c[2] = c[0]; }
				else if (spp == 4) c[3] = c[3];
				o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = c[3];
			}
		FREE (img);
	}
	FREE (offs);
	FREE (cnts);
	if (out) { *ow = w; *oh = h; }
	return out;
}

// Netpbm PAM "P7" (".pam"): WIDTH / HEIGHT / DEPTH / MAXVAL, 1-4 channels
static u8 *rimg_pam (const u8 *d, size_t n, uint *ow, uint *oh)
{
	if (n < 16 || memcmp (d, "P7\n", 3))
		return 0;
	uint w = 0, h = 0, depth = 0, mx = 255;
	size_t p = 3;
	bool end = false;
	while (p < n && !end)
	{
		size_t e = p;
		while (e < n && d[e] != '\n') e++;
		if (e >= n) return 0;
		char line[128];
		snprintf (line, sizeof (line), "%.*s", (int)(e - p < 127 ? e - p : 127), d + p);
		if (!strncmp (line, "WIDTH ", 6)) w = atoi (line + 6);
		else if (!strncmp (line, "HEIGHT ", 7)) h = atoi (line + 7);
		else if (!strncmp (line, "DEPTH ", 6)) depth = atoi (line + 6);
		else if (!strncmp (line, "MAXVAL ", 7)) mx = atoi (line + 7);
		else if (!strncmp (line, "ENDHDR", 6)) end = true;
		p = e + 1;
	}
	if (!end || !w || !h || w > 16384 || h > 16384 || !depth || depth > 4 || !mx || mx > 65535)
		return 0;
	const uint wide = mx > 255 ? 2 : 1;
	if (p + (size_t)w * h * depth * wide > n)
		return 0;
	u8 *out = MALLOC ((size_t)w * h * 4);
	if (!out)
		return 0;
	for (size_t i = 0; i < (size_t)w * h; i++)
	{
		uint c[4] = {0, 0, 0, 255};
		for (uint k = 0; k < depth; k++)
		{
			const u8 *q = d + p + (i * depth + k) * wide;
			const uint v = wide == 2 ? q[0] << 8 | q[1] : q[0];
			c[k] = v * 255 / mx;
		}
		u8 *o = out + i * 4;
		if (depth <= 2) { o[0] = o[1] = o[2] = c[0]; o[3] = depth == 2 ? c[1] : 255; }
		else { o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = depth == 4 ? c[3] : 255; }
	}
	*ow = w; *oh = h;
	return out;
}

// DPX (".dpx"): first image element, RGB / RGBA / luma, 8 / 10 / 12 / 16 bit, packed or filled
static u8 *rimg_dpx (const u8 *d, size_t n, uint *ow, uint *oh)
{
	if (n < 1024)
		return 0;
	const bool be = !memcmp (d, "SDPX", 4);
	if (!be && memcmp (d, "XPDS", 4))
		return 0;
	#define D32(o) (be ? rd_be32 (d + (o)) : (u32)d[(o)] | (u32)d[(o) + 1] << 8 | (u32)d[(o) + 2] << 16 | (u32)d[(o) + 3] << 24)
	#define D16(o) (be ? (uint)(d[(o)] << 8 | d[(o) + 1]) : (uint)(d[(o)] | d[(o) + 1] << 8))
	const size_t off = D32 (4);
	const uint w = D32 (772), h = D32 (776), desc = d[800], bits = d[803], packing = D16 (804);
	const uint nc = desc == 50 ? 3 : desc == 51 ? 4 : desc == 6 ? 1 : 0;
	if (!w || !h || w > 16384 || h > 16384 || !nc || (bits != 8 && bits != 10 && bits != 12 && bits != 16) || off >= n)
		return 0;
	u8 *out = MALLOC ((size_t)w * h * 4);
	if (!out)
		return 0;
	const u8 *data = d + off;
	const size_t avail = n - off;
	size_t pos = 0; // bit-less cursor in bytes for the non-10-bit cases
	for (uint y = 0; y < h; y++)
	{
		size_t rowpos = pos;
		uint word = 0, inword = 3;
		for (uint x = 0; x < w; x++)
		{
			uint c[4] = {0, 0, 0, 255};
			for (uint k = 0; k < nc; k++)
			{
				uint v;
				if (bits == 8)
				{
					if (pos >= avail) { FREE (out); return 0; }
					v = data[pos++];
				}
				else if (bits == 16)
				{
					if (pos + 2 > avail) { FREE (out); return 0; }
					v = ((be ? data[pos] << 8 | data[pos + 1] : data[pos + 1] << 8 | data[pos]) * 255u + 32767) / 65535;
					pos += 2;
				}
				else if (bits == 10 && packing)
				{
					if (inword == 3)
					{
						if (pos + 4 > avail) { FREE (out); return 0; }
						word = be ? rd_be32 (data + pos) : (u32)data[pos] | (u32)data[pos + 1] << 8 | (u32)data[pos + 2] << 16 | (u32)data[pos + 3] << 24;
						pos += 4;
						inword = 0;
					}
					const uint sh = packing == 1 ? 22 - inword * 10 : 20 - inword * 10; // method A: 31..22, 21..12, 11..2; method B: 29..20, 19..10, 9..0
					v = ((word >> sh & 1023) * 255u + 511) / 1023;
					inword++;
				}
				else if (bits == 12 && packing)
				{
					if (pos + 2 > avail) { FREE (out); return 0; }
					const uint u = be ? data[pos] << 8 | data[pos + 1] : data[pos + 1] << 8 | data[pos];
					v = ((packing == 1 ? u >> 4 : u & 0xfff) * 255u + 2047) / 4095;
					pos += 2;
				}
				else { FREE (out); return 0; }
				c[k] = v;
			}
			u8 *o = out + ((size_t)y * w + x) * 4;
			if (nc == 1) { o[0] = o[1] = o[2] = c[0]; o[3] = 255; }
			else { o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = c[3]; }
		}
		// rows of 10 bit filled data end on a word boundary; other depths are byte aligned already
		(void)rowpos;
	}
	#undef D32
	#undef D16
	*ow = w; *oh = h;
	return out;
}

// FITS (".fits", ".fit", ".fts"): primary HDU image, BITPIX 8 / 16 / 32 / -32 / -64, linearly scaled to 8 bit gray (8 bit data is kept), bottom row first in the file
static u8 *rimg_fits (const u8 *d, size_t n, uint *ow, uint *oh)
{
	if (n < 2880 || memcmp (d, "SIMPLE  =", 9))
		return 0;
	int bitpix = 0, naxis = 0;
	uint w = 0, h = 0;
	double bzero = 0, bscale = 1;
	size_t p = 0;
	bool end = false;
	for (; p + 80 <= n && !end; p += 80)
	{
		char card[81];
		memcpy (card, d + p, 80);
		card[80] = 0;
		if (!strncmp (card, "END     ", 8)) { end = true; p += 80; break; }
		if (card[8] != '=') continue;
		if (!strncmp (card, "BITPIX  ", 8)) bitpix = atoi (card + 10);
		else if (!strncmp (card, "NAXIS   ", 8)) naxis = atoi (card + 10);
		else if (!strncmp (card, "NAXIS1  ", 8)) w = atoi (card + 10);
		else if (!strncmp (card, "NAXIS2  ", 8)) h = atoi (card + 10);
		else if (!strncmp (card, "BZERO   ", 8)) bzero = atof (card + 10);
		else if (!strncmp (card, "BSCALE  ", 8)) bscale = atof (card + 10);
	}
	if (!end || naxis < 2 || !w || !h || w > 16384 || h > 16384)
		return 0;
	p = (p + 2879) / 2880 * 2880;
	const uint bytes = bitpix < 0 ? -bitpix / 8 : bitpix / 8;
	if (!(bitpix == 8 || bitpix == 16 || bitpix == 32 || bitpix == -32 || bitpix == -64) || p + (size_t)w * h * bytes > n)
		return 0;
	double *val = MALLOC ((size_t)w * h * sizeof (double));
	u8 *out = MALLOC ((size_t)w * h * 4);
	if (!val || !out) { FREE (val); FREE (out); return 0; }
	double lo = 1e300, hi = -1e300;
	for (size_t i = 0; i < (size_t)w * h; i++)
	{
		const u8 *q = d + p + i * bytes;
		double v;
		if (bitpix == 8) v = q[0];
		else if (bitpix == 16) v = (int16_t)(q[0] << 8 | q[1]);
		else if (bitpix == 32) v = (int32_t)rd_be32 (q);
		else if (bitpix == -32) { u32 u = rd_be32 (q); float f; memcpy (&f, &u, 4); v = f; }
		else { uint64_t u = (uint64_t)rd_be32 (q) << 32 | rd_be32 (q + 4); double f; memcpy (&f, &u, 8); v = f; }
		v = v * bscale + bzero;
		val[i] = v;
		if (v < lo) lo = v;
		if (v > hi) hi = v;
	}
	for (uint y = 0; y < h; y++)
		for (uint x = 0; x < w; x++)
		{
			const double v = val[(size_t)(h - 1 - y) * w + x];
			u8 g = bitpix == 8 ? (u8)v : (bitpix == 16 && bzero == 32768 && bscale == 1) ? (u8)((unsigned)v >> 8) : hi > lo ? (u8)((v - lo) * 255.0 / (hi - lo) + 0.5) : 0; // unsigned 16 bit data (BZERO 32768) keeps its high byte
			u8 *o = out + ((size_t)y * w + x) * 4;
			o[0] = o[1] = o[2] = g; o[3] = 255;
		}
	FREE (val);
	*ow = w; *oh = h;
	return out;
}

// X Window Dump ".xwd" (version 7): ZPixmap images of 8 bit PseudoColor / GrayScale (colour map) and 16 / 24 / 32 bit TrueColor / DirectColor via the channel masks
static u8 *rimg_xwd (const u8 *d, size_t n, uint *ow, uint *oh)
{
	if (n < 100 || rd_be32 (d + 4) != 7)
		return 0;
	const uint hs = rd_be32 (d), fmt = rd_be32 (d + 8), w = rd_be32 (d + 16), h = rd_be32 (d + 20), order = rd_be32 (d + 28), bpp = rd_be32 (d + 44), bpl = rd_be32 (d + 48);
	const uint rm = rd_be32 (d + 56), gm = rd_be32 (d + 60), bm = rd_be32 (d + 64), ncol = rd_be32 (d + 76);
	if (fmt != 2 || !w || !h || w > 16384 || h > 16384 || hs < 100 || hs > n || (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) || bpl < (w * bpp + 7) / 8 || ncol > 65536)
		return 0;
	const size_t cmap = hs, pix = cmap + (size_t)ncol * 12;
	if (pix + (size_t)bpl * h > n)
		return 0;
	u8 *out = MALLOC ((size_t)w * h * 4);
	if (!out)
		return 0;
	for (uint y = 0; y < h; y++)
		for (uint x = 0; x < w; x++)
		{
			const u8 *px = d + pix + (size_t)y * bpl + (size_t)x * (bpp / 8);
			u8 *o = out + ((size_t)y * w + x) * 4;
			o[3] = 255;
			if (bpp == 8 && ncol)
			{
				const uint v = px[0];
				bool found = false;
				for (uint i = 0; i < ncol && !found; i++)
				{
					const u8 *e = d + cmap + (size_t)i * 12;
					if (rd_be32 (e) == v) { o[0] = e[4]; o[1] = e[6]; o[2] = e[8]; found = true; }
				}
				if (!found) o[0] = o[1] = o[2] = v;
			}
			else if (bpp == 8)
				o[0] = o[1] = o[2] = px[0];
			else
			{
				u32 v = 0;
				for (uint k = 0; k < bpp / 8; k++)
					v = order ? v << 8 | px[k] : v | (u32)px[k] << (8 * k);
				const u32 m[3] = {rm, gm, bm};
				for (uint c = 0; c < 3; c++)
				{
					u32 mask = m[c];
					uint sh = 0, bits = 0;
					while (mask && !(mask & 1)) { mask >>= 1; sh++; }
					while (mask & 1) { mask >>= 1; bits++; }
					const u32 val = bits ? (v >> sh) & ((1u << bits) - 1) : 0;
					o[c] = !bits ? 0 : bits >= 8 ? val >> (bits - 8) : (u8)(val << (8 - bits) | val >> (2 * bits - 8 > 0 ? 2 * bits - 8 : 0)); // bit replication like ffmpeg
				}
			}
		}
	*ow = w; *oh = h;
	return out;
}

static enumError extract_rasterimg_file (ccp arg, ccp basedir, uint depth)
{
	const bool xwd = is_ext (arg, ".xwd"), pam = is_ext (arg, ".pam"), dpx = is_ext (arg, ".dpx"), fits = is_ext (arg, ".fits") || is_ext (arg, ".fit") || is_ext (arg, ".fts"), tif = is_ext (arg, ".tif") || is_ext (arg, ".tiff"), xbm = is_ext (arg, ".xbm"), xpm = is_ext (arg, ".xpm"), sun = is_ext (arg, ".ras") || is_ext (arg, ".sun"), sgi = is_ext (arg, ".sgi") || is_ext (arg, ".rgb") || is_ext (arg, ".bw"), pnm = is_ext (arg, ".pbm") || is_ext (arg, ".pgm") || is_ext (arg, ".ppm") || is_ext (arg, ".pnm"), qoi = is_ext (arg, ".qoi"), pcx = is_ext (arg, ".pcx"), ico = is_ext (arg, ".ico") || is_ext (arg, ".cur");
	if ((!qoi && !pcx && !ico && !pnm && !sun && !sgi && !xbm && !xpm && !tif && !pam && !dpx && !fits && !xwd) || (export_count == 0 && !opt_decode))
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
	if (qoi || pcx || pnm || sun || sgi || xbm || xpm || tif || pam || dpx || fits || xwd)
	{
		u8 *rgba = xwd ? rimg_xwd (raw, sz, &w, &h) : pam ? rimg_pam (raw, sz, &w, &h) : dpx ? rimg_dpx (raw, sz, &w, &h) : fits ? rimg_fits (raw, sz, &w, &h) : tif ? rimg_tiff (raw, sz, &w, &h) : xbm ? rimg_xbm (raw, sz, &w, &h) : xpm ? rimg_xpm (raw, sz, &w, &h) : sun ? rimg_sun (raw, sz, &w, &h) : sgi ? rimg_sgi (raw, sz, &w, &h) : qoi ? rimg_qoi (raw, sz, &w, &h) : pnm ? rimg_pnm (raw, sz, &w, &h) : rimg_pcx (raw, sz, &w, &h);
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

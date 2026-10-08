// EA "SHPG" shape/texture archives (FIFA, Madden and other EA Sports Wii games;
// the ".gsh" files, usually RefPack-compressed: "10 fb" + 24-bit size). Layout:
// "SHPG", u32 LE size, u32 BE count, 4-char directory tag, then count * {name[4],
// u32 BE offset}. Every offset points at a record chain {u8 code, u24 next
// offset (record size), ...}: code 0x10+N is a GX texture of format N with
// u16 width, u16 height at +4 and the pixels at +0x10; a following 0x3N record
// is the palette (N = GX palette format) for C4 / C8 textures. The first image
// of each entry is decoded to "<file>.<NNN>_<name>.png" in decode mode (xx).
static bool ea_refpack (const u8 *s, size_t n, u8 **out, size_t *out_size)
{
	if (n < 5 || s[1] != 0xfb || (s[0] & 0x7e) != 0x10)
		return false;
	const uint nb = (s[0] & 0x80) ? 4 : 3;
	size_t p = 2;
	if (s[0] & 1)
		p += nb;
	if (p + nb > n)
		return false;
	size_t size = 0;
	for (uint i = 0; i < nb; i++)
		size = (size << 8) | s[p + i];
	p += nb;
	if (!size || size > (size_t)256 << 20)
		return false;
	u8 *o = MALLOC (size);
	if (!o)
		return false;
	size_t q = 0;
	while (p < n && q < size)
	{
		const uint a = s[p++];
		size_t lit = 0, len = 0, dist = 0;
		if (a < 0x80)
		{
			if (p + 1 > n)
				break;
			const uint b = s[p++];
			lit = a & 3;
			len = ((a & 0x1c) >> 2) + 3;
			dist = ((a & 0x60) << 3) + b + 1;
		}
		else if (a < 0xc0)
		{
			if (p + 2 > n)
				break;
			const uint b = s[p], c = s[p + 1];
			p += 2;
			lit = b >> 6;
			len = (a & 0x3f) + 4;
			dist = ((b & 0x3f) << 8) + c + 1;
		}
		else if (a < 0xe0)
		{
			if (p + 3 > n)
				break;
			const uint b = s[p], c = s[p + 1], d = s[p + 2];
			p += 3;
			lit = a & 3;
			len = ((a & 0x0c) << 6) + d + 5;
			dist = ((a & 0x10) << 12) + (b << 8) + c + 1;
		}
		else if (a < 0xfc)
			lit = ((a & 0x1f) << 2) + 4;
		else
			lit = a & 3;
		if (lit > n - p || lit > size - q)
			lit = lit > n - p ? n - p : size - q;
		memcpy (o + q, s + p, lit);
		p += lit;
		q += lit;
		if (a >= 0xe0)
		{
			if (a >= 0xfc)
				break;
			continue;
		}
		if (dist > q || len > size - q)
			break;
		for (size_t i = 0; i < len; i++, q++)
			o[q] = o[q - dist];
	}
	if (q != size)
	{
		FREE (o);
		return false;
	}
	*out = o;
	*out_size = size;
	return true;
}

static void eashpg_rgb5a3 (uint v, u8 *o)
{
	if (v & 0x8000)
	{
		o[0] = ((v >> 10) & 31) * 255 / 31;
		o[1] = ((v >> 5) & 31) * 255 / 31;
		o[2] = (v & 31) * 255 / 31;
		o[3] = 255;
	}
	else
	{
		o[0] = ((v >> 8) & 15) * 17;
		o[1] = ((v >> 4) & 15) * 17;
		o[2] = (v & 15) * 17;
		o[3] = ((v >> 12) & 7) * 255 / 7;
	}
}

static void eashpg_pal (uint pfmt, uint v, u8 *o)
{
	if (pfmt == 0) // IA8
		o[0] = o[1] = o[2] = v & 255, o[3] = v >> 8;
	else if (pfmt == 1) // RGB565
	{
		o[0] = ((v >> 11) & 31) * 255 / 31;
		o[1] = ((v >> 5) & 63) * 255 / 63;
		o[2] = (v & 31) * 255 / 31;
		o[3] = 255;
	}
	else
		eashpg_rgb5a3 (v, o);
}

// decode a GX texture (format fmt, tiled) into cropped w*h RGBA; NULL if unsupported
static u8 *eashpg_decode (uint fmt, const u8 *src, size_t avail, uint w, uint h, const u8 *pal, uint pal_n, uint pfmt)
{
	uint bw, bh, bytes;
	switch (fmt)
	{
	case 0: case 8: case 14: bw = 8, bh = 8, bytes = 32; break;
	case 1: case 2: case 9: bw = 8, bh = 4, bytes = 32; break;
	case 3: case 4: case 5: bw = 4, bh = 4, bytes = 32; break;
	case 6: bw = 4, bh = 4, bytes = 64; break;
	default: return 0;
	}
	if ((fmt == 8 || fmt == 9) && !pal)
		return 0;
	const uint pw = (w + bw - 1) / bw * bw, ph = (h + bh - 1) / bh * bh;
	const size_t need = (size_t)(pw / bw) * (ph / bh) * bytes;
	if (need > avail)
		return 0;
	u8 *full = MALLOC ((size_t)pw * ph * 4);
	if (!full)
		return 0;
	if (fmt == 14)
	{
		u8 *c = GxDecodeCmprAlpha4 (src, 0, pw, ph);
		if (!c)
		{
			FREE (full);
			return 0;
		}
		memcpy (full, c, (size_t)pw * ph * 4);
		FREE (c);
	}
	else
	{
		const u8 *t = src;
		for (uint ty = 0; ty < ph; ty += bh)
			for (uint tx = 0; tx < pw; tx += bw, t += bytes)
				for (uint k = 0; k < bw * bh; k++)
				{
					u8 *o = full + ((size_t)(ty + k / bw) * pw + tx + k % bw) * 4;
					uint v;
					switch (fmt)
					{
					case 0: v = (k & 1) ? t[k / 2] & 15 : t[k / 2] >> 4; o[0] = o[1] = o[2] = o[3] = v * 17; break;
					case 1: o[0] = o[1] = o[2] = o[3] = t[k]; break;
					case 2: v = t[k]; o[0] = o[1] = o[2] = (v & 15) * 17; o[3] = (v >> 4) * 17; break;
					case 3: o[0] = o[1] = o[2] = t[2 * k + 1]; o[3] = t[2 * k]; break;
					case 4: eashpg_pal (1, (t[2 * k] << 8) | t[2 * k + 1], o); break;
					case 5: eashpg_rgb5a3 ((t[2 * k] << 8) | t[2 * k + 1], o); break;
					case 6: o[3] = t[2 * k]; o[0] = t[2 * k + 1]; o[1] = t[32 + 2 * k]; o[2] = t[32 + 2 * k + 1]; break;
					case 8:
						v = (k & 1) ? t[k / 2] & 15 : t[k / 2] >> 4;
						if (v >= pal_n)
							v = 0;
						eashpg_pal (pfmt, (pal[2 * v] << 8) | pal[2 * v + 1], o);
						break;
					default:
						v = t[k];
						if (v >= pal_n)
							v = 0;
						eashpg_pal (pfmt, (pal[2 * v] << 8) | pal[2 * v + 1], o);
						break;
					}
				}
	}
	u8 *rgba = MALLOC ((size_t)w * h * 4);
	if (rgba)
		for (uint y = 0; y < h; y++)
			memcpy (rgba + (size_t)y * w * 4, full + (size_t)y * pw * 4, (size_t)w * 4);
	FREE (full);
	return rgba;
}

static enumError extract_eashpg_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (export_count <= 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz > 5 && raw[1] == 0xfb && (raw[0] & 0x7e) == 0x10)
	{
		u8 *dec = 0;
		size_t dsz = 0;
		const bool ok = ea_refpack (raw, sz, &dec, &dsz);
		FREE (raw);
		if (!ok)
			return ERR_NOTHING_TO_DO;
		raw = dec;
		sz = dsz;
	}
	if (sz < 0x20 || memcmp (raw, "SHPG", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = rd_be32 (raw + 8);
	if (!count || count > 4096 || 16 + 8 * (size_t)count > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_OK;
	uint done = 0;
	for (uint i = 0; i < count && !err; i++)
	{
		const u8 *e = raw + 16 + 8 * (size_t)i;
		const size_t off = rd_be32 (e + 4);
		if (off + 16 > sz)
			continue;
		const uint code = raw[off];
		const size_t rec = ((size_t)raw[off + 1] << 16) | (raw[off + 2] << 8) | raw[off + 3];
		const uint fmt = code - 0x10;
		const uint w = (raw[off + 4] << 8) | raw[off + 5], h = (raw[off + 6] << 8) | raw[off + 7];
		if (code < 0x10 || fmt > 14 || rec < 16 || off + rec > sz || !w || !h || w > 2048 || h > 2048)
			continue;
		const u8 *pal = 0;
		uint pal_n = 0, pfmt = 0;
		if (off + rec + 16 <= sz && (raw[off + rec] & 0xf0) == 0x30)
		{
			const size_t prec = ((size_t)raw[off + rec + 1] << 16) | (raw[off + rec + 2] << 8) | raw[off + rec + 3];
			if (prec > 16 && off + rec + prec <= sz)
			{
				pal = raw + off + rec + 16;
				pal_n = (prec - 16) / 2;
				pfmt = raw[off + rec] & 3;
			}
		}
		u8 *rgba = eashpg_decode (fmt, raw + off + 16, rec - 16, w, h, pal, pal_n, pfmt);
		if (!rgba)
			continue;
		char name[5];
		memcpy (name, e, 4);
		name[4] = 0;
		for (int k = 3; k >= 0 && name[k] == ' '; k--)
			name[k] = 0;
		for (char *c = name; *c; c++)
			if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || *c == '_'))
				*c = '_';
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s.%03u_%s.png", arg, i, name);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE SHPG:%s[%u] %ux%u -> PNG:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, i, w, h, png);
		if (testmode)
			FREE (rgba);
		else
			err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png, 0, true);
		done++;
	}
	FREE (raw);
	if (!err && !done)
		return ERR_NOTHING_TO_DO;
	return err;
}

// Any other RefPack-compressed EA file (FIFA .big members, .pck, script .bin) is
// saved decompressed as "<file>.dec" in decode mode (xx).
static enumError extract_refpack_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (export_count <= 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *dec = 0;
	size_t sz = 0, dsz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const bool ok = sz > 5 && raw[1] == 0xfb && (raw[0] & 0x7e) == 0x10 && ea_refpack (raw, sz, &dec, &dsz);
	FREE (raw);
	if (!ok)
		return ERR_NOTHING_TO_DO;
	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s.dec", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE REFPACK:%s -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
	enumError err = ERR_OK;
	if (!testmode)
		err = SaveFile (dest, 0, 0, dec, dsz, 0);
	FREE (dec);
	return err;
}

// Nippon Ichi "IMY\0" textures (Phantom Brave: We Meet Again, Wii; ".imy"). Header (little-endian,
// 0x20 bytes): "IMY\0", u32 decoded size, u16 row bytes @8, u8 flags @0xa (bit0 raw, bit1 4-byte
// units), u8 bits per pixel @0xb (4, 8 or 32), u16 height @0xc, u16 palette entries @0xe. The
// palette (RGB5A3 words) follows, then the pixel stream: u16 control size (0 -> u32 follows), control
// bytes, then the literal units. Control byte: < 0x10 copies c+1 literal units, < 0xc0 repeats the
// (c-0x10+1)-th most recently consumed literal unit, otherwise (c-0xc0 & 15)+1 units are copied from
// the left, above, above-left or above-right (c-0xc0 >> 4). Units are 2 bytes (4 with flags bit1). The
// result is plain GX tile order (C4 8x8, C8 8x4, RGBA8 4x4, padded to whole tiles). Palettes are RGB565
// when flags bits 5-6 are 0 and RGB5A3 otherwise. Decoded to "<file>.png" in decode mode (xx).
// Decodes the IMY texture at "raw" (sz bytes available) to RGBA; *used receives the stream length.
static enumError imy_to_rgba (const u8 *raw, size_t sz, uint *out_w, uint *out_h, u8 **out_rgba, size_t *used)
{
	u8 *buf = 0, *rgba = 0;
	enumError err = ERR_NOTHING_TO_DO;
	size_t endpos = 0;
	if (sz < 0x24 || memcmp (raw, "IMY", 4))
		goto done;
	const uint stride = rd_le16 (raw + 8), flags = raw[10], bpp = raw[11], h = rd_le16 (raw + 12),
		   npal = rd_le16 (raw + 14);
	const uint w = bpp == 4 ? stride * 2 : bpp == 8 ? stride : bpp == 32 ? stride / 4 : 0;
	const uint tw = bpp == 32 ? 4 : 8, th = bpp == 4 ? 8 : 4;
	if (!w || !h || w > 0x400 || h > 0x1000 || (bpp != 32 && (!npal || npal > 256))
		|| (bpp == 32 && npal) || (bpp == 32 && stride % 4))
		goto done;
	const uint pw = (w + tw - 1) / tw * tw, ph = (h + th - 1) / th * th;
	size_t p = 0x20 + (size_t)npal * 2;
	const size_t total = (size_t)stride * h;
	if (p > sz)
		goto done;
	buf = MALLOC (total);
	if (!buf)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	if (flags & 1)
	{
		if (sz - p < total)
			goto done;
		memcpy (buf, raw + p, total);
		endpos = p + total;
	}
	else
	{
		const uint U = flags & 2 ? 4 : 2;
		if (sz - p < 2)
			goto done;
		size_t ctl = rd_le16 (raw + p), q = p + 2;
		if (!ctl)
		{
			if (sz - p < 6)
				goto done;
			ctl = rd_le32 (raw + p + 2);
			q = p + 6;
		}
		if (ctl > sz - q)
			goto done;
		size_t c = q, lit = q + ctl, o = 0;
		const int offs[4] = { -(int)U, -(int)stride, -(int)stride - (int)U, -(int)stride + (int)U };
		while (o < total)
		{
			if (c >= q + ctl)
				goto done;
			const uint b = raw[c++];
			if (b < 0x10)
			{
				const size_t n = (size_t)(b + 1) * U;
				if (n > total - o || n > sz - lit)
					goto done;
				memcpy (buf + o, raw + lit, n);
				lit += n, o += n;
			}
			else if (b < 0xc0)
			{
				const size_t back = (size_t)(b - 0x10 + 1) * U;
				if (U > total - o || back > lit - (q + ctl))
					goto done;
				memcpy (buf + o, raw + lit - back, U);
				o += U;
			}
			else
			{
				const size_t n = (size_t)((b - 0xc0) & 15) + 1;
				const int d = offs[(b - 0xc0) >> 4];
				if (d < 0 && (size_t)-d > o)
					goto done;
				if (n * U > total - o)
					goto done;
				for (size_t k = 0; k < n * U; k++, o++)
					buf[o] = buf[o + d];
			}
		}
		endpos = lit;
	}
	rgba = MALLOC ((size_t)w * h * 4);
	if (!rgba)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	u8 pal[256][4];
	if (bpp != 32)
		for (uint i = 0; i < npal; i++)
		{
			const uint v = (raw[0x20 + 2 * i] << 8) | raw[0x21 + 2 * i];
			if (!(flags & 0x60)) // blend mode 0 palettes are RGB565
			{
				pal[i][0] = ((v >> 11) & 31) * 255 / 31;
				pal[i][1] = ((v >> 5) & 63) * 255 / 63;
				pal[i][2] = (v & 31) * 255 / 31;
				pal[i][3] = 255;
			}
			else if (v & 0x8000)
			{
				pal[i][0] = ((v >> 10) & 31) * 255 / 31;
				pal[i][1] = ((v >> 5) & 31) * 255 / 31;
				pal[i][2] = (v & 31) * 255 / 31;
				pal[i][3] = 255;
			}
			else
			{
				pal[i][0] = ((v >> 8) & 15) * 17;
				pal[i][1] = ((v >> 4) & 15) * 17;
				pal[i][2] = (v & 15) * 17;
				pal[i][3] = ((v >> 12) & 7) * 255 / 7;
			}
		}
	{
		// tiles are laid out for the width / height rounded up to whole tiles; the buffer only holds
		// stride * h bytes, so a missing tail reads as zero
		const u8 *s = buf, *end = buf + total;
		memset (rgba, 0, (size_t)w * h * 4);
		for (uint ty = 0; ty < ph; ty += th)
			for (uint tx = 0; tx < pw; tx += tw)
			{
				if (bpp == 32)
				{
					for (uint k = 0; k < 16; k++)
					{
						const uint x = tx + k % 4, y = ty + k / 4;
						if (x >= w || y >= h || s + 64 > end)
							continue;
						u8 *o = rgba + ((size_t)y * w + x) * 4;
						o[3] = s[2 * k], o[0] = s[2 * k + 1], o[1] = s[32 + 2 * k], o[2] = s[33 + 2 * k];
					}
					s += 64;
				}
				else
					for (uint y = 0; y < th; y++)
						for (uint x = 0; x < tw; x++)
						{
							uint idx = 0;
							if (s < end)
								idx = bpp == 8 ? *s : (x & 1) ? (*s & 15) : (*s >> 4);
							if (bpp == 8 || (x & 1))
								s++;
							if (tx + x < w && ty + y < h)
								memcpy (rgba + ((size_t)(ty + y) * w + tx + x) * 4, pal[idx % npal], 4);
						}
			}
	}
	*out_w = w;
	*out_h = h;
	*out_rgba = rgba;
	rgba = 0;
	if (used)
		*used = endpos;
	err = ERR_OK;
done:
	FREE (rgba);
	FREE (buf);
	return err;
}

static enumError imy_save_png (ccp arg, ccp suffix, u8 *rgba, uint w, uint h)
{
	char png[PATH_MAX];
	snprintf (png, sizeof (png), "%s%s.png", arg, suffix);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE IMY:%s %ux%u -> PNG:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			arg, w, h, png);
	if (testmode)
	{
		FREE (rgba);
		return ERR_OK;
	}
	return SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png, 0, true); // takes ownership of rgba
}

static enumError extract_imy_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".imy") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	uint w, h;
	u8 *rgba;
	enumError err = imy_to_rgba (raw, sz, &w, &h, &rgba, 0);
	FREE (raw);
	return err ? err : imy_save_png (arg, "", rgba, w, h);
}

// Phantom Brave ".dat" / ".mpd" game data files that carry IMY textures back to back (map tiles
// "mp*.dat", animation sheets "anm*.dat", "titleImg.dat"): every "IMY\0" on a 4-byte boundary that
// decodes cleanly is written as "<file>.<offset>.png" in decode mode (xx).
static enumError extract_imycarve_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if ((!is_ext (arg, ".dat") && !is_ext (arg, ".mpd")) || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz > 0x4000000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_OK;
	uint done = 0;
	for (size_t p = 0; p + 0x24 <= sz && !err;)
	{
		uint w, h;
		u8 *rgba;
		size_t used = 0;
		if (memcmp (raw + p, "IMY", 4) || imy_to_rgba (raw + p, sz - p, &w, &h, &rgba, &used))
		{
			p += 4;
			continue;
		}
		char suffix[32];
		snprintf (suffix, sizeof (suffix), ".%06zx", p);
		err = imy_save_png (arg, suffix, rgba, w, h);
		done++;
		p = (p + (used > 4 ? used : 4) + 3) & ~(size_t)3;
	}
	FREE (raw);
	return err ? err : done ? ERR_OK : ERR_NOTHING_TO_DO;
}

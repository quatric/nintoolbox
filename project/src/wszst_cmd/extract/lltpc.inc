// Little League World Series Baseball 2009 "*.tpc" texture packs: big-endian {u8 0, u8 count, u16 0} then count * 32-byte entries {u32 texture offset, u32 palette offset, u16 height, u16 width, 01 01 01 01, u32 0, u8[3] 0, u8 GX format, u16 palette entries, u16 palette format (0 IA8, 1 RGB565, 2 RGB5A3)}.
// Formats CI4 (8), CI8 (9), RGB565 (4), RGB5A3 (5) and RGBA8 (6) are decoded to "<file>.d/NN.png".
static void lltpc_c16 (u8 *o, uint v, uint pf)
{
	uint r, g, b, a = 255;
	if (pf == 1)
	{
		r = ((v >> 11) & 31) * 255 / 31;
		g = ((v >> 5) & 63) * 255 / 63;
		b = (v & 31) * 255 / 31;
	}
	else if (pf == 0)
	{
		r = g = b = v & 255;
		a = v >> 8;
	}
	else if (v & 0x8000)
	{
		r = ((v >> 10) & 31) * 255 / 31;
		g = ((v >> 5) & 31) * 255 / 31;
		b = (v & 31) * 255 / 31;
	}
	else
	{
		r = ((v >> 8) & 15) * 17;
		g = ((v >> 4) & 15) * 17;
		b = (v & 15) * 17;
		a = ((v >> 12) & 7) * 255 / 7;
	}
	o[0] = (u8)r;
	o[1] = (u8)g;
	o[2] = (u8)b;
	o[3] = (u8)a;
}

static enumError extract_lltpc_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".tpc"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *d = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const long sz = ftell (f);
		if (sz < 36 || sz > 0x4000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)sz);
		if (fread (d, 1, (size_t)sz, f) != (size_t)sz || d[0] || d[2] || d[3])
			goto done;
		const uint n = d[1];
		if (!n || 4 + n * 32 > (uint)sz)
			goto done;
		for (uint i = 0; i < n; i++)
		{
			const u8 *e = d + 4 + i * 32;
			const uint h = be16 (e + 8), w = be16 (e + 10), fmt = e[0x17];
			if (!w || !h || w > 2048 || h > 2048 || (fmt != 4 && fmt != 5 && fmt != 6 && fmt != 8 && fmt != 9))
				goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT TPC:%s (%u textures) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = d + 4 + i * 32;
			const uint to = be32 (e), po = be32 (e + 4), h = be16 (e + 8), w = be16 (e + 10), fmt = e[0x17];
			const uint pn = be16 (e + 0x18), pf = be16 (e + 0x1a);
			const uint bw = fmt == 8 ? 8 : fmt == 9 ? 8 : 4, bh = fmt == 8 ? 8 : 4;
			const uint bsz = fmt == 8 ? 32 : fmt == 9 ? 32 : fmt == 6 ? 64 : 32;
			const u64 need = (u64)((w + bw - 1) / bw) * ((h + bh - 1) / bh) * bsz;
			if (to + need > (u64)sz || ((fmt == 8 || fmt == 9) && (po + (u64)(pn ? pn : 256) * 2 > (u64)sz)))
				continue;
			u8 *rgba = CALLOC (w * h, 4);
			const u8 *p = d + to;
			for (uint by = 0; by < h; by += bh)
				for (uint bx = 0; bx < w; bx += bw)
				{
					for (uint y = 0; y < bh; y++)
						for (uint x = 0; x < bw; x++)
						{
							u8 px[4] = { 0, 0, 0, 255 };
							if (fmt == 8)
							{
								const uint b = p[(y * bw + x) / 2], idx = (x & 1) ? b & 15 : b >> 4;
								lltpc_c16 (px, be16 (d + po + idx * 2), pf);
							}
							else if (fmt == 9)
								lltpc_c16 (px, be16 (d + po + p[y * bw + x] * 2), pf);
							else if (fmt == 6)
							{
								px[3] = p[(y * 4 + x) * 2];
								px[0] = p[(y * 4 + x) * 2 + 1];
								px[1] = p[32 + (y * 4 + x) * 2];
								px[2] = p[32 + (y * 4 + x) * 2 + 1];
							}
							else
								lltpc_c16 (px, be16 (p + (y * 4 + x) * 2), fmt == 4 ? 1 : 2);
							if (bx + x < w && by + y < h)
								memcpy (rgba + ((by + y) * w + bx + x) * 4, px, 4);
						}
					p += bsz;
				}
			char png[PATH_MAX];
			snprintf (png, sizeof (png), "%s/%02u.png", dest, i);
			const enumError e2 = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png, 0, true);
			if (err < e2)
				err = e2;
		}
	}
done:
	FREE (d);
	fclose (f);
	return err;
}

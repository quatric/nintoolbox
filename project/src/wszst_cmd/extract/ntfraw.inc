// NITRO raw 3D textures (".ntft" texels, ".ntfp" palette, ".ntfi" 4x4 block indices; headerless, about 3,600 DS games, usually with the ".ntfp" of the same base name): the DS GX texture formats without a header,
// so the format and size come from the file sizes. Palette: little endian BGR555 colors. Texels are a linear raster. Format: with an ".ntfi" the block compressed 4x4 format (4 bytes of 2 bit texels per block, one u16 of
// {palette offset in words | mode << 14} per block; mode 0: 3 palette colors + transparent, 1: 2 colors + their average + transparent, 2: 4 palette colors, 3: 2 colors + 5/3 and 3/5 blends); no palette means 16 bit direct color; palettes of 8 / 16 bytes are
// 2 bit / 3 bit indexed ("a5i3" with 8 colors is 16 bytes), 32 bytes or less 4 bit, 64 bytes 5 bit indexed with 3 bit alpha ("a3i5"), others 8 bit. The width is not stored: every power of two from 8 to 1024 that divides the pixel count and gives at most a 4:1 shape is tried and the one with the smoothest picture (least difference between neighbouring pixels) wins, which is a guess. Written as "<name>.png".
enum { F4X4, DIRECT, I2, A5I3, I4, A3I5, I8 };

static void ntf_render (int fmt, const u8 *tx, size_t tsz, const u8 *pal, uint ncol, const u8 *idx, size_t isz, uint px, uint w, u8 *rgba)
{
#define NTF_COL(c, q)                                                                                      \
	do                                                                                                     \
	{                                                                                                      \
		const uint c_ = (c);                                                                               \
		(q)[0] = (c_ & 31) * 255 / 31;                                                                     \
		(q)[1] = (c_ >> 5 & 31) * 255 / 31;                                                                \
		(q)[2] = (c_ >> 10 & 31) * 255 / 31;                                                               \
		(q)[3] = 255;                                                                                      \
	} while (0)
	if (fmt == F4X4)
	{
		for (uint b = 0; b < px / 16 && 2 * (size_t)b + 2 <= isz; b++)
		{
			const uint i = rd_le16 (idx + 2 * b), po = (i & 0x3fff) * 2, mode = i >> 14;
			uint cc[4] = { 0, 0, 0, 0 }, ca[4] = { 1, 1, 1, 1 }, pw[4];
			for (uint k = 0; k < 4; k++)
				pw[k] = po / 2 + k < ncol ? rd_le16 (pal + po + 2 * k) : 0;
			const uint r0 = pw[0] & 31, g0 = pw[0] >> 5 & 31, b0 = pw[0] >> 10 & 31, r1 = pw[1] & 31, g1 = pw[1] >> 5 & 31, b1 = pw[1] >> 10 & 31;
			cc[0] = pw[0];
			cc[1] = pw[1];
			if (mode == 0)
				cc[2] = pw[2], ca[3] = 0;
			else if (mode == 1)
				cc[2] = (r0 + r1) / 2 | (g0 + g1) / 2 << 5 | (b0 + b1) / 2 << 10, ca[3] = 0;
			else if (mode == 2)
				cc[2] = pw[2], cc[3] = pw[3];
			else
			{
				cc[2] = (r0 * 5 + r1 * 3) / 8 | (g0 * 5 + g1 * 3) / 8 << 5 | (b0 * 5 + b1 * 3) / 8 << 10;
				cc[3] = (r0 * 3 + r1 * 5) / 8 | (g0 * 3 + g1 * 5) / 8 << 5 | (b0 * 3 + b1 * 5) / 8 << 10;
			}
			const uint bx = (b % (w / 4)) * 4, by = (b / (w / 4)) * 4;
			for (uint y = 0; y < 4; y++)
				for (uint x = 0; x < 4; x++)
				{
					const uint t = tx[4 * b + y] >> (2 * x) & 3;
					u8 *q = rgba + 4 * ((size_t)(by + y) * w + bx + x);
					NTF_COL (cc[t], q);
					q[3] = ca[t] ? 255 : 0;
				}
		}
	}
	else
		for (uint p = 0; p < px; p++)
		{
			u8 *q = rgba + 4 * (size_t)p;
			uint v, a = 255;
			switch (fmt)
			{
			case DIRECT:
				v = rd_le16 (tx + 2 * p);
				NTF_COL (v, q);
				q[3] = v & 0x8000 ? 255 : 0;
				continue;
			case I2: v = tx[p / 4] >> (2 * (p & 3)) & 3; break;
			case I4: v = tx[p / 2] >> (4 * (p & 1)) & 15; break;
			case A5I3: v = tx[p] & 7; a = (tx[p] >> 3) * 255 / 31; break;
			case A3I5: v = tx[p] & 31; a = (tx[p] >> 5) * 255 / 7; break;
			default: v = tx[p]; break;
			}
			if (v < ncol)
				NTF_COL (rd_le16 (pal + 2 * v), q);
			q[3] = (u8)a;
		}
#undef NTF_COL
}

static enumError extract_ntfraw_file (ccp arg, ccp basedir, uint depth)
{
	ccp dot = strrchr (arg, '.');
	if (!dot || strcasecmp (dot, ".ntft"))
		return ERR_NOTHING_TO_DO;
	u8 *tx = 0, *pal = 0, *idx = 0, *rgba = 0;
	size_t tsz = 0, psz = 0, isz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &tx, &tsz, 0, 0, 0, false) || !tx || tsz < 8 || tsz > 0x400000)
		goto done;
	pal = nbfraw_load (arg, ".ntfp", &psz);
	idx = nbfraw_load (arg, ".ntfi", &isz);
	if (!pal || psz > 0x10000)
		psz = 0;
	const uint ncol = (uint)psz / 2;
	int fmt;
	uint px;
	if (idx && ncol)
		fmt = F4X4, px = (uint)tsz * 4;
	else if (!ncol)
		fmt = DIRECT, px = (uint)tsz / 2;
	else if (psz <= 8)
		fmt = I2, px = (uint)tsz * 4;
	else if (psz == 16)
		fmt = A5I3, px = (uint)tsz;
	else if (psz <= 32)
		fmt = I4, px = (uint)tsz * 2;
	else if (psz == 64)
		fmt = A3I5, px = (uint)tsz;
	else
		fmt = I8, px = (uint)tsz;
	if (px < 16)
		goto done;
	// the width is not stored: try every power of two and keep the one whose pixels differ least from their neighbours
	rgba = CALLOC (4, (size_t)px);
	u8 *best = CALLOC (4, (size_t)px);
	if (!rgba || !best)
	{
		err = ERR_OUT_OF_MEMORY;
		FREE (best);
		goto done;
	}
	uint bw = 0;
	u64 bs = ~(u64)0;
	for (uint w = 8; w <= 1024; w <<= 1)
	{
		const uint h = px / w;
		if (px % w || h < 8 || h > 1024 || h > 4 * w || w > 4 * h || (fmt == F4X4 && (w % 4 || h % 4)))
			continue;
		memset (rgba, 0, 4 * (size_t)px);
		ntf_render (fmt, tx, tsz, pal, ncol, idx, isz, px, w, rgba);
		u64 sc = 0;
		for (uint y = 0; y < h; y++)
			for (uint x = 0; x < w; x++)
			{
				const u8 *q = rgba + 4 * ((size_t)y * w + x);
				if (x + 1 < w)
					sc += abs ((int)q[0] - q[4]) + abs ((int)q[1] - q[5]) + abs ((int)q[2] - q[6]);
				if (y + 1 < h)
					sc += abs ((int)q[0] - q[4 * w]) + abs ((int)q[1] - q[4 * w + 1]) + abs ((int)q[2] - q[4 * w + 2]);
			}
		sc = sc * 1024 / px;
		if (sc < bs)
		{
			bs = sc;
			bw = w;
			memcpy (best, rgba, 4 * (size_t)px);
		}
	}
	if (bw)
	{
		const char *sl = strrchr (arg, '/');
		char name[PATH_MAX];
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *e = strrchr (name, '.');
		if (e)
			*e = 0;
		err = kyl_write_rgba_png (arg, basedir, name, (uint)-1, bw, px / bw, best);
	}
	FREE (best);
done:
	FREE (rgba);
	FREE (idx);
	FREE (pal);
	FREE (tx);
	return err;
}

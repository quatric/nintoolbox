// Kylotonn engine ".vid" videos (Cocoto Festival, Wii): {u16, u16, u8 length, name} then little-endian chunks SIZX SIZY PACK (10) NBFR FSEC SFRE SAMP
// STER SCRU, followed by one "VIDE" chunk per frame: {u32 LE decoded size, pack3 LZ stream (see kylotonn_txc.inc)}. The decoded stream drives
// gx_crunch_unpack9, a block codec on 8x8 blocks of big-endian RGB565 pixels (verified bit-exact against the game code run in an emulator):
//   the 2 bit block modes are packed four per byte, most significant bits first, fetched lazily (a new byte whenever the four are used up);
//   mode 0: copy from the previous frame, byte = (dy + 8) << 4 | (dx + 8);
//   mode 1: solid BE u16 colour;
//   mode 2: two BE u16 colours (4 level palette c0, (2c0 + c1 + 1) / 3, (c0 + 2c1 + 1) / 3, c1 per 8 bit channel, masked back to 5/6/5 bits) and one byte of four
//           2 bit indices, one solid 4x4 quadrant each (top-left, top-right, bottom-left, bottom-right);
//   mode 3: another 2 bit code: 0 = palette (2 words) + 4 bytes of 2 bit indices, one per 2x2 cell (row-pair by row-pair);
//           1 = four corner colours (TL, TR, BL, BR); left and right edges interpolated with /7 and masked, then every row interpolated between them;
//           2 = per quadrant (TL, TR, BL, BR) a palette (2 words) and 4 bytes of 2 bit indices, one byte per row of 4 pixels;
//           3 = palette: first word, then up to 15 more words ending at a repeat of the first word, then 4 bytes per row with 4 bit indices.
// Each frame becomes "<file>.d/<name>_<n>.png" (the audio is the sibling ".snd").
typedef struct kyl_vr_t
{
	const u8 *d;
	size_t n, p;
	uint cnt, byte;
	bool bad;
} kyl_vr_t;

static uint kyl_vr_u8 (kyl_vr_t *r)
{
	if (r->p >= r->n)
	{
		r->bad = true;
		return 0;
	}
	return r->d[r->p++];
}

static uint kyl_vr_u16 (kyl_vr_t *r)
{
	const uint a = kyl_vr_u8 (r);
	return a << 8 | kyl_vr_u8 (r);
}

static uint kyl_vr_code (kyl_vr_t *r)
{
	if (r->cnt == 4)
	{
		r->byte = kyl_vr_u8 (r);
		r->cnt = 0;
	}
	const uint v = r->byte >> 6 & 3;
	r->byte = r->byte << 2 & 0xff;
	r->cnt++;
	return v;
}

static uint kyl_vr_pk (const int c[3])
{
	return (c[0] & 0xf8) << 8 | (c[1] & 0xfc) << 3 | (c[2] & 0xf8) >> 3;
}

static void kyl_vr_ch (uint v, int c[3])
{
	c[0] = v >> 8 & 0xf8;
	c[1] = v >> 3 & 0xfc;
	c[2] = v << 3 & 0xf8;
}

static void kyl_vr_pal4 (uint c0, uint c1, uint p[4])
{
	int a[3], b[3], x[3], y[3];
	kyl_vr_ch (c0, a);
	kyl_vr_ch (c1, b);
	for (int i = 0; i < 3; i++)
	{
		x[i] = (2 * a[i] + b[i] + 1) / 3;
		y[i] = (a[i] + 2 * b[i] + 1) / 3;
	}
	p[0] = c0;
	p[1] = kyl_vr_pk (x);
	p[2] = kyl_vr_pk (y);
	p[3] = c1;
}

// returns the number of stream bytes used, or (size_t)-1 on an overrun
static size_t kyl_vid_frame (const u8 *data, size_t n, const u16 *prev, u16 *cur, uint W, uint H)
{
	kyl_vr_t r = { data, n, 0, 4, 0, false };
	for (uint by = 0; by < H; by += 8)
		for (uint bx = 0; bx < W; bx += 8)
		{
			const uint m = kyl_vr_code (&r);
			if (m == 0)
			{
				const uint b = kyl_vr_u8 (&r);
				const int dy = (int)(b >> 4) - 8, dx = (int)(b & 15) - 8;
				for (uint y = 0; y < 8; y++)
					for (uint x = 0; x < 8; x++)
					{
						const int sy = (int)(by + y) + dy, sx = (int)(bx + x) + dx;
						cur[(by + y) * W + bx + x] = sy >= 0 && sy < (int)H && sx >= 0 && sx < (int)W ? prev[sy * W + sx] : 0;
					}
			}
			else if (m == 1)
			{
				const uint c = kyl_vr_u16 (&r);
				for (uint y = 0; y < 8; y++)
					for (uint x = 0; x < 8; x++)
						cur[(by + y) * W + bx + x] = c;
			}
			else if (m == 2)
			{
				uint p[4];
				const uint c0 = kyl_vr_u16 (&r), c1 = kyl_vr_u16 (&r);
				kyl_vr_pal4 (c0, c1, p);
				const uint b = kyl_vr_u8 (&r);
				for (uint q = 0; q < 4; q++)
				{
					const uint c = p[b >> (6 - 2 * q) & 3], qx = (q & 1) * 4, qy = (q >> 1) * 4;
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 4; x++)
							cur[(by + qy + y) * W + bx + qx + x] = c;
				}
			}
			else
			{
				const uint s = kyl_vr_code (&r);
				if (s == 0)
				{
					uint p[4];
					const uint c0 = kyl_vr_u16 (&r), c1 = kyl_vr_u16 (&r);
					kyl_vr_pal4 (c0, c1, p);
					for (uint row = 0; row < 4; row++)
					{
						const uint b = kyl_vr_u8 (&r);
						for (uint k = 0; k < 4; k++)
							for (uint dy = 0; dy < 2; dy++)
								for (uint dx = 0; dx < 2; dx++)
									cur[(by + row * 2 + dy) * W + bx + k * 2 + dx] = p[b >> (6 - 2 * k) & 3];
					}
				}
				else if (s == 1)
				{
					int c[4][3];
					for (uint k = 0; k < 4; k++)
						kyl_vr_ch (kyl_vr_u16 (&r), c[k]);
					static const int mask[3] = { 0xf8, 0xfc, 0xf8 };
					for (uint y = 0; y < 8; y++)
					{
						int l[3], rr[3];
						for (int i = 0; i < 3; i++)
						{
							l[i] = ((c[0][i] * (7 - (int)y) + c[2][i] * (int)y) / 7) & mask[i];
							rr[i] = ((c[1][i] * (7 - (int)y) + c[3][i] * (int)y) / 7) & mask[i];
						}
						for (uint x = 0; x < 8; x++)
						{
							int v[3];
							for (int i = 0; i < 3; i++)
								v[i] = (l[i] * (7 - (int)x) + rr[i] * (int)x) / 7;
							cur[(by + y) * W + bx + x] = kyl_vr_pk (v);
						}
					}
				}
				else if (s == 2)
				{
					for (uint q = 0; q < 4; q++)
					{
						uint p[4];
						const uint c0 = kyl_vr_u16 (&r), c1 = kyl_vr_u16 (&r);
						kyl_vr_pal4 (c0, c1, p);
						const uint qx = (q & 1) * 4, qy = (q >> 1) * 4;
						for (uint y = 0; y < 4; y++)
						{
							const uint b = kyl_vr_u8 (&r);
							for (uint k = 0; k < 4; k++)
								cur[(by + qy + y) * W + bx + qx + k] = p[b >> (6 - 2 * k) & 3];
						}
					}
				}
				else
				{
					uint pal[16], np = 1;
					pal[0] = kyl_vr_u16 (&r);
					for (uint k = 0; k < 15; k++)
					{
						const uint v = kyl_vr_u16 (&r);
						if (v == pal[0])
							break;
						pal[np++] = v;
					}
					for (uint y = 0; y < 8; y++)
						for (uint k = 0; k < 4; k++)
						{
							const uint b = kyl_vr_u8 (&r);
							cur[(by + y) * W + bx + k * 2] = b >> 4 < np ? pal[b >> 4] : 0;
							cur[(by + y) * W + bx + k * 2 + 1] = (b & 15) < np ? pal[b & 15] : 0;
						}
				}
			}
			if (r.bad)
				return (size_t)-1;
		}
	return r.p;
}

static enumError kyl_write_rgb565_png (ccp arg, ccp basedir, ccp stem, uint index, uint w, uint h, const u16 *px)
{
	const size_t need = (size_t)w * h * 2;
	u8 *tpl = CALLOC (1, 0x40 + need);
	if (!tpl)
		return ERR_OUT_OF_MEMORY;
	u8 *p = tpl;
#define PUT32(v) do { const uint v_ = (v); *p++ = v_ >> 24; *p++ = v_ >> 16; *p++ = v_ >> 8; *p++ = v_; } while (0)
	PUT32 (0x0020af30); PUT32 (1); PUT32 (0x0c);
	PUT32 (0x14); PUT32 (0);
	*p++ = h >> 8; *p++ = h; *p++ = w >> 8; *p++ = w;
	PUT32 (4); PUT32 (0x40); PUT32 (0); PUT32 (0); PUT32 (1); PUT32 (1); PUT32 (0);
#undef PUT32
	u8 *d = tpl + 0x40; // RGB565, GX 4x4 tiles
	for (uint by = 0; by < h; by += 4)
		for (uint bx = 0; bx < w; bx += 4)
			for (uint y = 0; y < 4; y++)
				for (uint x = 0; x < 4; x++)
				{
					const uint v = px[(by + y) * w + bx + x];
					*d++ = v >> 8;
					*d++ = v;
				}
	char fn[PATH_MAX], dest[PATH_MAX], tmp[PATH_MAX], tmp_png[PATH_MAX], png[PATH_MAX];
	snprintf (fn, sizeof (fn), "%s_%04u.tmp.tpl", stem, index);
	beside_source_dest (dest, sizeof (dest), arg);
	snprintf (tmp, sizeof (tmp), "%s/%s%s", dest, basedir ? basedir : "", fn);
	snprintf (tmp_png, sizeof (tmp_png), "%s.png", tmp);
	snprintf (png, sizeof (png), "%s/%s%s_%04u.png", dest, basedir ? basedir : "", stem, index);
	enumError err = ERR_OK;
	if (!testmode)
	{
		err = shade_write (arg, dest, basedir, fn, tpl, 0x40 + (uint)need);
		if (!err)
		{
			const int old_verbose = verbose;
			verbose = -1; // one line per video, not per frame
			err = decode_image_if_possible (tmp);
			verbose = old_verbose;
		}
		if (!err && rename (tmp_png, png))
			err = ERROR1 (ERR_CANT_CREATE, "Can't create PNG: %s\n", png);
		unlink (tmp);
	}
	FREE (tpl);
	return err;
}

static enumError extract_kylotonn_vid_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".vid") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	uint W = 0, H = 0, pack = 0, nfr = 0;
	size_t first = 0;
	if (sz > 0x40 && raw[4])
	{
		for (size_t c = 6 + raw[4]; c + 8 <= sz;)
		{
			const u32 cs = kyl_le32 (raw + c + 4);
			if (cs < 8 || c + cs > sz)
				break;
			if (!memcmp (raw + c, "VIDE", 4))
			{
				first = c;
				break;
			}
			const u8 *pl = raw + c + 8;
			if (!memcmp (raw + c, "SIZX", 4) && cs >= 12) W = kyl_le32 (pl);
			else if (!memcmp (raw + c, "SIZY", 4) && cs >= 12) H = kyl_le32 (pl);
			else if (!memcmp (raw + c, "PACK", 4) && cs >= 12) pack = kyl_le32 (pl);
			else if (!memcmp (raw + c, "NBFR", 4) && cs >= 12) nfr = kyl_le32 (pl);
			c += cs;
		}
	}
	if (first && pack == 10 && W && H && !(W & 7) && !(H & 7) && W <= 4096 && H <= 4096 && nfr && nfr <= 100000)
	{
		char stem[PATH_MAX];
		const char *sl = strrchr (arg, '/');
		snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (stem, '.');
		if (dot)
			*dot = 0;
		u16 *prev = CALLOC ((size_t)W * H, sizeof (u16)), *cur = CALLOC ((size_t)W * H, sizeof (u16));
		u8 *lz = 0;
		size_t lz_cap = 0;
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT KYLOTONN VIDEO:%s %ux%u, %u frames -> %s_NNNN.png\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, W, H, nfr, stem);
		err = ERR_OK;
		uint done = 0;
		size_t c = first;
		while (prev && cur && done < nfr && c + 12 <= sz && !memcmp (raw + c, "VIDE", 4) && !err)
		{
			const u32 cs = kyl_le32 (raw + c + 4);
			if (cs < 12 || c + cs > sz)
				break;
			const size_t outsize = kyl_le32 (raw + c + 8);
			if (!outsize || outsize > (size_t)W * H * 4)
				break;
			if (outsize > lz_cap)
			{
				FREE (lz);
				lz = MALLOC (outsize);
				lz_cap = lz ? outsize : 0;
				if (!lz)
					break;
			}
			u8 *dec = kyl_unpack3 (raw + c + 12, cs - 12, outsize, false);
			if (!dec)
				break;
			const size_t used = kyl_vid_frame (dec, outsize, prev, cur, W, H);
			FREE (dec);
			if (used == (size_t)-1)
				break;
			err = kyl_write_rgb565_png (arg, basedir, stem, done, W, H, cur);
			u16 *t = prev;
			prev = cur;
			cur = t;
			done++;
			c += cs;
		}
		FREE (lz);
		FREE (prev);
		FREE (cur);
		if (!done && !err)
			err = ERR_NOTHING_TO_DO;
	}
	FREE (raw);
	return err;
}

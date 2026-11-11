// Amiga IFF: "FORM ILBM" pictures (".iff", ".lbm", ".ilbm": BMHD / CMAP / CAMG / BODY, uncompressed or ByteRun1, up to 8 bitplanes + optional mask plane, 24 bit deep
// pictures, EHB and HAM6 / HAM8) written to "<name>.png", and "FORM 8SVX" samples (".8svx", ".iff": VHDR + BODY signed 8 bit, raw or Fibonacci delta) written to
// "<name>.wav" (mono, 8 bit unsigned).
static enumError extract_amigaiff_file (ccp arg, ccp basedir, uint depth)
{
	if ((!is_ext (arg, ".iff") && !is_ext (arg, ".lbm") && !is_ext (arg, ".ilbm") && !is_ext (arg, ".8svx")) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	char stem[PATH_MAX];
	const char *sl = strrchr (arg, '/');
	snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
	char *dot = strrchr (stem, '.');
	if (dot)
		*dot = 0;
	if (sz >= 12 && !memcmp (raw, "FORM", 4) && (!memcmp (raw + 8, "ILBM", 4) || !memcmp (raw + 8, "8SVX", 4)))
	{
		const bool ilbm = !memcmp (raw + 8, "ILBM", 4);
		uint w = 0, h = 0, planes = 0, mask = 0, comp = 0, camg = 0, rate = 0, svxcomp = 0;
		const u8 *cmap = 0, *body = 0;
		size_t cmap_n = 0, body_n = 0;
		for (size_t p = 12; p + 8 <= sz;)
		{
			const size_t cs = rd_be32 (raw + p + 4);
			const u8 *c = raw + p + 8;
			if (p + 8 + cs > sz)
				break;
			if (!memcmp (raw + p, "BMHD", 4) && cs >= 20)
			{
				w = c[0] << 8 | c[1]; h = c[2] << 8 | c[3]; planes = c[8]; mask = c[9]; comp = c[10];
			}
			else if (!memcmp (raw + p, "CMAP", 4)) { cmap = c; cmap_n = cs / 3; }
			else if (!memcmp (raw + p, "CAMG", 4) && cs >= 4) camg = rd_be32 (c);
			else if (!memcmp (raw + p, "VHDR", 4) && cs >= 20) { rate = c[12] << 8 | c[13]; svxcomp = c[15]; }
			else if (!memcmp (raw + p, "BODY", 4)) { body = c; body_n = cs; }
			p += 8 + cs + (cs & 1);
		}
		if (ilbm && w && h && w <= 16384 && h <= 16384 && planes && planes <= 32 && body && comp <= 1 && (mask == 0 || mask == 1 || mask == 2))
		{
			const size_t rowbytes = ((w + 15) / 16) * 2, nplanes = planes + (mask == 1 ? 1 : 0);
			const size_t need = rowbytes * nplanes * h;
			u8 *planar = MALLOC (need);
			bool ok = planar != 0;
			if (ok && comp == 0)
			{
				ok = body_n >= need;
				if (ok)
					memcpy (planar, body, need);
			}
			else if (ok)
			{
				size_t i = 0, o = 0;
				while (o < need && i < body_n)
				{
					const int8_t n = (int8_t)body[i++];
					if (n >= 0)
					{
						size_t cnt = (size_t)n + 1;
						if (i + cnt > body_n || o + cnt > need) { ok = false; break; }
						memcpy (planar + o, body + i, cnt);
						i += cnt; o += cnt;
					}
					else if (n != -128)
					{
						size_t cnt = (size_t)(1 - n);
						if (i >= body_n || o + cnt > need) { ok = false; break; }
						memset (planar + o, body[i++], cnt);
						o += cnt;
					}
				}
				ok = ok && o == need;
			}
			u8 *rgba = ok ? CALLOC (1, (size_t)w * h * 4) : 0;
			if (rgba)
			{
				const bool ham = (camg & 0x800) != 0, ehb = (camg & 0x80) != 0 && !ham;
				for (uint y = 0; y < h; y++)
				{
					uint pr = 0, pg = 0, pb = 0;
					for (uint x = 0; x < w; x++)
					{
						uint v = 0;
						for (uint k = 0; k < planes; k++)
							v |= (uint)(planar[((size_t)y * nplanes + k) * rowbytes + x / 8] >> (7 - x % 8) & 1) << k;
						u8 *o = rgba + ((size_t)y * w + x) * 4;
						uint r, g, b;
						if (planes > 8 && !cmap)
						{
							r = v & 255; g = v >> 8 & 255; b = v >> 16 & 255;
						}
						else if (ham && planes >= 5)
						{
							const uint hb = planes - 2, data = v & ((1u << hb) - 1), ctl = v >> hb;
							const uint sc = hb == 4 ? 17 : 4; // HAM6 4 bit components, HAM8 6 bit (shifted)
							if (ctl == 0)
							{
								const u8 *c = cmap && data < cmap_n ? cmap + data * 3 : 0;
								pr = c ? c[0] : 0; pg = c ? c[1] : 0; pb = c ? c[2] : 0;
							}
							else
							{
								const uint cv = hb == 4 ? data * sc : (data << 2 | data >> 4);
								if (ctl == 1) pb = cv; else if (ctl == 2) pr = cv; else pg = cv;
							}
							r = pr; g = pg; b = pb;
						}
						else
						{
							uint idx = v;
							bool half = false;
							if (ehb && planes == 6 && idx >= 32) { idx -= 32; half = true; }
							const u8 *c = cmap && idx < cmap_n ? cmap + idx * 3 : 0;
							r = c ? c[0] : idx * 255 / ((1u << planes) - 1);
							g = c ? c[1] : r; b = c ? c[2] : r;
							if (half) { r >>= 1; g >>= 1; b >>= 1; }
						}
						o[0] = r; o[1] = g; o[2] = b; o[3] = 255;
						if (mask == 1 && !(planar[((size_t)y * nplanes + planes) * rowbytes + x / 8] >> (7 - x % 8) & 1))
							o[3] = 0;
					}
				}
				err = testmode ? ERR_OK : kyl_write_rgba_png (arg, basedir, stem, (uint)-1, w, h, rgba);
			}
			FREE (rgba);
			FREE (planar);
		}
		else if (!ilbm && body && rate && svxcomp <= 1)
		{
			size_t n = body_n;
			u8 *pcm = 0;
			if (svxcomp == 0)
			{
				pcm = MALLOC (n ? n : 1);
				for (size_t i = 0; pcm && i < n; i++)
					pcm[i] = body[i] ^ 0x80;
			}
			else if (n > 2)
			{
				static const int8_t fib[16] = {-34, -21, -13, -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8, 13, 21};
				n = (n - 2) * 2;
				pcm = MALLOC (n);
				int v = (int8_t)body[1];
				for (size_t i = 0; pcm && i < n; i++)
				{
					const uint nib = i & 1 ? body[2 + i / 2] & 15 : body[2 + i / 2] >> 4;
					v += fib[nib];
					if (v > 127) v = 127;
					if (v < -128) v = -128;
					pcm[i] = (u8)(v ^ 0x80);
				}
			}
			if (pcm)
			{
				err = pcm_write_wav (arg, basedir, 1, rate, 8, pcm, n);
				FREE (pcm);
			}
		}
	}
	FREE (raw);
	return err;
}

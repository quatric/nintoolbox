// GIF87a / GIF89a ".gif": every frame is composed on the logical screen (transparency, disposal methods 0-3, local / global palettes, interlacing) and written as
// "<file>.d/<name>_NNNN.png" (one frame: "<name>.png"); "info.txt" lists the canvas size, the frame delays (1/100 s) and the loop count.
static bool gif_lzw (const u8 *src, size_t n, uint minbits, u8 *out, size_t on)
{
	static u16 prefix[4096];
	static u8 suffix[4096], stackbuf[4097];
	if (minbits < 2 || minbits > 8)
		return false;
	const uint clear = 1u << minbits, eoi = clear + 1;
	uint bits = minbits + 1, next = eoi + 1, bitbuf = 0, nb = 0;
	int old = -1;
	size_t pos = 0, o = 0;
	u8 first = 0;
	while (o < on)
	{
		while (nb < bits)
		{
			if (pos >= n) return o > 0;
			bitbuf |= (uint)src[pos++] << nb;
			nb += 8;
		}
		const uint code = bitbuf & ((1u << bits) - 1);
		bitbuf >>= bits;
		nb -= bits;
		if (code == clear) { bits = minbits + 1; next = eoi + 1; old = -1; continue; }
		if (code == eoi) break;
		uint cur = code;
		size_t sp = 0;
		if (old < 0)
		{
			if (code >= clear) return false;
			out[o++] = first = code;
			old = code;
			continue;
		}
		if (code >= next)
		{
			if (code > next) return false;
			stackbuf[sp++] = first;
			cur = old;
		}
		while (cur >= clear)
		{
			if (sp >= 4096 || cur >= next) return false;
			stackbuf[sp++] = suffix[cur];
			cur = prefix[cur];
		}
		stackbuf[sp++] = first = cur;
		while (sp && o < on)
			out[o++] = stackbuf[--sp];
		if (next < 4096)
		{
			prefix[next] = old;
			suffix[next] = first;
			next++;
			if (next == (1u << bits) && bits < 12) bits++;
		}
		old = code;
	}
	return o == on || o > 0;
}

static enumError extract_gif_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".gif") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 13 && (!memcmp (raw, "GIF87a", 6) || !memcmp (raw, "GIF89a", 6)))
	{
		const uint cw = raw[6] | raw[7] << 8, ch = raw[8] | raw[9] << 8;
		const uint flags = raw[10], bgi = raw[11];
		size_t p = 13;
		const u8 *gpal = 0;
		uint gcount = 0;
		if (flags & 0x80)
		{
			gcount = 2u << (flags & 7);
			gpal = raw + p;
			p += (size_t)gcount * 3;
		}
		if (cw && ch && cw <= 16384 && ch <= 16384 && p < sz)
		{
			u8 *canvas = CALLOC ((size_t)cw * ch, 4);
			u8 *prev = MALLOC ((size_t)cw * ch * 4);
			u8 *frame = MALLOC ((size_t)cw * ch);
			retro_info_t info = {{0}, 0};
			retro_info (&info, "canvas: %ux%u\n", cw, ch);
			uint gce_delay = 0, gce_flags = 0, tcol = 0, nframes = 0, loops = 1;
			char stem[PATH_MAX], dest[PATH_MAX];
			const char *sl = strrchr (arg, '/');
			snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (stem, '.');
			if (dot) *dot = 0;
			beside_source_dest (dest, sizeof (dest), arg);
			// count frames first so single images keep the plain name
			uint total = 0;
			for (size_t q = p; q < sz;)
			{
				if (raw[q] == 0x3b) break;
				if (raw[q] == 0x2c) { total++; q += 10; if (q > sz) break; const uint lf = raw[q - 1]; if (lf & 0x80) q += (size_t)3 << ((lf & 7) + 1); q++; while (q < sz && raw[q]) q += raw[q] + 1; q++; }
				else if (raw[q] == 0x21) { q += 2; while (q < sz && raw[q]) q += raw[q] + 1; q++; }
				else break;
			}
			bool ok = canvas && prev && frame;
			err = ok ? ERR_OK : ERR_OUT_OF_MEMORY;
			while (ok && p < sz && err == ERR_OK)
			{
				const u8 b = raw[p];
				if (b == 0x3b)
					break;
				if (b == 0x21 && p + 2 <= sz)
				{
					const u8 label = raw[p + 1];
					size_t q = p + 2;
					if (label == 0xf9 && q + 5 < sz && raw[q] >= 4)
					{
						gce_flags = raw[q + 1];
						gce_delay = raw[q + 2] | raw[q + 3] << 8;
						tcol = raw[q + 4];
					}
					else if (label == 0xff && q + 17 < sz && !memcmp (raw + q + 1, "NETSCAPE2.0", 11) && raw[q + 12] == 3)
						loops = raw[q + 14] | raw[q + 15] << 8;
					while (q < sz && raw[q]) q += raw[q] + 1;
					p = q + 1;
					continue;
				}
				if (b != 0x2c || p + 10 > sz)
					break;
				const uint fx = raw[p + 1] | raw[p + 2] << 8, fy = raw[p + 3] | raw[p + 4] << 8, fw = raw[p + 5] | raw[p + 6] << 8, fh = raw[p + 7] | raw[p + 8] << 8, lf = raw[p + 9];
				p += 10;
				const u8 *pal = gpal;
				uint pcount = gcount;
				if (lf & 0x80)
				{
					pcount = 2u << (lf & 7);
					pal = raw + p;
					p += (size_t)pcount * 3;
				}
				if (p + 1 > sz || !fw || !fh || fx + fw > cw || fy + fh > ch || !pal) { ok = false; break; }
				const uint minbits = raw[p++];
				size_t dl = 0;
				for (size_t q = p; q < sz && raw[q]; q += raw[q] + 1) dl += raw[q];
				u8 *data = MALLOC (dl + 1);
				if (!data) { err = ERR_OUT_OF_MEMORY; break; }
				size_t dp = 0;
				for (; p < sz && raw[p]; p += raw[p] + 1)
				{
					if (p + 1 + raw[p] > sz) break;
					memcpy (data + dp, raw + p + 1, raw[p]);
					dp += raw[p];
				}
				p++;
				memset (frame, 0, (size_t)cw * ch);
				const bool good = gif_lzw (data, dp, minbits, frame, (size_t)fw * fh);
				FREE (data);
				if (!good) { ok = false; break; }
				const uint disposal = gce_flags >> 2 & 7;
				const bool transp = (gce_flags & 1) != 0;
				if (disposal == 3)
					memcpy (prev, canvas, (size_t)cw * ch * 4);
				// interlace: rows come in four passes
				static const uint pass_start[4] = {0, 4, 2, 1}, pass_step[4] = {8, 8, 4, 2};
				uint srow = 0;
				for (uint pass = 0; pass < (lf & 0x40 ? 4u : 1u); pass++)
					for (uint y = lf & 0x40 ? pass_start[pass] : 0; y < fh; y += lf & 0x40 ? pass_step[pass] : 1, srow++)
						for (uint x = 0; x < fw; x++)
						{
							const uint idx = frame[(size_t)srow * fw + x];
							if (transp && idx == tcol) continue;
							if (idx >= pcount) continue;
							u8 *o = canvas + (((size_t)(fy + y)) * cw + fx + x) * 4;
							o[0] = pal[idx * 3]; o[1] = pal[idx * 3 + 1]; o[2] = pal[idx * 3 + 2]; o[3] = 255;
						}
				char name[PATH_MAX];
				if (total > 1) snprintf (name, sizeof (name), "%s_%04u", stem, nframes);
				else snprintf (name, sizeof (name), "%s", stem);
				err = testmode ? ERR_OK : kyl_write_rgba_png (arg, basedir, name, (uint)-1, cw, ch, canvas);
				retro_info (&info, "frame %u: %ux%u at %u,%u, delay %u, disposal %u%s\n", nframes, fw, fh, fx, fy, gce_delay, disposal, transp ? ", transparent" : "");
				nframes++;
				if (disposal == 2)
					for (uint y = 0; y < fh; y++)
						for (uint x = 0; x < fw; x++)
							memset (canvas + (((size_t)(fy + y)) * cw + fx + x) * 4, 0, 4);
				else if (disposal == 3)
					memcpy (canvas, prev, (size_t)cw * ch * 4);
				gce_flags = 0; gce_delay = 0;
			}
			(void)bgi;
			if (!nframes && err == ERR_OK)
				err = ERR_NOTHING_TO_DO;
			else if (nframes && err == ERR_OK && total > 1)
			{
				retro_info (&info, "loops: %u\n", loops);
				err = testmode ? ERR_OK : shade_write (arg, dest, basedir, "info.txt", (u8 *)info.buf, (uint)info.n);
			}
			FREE (canvas); FREE (prev); FREE (frame);
		}
	}
	FREE (raw);
	return err;
}

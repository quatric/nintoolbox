// Kylotonn engine ".txc" texture members (Cocoto Festival, Wii): a u32 (0 or 1), then little-endian chunks
// {tag, u32 size including the 8 header bytes}. "BTCH" {TYPE, SIZE, MTRD...}; every "MTRD" holds VERS, NAME ({u16, u16, u8 length, text}), SIZX, SIZY,
// BITP (bits per pixel: 8, 16 or 32), PACK (5), SIZE, optionally PALE, and PICT.
//
// PACK 5 is the engine's gx_crunch_pack5 = an LZ pass ("pack3") whose output bytes are all Huffman coded ("pack4"):
//   header: 256 times { present bit; if set a 2 bit length class (00 -> 8, 01 -> 16, 10 -> 24, 11 -> 32 bits) and that many bits of
//   symbol weight, MSB first }; then the code bits, MSB first (bit 0 -> left child, 1 -> right child).
//   The tree is built from a 256 entry list in symbol order by repeatedly merging the lightest node with a second node (see
//   kyl_build_tree; the selection quirks must match the encoder). The decoded bytes are the pack3 stream: control byte c with
//   bit 7 clear = c+1 literals, 10xxxxxx = copy (x+1) bytes from a 16 bit big-endian distance+1, 11xxxxxx = copy (x+1) bytes from an 8 bit distance+1.
// "PALE" (8 bit textures) is a plain pack3 stream that expands to 256 x 4 bytes.
// 32 bit pixels are ARGB bytes, 16 bit pixels are little-endian ARGB4444; both are written as PNG. 8 bit images are written raw
// as "<name>_<n>.8bpp" with "<name>_<n>.pal" because their pixel layout is not understood yet.
typedef struct kyl_node_t
{
	int v;
	uint w;
	int l, r, prev, next;
} kyl_node_t;

typedef struct kyl_bits_t
{
	const u8 *p;
	size_t n, pos;
	int bit;
	bool eof;
} kyl_bits_t;

static uint kyl_bit (kyl_bits_t *b)
{
	if (b->pos >= b->n)
	{
		b->eof = true;
		return 0;
	}
	const uint v = b->p[b->pos] >> b->bit & 1;
	if (--b->bit < 0)
	{
		b->bit = 7;
		b->pos++;
	}
	return v;
}

static uint kyl_bits (kyl_bits_t *b, uint n)
{
	uint v = 0;
	while (n--)
		v = v << 1 | kyl_bit (b);
	return v;
}

// returns the output (outsize bytes, CALLOC) or 0
static u8 *kyl_unpack3 (const u8 *s, size_t n, size_t outsize, bool must_consume)
{
	u8 *o = CALLOC (1, outsize ? outsize : 1);
	if (!o)
		return 0;
	size_t i = 0, k = 0;
	while (i < n && k < outsize)
	{
		const uint c = s[i++];
		if (!(c & 0x80))
		{
			size_t m = c + 1;
			if (i + m > n || k + m > outsize)
				goto fail;
			memcpy (o + k, s + i, m);
			i += m;
			k += m;
		}
		else
		{
			size_t m = (c & 0x3f) + 1, d;
			if ((c & 0xc0) == 0x80)
			{
				if (i + 2 > n)
					goto fail;
				d = ((size_t)s[i] << 8) + s[i + 1] + 1;
				i += 2;
			}
			else
			{
				if (i + 1 > n)
					goto fail;
				d = (size_t)s[i++] + 1;
			}
			if (d > k || k + m > outsize)
				goto fail;
			for (; m; m--, k++)
				o[k] = o[k - d];
		}
	}
	if (k == outsize && (!must_consume || i == n))
		return o;
fail:
	FREE (o);
	return 0;
}

static u8 *kyl_unpack5 (const u8 *src, size_t n, size_t outsize)
{
	kyl_node_t nd[512];
	int nn = 256;
	kyl_bits_t b = { src, n, 0, 7, false };
	for (int i = 0; i < 256; i++)
	{
		nd[i] = (kyl_node_t){ i, 0, -1, -1, i - 1, i < 255 ? i + 1 : -1 };
		if (kyl_bit (&b))
		{
			uint k = !kyl_bit (&b) ? (kyl_bit (&b) ? 16 : 8) : (kyl_bit (&b) ? 32 : 24);
			nd[i].w = kyl_bits (&b, k);
		}
	}
	if (b.eof)
		return 0;
	int head = 0;
	while (nd[head].next >= 0)
	{
		int mn = head;
		for (int c = head; c >= 0; c = nd[c].next)
			if (nd[c].w < nd[mn].w)
				mn = c;
		int sc = head != mn ? head : nd[head].next;
		for (int c = head; c >= 0; c = nd[c].next)
			if (nd[c].w < nd[sc].w && c != mn)
				mn = c;
		if (nn >= 512)
			return 0;
		const int nw = nn++;
		nd[nw] = (kyl_node_t){ -1, nd[sc].w + nd[mn].w, mn, sc, nd[mn].prev, nd[mn].next };
		if (nd[nw].prev < 0)
			head = nw;
		else
			nd[nd[nw].prev].next = nw;
		if (nd[nw].next >= 0)
			nd[nd[nw].next].prev = nw;
		if (nd[sc].prev < 0)
		{
			head = nd[sc].next;
			nd[head].prev = -1;
		}
		else
		{
			nd[nd[sc].prev].next = nd[sc].next;
			if (nd[sc].next >= 0)
				nd[nd[sc].next].prev = nd[sc].prev;
		}
	}
	const int root = head;
	u8 *o = CALLOC (1, outsize ? outsize : 1);
	if (!o)
		return 0;
	// Huffman decode into a temporary pack3 stream is avoided: decode the LZ stream on the fly
	size_t k = 0;
#define KSYM(dst) do { int q_ = root; while (nd[q_].v == -1) { q_ = kyl_bit (&b) ? nd[q_].r : nd[q_].l; if (b.eof) goto fail; } dst = nd[q_].v & 255; } while (0)
	while (k < outsize)
	{
		uint c;
		KSYM (c);
		if (!(c & 0x80))
		{
			uint m = c + 1;
			if (k + m > outsize)
				goto fail;
			while (m--)
			{
				uint s;
				KSYM (s);
				o[k++] = s;
			}
		}
		else
		{
			uint m = (c & 0x3f) + 1, d, s;
			if ((c & 0xc0) == 0x80)
			{
				uint s2;
				KSYM (s);
				KSYM (s2);
				d = (s << 8) + s2 + 1;
			}
			else
			{
				KSYM (s);
				d = s + 1;
			}
			if (d > k || k + m > outsize)
				goto fail;
			for (; m; m--, k++)
				o[k] = o[k - d];
		}
	}
#undef KSYM
	return o;
fail:
	FREE (o);
	return 0;
}

static u32 kyl_le32 (const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

// RGBA (8 bit per channel, row major) -> synthetic RGBA8 TPL, then PNG through the regular decoder
static enumError kyl_write_rgba_png (ccp arg, ccp basedir, ccp name, uint index, uint w, uint h, const u8 *rgba)
{
	const uint bw = (w + 3) & ~3u, bh = (h + 3) & ~3u;
	const size_t need = (size_t)bw * bh * 4;
	u8 *tpl = CALLOC (1, 0x40 + need);
	if (!tpl)
		return ERR_OUT_OF_MEMORY;
	u8 *p = tpl;
#define PUT32(v) do { const uint v_ = (v); *p++ = v_ >> 24; *p++ = v_ >> 16; *p++ = v_ >> 8; *p++ = v_; } while (0)
	PUT32 (0x0020af30); PUT32 (1); PUT32 (0x0c);
	PUT32 (0x14); PUT32 (0);
	*p++ = h >> 8; *p++ = h; *p++ = w >> 8; *p++ = w;
	PUT32 (6); PUT32 (0x40); PUT32 (0); PUT32 (0); PUT32 (1); PUT32 (1); PUT32 (0);
#undef PUT32
	u8 *d = tpl + 0x40;
	for (uint by = 0; by < bh; by += 4)
		for (uint bx = 0; bx < bw; bx += 4)
		{
			for (uint y = 0; y < 4; y++)
				for (uint x = 0; x < 4; x++)
				{
					const uint sx = bx + x < w ? bx + x : w - 1, sy = by + y < h ? by + y : h - 1;
					const u8 *s = rgba + ((size_t)sy * w + sx) * 4;
					d[(y * 4 + x) * 2] = s[3];
					d[(y * 4 + x) * 2 + 1] = s[0];
					d[32 + (y * 4 + x) * 2] = s[1];
					d[32 + (y * 4 + x) * 2 + 1] = s[2];
				}
			d += 64;
		}
	char safe[PATH_MAX], fn[PATH_MAX], dest[PATH_MAX], tmp[PATH_MAX], tmp_png[PATH_MAX], png[PATH_MAX];
	snprintf (safe, sizeof (safe), "%s", name);
	for (char *s = safe; *s; s++)
		if (*s == '/' || *s == '\\' || *s == ':')
			*s = '_';
	snprintf (fn, sizeof (fn), "%s_%u.tmp.tpl", safe, index);
	beside_source_dest (dest, sizeof (dest), arg);
	snprintf (tmp, sizeof (tmp), "%s/%s%s", dest, basedir ? basedir : "", fn);
	snprintf (tmp_png, sizeof (tmp_png), "%s.png", tmp);
	snprintf (png, sizeof (png), "%s/%s%s_%u.png", dest, basedir ? basedir : "", safe, index);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE KYLOTONN TEXTURE:%s %s (%ux%u) -> PNG:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, safe, w, h, png);
	enumError err = ERR_OK;
	if (!testmode)
	{
		err = shade_write (arg, dest, basedir, fn, tpl, 0x40 + (uint)need);
		if (!err)
			err = decode_image_if_possible (tmp);
		if (!err && rename (tmp_png, png))
			err = ERROR1 (ERR_CANT_CREATE, "Can't create PNG: %s\n", png);
		unlink (tmp);
	}
	FREE (tpl);
	return err;
}

static enumError extract_kylotonn_txc_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".txc") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz >= 0x40 && !memcmp (raw + 4, "BTCH", 4))
	{
		uint index = 0;
		for (size_t b0 = 4; b0 + 8 <= sz && !memcmp (raw + b0, "BTCH", 4);)
		{
		const u32 bsz = kyl_le32 (raw + b0 + 4);
		if (bsz < 8)
			break;
		const size_t btch_end = b0 + bsz <= sz ? b0 + bsz : sz;
		for (size_t m = b0 + 8; m + 8 <= btch_end;)
		{
			const u32 ms = kyl_le32 (raw + m + 4);
			if (ms < 8 || m + ms > btch_end)
				break;
			if (!memcmp (raw + m, "MTRD", 4))
			{
				for (size_t c0 = m + 8; c0 + 8 <= m + ms;)
				{
				// one image = the chunks from a "VERS" up to the next "VERS" (or the end of the MTRD)
				size_t cend = c0 + kyl_le32 (raw + c0 + 4);
				while (cend + 8 <= m + ms && memcmp (raw + cend, "VERS", 4) && kyl_le32 (raw + cend + 4) >= 8)
					cend += kyl_le32 (raw + cend + 4);
				if (cend > m + ms)
					cend = m + ms;
				uint w = 0, h = 0, bpp = 0, pack = 0;
				const u8 *pict = 0, *pale = 0;
				size_t pict_n = 0, pale_n = 0;
				char name[256] = "texture";
				for (size_t c = c0; c + 8 <= cend;)
				{
					const u32 cs = kyl_le32 (raw + c + 4);
					if (cs < 8 || c + cs > cend)
						break;
					const u8 *pl = raw + c + 8;
					if (!memcmp (raw + c, "SIZX", 4) && cs >= 12) w = kyl_le32 (pl);
					else if (!memcmp (raw + c, "SIZY", 4) && cs >= 12) h = kyl_le32 (pl);
					else if (!memcmp (raw + c, "BITP", 4) && cs >= 12) bpp = kyl_le32 (pl);
					else if (!memcmp (raw + c, "PACK", 4) && cs >= 12) pack = kyl_le32 (pl);
					else if (!memcmp (raw + c, "PICT", 4)) { pict = pl; pict_n = cs - 8; }
					else if (!memcmp (raw + c, "PALE", 4)) { pale = pl; pale_n = cs - 8; }
					else if (!memcmp (raw + c, "NAME", 4) && cs >= 14)
					{
						const uint l = pl[4] < sizeof (name) - 1 && pl[4] + 5u <= cs - 8 ? pl[4] : 0;
						if (l)
						{
							memcpy (name, pl + 5, l);
							name[l] = 0;
							for (char *s = name; *s; s++)
								if ((u8)*s < 0x20)
									*s = 0;
						}
					}
					c += cs;
				}
				const size_t px = (size_t)w * h * bpp / 8;
				if (pict && w && h && px && px <= 0x10000000 && (bpp == 8 || bpp == 16 || bpp == 32) && (pack == 5 || pack == 3 || pack == 0))
				{
					u8 *data = pack == 5 ? kyl_unpack5 (pict, pict_n, px) : pack == 3 ? kyl_unpack3 (pict, pict_n, px, false) : 0;
					if (pack == 0 && pict_n >= px)
					{
						data = MALLOC (px);
						if (data)
							memcpy (data, pict, px);
					}
					if (data)
					{
						if (bpp == 32 || bpp == 16)
						{
							u8 *rgba = MALLOC ((size_t)w * h * 4);
							if (rgba)
							{
								for (size_t i = 0; i < (size_t)w * h; i++)
									if (bpp == 32)
									{
										rgba[i * 4] = data[i * 4 + 1];
										rgba[i * 4 + 1] = data[i * 4 + 2];
										rgba[i * 4 + 2] = data[i * 4 + 3];
										rgba[i * 4 + 3] = data[i * 4];
									}
									else
									{
										const uint v = data[i * 2] | data[i * 2 + 1] << 8;
										rgba[i * 4] = (v >> 8 & 15) * 17;
										rgba[i * 4 + 1] = (v >> 4 & 15) * 17;
										rgba[i * 4 + 2] = (v & 15) * 17;
										rgba[i * 4 + 3] = (v >> 12 & 15) * 17;
									}
								const enumError e = kyl_write_rgba_png (arg, basedir, name, index, w, h, rgba);
								err = e && err == ERR_NOTHING_TO_DO ? e : err == ERR_NOTHING_TO_DO ? ERR_OK : err;
								FREE (rgba);
							}
						}
						else
						{
							char dest[PATH_MAX], fn[PATH_MAX];
							beside_source_dest (dest, sizeof (dest), arg);
							snprintf (fn, sizeof (fn), "%s_%u.8bpp", name, index);
							for (char *s = fn; *s; s++)
								if (*s == '/' || *s == '\\' || *s == ':')
									*s = '_';
							if (!testmode)
								err = shade_write (arg, dest, basedir, fn, data, px);
							if (!err && pale)
							{
								u8 *pal = kyl_unpack3 (pale, pale_n, 1024, false);
								if (pal)
								{
									snprintf (fn + strlen (fn) - 5, 6, ".pal");
									err = testmode ? ERR_OK : shade_write (arg, dest, basedir, fn, pal, 1024);
									FREE (pal);
								}
							}
						}
						FREE (data);
					}
				}
				index++;
				c0 = cend;
				}
			}
			m += ms;
		}
		b0 = btch_end;
		}
	}
	FREE (raw);
	return err;
}

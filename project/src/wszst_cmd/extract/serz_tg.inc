// Ubisoft/Kuju "SERZ" texture groups (".TgWii": Sleepover Party; little endian, GX-native big-endian pixel data). "SERZ" + u32 version, then a token stream: 0xff 'C' (+5 bytes),
// 0xff 'P' {name, u32 id, u32 child count}, 0xff 'V' {name, type, value}, 0xff 'B' {u32 length, bytes}, 0xff 'p' {u16}, 0xff 'N'. A name/type/string is either 0xffff + u32 length +
// text (new, added to the string table) or a u16 index into that table. Every P/V/B definition also gets a 1-based "entity" number; later instances of the same class are written
// compactly as {u8 entity, payload} (P: id + count, V: the value of its type, B: u32 length + bytes). A cHcTexture holds Name, an image (cHcImageWii: Format as HC_IMAGE_FORMAT_*,
// Width/Height as u16) and its pixel blob (base level first, mips after it; PAL8 palette in a second 512-byte blob, otherwise the indices are written as an 8-bit grey image). The base level is decoded to "<texture name>.png".
typedef struct serz_str_t { const u8 *s; uint n; } serz_str_t;
typedef struct serz_ent_t { serz_str_t name, type; char kind; } serz_ent_t;
typedef struct serz_tex_t { serz_str_t name, fmt; uint w, h; const u8 *blob[2]; uint blen[2]; uint nblob; } serz_tex_t;

static bool serz_is (serz_str_t s, ccp t)
{
	return s.s && s.n == strlen (t) && !memcmp (s.s, t, s.n);
}

static bool serz_ref (const u8 *d, size_t sz, size_t *p, serz_str_t *tab, uint *ntab, serz_str_t *out)
{
	if (*p + 2 > sz)
		return false;
	const uint v = rd_le16 (d + *p);
	*p += 2;
	if (v == 0xffff)
	{
		if (*p + 4 > sz)
			return false;
		const uint n = rd_le32 (d + *p);
		*p += 4;
		if (n > 256 || *p + n > sz || *ntab >= 8192)
			return false;
		tab[*ntab].s = d + *p;
		tab[*ntab].n = n;
		*out = tab[(*ntab)++];
		*p += n;
		return true;
	}
	if (v >= *ntab)
		return false;
	*out = tab[v];
	return true;
}

static uint serz_vsize (serz_str_t t)
{
	if (serz_is (t, "sUInt32") || serz_is (t, "sInt32") || serz_is (t, "sFloat32"))
		return 4;
	if (serz_is (t, "sUInt16") || serz_is (t, "sInt16"))
		return 2;
	if (serz_is (t, "bool") || serz_is (t, "sUInt8") || serz_is (t, "sInt8"))
		return 1;
	if (serz_is (t, "sUInt64") || serz_is (t, "sInt64") || serz_is (t, "sFloat64"))
		return 8;
	return 0;
}

static bool serz_emit (const serz_tex_t *t, ccp dest, uint index, uint *wrote)
{
	image_format_t gx;
	uint npal = 0;
	if (serz_is (t->fmt, "HC_IMAGE_FORMAT_COLA8888"))
		gx = IMG_RGBA32;
	else if (serz_is (t->fmt, "HC_IMAGE_FORMAT_COL565"))
		gx = IMG_RGB565;
	else if (serz_is (t->fmt, "HC_IMAGE_FORMAT_COLA5551"))
		gx = IMG_RGB5A3;
	else if (serz_is (t->fmt, "HC_IMAGE_FORMAT_COMPRESSED"))
		gx = IMG_CMPR;
	else if (serz_is (t->fmt, "HC_IMAGE_FORMAT_PAL8"))
	{
		// with a 512-byte palette blob it is C8; without one the indices are shown as 8-bit intensity
		gx = t->nblob >= 2 && t->blen[1] >= 512 ? IMG_C8 : IMG_I8;
		npal = gx == IMG_C8 ? 256 : 0;
	}
	else
		return false;
	uint isz = 0;
	if (!t->nblob || !t->w || !t->h || t->w > 4096 || t->h > 4096 || !CalcImageGeometry (gx, t->w, t->h, 0, 0, 0, 0, &isz) || !isz || t->blen[0] < isz
		|| (npal && (t->nblob < 2 || t->blen[1] < 512)))
		return false;
	Image_t img;
	InitializeIMG (&img);
	img.data = CALLOC (1, isz + 64);
	memcpy (img.data, t->blob[0], isz);
	img.data_alloced = true;
	img.data_size = isz;
	img.width = t->w;
	img.height = t->h;
	img.iform = img.info_iform = gx;
	img.info_fform = FF_PNG;
	img.info_n_image = 1;
	img.endian = &be_func;
	CalcImageGeometry (img.iform, img.width, img.height, &img.xwidth, &img.xheight, 0, 0, 0);
	if (npal)
	{
		img.pal = CALLOC (1, 4 * npal + 64);
		memcpy (img.pal, t->blob[1], 2 * npal);
		img.pal_alloced = true;
		img.pal_size = 2 * npal;
		img.n_pal = npal;
		img.pform = img.info_pform = PAL_RGB5A3;
	}
	char nm[80], path[PATH_MAX];
	snprintf (nm, sizeof (nm), "%.*s", t->name.n < 64 ? (int)t->name.n : 64, t->name.s ? (ccp)t->name.s : "tex");
	for (char *c = nm; *c; c++)
		if (!isalnum ((u8)*c) && *c != '_' && *c != '-' && *c != '.')
			*c = '_';
	snprintf (path, sizeof (path), "%s/%04u_%s.png", dest, index, nm);
	bool ok = false;
	if (!ConvertIMG (&img, false, 0, IMG_X_RGB, PAL_INVALID) && !SavePNG (&img, false, 0, path, 0, 0, true, 0))
	{
		(*wrote)++;
		ok = true;
	}
	ResetIMG (&img);
	return ok;
}

static enumError extract_serz_tg_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	(void)basedir;
	if (!is_ext (arg, ".TgWii") || !file_has_magic (arg, "SERZ", 4))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	serz_str_t *tab = CALLOC (8192, sizeof (serz_str_t));
	serz_ent_t *ent = CALLOC (8192, sizeof (serz_ent_t));
	uint ntab = 0, nent = 0, tidx = 0, ntex = 0, wrote = 0;
	bool have_t = false, have_cur = false;
	serz_tex_t cur;
	memset (&cur, 0, sizeof (cur));
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	bool made = false;
	size_t p = 8;
	#define SERZ_FLUSH() \
		do { \
			if (have_cur) \
			{ \
				if (!made && !testmode) { CreatePath (dest, true); made = true; } \
				if (testmode ? cur.nblob != 0 : serz_emit (&cur, dest, ntex, &wrote)) \
					{ if (testmode) wrote++; } \
				ntex++; \
			} \
			memset (&cur, 0, sizeof (cur)); \
			have_cur = false; \
		} while (0)
	while (tab && ent && p + 2 <= sz)
	{
		bool fail = false;
		const u8 b = raw[p];
		if (b == 0xff)
		{
			const u8 k = raw[p + 1];
			p += 2;
			if (k == 'C')
				p += 5;
			else if (k == 'P')
			{
				serz_str_t n;
				if (!serz_ref (raw, sz, &p, tab, &ntab, &n) || p + 8 > sz || nent >= 8192)
					fail = true;
				else
				{
					p += 8;
					ent[nent].name = n;
					ent[nent].kind = 'P';
					if (serz_is (n, "cHcTexture"))
					{
						SERZ_FLUSH ();
						tidx = nent;
						have_t = have_cur = true;
					}
					nent++;
				}
			}
			else if (k == 'V')
			{
				serz_str_t n, t;
				if (!serz_ref (raw, sz, &p, tab, &ntab, &n) || !serz_ref (raw, sz, &p, tab, &ntab, &t) || nent >= 8192)
					fail = true;
				else
				{
					ent[nent].name = n;
					ent[nent].type = t;
					ent[nent].kind = 'V';
					nent++;
					const uint vs = serz_vsize (t);
					serz_str_t sv = { 0, 0 };
					uint val = 0;
					if (vs)
					{
						if (p + vs > sz)
							fail = true;
						else
						{
							val = vs >= 4 ? rd_le32 (raw + p) : vs == 2 ? rd_le16 (raw + p) : raw[p];
							p += vs;
						}
					}
					else if (!serz_ref (raw, sz, &p, tab, &ntab, &sv))
						fail = true;
					if (!fail && have_cur)
					{
						if (serz_is (n, "Name") && vs == 0)
							cur.name = sv;
						else if (serz_is (n, "Format") && vs == 0)
							cur.fmt = sv;
						else if (serz_is (n, "Width") && vs == 2)
							cur.w = val;
						else if (serz_is (n, "Height") && vs == 2)
							cur.h = val;
					}
				}
			}
			else if (k == 'B')
			{
				if (p + 4 > sz || nent >= 8192)
					fail = true;
				else
				{
					const size_t n = rd_le32 (raw + p);
					ent[nent].kind = 'B';
					nent++;
					if (p + 4 + n > sz)
						fail = true;
					else
					{
						if (have_cur && cur.nblob < 2)
						{
							cur.blob[cur.nblob] = raw + p + 4;
							cur.blen[cur.nblob++] = (uint)n;
						}
						p += 4 + n;
					}
				}
			}
			else if (k == 'p')
				p += 2;
			else if (k != 'N')
				fail = true;
		}
		else
		{
			p++;
			if (!b || b > nent)
				fail = true;
			else
			{
				const serz_ent_t *e = &ent[b - 1];
				if (e->kind == 'B')
				{
					if (p + 4 > sz)
						fail = true;
					else
					{
						const size_t n = rd_le32 (raw + p);
						if (p + 4 + n > sz)
							fail = true;
						else
						{
							if (have_cur && cur.nblob < 2)
							{
								cur.blob[cur.nblob] = raw + p + 4;
								cur.blen[cur.nblob++] = (uint)n;
							}
							p += 4 + n;
						}
					}
				}
				else if (e->kind == 'P')
				{
					if (p + 8 > sz)
						fail = true;
					else
					{
						p += 8;
						if (b - 1 == tidx && have_t)
						{
							SERZ_FLUSH ();
							have_cur = true;
						}
					}
				}
				else
				{
					const uint vs = serz_vsize (e->type);
					serz_str_t sv = { 0, 0 };
					uint val = 0;
					if (vs)
					{
						if (p + vs > sz)
							fail = true;
						else
						{
							val = vs >= 4 ? rd_le32 (raw + p) : vs == 2 ? rd_le16 (raw + p) : raw[p];
							p += vs;
						}
					}
					else if (!serz_ref (raw, sz, &p, tab, &ntab, &sv))
						fail = true;
					if (!fail && have_cur)
					{
						if (serz_is (e->name, "Name") && vs == 0)
							cur.name = sv;
						else if (serz_is (e->name, "Format") && vs == 0)
							cur.fmt = sv;
						else if (serz_is (e->name, "Width") && vs == 2)
							cur.w = val;
						else if (serz_is (e->name, "Height") && vs == 2)
							cur.h = val;
					}
				}
			}
		}
		if (fail)
		{
			// resynchronise on the next compact cHcTexture header {entity, id, 6, 0x04 (Width)}
			bool found = false;
			if (have_t)
				for (size_t q = p; q + 10 <= sz; q++)
					if (raw[q] == tidx + 1 && rd_le32 (raw + q + 5) == 6 && raw[q + 9] == 4)
					{
						p = q;
						found = true;
						break;
					}
			if (!found)
				break;
		}
	}
	SERZ_FLUSH ();
	#undef SERZ_FLUSH
	enumError err = ERR_NOTHING_TO_DO;
	if (wrote)
	{
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE SERZ-TG:%s (%u textures) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, wrote, dest);
		err = ERR_OK;
	}
	FREE (tab);
	FREE (ent);
	FREE (raw);
	return err;
}

// BTI header tools and GameCube banner (.bnr) import, ported from GCFT (gclib/bti.py, gcft_ui/bti_tab.py).
//
//   wszst BTI INFO file.(bti|bmd|bdl|bmt)             print the header of every texture
//   wszst BTI SET  in out key=value... [tex=N]         patch header attributes (all textures, or only N)
//   wszst BNR file.bnr [out.bti]                       print banner text, write the 96x32 image as .bti
//
// Keys: wrap_s wrap_t (clamp|repeat|mirror), min_filter mag_filter (nearest|linear|nearest_mipmap_nearest|
// nearest_mipmap_linear|linear_mipmap_nearest|linear_mipmap_linear), alpha, min_lod, max_lod, lod_bias (numbers).
// Only the 0x20-byte header is changed; image data is never re-encoded.

typedef struct bti_loc_t
{
	size_t off; // header offset in file
	char name[0x40];
} bti_loc_t;

static u32 bti_r32 (const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u16 bti_r16 (const u8 *p) { return (u16)(p[0] << 8 | p[1]); }

static int bti_write (ccp path, const u8 *d, size_t n)
{
	FILE *f = fopen (path, "wb");
	if (!f)
		return 1;
	const int bad = fwrite (d, 1, n, f) != n;
	return fclose (f) || bad;
}

// Find all texture headers: a standalone .bti has one at 0, a J3D file has them in TEX1.
// Returns the count (0 = not recognised). Caller frees *out.
static uint bti_locate (const u8 *d, size_t sz, bti_loc_t **out)
{
	*out = 0;
	if (sz >= 0x20 && !memcmp (d, "J3D2", 4))
	{
		for (size_t pos = 0x20; pos + 0x14 <= sz;)
		{
			const u32 ss = bti_r32 (d + pos + 4);
			if (ss < 8 || pos + ss > sz)
				break;
			if (!memcmp (d + pos, "TEX1", 4))
			{
				const uint n = bti_r16 (d + pos + 8);
				const u32 ho = bti_r32 (d + pos + 0xc), no = bti_r32 (d + pos + 0x10);
				if (!n || pos + ho + (size_t)n * 0x20 > sz)
					return 0;
				bti_loc_t *l = CALLOC (n, sizeof (*l));
				for (uint i = 0; i < n; i++)
				{
					l[i].off = pos + ho + (size_t)i * 0x20;
					snprintf (l[i].name, sizeof (l[i].name), "tex%u", i);
					const size_t e = pos + no + 4 + (size_t)i * 4;
					if (no && e + 4 <= sz && bti_r16 (d + pos + no) == n)
					{
						const size_t s = pos + no + bti_r16 (d + e + 2);
						if (s < sz)
							snprintf (l[i].name, sizeof (l[i].name), "%.*s", (int)(sz - s < 0x3f ? sz - s : 0x3f), d + s);
					}
				}
				*out = l;
				return n;
			}
			pos += ss;
		}
		return 0;
	}
	if (sz >= 0x20 && bti_r16 (d + 2) && bti_r16 (d + 4) && bti_r32 (d + 0x1c) >= 0x20)
	{
		bti_loc_t *l = CALLOC (1, sizeof (*l));
		snprintf (l->name, sizeof (l->name), "image");
		*out = l;
		return 1;
	}
	return 0;
}

static ccp bti_fmt (uint f)
{
	static ccp n[] = { "I4", "I8", "IA4", "IA8", "RGB565", "RGB5A3", "RGBA32", "?7", "C4", "C8", "C14X2" };
	return f < 11 ? n[f] : "?";
}

static ccp const bti_wrap_n[] = { "clamp", "repeat", "mirror" };
static ccp const bti_filt_n[] = { "nearest", "linear", "nearest_mipmap_nearest", "nearest_mipmap_linear",
	"linear_mipmap_nearest", "linear_mipmap_linear" };

static int bti_val (ccp s, ccp const *names, uint n, int *v)
{
	for (uint i = 0; i < n; i++)
		if (!strcasecmp (s, names[i]))
			return *v = (int)i, 0;
	char *e;
	const long x = strtol (s, &e, 0);
	return *s && !*e ? (*v = (int)x, 0) : 1;
}

static enumError cmd_bti ()
{
	ParamList_t *pa = first_param;
	if (!pa || !pa->next)
		return ERROR0 (ERR_MISSING_PARAM, "Usage: wszst BTI INFO file | BTI SET in out key=value...\n");
	const bool set = !strcasecmp (pa->arg, "SET");
	if (!set && strcasecmp (pa->arg, "INFO"))
		return ERROR0 (ERR_SYNTAX, "Unknown BTI action (use INFO or SET): %s\n", pa->arg);
	ccp in = pa->next->arg;
	u8 *d = 0;
	size_t sz = 0;
	if (LoadFileAlloc (in, 0, 0, &d, &sz, 0, 0, 0, false) || !d)
		return ERROR1 (ERR_CANT_OPEN, "Can't read: %s\n", in);
	bti_loc_t *loc;
	const uint n = bti_locate (d, sz, &loc);
	if (!n)
	{
		FREE (d);
		return ERROR0 (ERR_INVALID_DATA, "No BTI texture header found (need .bti, J3D model or BMT): %s\n", in);
	}
	enumError err = ERR_OK;
	if (!set)
	{
		printf ("\n#   name                 fmt      size     wrap(s,t)      min/mag filter  mips alpha lodbias\n");
		for (uint i = 0; i < n; i++)
		{
			const u8 *h = d + loc[i].off;
			printf ("%-3u %-20s %-8s %4ux%-4u %-6s %-6s %u/%u %u    %-3u  %d\n", i, loc[i].name,
				bti_fmt (h[0]), bti_r16 (h + 2), bti_r16 (h + 4), h[6] < 3 ? bti_wrap_n[h[6]] : "?",
				h[7] < 3 ? bti_wrap_n[h[7]] : "?", h[0x14], h[0x15], h[0x18], h[1], (int)(s16)bti_r16 (h + 0x1a));
		}
		goto done;
	}
	if (!pa->next->next)
	{
		err = ERROR0 (ERR_MISSING_PARAM, "Missing output file!\n");
		goto done;
	}
	{
		ccp out = pa->next->next->arg;
		int only = -1;
		for (ParamList_t *q = pa->next->next->next; q; q = q->next)
			if (!strncasecmp (q->arg, "tex=", 4))
			{
				only = atoi (q->arg + 4);
				if (only < 0 || only >= (int)n)
				{
					err = ERROR0 (ERR_SEMANTIC, "Texture index out of range (0..%u): %s\n", n - 1, q->arg);
					goto done;
				}
			}
		for (ParamList_t *q = pa->next->next->next; q && !err; q = q->next)
		{
			char *eq = strchr (q->arg, '=');
			if (!eq || !strncasecmp (q->arg, "tex=", 4))
				continue;
			char key[32];
			snprintf (key, sizeof (key), "%.*s", (int)(eq - q->arg < 31 ? eq - q->arg : 31), q->arg);
			int v = 0, bad;
			uint off, w = 1;
			if (!strcasecmp (key, "wrap_s") || !strcasecmp (key, "wrap_t"))
				off = key[5] == 's' || key[5] == 'S' ? 6 : 7, bad = bti_val (eq + 1, bti_wrap_n, 3, &v) || v < 0 || v > 2;
			else if (!strcasecmp (key, "min_filter") || !strcasecmp (key, "mag_filter"))
				off = !strcasecmp (key, "min_filter") ? 0x14 : 0x15, bad = bti_val (eq + 1, bti_filt_n, 6, &v) || v < 0 || v > 5;
			else if (!strcasecmp (key, "alpha"))
				off = 1, bad = bti_val (eq + 1, 0, 0, &v) || v < 0 || v > 255;
			else if (!strcasecmp (key, "min_lod"))
				off = 0x16, bad = bti_val (eq + 1, 0, 0, &v) || v < 0 || v > 255;
			else if (!strcasecmp (key, "max_lod"))
				off = 0x17, bad = bti_val (eq + 1, 0, 0, &v) || v < 0 || v > 255;
			else if (!strcasecmp (key, "lod_bias"))
				off = 0x1a, w = 2, bad = bti_val (eq + 1, 0, 0, &v) || v < -32768 || v > 32767;
			else
			{
				err = ERROR0 (ERR_SYNTAX, "Unknown BTI key: %s\n", key);
				break;
			}
			if (bad)
			{
				err = ERROR0 (ERR_SEMANTIC, "Invalid value: %s\n", q->arg);
				break;
			}
			for (uint i = 0; i < n; i++)
				if (only < 0 || (uint)only == i)
				{
					u8 *h = d + loc[i].off;
					if (w == 2)
						h[off] = (u8)(v >> 8), h[off + 1] = (u8)v;
					else
						h[off] = (u8)v;
				}
		}
		if (!err)
		{
			if (bti_write (out, d, sz))
				err = ERROR1 (ERR_CANT_CREATE, "Can't write file: %s\n", out);
			else
				printf ("BTI SET %s: %u texture header(s) patched\n", out, only < 0 ? n : 1);
		}
	}
done:
	FREE (loc);
	FREE (d);
	return err;
}

static void bti_print_text (ccp label, const u8 *p, uint len)
{
	char b[0x81];
	uint i = 0;
	for (; i < len && p[i] && i < 0x80; i++)
		b[i] = p[i] >= 0x20 && p[i] < 0x7f ? (char)p[i] : (p[i] == '\n' ? ' ' : '?');
	b[i] = 0;
	printf ("%-14s %s\n", label, b);
}

static enumError cmd_bnr ()
{
	if (!first_param)
		return ERROR0 (ERR_MISSING_PARAM, "Usage: wszst BNR file.bnr [out.bti]\n");
	ccp in = first_param->arg;
	u8 *d = 0;
	size_t sz = 0;
	if (LoadFileAlloc (in, 0, 0, &d, &sz, 0, 0, 0, false) || !d)
		return ERROR1 (ERR_CANT_OPEN, "Can't read: %s\n", in);
	const bool b2 = sz == 0x1de0 && !memcmp (d, "BNR2", 4);
	if (!b2 && !(sz == 0x1960 && !memcmp (d, "BNR1", 4)))
	{
		FREE (d);
		return ERROR0 (ERR_INVALID_DATA, "Not a GameCube banner (need BNR1, 0x1960 bytes, or BNR2, 0x1de0 bytes): %s\n", in);
	}
	for (uint i = 0; i < (b2 ? 6u : 1u); i++)
	{
		const u8 *t = d + 0x1820 + i * 0x140;
		if (b2)
			printf ("\nBanner text #%u\n", i);
		bti_print_text ("short name:", t, 0x20);
		bti_print_text ("short maker:", t + 0x20, 0x20);
		bti_print_text ("full name:", t + 0x40, 0x40);
		bti_print_text ("full maker:", t + 0x80, 0x40);
		bti_print_text ("description:", t + 0xc0, 0x80);
	}
	enumError err = ERR_OK;
	if (first_param->next)
	{
		u8 *b = CALLOC (0x20 + 0x1800, 1);
		b[0] = 5; // RGB5A3
		b[3] = 96;
		b[5] = 32;
		b[0x1f] = 0x20; // image data offset
		memcpy (b + 0x20, d + 0x20, 0x1800);
		if (bti_write (first_param->next->arg, b, 0x1820))
			err = ERROR1 (ERR_CANT_CREATE, "Can't write file: %s\n", first_param->next->arg);
		else
			printf ("\nBNR image (96x32 RGB5A3) -> %s\n", first_param->next->arg);
		FREE (b);
	}
	FREE (d);
	return err;
}

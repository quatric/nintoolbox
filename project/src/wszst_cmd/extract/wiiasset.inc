// "We Wish You a Merry Christmas" (Wii) compiled assets "ASSETS/<id>" (8 digits, no extension), big-endian: {u16 container version 1, u16 asset version, char type[4]}, followed by a type
// specific header. Decoded types: "SND\0" {u32 0x28, u32 bytes after 0x38 (header 0x60 + ADPCM), f32 volume, u32 rate, u32 channels 1, ..., 0x60-byte DSP header at 0x38 (coefficients at +0x1c), ADPCM data at 0x98} -> WAV;
// "BMP\0" {u32 0xc, u32 data bytes (mip chain), u16 format (3 = CMPR, 6 = RGB565 (best guess), 7 = RGB5A3), u16 width, u16 height, u16 mip count, u32 flags, data at 0x1c} -> base level PNG.
// Output goes to "<file>.d/<name>.wav|png"; <name> comes from the AssetMap.xml next to the ASSETS directory ({id="N" ... name="X"}), else the file name.
// GEOM, ANIM, MAT, LOAD and BIN assets are not decoded.
static bool wiiasset_name (ccp arg, char *out, size_t out_size)
{
	char dir[PATH_MAX], map[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s%s", strchr (arg, '/') ? "" : "./", arg);
	char *sl = strrchr (dir, '/');
	*sl = 0;
	snprintf (map, sizeof (map), "%s/../AssetMap.xml", dir);
	u8 *x = 0;
	size_t xs = 0;
	if (LoadFileAlloc (map, 0, 0, &x, &xs, 0, 0, 0, false) || !x)
	{
		FREE (x);
		return false;
	}
	char key[64];
	snprintf (key, sizeof (key), " id=\"%u\" ", (uint)strtoul (sl + 1, 0, 10));
	bool ok = false;
	char *t = MALLOC (xs + 1);
	if (t)
	{
		memcpy (t, x, xs);
		t[xs] = 0;
		char *p = strstr (t, key);
		if (p)
		{
			char *e = strchr (p, '>');
			char *n = strstr (p, " name=\"");
			if (n && e && n < e)
			{
				n += 7;
				char *q = strchr (n, '"');
				if (q)
				{
					*q = 0;
					for (char *c = n; *c; c++)
						if (*c == '/' || *c == '\\' || (u8)*c < 0x20)
							*c = '_';
					snprintf (out, out_size, "%s", n);
					ok = n[0] != 0;
				}
			}
		}
		FREE (t);
	}
	FREE (x);
	return ok;
}

static enumError extract_wiiasset_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	if (strlen (base) != 8 || strspn (base, "0123456789") != 8)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	s16 *pcm = 0;
	const bool snd = sz > 0x98 && !memcmp (raw + 4, "SND\0", 4) && rd_be16 (raw) == 1;
	const bool bmp = sz > 0x1c && !memcmp (raw + 4, "BMP\0", 4) && rd_be16 (raw) == 1;
	if (!snd && !bmp)
		goto done;
	{
		char nm[0x100], dest[PATH_MAX], out[PATH_MAX];
		if (!wiiasset_name (arg, nm, sizeof (nm)))
			snprintf (nm, sizeof (nm), "%s", base);
		snprintf (dest, sizeof (dest), "%s.d", arg);
		if (snd)
		{
			const u32 db = rd_be32 (raw + 0x0c), rate = rd_be32 (raw + 0x14), nib = rd_be32 (raw + 0x38 + 4);
			if (rd_be32 (raw + 8) != 0x28 || rd_be32 (raw + 0x18) != 1 || 0x38 + (u64)db > sz || db <= 0x60 || rate < 4000 || rate > 96000 || (u64)nib / 2 > db - 0x5f)
				goto done;
			const size_t fr = (db - 0x60 + 7) / 8;
			s16 coef[16];
			for (uint k = 0; k < 16; k++)
				coef[k] = (s16)rd_be16 (raw + 0x38 + 0x1c + 2 * k);
			int h1 = (s16)rd_be16 (raw + 0x38 + 0x40), h2 = (s16)rd_be16 (raw + 0x38 + 0x42);
			pcm = MALLOC (fr * 14 * sizeof (s16));
			if (!pcm)
				goto done;
			size_t ns = 0;
			for (size_t f = 0; f < fr && ns < nib * 14 / 16 + 14; f++)
			{
				const u8 *src = raw + 0x98 + f * 8;
				if (0x98 + f * 8 + 8 > sz)
					break;
				const int sc = 1 << (src[0] & 15), ci = (src[0] >> 4) & 7;
				for (uint k = 0; k < 14; k++)
				{
					int v = (src[1 + k / 2] >> ((k & 1) ? 0 : 4)) & 15;
					if (v >= 8)
						v -= 16;
					int s = (v * sc * 2048 + 1024 + coef[2 * ci] * h1 + coef[2 * ci + 1] * h2) >> 11;
					s = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
					h2 = h1;
					h1 = s;
					pcm[ns++] = (s16)s;
				}
			}
			const uint cnt = rd_be32 (raw + 0x1c) < ns ? rd_be32 (raw + 0x1c) : (uint)ns;
			snprintf (out, sizeof (out), "%s.wav", nm);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE ASSET-SND:%s (%u samples) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, cnt, dest, out);
			err = ERR_OK;
			if (!testmode)
			{
				size_t wn = 0;
				u8 *w = nds_strm_make_wav (pcm, cnt, 1, rate, &wn);
				err = w ? shade_write (arg, dest, basedir, out, w, (uint)wn) : ERR_OUT_OF_MEMORY;
				FREE (w);
			}
		}
		else
		{
			const uint f = rd_be16 (raw + 0x10), w = rd_be16 (raw + 0x12), h = rd_be16 (raw + 0x14), ds = rd_be32 (raw + 0x0c);
			const uint gx = f == 3 ? 0xe : f == 6 ? 4 : f == 7 ? 5 : 0;
			if (!gx || !w || !h || (w & 7) || (h & 7) || 0x1c + (u64)ds > sz || ds < (gx == 0xe ? w * h / 2 : w * h * 2))
				goto done;
			snprintf (out, sizeof (out), "%s/%s.png", dest, nm);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE ASSET-BMP:%s (%ux%u) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h, out);
			err = ERR_OK;
			if (!testmode)
			{
				CreatePath (dest, true);
				err = wiitex_write_raw (out, w, h, gx, raw + 0x1c, ds);
			}
		}
	}
done:
	FREE (pcm);
	FREE (raw);
	return err;
}

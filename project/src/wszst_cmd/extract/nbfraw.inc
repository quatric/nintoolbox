// NITRO raw graphics triple (".nbfc" characters, ".nbfp" palette, ".nbfs" screen) of about 1,800 DS games: headerless dumps of the tile data (4 or 8 bit, 8x8 tiles), a little endian BGR555 palette and
// a little endian u16 tile map (tile 0-9, h flip 10, v flip 11, palette bank 12-15, 32 tiles wide). The three files share one base name, so a ".nbfc" is rendered together with its ".nbfp" (required) and ".nbfs" (optional);
// without a map the tiles are shown as a 32 tiles wide sheet. Bit depth: 4 bit for palettes of 16 colors or maps that use palette banks, else 8 bit; a bank beyond the palette falls back to bank 0.
// Written as "<name>.png".
static u8 *nbfraw_load (ccp arg, ccp ext, size_t *sz)
{
	char path[PATH_MAX];
	u8 *d = 0;
	snprintf (path, sizeof (path), "%s", arg);
	char *dot = strrchr (path, '.');
	if (!dot || strlen (dot) != 5 || strlen (ext) != 5)
		return 0;
	for (uint up = 0; up < 2; up++)
	{
		for (uint i = 0; i < 5; i++)
			dot[i] = up ? toupper ((u8)ext[i]) : ext[i];
		if (!LoadFileAlloc (path, 0, 0, &d, sz, 0, 0, 0, false) && d)
			return d;
		FREE (d);
		d = 0;
	}
	return 0;
}

static enumError extract_nbfraw_file (ccp arg, ccp basedir, uint depth)
{
	ccp dot = strrchr (arg, '.');
	if (!dot || strcasecmp (dot, ".nbfc"))
		return ERR_NOTHING_TO_DO;
	u8 *tiles = 0, *pal = 0, *map = 0, *rgba = 0;
	size_t tsz = 0, psz = 0, msz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &tiles, &tsz, 0, 0, 0, false) || !tiles || tsz < 32 || tsz > 0x400000)
		goto done;
	pal = nbfraw_load (arg, ".nbfp", &psz);
	if (!pal || psz < 2 || psz > 0x200)
		goto done;
	map = nbfraw_load (arg, ".nbfs", &msz);
	const uint ncol = (uint)psz / 2, nent = (uint)msz / 2;
	bool banks = false;
	for (uint i = 0; i < nent; i++)
		if (rd_le16 (map + 2 * i) >> 12)
			banks = true;
	const uint bpp = ncol <= 16 || banks ? 4 : 8, tb = bpp * 8;
	uint wt, ht;
	if (nent >= 16 && nent <= 4096)
	{
		wt = nent % 32 == 0 ? 32 : (nent % 16 == 0 ? 16 : 0);
		if (!wt)
			goto done;
		ht = nent / wt;
	}
	else
	{
		FREE (map);
		map = 0;
		const uint nt = (uint)(tsz / tb);
		wt = 32;
		ht = (nt + 31) / 32;
	}
	if (!ht || ht > 128)
		goto done;
	rgba = CALLOC (4, (size_t)wt * 8 * ht * 8);
	if (!rgba)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	for (uint i = 0; i < wt * ht; i++)
	{
		const uint e = map ? rd_le16 (map + 2 * i) : i, t = map ? e & 0x3ff : i, hf = map ? e >> 10 & 1 : 0, vf = map ? e >> 11 & 1 : 0, bk = map ? e >> 12 : 0;
		for (uint y = 0; y < 8; y++)
			for (uint x = 0; x < 8; x++)
			{
				const uint sy = vf ? 7 - y : y, sx = hf ? 7 - x : x;
				const size_t o = (size_t)t * tb + (bpp == 4 ? sy * 4 + sx / 2 : sy * 8 + sx);
				if (o >= tsz)
					continue;
				uint v = tiles[o];
				if (bpp == 4)
					v = sx & 1 ? v >> 4 : v & 15;
				uint ci = v + (bpp == 4 ? bk * 16 : 0);
				if (ci >= ncol)
					ci = v;
				if (ci >= ncol)
					continue;
				const uint c = rd_le16 (pal + 2 * ci);
				u8 *q = rgba + 4 * ((size_t)((i / wt) * 8 + y) * wt * 8 + (i % wt) * 8 + x);
				q[0] = (c & 31) * 255 / 31;
				q[1] = (c >> 5 & 31) * 255 / 31;
				q[2] = (c >> 10 & 31) * 255 / 31;
				q[3] = 255;
			}
	}
	{
		const char *sl = strrchr (arg, '/');
		char name[PATH_MAX];
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *e = strrchr (name, '.');
		if (e)
			*e = 0;
		err = kyl_write_rgba_png (arg, basedir, name, (uint)-1, wt * 8, ht * 8, rgba);
	}
done:
	FREE (rgba);
	FREE (map);
	FREE (pal);
	FREE (tiles);
	return err;
}

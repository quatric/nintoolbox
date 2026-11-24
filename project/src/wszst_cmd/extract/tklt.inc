// "TKLT" tile sets (".atk" of the Battle of Giants engine, about 40 DS games): {char "TKLT", u16 4 bit tile count, u16 8 bit tile count, u8 flag, u8 mode..., u32 length + palette path
// ("2d/palette/..."), tiles}, ending exactly after the 8x8 4 bit tiles (32 bytes each) and then the 8 bit ones (64 bytes each) in this layout; "VNKT" (Battle of Giants: Dragons) has the same header and then {u32 size, Nintendo LZ10 stream of count * 32 bytes}.
// The TTLP palette is found under the "2d" folder of the file's own path. Written as a 16 tiles wide sheet "<name>.png".

// Builds "<folder holding 2d/>/<rel>" for a file somewhere below a "2d" folder; false when PATH is not below one.
static bool tklt_path (char *out, size_t outsz, ccp path, const u8 *rel, size_t rl)
{
	ccp r = !strncmp (path, "2d/", 3) ? path - 1 : strstr (path, "/2d/");
	if (!r)
		return false;
	snprintf (out, outsz, "%.*s%s%.*s", r < path ? 0 : (int)(r - path), path, r < path ? "" : "/", (int)rl, (ccp)rel);
	return true;
}

// Nintendo LZ10 on a zero-padded copy: the last back reference of some VNKT/VNFP streams lacks its second byte at the end of the file.
static bool tklt_lz10 (const u8 *src, size_t n, u8 **out, size_t *outn)
{
	u8 *pad = CALLOC (1, n + 4);
	size_t used;
	memcpy (pad, src, n);
	const bool ok = packlz_lz10 (pad, n + 4, &used, out, outn);
	FREE (pad);
	return ok;
}

// Loads the tiles of a TKLT/VNKT file (malloc'd: *n4 4 bit tiles of 32 bytes, then *n8 8 bit tiles of 64 bytes) and its TTLP palette (malloc'd file, *ncol colours from +8).
static bool tklt_load (ccp path, u8 **tiles, size_t *n4, size_t *n8, u8 **pal, uint *ncol)
{
	u8 *raw = 0;
	size_t sz = 0, psz = 0;
	*tiles = *pal = 0;
	if (LoadFileAlloc (path, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return false;
	bool ok = false;
	const bool lz = sz >= 0x30 && !memcmp (raw, "VNKT", 4);
	if (sz < 0x30 || (!lz && memcmp (raw, "TKLT", 4)))
		goto done;
	*n4 = rd_le16 (raw + 4);
	*n8 = rd_le16 (raw + 6);
	const size_t pl = rd_le32 (raw + 0x0d), data = 0x11 + pl, bytes = *n4 * 32 + *n8 * 64;
	if (!bytes || pl < 8 || pl > 200 || data + 4 > sz)
		goto done;
	if (lz)
	{
		size_t n;
		if (rd_le32 (raw + data) > sz - data - 4 || !tklt_lz10 (raw + data + 4, sz - data - 4, tiles, &n) || n != bytes)
			goto done;
	}
	else
	{
		if (data + bytes != sz)
			goto done;
		*tiles = MALLOC (bytes);
		memcpy (*tiles, raw + data, bytes);
	}
	char pp[PATH_MAX];
	if (!tklt_path (pp, sizeof (pp), path, raw + 0x11, pl))
		goto done;
	if (LoadFileAlloc (pp, 0, 0, pal, &psz, 0, 0, 0, false) || !*pal || psz < 10 || memcmp (*pal, "TTLP", 4))
		goto done;
	size_t pc = rd_le16 (*pal + 4);
	if (pc > psz - 8)
		pc = psz - 8;
	*ncol = (uint)pc / 2;
	ok = true;
done:
	if (!ok)
	{
		FREE (*tiles);
		FREE (*pal);
		*tiles = *pal = 0;
	}
	FREE (raw);
	return ok;
}

// Draws tile T (4 bit tiles first, then 8 bit ones; 4 bit tiles use palette bank BANK) flipped as asked at pixel X, Y of a W wide RGBA image; colour 0 stays transparent.
static void tklt_draw (u8 *rgba, uint W, uint X, uint Y, const u8 *tiles, size_t n4, size_t n8, size_t t, const u8 *pal, uint ncol, uint bank, bool hf, bool vf)
{
	if (t >= n4 + n8)
		return;
	const bool b8 = t >= n4;
	const u8 *tile = b8 ? tiles + n4 * 32 + (t - n4) * 64 : tiles + t * 32;
	for (uint y = 0; y < 8; y++)
		for (uint x = 0; x < 8; x++)
		{
			const uint sx = hf ? 7 - x : x, sy = vf ? 7 - y : y;
			const uint b = b8 ? tile[sy * 8 + sx] : tile[sy * 4 + sx / 2], c = b8 ? b : sx & 1 ? b >> 4 : b & 15, ci = b8 ? c : bank * 16 + c;
			if (!c || ci >= ncol)
				continue;
			const uint v = rd_le16 (pal + 8 + 2 * ci);
			u8 *q = rgba + 4 * ((size_t)(Y + y) * W + X + x);
			q[0] = (v & 31) * 255 / 31;
			q[1] = (v >> 5 & 31) * 255 / 31;
			q[2] = (v >> 10 & 31) * 255 / 31;
			q[3] = 255;
		}
}

static enumError extract_tklt_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *tiles = 0, *pal = 0, *rgba = 0;
	size_t n4 = 0, n8 = 0;
	uint ncol = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (!tklt_load (arg, &tiles, &n4, &n8, &pal, &ncol))
		return ERR_NOTHING_TO_DO;
	const size_t nt = n4 + n8;
	const uint W = 16 * 8, H = (uint)((nt + 15) / 16) * 8;
	rgba = CALLOC (4, (size_t)W * H);
	if (!rgba)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	for (size_t t = 0; t < nt; t++)
		tklt_draw (rgba, W, (uint)(t % 16) * 8, (uint)(t / 16) * 8, tiles, n4, n8, t, pal, ncol, 0, false, false);
	{
		const char *sl = strrchr (arg, '/');
		char name[PATH_MAX];
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *e = strrchr (name, '.');
		if (e)
			*e = 0;
		err = kyl_write_rgba_png (arg, basedir, name, (uint)-1, W, H, rgba);
	}
done:
	FREE (rgba);
	FREE (pal);
	FREE (tiles);
	return err;
}

// "VNFP" playfields (".apl", Battle of Giants: Dragons; little endian): {char "VNFP", u16 3, u16 flags, u32 length + tile set path ("2d/tilekit/....atk"), 5 bytes, then records
// {u8 1, u8 kind, u8 1, u16 width, u16 height (tiles), ...}}. Kind 5 is a BG layer {u8 layer, u8 0, u8 flag, u32 size, Nintendo LZ10 stream of width * height u16 map entries
// (tile | 0x400 h-flip | 0x800 v-flip | palette bank << 12)}; kind 1 {u32 size, stream in a codec not identified yet (also used by the older "D2FP" playfields)} ends the walk.
// Each BG layer is drawn with the tile set and its TTLP palette to "<name>_bgN.png".
static enumError extract_vnfp_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".apl") || !file_has_magic (arg, "VNFP", 4))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *tiles = 0, *pal = 0;
	size_t sz = 0, n4 = 0, n8 = 0;
	uint ncol = 0, wrote = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x20)
		goto done;
	const size_t pl = rd_le32 (raw + 8);
	char tp[PATH_MAX];
	if (pl < 8 || pl > 200 || 12 + pl + 5 > sz || !tklt_path (tp, sizeof (tp), arg, raw + 12, pl))
		goto done;
	if (!tklt_load (tp, &tiles, &n4, &n8, &pal, &ncol))
		goto done;
	const char *sl = strrchr (arg, '/');
	char name[PATH_MAX];
	snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
	char *e = strrchr (name, '.');
	if (e)
		*e = 0;
	for (size_t o = 12 + pl + 5; o + 14 <= sz && raw[o] == 1 && raw[o + 1] == 5 && raw[o + 2] == 1;)
	{
		const uint w = rd_le16 (raw + o + 3), h = rd_le16 (raw + o + 5), layer = raw[o + 7], b8 = raw[o + 9] == 1;
		const size_t len = rd_le32 (raw + o + 10);
		u8 *map = 0;
		size_t n;
		if (!w || !h || w > 256 || h > 256 || len > sz - o - 14 || !tklt_lz10 (raw + o + 14, len, &map, &n) || n < (size_t)w * h * 2)
		{
			FREE (map);
			break;
		}
		o += 14 + len;
		u8 *rgba = CALLOC (4, (size_t)w * 8 * h * 8);
		for (uint i = 0; rgba && i < w * h; i++)
		{
			// VRAM layout: 4 bit tiles from character 1 (character 0 stays blank), 8 bit tiles from character 512
			const uint v = rd_le16 (map + 2 * i), t = v & 0x3ff;
			if (b8 ? t >= 512 : t >= 1)
				tklt_draw (rgba, w * 8, i % w * 8, i / w * 8, tiles, n4, n8, b8 ? n4 + t - 512 : t - 1, pal, ncol, v >> 12, v & 0x400, v & 0x800);
		}
		FREE (map);
		char nm[PATH_MAX];
		snprintf (nm, sizeof (nm), "%s_bg%u", name, layer);
		if (rgba && !kyl_write_rgba_png (arg, basedir, nm, (uint)-1, w * 8, h * 8, rgba))
			wrote++;
		FREE (rgba);
	}
	if (wrote)
		err = ERR_OK;
done:
	FREE (pal);
	FREE (tiles);
	FREE (raw);
	return err;
}

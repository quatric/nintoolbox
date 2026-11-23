// "TKLT" tile sets (".atk" of the Battle of Giants engine, about 40 DS games): {char "TKLT", u32 tile count, u8 flag, u8 mode..., u32 length + palette path ("2d/palette/..."), tiles}, ending
// exactly after count 8x8 4 bit tiles (32 bytes each) in this layout; the TTLP palette is found under the "2d" folder of the file's own path. Written as a 16 tiles wide sheet "<name>.png".
// The matching ".apl" playfields (tile maps) are not decoded.
static enumError extract_tklt_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0, *pal = 0, *rgba = 0;
	size_t sz = 0, psz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0x30 || memcmp (raw, "TKLT", 4))
		goto done;
	const size_t nt = rd_le32 (raw + 4), pl = rd_le32 (raw + 0x0d), data = 0x11 + pl;
	if (!nt || nt > 4096 || pl < 8 || pl > 200 || data + nt * 32 != sz)
		goto done;
	char path[PATH_MAX];
	ccp r = strstr (arg, "/2d/");
	if (!r)
		goto done;
	snprintf (path, sizeof (path), "%.*s/%.*s", (int)(r - arg), arg, (int)pl, (ccp)raw + 0x11);
	if (LoadFileAlloc (path, 0, 0, &pal, &psz, 0, 0, 0, false) || !pal || psz < 10 || memcmp (pal, "TTLP", 4))
		goto done;
	size_t pc = rd_le16 (pal + 4);
	if (pc > psz - 8)
		pc = psz - 8;
	const uint ncol = (uint)pc / 2, W = 16 * 8, H = (uint)((nt + 15) / 16) * 8;
	rgba = CALLOC (4, (size_t)W * H);
	if (!rgba)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	for (size_t t = 0; t < nt; t++)
		for (uint y = 0; y < 8; y++)
			for (uint x = 0; x < 8; x++)
			{
				const uint b = raw[data + t * 32 + y * 4 + x / 2], c = x & 1 ? b >> 4 : b & 15;
				if (!c || c >= ncol)
					continue;
				const uint v = rd_le16 (pal + 8 + 2 * c);
				u8 *q = rgba + 4 * ((size_t)((t / 16) * 8 + y) * W + (t % 16) * 8 + x);
				q[0] = (v & 31) * 255 / 31;
				q[1] = (v >> 5 & 31) * 255 / 31;
				q[2] = (v >> 10 & 31) * 255 / 31;
				q[3] = 255;
			}
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
	FREE (raw);
	return err;
}

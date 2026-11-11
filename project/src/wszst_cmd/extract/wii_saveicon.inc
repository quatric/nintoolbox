// Wii save game images stored as plain files: "savebanner.raw" (192x64) and "saveicon<N>.raw" (48x48), RGB5A3 in the usual GX 4x4 tiling
// with no header (24576 and 4608 bytes). Written as "<file>.d/<name>.png" through a synthetic TPL (Cocoto Festival ships them in its archive).
static enumError extract_wii_saveicon_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".raw") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	const char *sl = strrchr (arg, '/');
	const char *base = sl ? sl + 1 : arg;
	const bool banner = !strcasecmp (base, "savebanner.raw");
	const bool icon = !strncasecmp (base, "saveicon", 8) && base[8] >= '0' && base[8] <= '9' && !strcasecmp (base + 9, ".raw");
	if (!banner && !icon)
		return ERR_NOTHING_TO_DO;
	const uint w = banner ? 192 : 48, h = banner ? 64 : 48;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz != (size_t)w * h * 2)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	u8 *tpl = CALLOC (1, 0x40 + sz);
	if (!tpl)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	u8 *p = tpl;
#define PUT32(v) do { const uint v_ = (v); *p++ = v_ >> 24; *p++ = v_ >> 16; *p++ = v_ >> 8; *p++ = v_; } while (0)
	PUT32 (0x0020af30); PUT32 (1); PUT32 (0x0c);
	PUT32 (0x14); PUT32 (0);
	*p++ = h >> 8; *p++ = h; *p++ = w >> 8; *p++ = w;
	PUT32 (5); PUT32 (0x40); PUT32 (0); PUT32 (0); PUT32 (1); PUT32 (1); PUT32 (0);
#undef PUT32
	memcpy (tpl + 0x40, raw, sz);
	char stem[PATH_MAX], fn[PATH_MAX], dest[PATH_MAX], tmp[PATH_MAX], tmp_png[PATH_MAX], png[PATH_MAX];
	snprintf (stem, sizeof (stem), "%s", base);
	char *dot = strrchr (stem, '.');
	if (dot)
		*dot = 0;
	snprintf (fn, sizeof (fn), "%s.tmp.tpl", stem);
	beside_source_dest (dest, sizeof (dest), arg);
	snprintf (tmp, sizeof (tmp), "%s/%s%s", dest, basedir ? basedir : "", fn);
	snprintf (tmp_png, sizeof (tmp_png), "%s.png", tmp);
	snprintf (png, sizeof (png), "%s/%s%s.png", dest, basedir ? basedir : "", stem);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE WII SAVE IMAGE:%s (%ux%u) -> PNG:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h, png);
	enumError err = ERR_OK;
	if (!testmode)
	{
		err = shade_write (arg, dest, basedir, fn, tpl, 0x40 + (uint)sz);
		if (!err)
			err = decode_image_if_possible (tmp);
		if (!err && rename (tmp_png, png))
			err = ERROR1 (ERR_CANT_CREATE, "Can't create PNG: %s\n", png);
		unlink (tmp);
	}
	FREE (tpl);
	FREE (raw);
	return err;
}

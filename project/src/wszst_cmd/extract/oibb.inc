// "OIBB" object pack (Doctor Fizzwizzle's Animal Rescue `*.pak`, SPG "AxEngine"): "OIBB", u32 LE version 4, offset
// tables, then a memory image of the game's objects. Textures are "TXTR" objects: the tag, then big-endian
// {u32 object size, u32 pixel bytes, u32 (pointer, ignored), u32 width, u32 height, u32 1, u32 GX format (9 = C8,
// 8 = C4, 6 = RGBA8), u32 1, u32 palette bytes, ...}; the object is 0x40 header + palette (RGB5A3, 256 / 16
// entries) + GX tiles (C8 8x4, C4 8x8, RGBA8 4x4, width/height padded to the tile size), size = 0x40 + palette +
// pixel bytes. Embedded RIFF/WAVE files are carved out as "snd_NNNN.wav". Every texture is written as "tex_NNNN_<w>x<h>.png"; other objects are not decoded.
static enumError extract_oibb_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pak"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x100 || sz > 0x20000000 || memcmp (raw, "OIBB", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	uint n = 0;
	for (size_t o = 0x20; o + 0x40 <= sz; o += 4)
	{
		if (memcmp (raw + o, "TXTR", 4))
			continue;
		const u32 osz = rd_be32 (raw + o + 4), ds = rd_be32 (raw + o + 8), w = rd_be32 (raw + o + 0x10),
			h = rd_be32 (raw + o + 0x14), fmt = rd_be32 (raw + o + 0x1c), pb = rd_be32 (raw + o + 0x24);
		if (!w || !h || w > 4096 || h > 4096 || (fmt != 6 && fmt != 8 && fmt != 9) || pb > 512 || osz != 0x40 + pb + (u64)ds
			|| o + osz > sz)
			continue;
		const uint tw = fmt == 6 ? 4 : 8, th = fmt == 8 ? 8 : 4;
		const uint pw = (w + tw - 1) / tw * tw, ph = (h + th - 1) / th * th;
		const size_t need = fmt == 6 ? (size_t)pw * ph * 4 : fmt == 8 ? (size_t)pw * ph / 2 : (size_t)pw * ph;
		if (need > ds || (fmt != 6 && !pb))
			continue;
		if (n == 0 && (verbose >= 0 || testmode))
			fprintf (stdlog, "%s%sEXTRACT OIBB:%s -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
		if (testmode)
		{
			n++;
			continue;
		}
		u8 *rgba = 0;
		const u8 *pal = raw + o + 0x40, *pix = pal + pb;
		if (DecodeGXTexture_RGBA (&rgba, pw, ph, fmt, pix, ds, fmt == 6 ? 0 : pal, fmt == 6 ? 0 : pb / 2, 2) || !rgba)
		{
			FREE (rgba);
			continue;
		}
		if (pw != w)
			for (uint y = 1; y < h; y++)
				memmove (rgba + (size_t)y * w * 4, rgba + (size_t)y * pw * 4, (size_t)w * 4);
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%stex_%04u_%ux%u.png", dest, basedir ? basedir : "", n, w, h);
		if (!SaveDecodedRGBAToPNG (rgba, w, h, &be_func, path, 0, true))
			n++;
		o += osz - 4;
	}
	// embedded little-endian RIFF/WAVE files (sounds.pak: complete PCM wave files inside the object image)
	uint nw = 0;
	for (size_t o = 0x20; !testmode && o + 44 <= sz; o += 4)
	{
		if (memcmp (raw + o, "RIFF", 4) || memcmp (raw + o + 8, "WAVEfmt ", 8))
			continue;
		const u64 tot = (u64)rd_le32 (raw + o + 4) + 8;
		if (tot < 44 || o + tot > sz)
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%ssnd_%04u.wav", dest, basedir ? basedir : "", nw);
		if (n == 0 && nw == 0 && verbose >= 0)
			fprintf (stdlog, "%sEXTRACT OIBB:%s -> %s/\n", verbose > 0 ? "\n" : "", arg, dest);
		File_t F;
		enumError e2 = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (raw + o, 1, tot, F.f) != tot)
			e2 = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)tot, path);
		ResetFile (&F, opt_preserve);
		if (!e2)
			nw++;
		o += (size_t)tot - 4;
	}
	n += nw;
	FREE (raw);
	return n ? ERR_OK : ERR_NOTHING_TO_DO;
}

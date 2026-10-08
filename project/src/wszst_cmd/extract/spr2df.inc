// Messiah-engine "2df" sprite sheets ".spr" (Gummy Bears): little-endian
//   "2df\0", u32 version = 11, ...; u32 texture_count at 0x108.
// The last 0x20-byte records before the pixel data are texture descriptors
//   {u32 0x10001, u32 mode, u32 end, u32 width, u32 height, u32 0, u32 fmt,
//    u32 size + 8}
// followed by the textures themselves, each padded to 0x20 bytes. Pixels are
// GX RGBA8 (size = w*h*4) or RGB5A3 (size = w*h*2). Decoded to
// "<name>.<NN>.png" in decode mode (xx).
static enumError extract_spr2df_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (export_count <= 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;
	if (!is_ext (arg, ".spr"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size < 0x200 || memcmp (raw, "2df\0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = rd_le32 (raw + 0x108);
	if (!count || count > 64)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// locate the descriptor run
	size_t desc[64];
	uint n = 0;
	const size_t lim = raw_size < 0x10000 ? raw_size : 0x10000;
	for (size_t o = 0x100; o + 0x20 <= lim && n < count;)
	{
		const u8 *d = raw + o;
		const uint mode = rd_le32 (d + 4), w = rd_le32 (d + 12), h = rd_le32 (d + 16);
		if (rd_le32 (d) == 0x10001 && (mode == 2 || mode == 6) && w && w <= 4096 && h && h <= 4096
			&& !rd_le32 (d + 20) && rd_le32 (d + 28) > 8)
		{
			desc[n++] = o;
			o += 0x20;
		}
		else
			o += 4;
	}
	if (n != count)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	enumError err = ERR_OK;
	size_t pos = desc[n - 1] + 0x20;
	uint done = 0;
	for (uint t = 0; t < n && !err; t++)
	{
		const u8 *d = raw + desc[t];
		const uint w = rd_le32 (d + 12), h = rd_le32 (d + 16), size = rd_le32 (d + 28) - 8;
		const size_t step = ((size_t)size + 8 + 0x1f) & ~(size_t)0x1f;
		const uint bpp = (uint)(((u64)size * 8) / ((u64)w * h));
		if (pos > raw_size || size > raw_size - pos || (w | h) & 3 || (bpp != 32 && bpp != 16))
			break;
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s.%02u.png", arg, t);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE 2DF:%s[%u] %ux%u -> PNG:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, t, w, h, png);
		if (!testmode)
		{
			u8 *rgba = MALLOC ((size_t)w * h * 4);
			if (!rgba)
			{
				err = ERR_OUT_OF_MEMORY;
				break;
			}
			const u8 *src = raw + pos;
			for (uint by = 0; by < h; by += 4)
				for (uint bx = 0; bx < w; bx += 4, src += bpp == 32 ? 64 : 32)
					for (uint i = 0; i < 16; i++)
					{
						u8 *o = rgba + ((size_t)(by + i / 4) * w + bx + i % 4) * 4;
						if (bpp == 32)
						{
							o[0] = src[2 * i + 1];
							o[1] = src[32 + 2 * i];
							o[2] = src[32 + 2 * i + 1];
							o[3] = src[2 * i];
						}
						else
						{
							const uint v = (src[2 * i] << 8) | src[2 * i + 1];
							if (v & 0x8000)
							{
								o[0] = ((v >> 10) & 31) * 255 / 31;
								o[1] = ((v >> 5) & 31) * 255 / 31;
								o[2] = (v & 31) * 255 / 31;
								o[3] = 255;
							}
							else
							{
								o[0] = ((v >> 8) & 15) * 17;
								o[1] = ((v >> 4) & 15) * 17;
								o[2] = (v & 15) * 17;
								o[3] = ((v >> 12) & 7) * 255 / 7;
							}
						}
					}
			err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png, 0, true);
		}
		done++;
		pos += step;
	}
	FREE (raw);
	if (!err && !done)
		return ERR_NOTHING_TO_DO;
	return err;
}

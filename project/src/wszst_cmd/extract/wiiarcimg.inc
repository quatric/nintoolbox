// "wii\0" resource archive, Ski-Doo Snowmobile Challenge flavour (files/*.ARC, Mere Mortals engine); fallback behind the Petanque Master reader (wiiresarc.inc), which
// rejects these. Big endian header {"wii\0", header size, resource count N, variant, name table offset, header size, ...}; info block at 0xe0 {2, header size, data base (header size +
// a fixup block), fixup size, data size}; N * 0x20 byte records from 0x104 {u32 type, u32 size, u32 offset into the data area, u32 id, 0, 0x20 align, 0, 0}; N absolute offsets to
// NUL terminated names. Each resource is written as "NNNN_<name>.t<type>.bin" under "<file>.d/"; type 2 image resources whose 0x60 byte header holds BE u16 {w, h, padded w, padded h}
// and the GX format at +0x0c (CMPR / RGB565 / RGB5A3 / RGBA8) are also decoded to ".png". Variant 1 (6 *SOUNDS / CROWD / ENGINE ARCs) holds GC DSP-ADPCM images with the standard
// 0x60 byte header, carved as "NNN.dsp" by a validated scan; variant 2 (the 254 MB MUSIC.ARC streams) is not handled.
static enumError extract_wiiarcimg_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".arc") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0x120 || memcmp (raw, "wii", 4) || rd_be32 (raw + 0xc) > 1)
		goto no;
	if (rd_be32 (raw + 0xc) == 1)
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		uint nd = 0;
		enumError err = ERR_NOTHING_TO_DO;
		for (size_t i = 0x100; i + 0x60 <= sz; i += 4)
		{
			const u32 ns = rd_be32 (raw + i), nn = rd_be32 (raw + i + 4), rate = rd_be32 (raw + i + 8);
			if (!ns || ns > 0x4000000 || rate < 4000 || rate > 96000 || rd_be16 (raw + i + 12) > 1 || rd_be16 (raw + i + 14) || rd_be32 (raw + i + 0x18) != 2)
				continue;
			if (nn != ns / 14 * 16 + (ns % 14 ? ns % 14 + 2 : 0) || i + 0x60 + (nn + 1) / 2 > sz)
				continue;
			if (!nd)
			{
				err = ERR_OK;
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sEXTRACT WII-ARC:%s (sound variant) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
			}
			char fn[64];
			snprintf (fn, sizeof (fn), "%03u.dsp", nd++);
			if (!testmode && (err = shade_write (arg, dest, basedir, fn, raw + i, 0x60 + (nn + 1) / 2)))
				break;
			i += 0x60 + (nn + 1) / 2;
			i = (i + 3 & ~(size_t)3) - 4;
		}
		FREE (raw);
		return err;
	}
	{
		const u32 hs = rd_be32 (raw + 4), n = rd_be32 (raw + 8), nt = rd_be32 (raw + 0x10), base = rd_be32 (raw + 0xe8), dsz = rd_be32 (raw + 0xf0);
		if (!n || n > 100000 || hs > sz || nt > sz || 0x104 + 0x20 * (u64)n > hs || (u64)nt + 4 * (u64)n > sz || base > sz || (u64)base + dsz > sz || rd_be32 (raw + 0xe0) != 2)
			goto no;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT WII-ARC:%s (%u resources) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		if (testmode)
		{
			FREE (raw);
			return ERR_OK;
		}
		enumError err = ERR_OK;
		for (uint i = 0; i < n && !err; i++)
		{
			const u8 *r = raw + 0x104 + 0x20 * (size_t)i;
			const u32 ty = rd_be32 (r), rs = rd_be32 (r + 4), ro = rd_be32 (r + 8), no = rd_be32 (raw + nt + 4 * (size_t)i);
			if ((u64)ro + rs > dsz || no >= sz)
				continue;
			char nm[64];
			uint k = 0;
			for (; k < sizeof (nm) - 1 && no + k < sz && raw[no + k] > 0x20 && raw[no + k] < 0x7f && raw[no + k] != '/'; k++)
				nm[k] = raw[no + k];
			nm[k] = 0;
			char fn[128];
			snprintf (fn, sizeof (fn), "%04u_%s.t%u.bin", i, nm, ty);
			err = shade_write (arg, dest, basedir, fn, raw + base + ro, rs);
			if (err || ty != 2 || rs < 0x60)
				continue;
			const u8 *h = raw + base + ro;
			const uint w = rd_be16 (h), hh = rd_be16 (h + 2), pw = rd_be16 (h + 4), ph = rd_be16 (h + 6), f = h[0x0c];
			if (!w || !hh || w > 2048 || hh > 2048 || pw < w || ph < hh || pw > w + 8 || ph > hh + 8 || (f != 14 && f != 4 && f != 5 && f != 6))
				continue;
			const u64 need = f == 14 ? ((pw + 7ull) & ~7ull) * ((ph + 7ull) & ~7ull) / 2 : f == 6 ? ((pw + 3ull) & ~3ull) * ((ph + 3ull) & ~3ull) * 4 : ((pw + 3ull) & ~3ull) * ((ph + 3ull) & ~3ull) * 2;
			if (need + 0x60 > rs + 0x200)
				continue;
			u8 *tmp = CALLOC (1, need + 32);
			memcpy (tmp, h + 0x60, rs - 0x60 < need ? rs - 0x60 : need);
			u8 *rgba = 0;
			if (!DecodeGXTexture_RGBA (&rgba, pw, ph, f, tmp, (uint)need, 0, 0, 0) && rgba)
			{
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%s%04u_%s.png", dest, basedir ? basedir : "", i, nm);
				SaveDecodedRGBAToPNG (rgba, pw, ph, &be_func, path, 0, true);
			}
			FREE (tmp);
		}
		FREE (raw);
		return err;
	}
no:
	FREE (raw);
	return ERR_NOTHING_TO_DO;
}

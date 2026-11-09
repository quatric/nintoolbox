// Shrek Forever After (Page 44 / Activision) "APKF" resource packages ("*.apk"; layout recovered by tracing the game's APK loader in Ghidra and checked on
// all 365 retail files). Big endian: {"APKF", u16 LE version 0x0105, u32 flags, u32 -1, u32 pool count n, u32 pool table offset (counted from +8, the table
// starts at +0xc more), ...}. A pool entry is 0x18 bytes {tag reversed ("IMG\0", "DATA"), u32 flags, 2 words, u32 size @+0x10, u32 offset @+0x14 (relative
// to the field)}. From +0x1c the groups follow, each {tag reversed ("TEX", "MAT", "MESH", "SKEL", "FONT"), u32 version, u32 item words, u32 item table
// offset (from +0xc after the group start), u32 item count, u32 per pool word [n] (byte 3: pool index, low 24 bits: alignment)}, ended by a zero tag. An item
// is {u32 first word, u32 size in each pool [item words]}; the sizes are consumed sequentially (aligned) from the pools. A TEX item keeps its 0x40 byte
// header in a pool: "GCNT", u32 3, u32 0x00400000, u32 data size, u16 width, u16 height, u8 GX format, u8 palette format, u8 mipmap count - 1, then the GX
// pixel data (all mipmaps) followed by a 256 / 16 entry palette for C8 / C4. Textures are written as "tex_NNNN.png" (largest mipmap only); materials,
// meshes and skeletons are not decoded.
static enumError extract_apkf_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".apk"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 || memcmp (raw, "APKF", 4) || raw[4] != 5 || raw[5] != 1)
		goto done;
	{
		const u32 n = rd_be32 (raw + 0x10), po = rd_be32 (raw + 0x14);
		if (!n || n > 8 || (u64)po + 0x14 + 0x18 * (u64)n > sz)
			goto done;
		u32 pbase[8];
		for (uint i = 0; i < n; i++)
		{
			const u64 e = 8 + 0xc + (u64)po + 0x18 * (u64)i;
			if (e + 0x18 > sz)
				goto done;
			pbase[i] = (u32)(e + 0x14 + rd_be32 (raw + e + 0x14));
		}
		u32 g = 0x1c;
		uint ntex = 0;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		for (; (u64)g + 20 + 4 * (u64)n <= sz && rd_be32 (raw + g); g += 20 + 4 * n)
		{
			if (memcmp (raw + g, "\0XET", 4))
				continue;
			const u32 isz = rd_be32 (raw + g + 8), ioff = rd_be32 (raw + g + 12), cnt = rd_be32 (raw + g + 16);
			if (isz < 1 || isz > 16 || cnt > 0x100000 || (u64)g + 0xc + ioff + 4 * (u64)(isz + 1) * cnt > sz)
				goto done;
			u32 cur[8] = { 0 };
			for (uint c = 0; c < cnt; c++)
			{
				const u8 *it = raw + g + 0xc + ioff + 4 * (size_t)(isz + 1) * c;
				const u8 *hdr = 0;
				u32 isize = 0;
				for (uint p = 0; p < n; p++)
				{
					const u32 w = rd_be32 (raw + g + 20 + 4 * p), idx = w >> 24, al = w & 0xffffff;
					if (idx >= n || idx + 1 > isz)
						continue;
					if (al)
						cur[idx] = (cur[idx] + al - 1) & ~(al - 1);
					const u32 s = rd_be32 (it + 4 + 4 * idx);
					const u64 pos = (u64)pbase[idx] + cur[idx];
					if (pos + 0x40 <= sz && !memcmp (raw + pos, "GCNT", 4) && pos + s <= sz)
						hdr = raw + pos, isize = s;
					cur[idx] += s;
				}
				if (!hdr)
					continue;
				const uint w = rd_be16 (hdr + 0x10), h = rd_be16 (hdr + 0x12), fm = hdr[0x14], pf = hdr[0x15];
				const u32 ds = rd_be32 (hdr + 0xc);
				if (!w || !h || w > 4096 || h > 4096 || isize < 0x40 || ds > isize - 0x40)
					continue;
				uint bw, bh, bits;
				switch (fm)
				{
					case 0: case 8: bw = 8, bh = 8, bits = 4; break;
					case 1: case 2: case 9: bw = 8, bh = 4, bits = 8; break;
					case 3: case 4: case 5: case 10: bw = 4, bh = 4, bits = 16; break;
					case 6: bw = 4, bh = 4, bits = 32; break;
					case 14: bw = 8, bh = 8, bits = 4; break;
					default: continue;
				}
				(void)bits;
				const u64 base = fm == 6 ? 64 * (u64)((w + 3) / 4) * ((h + 3) / 4) : 32 * (u64)((w + bw - 1) / bw) * ((h + bh - 1) / bh);
				const uint pc = fm == 9 ? 256 : fm == 8 ? 16 : 0;
				if (base > ds || (pc && (u64)0x40 + ds + 2 * pc > isize))
					continue;
				ntex++;
				if (testmode)
					continue;
				u8 *rgba = 0;
				if (DecodeGXTexture_RGBA (&rgba, w, h, fm, hdr + 0x40, (uint)base, pc ? hdr + 0x40 + ds : 0, pc, pf) || !rgba)
				{
					FREE (rgba);
					continue;
				}
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%stex_%04u.png", dest, basedir ? basedir : "", c);
				SaveDecodedRGBAToPNG (rgba, w, h, &be_func, path, 0, true);
			}
		}
		if (!ntex)
			goto done;
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT APKF:%s (%u textures) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ntex, dest);
		err = ERR_OK;
	}
done:
	FREE (raw);
	return err;
}

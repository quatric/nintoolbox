// Unity 2.x Wii SerializedFile ("sharedassets*.assets", "levelN", "mainData"): big-endian, version 6.
//   header {u32 metadata_size, u32 file_size, u32 version=6, u32 data_offset=0}; the metadata block sits at
//   file_size - metadata_size: u8 endian(1 = BE), i32 type_count (0), i32 object_count, then per object
//   {i32 path_id, u32 byte_start, u32 byte_size, i32 type_id, u16 class_id, u16 destroyed}.
//   class 28 Texture2D: name (u32 len, padded to 4) + BE words {0, 1, 2, width, height, image_size, format << 16,
//     data_size, lz_size} + Nintendo LZ11 stream of the first mip level plus the chain (rows are bottom-up).
//     Format 0x10 = GX IA8, 0x11 = RGB565, 0x12 = RGB5A3, 0x13 = RGBA8, 0x14 = CMPR.
//   class 83 AudioClip: name, float length, 4 words, u32 chunk size, ..., two standard 0x60-byte .dsp headers
//     (at +32 from the end of the name, the second found by its repeated sample/nibble/rate words) each followed by one
//     channel of DSP-ADPCM.
//   class 49 TextAsset: name + u32 length + bytes.
// Later Wii builds (My Animal Centre, JumpStart, El Chavo, ...) keep version 6 but store type trees: type_count > 0,
//   per type {i32 class, node}, node = {cstring type, cstring name, i32 size, index, is_array, version, flags, child count,
//   children}. Texture2D then is name, i32 width, height, complete image size, format, bool mip map (padded to 4), i32 image
//   count, dimension, 16-byte settings, ResourceId {u32 offset, u32 bundle}, u32 inline size (+ data). The raw GX data (base
//   level, then the mip chain) sits at that offset in the ".ures" stream of the scene owning the bundle (bundle = scene << 16
//   | stream): "mainData-<stream>.ures" for scene 0, else "level<scene - 1>-<stream>.ures" (levelK lists sharedassets<K + 1>
//   as its dependency). Format 0x0e = I8. Streams of unused scenes are not on the disc.
static bool unityassets_object (const u8 *d, size_t size, uint start, uint osz, const u8 **name, uint *nlen, const u8 **body)
{
	if (start > size || osz > size - start || osz < 8)
		return false;
	const uint n = rd_be32 (d + start);
	const size_t q = 4 + ((size_t)n + 3 & ~3u);
	if (n > 256 || q > osz)
		return false;
	*name = d + start + 4;
	*nlen = n;
	*body = d + start + q;
	return true;
}

// Skips one type tree node; returns the position after it or 0.
static size_t unityassets_skip_node (const u8 *d, size_t end, size_t p, int depth)
{
	for (int s = 0; s < 2; s++)
	{
		while (p < end && d[p])
			p++;
		if (++p > end)
			return 0;
	}
	if (depth > 64 || p + 24 > end)
		return 0;
	const u32 children = rd_be32 (d + p + 20);
	p += 24;
	for (u32 i = 0; i < children && p; i++)
		p = unityassets_skip_node (d, end, p, depth + 1);
	return p;
}

// Loads the ".ures" stream of BUNDLE beside ARG (cached in *buf / *cur).
static bool unityassets_ures (ccp arg, u32 bundle, u8 **buf, size_t *bsz, s64 *cur)
{
	if (*cur == (s64)bundle)
		return *buf != 0;
	FREE (*buf);
	*buf = 0, *bsz = 0, *cur = bundle;
	char dir[PATH_MAX], fn[PATH_MAX];
	ccp sl = strrchr (arg, '/');
	snprintf (dir, sizeof (dir), "%.*s", sl ? (int)(sl - arg) : 1, sl ? arg : ".");
	// bundle = scene << 16 | stream; scene 0 is "mainData", scene i > 0 is "level<i - 1>"
	if (bundle >> 16)
		snprintf (fn, sizeof (fn), "%s/level%u-%u.ures", dir, (bundle >> 16) - 1, bundle & 0xffff);
	else
		snprintf (fn, sizeof (fn), "%s/mainData-%u.ures", dir, bundle & 0xffff);
	if (LoadFileAlloc (fn, 0, 0, buf, bsz, 0, 2, 0, false) || !*buf)
	{
		FREE (*buf);
		*buf = 0;
	}
	return *buf != 0;
}

static void unityassets_name (char *out, size_t sz, int pid, const u8 *name, uint nlen, ccp ext)
{
	char base[128];
	uint k = 0;
	for (; k < nlen && k < sizeof (base) - 1; k++)
		base[k] = (isalnum (name[k]) || name[k] == '_' || name[k] == '-' || name[k] == '.') ? name[k] : '_';
	base[k] = 0;
	snprintf (out, sz, "%05d_%s%s", pid, base[0] ? base : "unnamed", ext);
}

static enumError extract_unityassets_file (ccp arg, ccp basedir, uint depth)
{
	ccp fn = strrchr (arg, '/');
	fn = fn ? fn + 1 : arg;
	if (!is_ext (arg, ".assets") && strncmp (fn, "level", 5) && strcmp (fn, "mainData"))
		return ERR_NOTHING_TO_DO;
	u8 *d = 0;
	size_t size = 0;
	if (LoadFileAlloc (arg, 0, 0, &d, &size, 0, 0, 0, false) || size < 0x30 || size > 0x7fffffff)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	const u32 msz = rd_be32 (d), fsz = rd_be32 (d + 4), ver = rd_be32 (d + 8), doff = rd_be32 (d + 12);
	if (ver != 6 || doff || fsz != size || msz < 9 || msz > fsz - 16 || d[fsz - msz] != 1)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	size_t p = fsz - msz + 1;
	const int tc = (int)rd_be32 (d + p);
	p += 4;
	for (int t = 0; t < tc && p; t++)
		p = p + 4 <= fsz ? unityassets_skip_node (d, fsz, p + 4, 0) : 0;
	const int oc = p && p + 4 <= fsz ? (int)rd_be32 (d + p) : 0;
	p += 4;
	if (tc < 0 || tc > 4096 || oc <= 0 || oc > 100000 || p + 20ull * oc > fsz)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (testmode)
	{
		FREE (d);
		fprintf (stdlog, "WOULD EXTRACT UNITY-ASSETS:%s -> %s/\n", arg, dest);
		return ERR_OK;
	}
	uint n_out = 0;
	u8 *ures = 0;
	size_t ures_sz = 0;
	s64 ures_cur = -1;
	for (int i = 0; i < oc; i++)
	{
		const u8 *o = d + p + 20 * (size_t)i;
		const int pid = (int)rd_be32 (o);
		const uint start = rd_be32 (o + 4), osz = rd_be32 (o + 8);
		const uint cls = rd_be16 (o + 16);
		const u8 *name, *body;
		uint nlen;
		if ((cls != 28 && cls != 83 && cls != 49) || !unityassets_object (d, fsz, start, osz, &name, &nlen, &body))
			continue;
		const size_t blen = osz - (size_t)(body - (d + start));
		char fname[160], path[PATH_MAX];
		if (cls == 28 && tc > 0 && blen >= 56)
		{
			const uint w = rd_be32 (body), h = rd_be32 (body + 4), cis = rd_be32 (body + 8),
					   fmt = rd_be32 (body + 12);
			const u32 rid = rd_be32 (body + 44), bundle = rd_be32 (body + 48),
					  inl = rd_be32 (body + 52);
			static const int gx[]
				= { [0x0e] = 1, [0x10] = 3, [0x11] = 4, [0x12] = 5, [0x13] = 6, [0x14] = 14 };
			if (!w || !h || w > 4096 || h > 4096 || fmt < 0x0e || fmt > 0x14 || fmt == 0x0f || !cis)
				continue;
			const u8 *src = 0;
			if (inl >= cis && inl <= blen - 56)
				src = body + 56;
			else if (unityassets_ures (arg, bundle, &ures, &ures_sz, &ures_cur) && rid <= ures_sz
				&& cis <= ures_sz - rid)
				src = ures + rid;
			if (!src)
				continue;
			u8 *rgba = 0;
			if (DecodeGXTexture_RGBA (&rgba, w, h, gx[fmt], src, cis, 0, 0, 0) || !rgba)
			{
				FREE (rgba);
				continue;
			}
			// Unity stores rows bottom-up
			for (uint y = 0; y < h / 2; y++)
			{
				u8 *a = rgba + (size_t)y * w * 4, *b = rgba + (size_t)(h - 1 - y) * w * 4;
				for (size_t k = 0; k < (size_t)w * 4; k++)
				{
					const u8 t = a[k];
					a[k] = b[k];
					b[k] = t;
				}
			}
			unityassets_name (fname, sizeof (fname), pid, name, nlen, ".png");
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", fname);
			if (!SaveDecodedRGBAToPNG (rgba, w, h, &be_func, path, 0, true))
				n_out++;
		}
		else if (cls == 28 && tc == 0 && blen >= 36)
		{
			const uint w = rd_be32 (body + 12), h = rd_be32 (body + 16), fmt = rd_be32 (body + 24) >> 16;
			const uint dsz = rd_be32 (body + 28), lsz = rd_be32 (body + 32);
			static const int gx[] = {[0x10] = 3, [0x11] = 4, [0x12] = 5, [0x13] = 6, [0x14] = 14};
			if (!w || !h || w > 4096 || h > 4096 || fmt < 0x10 || fmt > 0x14 || lsz > blen - 36 || lsz < 5 || body[36] != 0x11)
				continue;
			u8 *raw = 0, *rgba = 0;
			uint rsz = 0;
			if (DecodeLZ10LZ11 (&raw, &rsz, body + 36, lsz) || rsz < dsz)
			{
				FREE (raw);
				continue;
			}
			const enumError e = DecodeGXTexture_RGBA (&rgba, w, h, gx[fmt], raw, rsz, 0, 0, 0);
			FREE (raw);
			if (e || !rgba)
			{
				FREE (rgba);
				continue;
			}
			// Unity stores rows bottom-up
			for (uint y = 0; y < h / 2; y++)
			{
				u8 *a = rgba + (size_t)y * w * 4, *b = rgba + (size_t)(h - 1 - y) * w * 4;
				for (size_t k = 0; k < (size_t)w * 4; k++)
				{
					const u8 t = a[k];
					a[k] = b[k];
					b[k] = t;
				}
			}
			unityassets_name (fname, sizeof (fname), pid, name, nlen, ".png");
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", fname);
			if (!SaveDecodedRGBAToPNG (rgba, w, h, &be_func, path, 0, true))
				n_out++;
		}
		else if (cls == 83 && tc == 0 && blen >= 32 + 0x60)
		{
			const uint chunk = rd_be32 (body + 20);
			const uint nch = rd_be32 (body + 12);
			const size_t first = 32;
			size_t second = 0; // the second stream repeats the sample/nibble/rate words of the first header
			for (size_t k = first + 0x60; nch > 1 && k + chunk <= blen; k++)
				if (!memcmp (body + k, body + first, 12))
				{
					second = k;
					break;
				}
			if (nch > 1 && !second)
				continue;
			if (chunk < 0x60 || (nch != 1 && nch != 2) || rd_be32 (body + 32 + 8) < 4000 || rd_be32 (body + 32 + 8) > 96000)
				continue;
			for (uint c = 0; c < nch; c++)
			{
				const size_t off = c ? second : first;
				if (off + chunk > blen)
					break;
				char ext[16];
				snprintf (ext, sizeof (ext), nch > 1 ? "_ch%u.dsp" : ".dsp", c);
				unityassets_name (fname, sizeof (fname), pid, name, nlen, ext);
				snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", fname);
				File_t F;
				CreateFileOpt (&F, true, path, false, arg);
				if (F.f)
				{
					fwrite (body + off, 1, chunk, F.f);
					n_out++;
				}
				ResetFile (&F, opt_preserve);
			}
		}
		else if (cls == 49 && blen >= 4)
		{
			const uint len = rd_be32 (body);
			if (len > blen - 4)
				continue;
			unityassets_name (fname, sizeof (fname), pid, name, nlen, ".bytes");
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", fname);
			File_t F;
			CreateFileOpt (&F, true, path, false, arg);
			if (F.f)
			{
				fwrite (body + 4, 1, len, F.f);
				n_out++;
			}
			ResetFile (&F, opt_preserve);
		}
	}
	FREE (ures);
	FREE (d);
	if (!n_out)
		return ERR_NOTHING_TO_DO;
	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT UNITY-ASSETS:%s -> %s/ (%u files)\n", verbose > 0 ? "\n" : "", arg, dest, n_out);
	return ERR_OK;
}

// Imagine engine scene mesh (".wsn": Party Babyz "babyz/motion/*/base.wsn", the Imagine LZ wrapper is unpacked first; layout recovered by tracing the
// game's scene loader in Ghidra and checked on 60 retail files): big endian {u32 object count, u32 texture count, u32 pool size, u32 fill, then u32 object
// offsets [n], u32 texture offsets [t], u32 texture size words [t] (low 16 bits = square edge in pixels), u32 texture format words [t] (0 = RGB565, 1 = RGB5A3;
// 16 bit pixels in 4x4 GX tiles)}. An object is {u32 mesh count, ..., u32 mesh offsets [count] at +0x38}; a mesh is a 0x30 byte header {u32 vertex count, ...,
// u32 vertex pointer at +0x20 (always header + 0x30)} followed by vertex records of 28 bytes {float x, y, z, float a, b (about 1.1 / 0.93, meaning
// unknown), u32 ?, u8 r, g, b, a}; the vertices form a plain triangle list. Beastly meshes keep the same 28 byte records but point to them from +0x28
// (positions are huge, about 1e5, and meant to be scaled by the bone matrices). Written as "model.obj" (positions with the vertex colours as extra "v" components) and
// "tex_NN.png".
static bool wsn_in (size_t sz, u64 o, u64 n)
{
	return o <= sz && n <= sz - o;
}

static enumError extract_imaginewsn_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".wsn"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	char *obj = 0;
	if (sz < 0x30)
		goto done;
	{
		const uint no = rd_be32 (raw), nt = rd_be32 (raw + 4);
		if (!no || no > 64 || nt > 64 || !wsn_in (sz, 0x10, 4 * (u64)no + 12 * (u64)nt))
			goto done;
		size_t cap = 1 << 16, used = 0;
		obj = MALLOC (cap);
		if (!obj)
		{
			err = ERR_OUT_OF_MEMORY;
			goto done;
		}
		bool ok = true;
#define WSN_EMIT(...) \
	do { \
		for (;;) { \
			const int w_ = snprintf (obj + used, cap - used, __VA_ARGS__); \
			if (w_ >= 0 && (size_t)w_ < cap - used) { used += (size_t)w_; break; } \
			char *nb_ = REALLOC (obj, cap * 2); \
			if (!nb_) { ok = false; break; } \
			obj = nb_; cap *= 2; \
		} \
	} while (0)
		uint ntri = 0, nmesh = 0, vbase = 1;
		for (uint oi = 0; oi < no && ok; oi++)
		{
			const u32 oo = rd_be32 (raw + 0x10 + 4 * oi);
			if (!wsn_in (sz, oo, 0x40))
				goto bad;
			const u32 nm = rd_be32 (raw + oo);
			if (!nm || nm > 1024 || !wsn_in (sz, (u64)oo + 0x38, 4 * (u64)nm))
				goto bad;
			for (uint mi = 0; mi < nm && ok; mi++)
			{
				const u32 mo = rd_be32 (raw + oo + 0x38 + 4 * mi);
				if (!wsn_in (sz, mo, 0x30))
					goto bad;
				const u32 n = rd_be32 (raw + mo);
				u32 vp = rd_be32 (raw + mo + 0x20);
				if (vp != mo + 0x30 && wsn_in (sz, mo, 0x34))
					vp = rd_be32 (raw + mo + 0x28); // Beastly variant: {u32 index count, u32 unique vertex count, ..., +0x28 expanded vertex records, +0x2c u16 indices, +0x30 position array}
				if (vp < mo + 0x30 || n > 0x100000 || !wsn_in (sz, vp, 28 * (u64)n))
					goto bad;
				WSN_EMIT ("o obj%u_mesh%u\n", oi, mi);
				for (uint v = 0; v < n && ok; v++)
				{
					const u8 *r = raw + vp + 28 * (size_t)v;
					float f[3];
					for (uint k = 0; k < 3; k++)
					{
						const u32 b = rd_be32 (r + 4 * k);
						memcpy (&f[k], &b, 4);
						if (!isfinite (f[k]))
							f[k] = 0;
					}
					WSN_EMIT ("v %.4f %.4f %.4f %.3f %.3f %.3f\n", f[0], f[1], f[2], r[24] / 255.0, r[25] / 255.0, r[26] / 255.0);
				}
				for (uint t = 0; t + 2 < n && ok; t += 3)
				{
					WSN_EMIT ("f %u %u %u\n", vbase + t, vbase + t + 1, vbase + t + 2);
					ntri++;
				}
				vbase += n;
				nmesh++;
			}
		}
		if (!ok || !ntri)
			goto bad;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT WSN:%s (%u meshes, %u triangles, %u textures) -> %s/model.obj\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, nmesh, ntri, nt, dest);
		err = ERR_OK;
		if (!testmode)
		{
			err = shade_write (arg, dest, basedir, "model.obj", (u8 *)obj, (uint)used);
			const u32 tb = 0x10 + 4 * no;
			for (uint i = 0; i < nt && !err; i++)
			{
				const u32 off = rd_be32 (raw + tb + 4 * i), edge = rd_be32 (raw + tb + 4 * nt + 4 * i) & 0xffff, fm = rd_be32 (raw + tb + 8 * nt + 4 * i);
				if (!edge || edge > 2048 || edge % 4 || !wsn_in (sz, off, 2 * (u64)edge * edge))
					continue;
				u8 *rgba = 0;
				if (DecodeGXTexture_RGBA (&rgba, edge, edge, fm == 1 ? 5 : 4, raw + off, 2 * edge * edge, 0, 0, 0) || !rgba)
				{
					FREE (rgba);
					continue;
				}
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%stex_%02u.png", dest, basedir ? basedir : "", i);
				SaveDecodedRGBAToPNG (rgba, edge, edge, &be_func, path, 0, true);
			}
		}
	}
	goto done;
bad:
	err = ERR_NOTHING_TO_DO;
done:
	FREE (obj);
	FREE (raw);
	return err;
}

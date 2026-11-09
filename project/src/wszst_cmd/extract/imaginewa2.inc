// Imagine engine skeletal animation (".wa2": Beastly "odyssey/motion/<name>/anim.wa2", the Imagine LZ wrapper is unpacked first; layout recovered by tracing the
// game's anim loader in Ghidra and checked against the file sizes): big endian {u32 clip count, u32 bone count, u32 bone offsets [bones], u32 clip offsets
// [clips]}. A bone is {u32 index, float x, y, z (bind translation), u32 child count, u32 0 (runtime pointer), u32 child indices [count] at +0x18}. A clip is
// {u32 frame count N, u32 0, 4 runtime words, then from +0x18: u32 [N] (frame flags), float3 [N] (root motion), float3 [N] (second track, large smooth values),
// then N frames of bone quaternions {float x, y, z, w} [bones] (unit length)}: the clip occupies exactly 0x18 + N * (0x1c + 16 * bones) bytes. The names of the
// clips are the "#define ANIM_..." numbers in the sibling "anim_<name>.h". Written as "anim.json" (bones with parents, clips with all tracks).
static enumError extract_imaginewa2_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".wa2"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	char *js = 0;
	if (sz < 0x20)
		goto done;
	{
		const uint nc = rd_be32 (raw), nb = rd_be32 (raw + 4);
		if (!nc || nc > 4096 || !nb || nb > 1024 || 8 + 4 * ((u64)nb + nc) > sz)
			goto done;
		const u32 *bo = 0, *co = 0;
		(void)bo;
		(void)co;
		// validate bones and clips
		for (uint i = 0; i < nb; i++)
		{
			const u32 o = rd_be32 (raw + 8 + 4 * i);
			if (o + 0x18 > sz || rd_be32 (raw + o) != i)
				goto done;
			const u32 cc = rd_be32 (raw + o + 0x10);
			if (cc > nb || (u64)o + 0x18 + 4 * (u64)cc > sz)
				goto done;
		}
		u64 total = 0;
		for (uint c = 0; c < nc; c++)
		{
			const u32 o = rd_be32 (raw + 8 + 4 * nb + 4 * c);
			const u32 end = c + 1 < nc ? rd_be32 (raw + 8 + 4 * nb + 4 * (c + 1)) : (u32)sz;
			if (o < 8 + 4 * (nb + nc) || o + 0x18 > sz || end < o || end > sz)
				goto done;
			const u32 n = rd_be32 (raw + o);
			if (!n || n > 100000 || (u64)end - o != 0x18 + (u64)n * (0x1c + 16 * (u64)nb))
				goto done;
			total += n;
		}
		size_t cap = 4096 + (size_t)nb * 200 + (size_t)total * (40 + (size_t)nb * 60), used = 0;
		js = MALLOC (cap);
		if (!js)
		{
			err = ERR_OUT_OF_MEMORY;
			goto done;
		}
#define WA2_EMIT(...) \
	do { \
		const int w_ = snprintf (js + used, cap - used, __VA_ARGS__); \
		if (w_ < 0 || (size_t)w_ >= cap - used) { err = ERR_OUT_OF_MEMORY; goto done; } \
		used += (size_t)w_; \
	} while (0)
#define WA2_F(p) ({ u32 b_ = rd_be32 (p); float f_; memcpy (&f_, &b_, 4); (double)(isfinite (f_) ? f_ : 0.0f); })
		// parents from the child lists
		int *parent = MALLOC (sizeof (int) * nb);
		if (!parent)
		{
			err = ERR_OUT_OF_MEMORY;
			goto done;
		}
		for (uint i = 0; i < nb; i++)
			parent[i] = -1;
		for (uint i = 0; i < nb; i++)
		{
			const u32 o = rd_be32 (raw + 8 + 4 * i), cc = rd_be32 (raw + o + 0x10);
			for (uint k = 0; k < cc; k++)
			{
				const u32 ch = rd_be32 (raw + o + 0x18 + 4 * k);
				if (ch < nb)
					parent[ch] = (int)i;
			}
		}
		WA2_EMIT ("{\n \"bones\": [\n");
		for (uint i = 0; i < nb; i++)
		{
			const u32 o = rd_be32 (raw + 8 + 4 * i);
			WA2_EMIT ("  {\"index\": %u, \"parent\": %d, \"translation\": [%.6g, %.6g, %.6g]}%s\n", i, parent[i], WA2_F (raw + o + 4), WA2_F (raw + o + 8),
				WA2_F (raw + o + 12), i + 1 < nb ? "," : "");
		}
		FREE (parent);
		WA2_EMIT (" ],\n \"clips\": [\n");
		for (uint c = 0; c < nc; c++)
		{
			const u32 o = rd_be32 (raw + 8 + 4 * nb + 4 * c), n = rd_be32 (raw + o);
			const u8 *a0 = raw + o + 0x18, *a1 = a0 + 4 * (size_t)n, *a2 = a1 + 12 * (size_t)n, *q = a2 + 12 * (size_t)n;
			WA2_EMIT ("  {\"clip\": %u, \"frames\": %u, \"flags\": [", c, n);
			for (uint f = 0; f < n; f++)
				WA2_EMIT ("%u%s", rd_be32 (a0 + 4 * (size_t)f), f + 1 < n ? "," : "");
			WA2_EMIT ("], \"root\": [");
			for (uint f = 0; f < n; f++)
				WA2_EMIT ("[%.6g,%.6g,%.6g]%s", WA2_F (a1 + 12 * (size_t)f), WA2_F (a1 + 12 * (size_t)f + 4), WA2_F (a1 + 12 * (size_t)f + 8), f + 1 < n ? "," : "");
			WA2_EMIT ("], \"track2\": [");
			for (uint f = 0; f < n; f++)
				WA2_EMIT ("[%.8g,%.8g,%.8g]%s", WA2_F (a2 + 12 * (size_t)f), WA2_F (a2 + 12 * (size_t)f + 4), WA2_F (a2 + 12 * (size_t)f + 8), f + 1 < n ? "," : "");
			WA2_EMIT ("], \"rotations\": [\n");
			for (uint f = 0; f < n; f++)
			{
				WA2_EMIT ("   [");
				for (uint b = 0; b < nb; b++)
				{
					const u8 *p = q + 16 * ((size_t)f * nb + b);
					WA2_EMIT ("[%.6g,%.6g,%.6g,%.6g]%s", WA2_F (p), WA2_F (p + 4), WA2_F (p + 8), WA2_F (p + 12), b + 1 < nb ? "," : "");
				}
				WA2_EMIT ("]%s\n", f + 1 < n ? "," : "");
			}
			WA2_EMIT ("  ]}%s\n", c + 1 < nc ? "," : "");
		}
		WA2_EMIT (" ]\n}\n");
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT IMAGINE-WA2:%s (%u bones, %u clips, %llu frames) -> %s/anim.json\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nb, nc,
				(unsigned long long)total, dest);
		err = ERR_OK;
		if (!testmode)
			err = shade_write (arg, dest, basedir, "anim.json", (u8 *)js, (uint)used);
	}
done:
	FREE (js);
	FREE (raw);
	return err;
}

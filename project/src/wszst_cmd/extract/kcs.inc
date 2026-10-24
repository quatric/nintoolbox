// Spyro: Dawn of the Dragon streamed animation buffers ".KCS" (STREAMS/REVn.KCS): a run of big-endian chunks
// {u32 size, u32 size, u32 type, u32 count, ...} separated by 0xcd / zero padding; the buffer is padded to a
// multiple of 128 KiB. Type 0 is a skeleton pose: {u32 0, u32 0, f32 duration, u32 0, u32 0} after the
// 16 byte head, then count records of 36 bytes {u32 bone id, u32 0, f32 quaternion x y z w, f32 position x y z}
// from offset 0x24. Type 3 holds count records of 40 bytes from offset 0x14, listed as raw words.
// kcs.tsv has "chunk<TAB>offset<TAB>size<TAB>type<TAB>count<TAB>duration", each followed by its records.
static enumError extract_kcs_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".kcs"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x24 || rd_be32 (raw) != rd_be32 (raw + 4) || rd_be32 (raw) < 0x24 || rd_be32 (raw) > sz || rd_be32 (raw + 8) > 3)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t cap = sz * 6 + 4096, used = 0;
	char *out = MALLOC (cap);
	if (!out)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint chunks = 0;
	size_t o = 0;
	while (o + 16 <= sz)
	{
		const u32 a = rd_be32 (raw + o), t = rd_be32 (raw + o + 8), c = rd_be32 (raw + o + 12);
		if (a != rd_be32 (raw + o + 4) || a < 0x14 || a % 4 || a > sz - o || !c || t != 0 && t != 3)
			break;
		const size_t rs = t ? 40 : 36, head = t ? 0x14 : 0x24;
		if (head + (size_t)c * rs > a || used + (size_t)c * 160 + 256 > cap)
			break;
		u32 df = rd_be32 (raw + o + 24);
		float dur;
		memcpy (&dur, &df, 4);
		used += snprintf (out + used, cap - used, "chunk\t%u\t0x%zx\t%u\t%u\t%u\t%g\n", chunks, o, a, t, c, t ? 0.0 : dur);
		for (u32 i = 0; i < c; i++)
		{
			const u8 *r = raw + o + head + rs * i;
			if (!t)
			{
				float f[7];
				for (uint k = 0; k < 7; k++)
				{
					const u32 w = rd_be32 (r + 8 + 4 * k);
					memcpy (f + k, &w, 4);
				}
				used += snprintf (out + used, cap - used, "%u\t%g\t%g\t%g\t%g\t%g\t%g\t%g\n", rd_be32 (r), f[0], f[1],
					f[2], f[3], f[4], f[5], f[6]);
			}
			else
			{
				for (uint k = 0; k < 10; k++)
					used += snprintf (out + used, cap - used, "%s%08x", k ? " " : "", rd_be32 (r + 4 * k));
				out[used++] = '\n';
			}
		}
		chunks++;
		o += a;
		while (o + 4 <= sz && (rd_be32 (raw + o) == 0xcdcdcdcdu || !rd_be32 (raw + o)))
			o += 4;
	}
	if (!chunks)
	{
		FREE (out);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT KCS:%s (%u chunks) -> %s/kcs.tsv\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, chunks, dest);
	const enumError err = shade_write (arg, dest, basedir, "kcs.tsv", (u8 *)out, (uint)used);
	FREE (out);
	FREE (raw);
	return err;
}

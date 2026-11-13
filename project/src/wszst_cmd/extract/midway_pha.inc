// Midway animation packs (".pha", members of TNA Impact! ".WAD" packs; big endian): u16 animation count @8, u32 end of the NUL-separated name table @0x0c (names start at 0x20),
// then at that offset one 36-byte record per animation {u32 start frame (cumulative), u32 flags (0x40000028), u32 0, float speed, float blend time, 3 x u32 0, u32 data offset};
// the last name is a "ZZ_DUMMY" placeholder. Animation k's payload is the range up to the next record's offset (the last one runs to the end of the file). Written as
// "<name>.pha.d/NNNN_<animation>.anm" (raw payloads, their internal key format is not decoded) plus "index.txt" listing name, start frame, speed, blend and size.
static enumError extract_midway_pha_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".pha"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const size_t n = sz > 0x40 ? rd_be16 (raw + 8) : 0, end = sz > 0x40 ? rd_be32 (raw + 0x0c) : 0;
	bool ok = n > 1 && end > 0x20 && end + n * 36 <= sz && !raw[end - 1];
	size_t first = ok ? rd_be32 (raw + end + 32) : 0;
	ok = ok && first >= end + n * 36 && first <= sz;
	for (size_t i = 1; ok && i < n; i++)
	{
		const size_t o0 = rd_be32 (raw + end + 36 * (i - 1) + 32), o1 = rd_be32 (raw + end + 36 * i + 32);
		ok = o1 >= o0 && o1 <= sz;
	}
	if (ok)
	{
		char *txt = CALLOC (n * 128 + 256, 1);
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT MIDWAY-PHA:%s (%zu animations) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		size_t o = txt ? (size_t)sprintf (txt, "# name start_frame speed blend payload_bytes\n") : 0;
		const u8 *nm = raw + 0x20;
		err = txt ? ERR_OK : ERR_NOTHING_TO_DO;
		for (size_t i = 0; txt && i < n && !err; i++)
		{
			const u8 *r = raw + end + 36 * i;
			const size_t a = rd_be32 (r + 32), b = i + 1 < n ? rd_be32 (raw + end + 36 * (i + 1) + 32) : sz;
			char nm1[96], name[160];
			if (nm >= raw + end)
				break;
			snprintf (nm1, sizeof (nm1), "%.80s", (ccp)nm);
			nm += strlen ((ccp)nm) + 1;
			uint sp = rd_be32 (r + 12), bl = rd_be32 (r + 16);
			float fs, fb;
			memcpy (&fs, &sp, 4);
			memcpy (&fb, &bl, 4);
			o += (size_t)sprintf (txt + o, "%s %u %.4f %.4f %zu\n", nm1, rd_be32 (r), fs, fb, b - a);
			for (char *c = nm1; *c; c++)
				if (!isalnum ((u8)*c) && *c != '_' && *c != '-')
					*c = '_';
			snprintf (name, sizeof (name), "%04zu_%s.anm", i, nm1);
			if (b > a)
				err = shade_write (arg, dest, basedir, name, raw + a, (uint)(b - a));
		}
		if (!err)
			err = shade_write (arg, dest, basedir, "index.txt", (u8 *)txt, (uint)o);
		FREE (txt);
	}
	FREE (raw);
	return err;
}

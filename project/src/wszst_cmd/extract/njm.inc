// Ninja motion ("MDMN" = NMDM reversed, node animation; "MACN" = NCAM reversed, camera animation) used by Radirgy Noa and
// Illvelo (".njm"). Big endian: tag, u32 size, then {u32 mdata offset, u32 frame count, u16 type, u16 inp_fn} (offsets are
// relative to file + 8). inp_fn & 15 is the number of channels, taken from the set bits of "type" in ascending order.
// mdata is an array of nodes (MDMN: (size + 8 - mdata) / (8 * channels) of them, MACN: one), each node {u32 key offset[n],
// u32 key count[n]}. Key sizes: bit0 position and bit2 scale / bit6 target = u32 frame + 3 floats (16 bytes); bit5 rotation =
// u16 frame + 3 s16 BAMS angles (8 bytes); bit7 roll and bit8 field of view = u16 frame + s16 BAMS angle (4 bytes).
// Written as "motion.json" in the "<file>.d" directory (angles in degrees).
typedef struct njm_out_t { char *b; size_t used, cap; } njm_out_t;

static void njm_put (njm_out_t *o, const char *fmt, ...)
{
	va_list ap;
	char tmp[256];
	va_start (ap, fmt);
	const int n = vsnprintf (tmp, sizeof (tmp), fmt, ap);
	va_end (ap);
	if (n <= 0 || !o->b)
		return;
	if (o->used + (size_t)n + 1 > o->cap)
	{
		size_t nc = o->cap * 2 + n + 1;
		char *nb = MALLOC (nc);
		if (!nb)
		{
			FREE (o->b);
			o->b = 0;
			return;
		}
		memcpy (nb, o->b, o->used);
		FREE (o->b);
		o->b = nb;
		o->cap = nc;
	}
	memcpy (o->b + o->used, tmp, n + 1);
	o->used += n;
}

static float njm_f (const u8 *p)
{
	const u32 v = rd_be32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static enumError extract_njm_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".njm"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x14 || sz > 0x10000000
		|| (memcmp (raw, "MDMN", 4) && memcmp (raw, "MACN", 4)))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	static const struct { uint bit, ks; ccp name; } CH[] = { { 0, 16, "pos" }, { 2, 16, "scl" }, { 5, 8, "rot" },
		{ 6, 16, "target" }, { 7, 4, "roll" }, { 8, 4, "fov" } };
	const bool cam = !memcmp (raw, "MACN", 4);
	const u32 tsz = rd_be32 (raw + 4), mo = rd_be32 (raw + 8) + 8, nfr = rd_be32 (raw + 12);
	const uint type = rd_be16 (raw + 16), n = rd_be16 (raw + 18) & 15;
	uint sel[8], nsel = 0;
	for (uint b = 0; b < 16; b++)
		if (type >> b & 1)
		{
			uint k = 0;
			while (k < sizeof (CH) / sizeof (*CH) && CH[k].bit != b)
				k++;
			if (k == sizeof (CH) / sizeof (*CH) || nsel == 8)
				goto bad;
			sel[nsel++] = k;
		}
	if (nsel != n || !n || mo > sz || mo + 8 * (size_t)n > sz)
		goto bad;
	{
		const uint nodes = cam ? 1 : tsz + 8 > mo ? (uint)((tsz + 8 - mo) / (8 * n)) : 0;
		njm_out_t o = { MALLOC (1 << 16), 0, 1 << 16 };
		if (!o.b)
			goto bad;
		njm_put (&o, "{\n\"kind\": \"%s\",\n\"frames\": %u,\n\"nodes\": [", cam ? "camera" : "nodes", nfr);
		for (uint i = 0; i < nodes && mo + 8 * (size_t)n * (i + 1) <= sz; i++)
		{
			const u8 *nd = raw + mo + 8 * (size_t)n * i;
			njm_put (&o, "%s\n {", i ? "," : "");
			bool first = true;
			for (uint c = 0; c < n; c++)
			{
				const uint ks = CH[sel[c]].ks;
				const u32 off = rd_be32 (nd + 4 * c) + 8, cnt = rd_be32 (nd + 4 * n + 4 * c);
				if (!cnt)
					continue;
				if (off > sz || cnt > (sz - off) / ks)
					continue;
				njm_put (&o, "%s\"%s\": [", first ? "" : ", ", CH[sel[c]].name);
				first = false;
				for (uint k = 0; k < cnt; k++)
				{
					const u8 *p = raw + off + (size_t)k * ks;
					if (ks == 16)
						njm_put (&o, "%s[%u,%g,%g,%g]", k ? "," : "", rd_be32 (p), njm_f (p + 4), njm_f (p + 8), njm_f (p + 12));
					else if (ks == 8)
						njm_put (&o, "%s[%u,%.3f,%.3f,%.3f]", k ? "," : "", rd_be16 (p), (short)rd_be16 (p + 2) * 360.0 / 65536,
							(short)rd_be16 (p + 4) * 360.0 / 65536, (short)rd_be16 (p + 6) * 360.0 / 65536);
					else
						njm_put (&o, "%s[%u,%.3f]", k ? "," : "", rd_be16 (p), (short)rd_be16 (p + 2) * 360.0 / 65536);
				}
				njm_put (&o, "]");
			}
			njm_put (&o, "}");
		}
		njm_put (&o, "\n]\n}\n");
		enumError err = ERR_OK;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT NINJA-MOTION:%s (%u nodes, %u frames) -> %s/motion.json\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, nodes, nfr, dest);
		if (!testmode)
		{
			if (!o.b)
				err = ERR_OUT_OF_MEMORY;
			else
				err = shade_write (arg, dest, basedir, "motion.json", (u8 *)o.b, (uint)o.used);
		}
		FREE (o.b);
		FREE (raw);
		return err;
	}
bad:
	FREE (raw);
	return ERR_NOTHING_TO_DO;
}

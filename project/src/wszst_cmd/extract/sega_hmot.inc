// SEGA "HMOT" motions (Sakura Wars: So Long, My Love; ".hmot" members of ".amf"/".urs" archives; little endian), layout recovered by analysis of 73 motions of the retail disc.
// {"HMOT", u32 size, u32 header size 0x20, 0, then a "KFMO" chunk at 0x20 whose offsets are all relative to 0x20}. KFMO: {"KFMO", u32 size, u32 0x10, 0}, u32 flags, u16 bone count,
// u16 1, f32 frame count, u32 offset of the bone table. A bone table entry is 16 bytes {u32 0, u32 channel mask << 24, u32 offset of the curve offset list (0 = none), u32 offset of the base
// values}. Channels follow the set bits of the mask from bit 0 up (bits 0-3 rotation components, bit 4 unused, bits 5-7 translation x y z); the base values are one f32 per set bit and the curve
// offset list one u32 per set bit (0 = channel is static). A curve starts with a 16-byte header {u8 1, u8 bytes per sample, u8 binary point, 0, 8 zero bytes, u32 offset of the samples}; with
// 2 bytes per sample the samples are frames + 1 s16 values divided by 2^binary point (one per frame, the last equals the first on looping motions). Curves with 3 bytes per sample (rare,
// all zero in the checked files) are written as raw bytes. How curve values combine with the base values is not verified: both are written as found. Written as "<name>.json".
static bool hmot_chan (uint bit, char *name, size_t sz)
{
	if (bit < 4)
		snprintf (name, sz, "rot%u", bit);
	else if (bit == 4)
		snprintf (name, sz, "chan4");
	else
		snprintf (name, sz, "t%c", "xyz"[bit - 5]);
	return true;
}

static enumError extract_sega_hmot_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".hmot"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x60 || memcmp (c, "HMOT", 4) || memcmp (c + 0x20, "KFMO", 4))
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	const uint nb = rd_le16 (c + 0x34), tab = 0x20 + rd_le32 (c + 0x3c);
	float frames;
	{
		const u32 fv = rd_le32 (c + 0x38);
		memcpy (&frames, &fv, 4);
	}
	const uint nf = frames > 0 && frames < 100000 ? (uint)(frames + 0.5f) : 0;
	if (!nb || nb > 1024 || (u64)tab + 16ull * nb > cs)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	uint ncurve = 0;
	sumo_printf (&b, "{\n\t\"format\": \"Sakura Wars HMOT\",\n\t\"frames\": %u,\n\t\"bones\": [", nf);
	for (uint i = 0; i < nb; i++)
	{
		const u8 *e = c + tab + 16 * i;
		const uint mask = e[7], pc = (uint)__builtin_popcount (mask);
		const u64 ao = rd_le32 (e + 8), bo = rd_le32 (e + 12);
		sumo_printf (&b, "%s\n\t\t{\"bone\": %u, \"mask\": %u", i ? "," : "", i, mask);
		uint k = 0;
		for (uint bit = 0; bit < 8; bit++)
		{
			if (!(mask >> bit & 1))
				continue;
			char name[16];
			hmot_chan (bit, name, sizeof (name));
			sumo_printf (&b, ", \"%s\": {", name);
			if (0x20 + bo + 4ull * (k + 1) <= cs)
			{
				const u32 fv = rd_le32 (c + 0x20 + bo + 4 * k);
				float f;
				memcpy (&f, &fv, 4);
				sumo_printf (&b, "\"base\": %.9g", (double)f);
			}
			const u64 co = ao && 0x20 + ao + 4ull * (k + 1) <= cs ? rd_le32 (c + 0x20 + ao + 4 * k) : 0;
			if (co && 0x20 + co + 16 <= cs)
			{
				const u8 *h = c + 0x20 + co;
				const u64 sp = 0x20 + (u64)rd_le32 (h + 12);
				if (h[0] == 1 && h[1] == 2 && sp + 2ull * (nf + 1) <= cs)
				{
					sumo_printf (&b, ", \"curve\": [");
					for (uint s = 0; s <= nf; s++)
						sumo_printf (&b, "%s%.6g", s ? ", " : "", (short)rd_le16 (c + sp + 2 * s) / (double)(1u << (h[2] & 31)));
					sumo_printf (&b, "]");
					ncurve++;
				}
				else if (h[0] == 1 && sp < cs)
				{
					sumo_printf (&b, ", \"raw_bytes_per_sample\": %u, \"binary_point\": %u", h[1], h[2]);
					ncurve++;
				}
			}
			sumo_printf (&b, "}");
			k++;
		}
		(void)pc;
		sumo_printf (&b, "}");
	}
	sumo_printf (&b, "\n\t]\n}\n");
	FREE (c);
	enumError err = ERR_NOTHING_TO_DO;
	if (b.ok && b.n && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".json");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SEGA-HMOT:%s (%u bones, %u frames, %u curves) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nb, nf, ncurve, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}

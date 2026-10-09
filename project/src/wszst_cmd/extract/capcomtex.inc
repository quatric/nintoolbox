// Capcom MT Framework Wii textures / palettes (Sengoku Basara 3 ".tex" = "\0XET", ".plt" =
// "\0TLP"): a 0x20-byte big-endian header ({magic, u32 version, u32 0x20, ...}) in front of a
// plain BRRES that holds the TEX0 / PLT0 data. The BRRES is written as "<name>.brres" so the
// regular BRRES extraction (xx: PNG) runs on it. C4/C8 textures keep their palette in the
// sibling ".plt" (PLT0), so in decode mode (xx) those are also decoded to "<file>.png" here.
static enumError extract_capcomtex_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".tex") && !is_ext (arg, ".plt"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x40 || (memcmp (raw, "\0XET", 4) && memcmp (raw, "\0TLP", 4)) || rd_be32 (raw + 8) != 0x20
		|| memcmp (raw + 0x20, "bres", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if ((export_count > 0 || opt_decode) && is_ext (arg, ".tex"))
	{
		const u8 *b = raw + 0x20;
		const size_t bsz = sz - 0x20;
		size_t t0 = 0;
		for (size_t k = 0; k + 0x30 <= bsz; k += 4)
			if (!memcmp (b + k, "TEX0", 4))
			{
				t0 = k;
				break;
			}
		if (t0)
		{
			const uint w = rd_be16 (b + t0 + 0x1c), h = rd_be16 (b + t0 + 0x1e), fmt = rd_be32 (b + t0 + 0x20);
			const size_t doff = rd_be32 (b + t0 + 0x10);
			char pltname[PATH_MAX];
			snprintf (pltname, sizeof (pltname), "%s", arg);
			strcpy (pltname + strlen (pltname) - 4, ".plt");
			u8 *plt = 0;
			size_t plt_sz = 0;
			if ((fmt == 8 || fmt == 9) && w && h && w <= 2048 && h <= 2048 && t0 + doff < bsz
				&& !LoadFileAlloc (pltname, 0, 0, &plt, &plt_sz, 0, 0, 0, false) && plt && plt_sz > 0x80
				&& !memcmp (plt, "\0TLP", 4))
			{
				const u8 *pb = plt + 0x20;
				size_t p0 = 0;
				for (size_t k = 0; k + 0x30 <= plt_sz - 0x20; k += 4)
					if (!memcmp (pb + k, "PLT0", 4))
					{
						p0 = k;
						break;
					}
				const size_t pdoff = p0 ? rd_be32 (pb + p0 + 0x10) : 0;
				const uint pfmt = p0 ? rd_be32 (pb + p0 + 0x18) : 0, pn = p0 ? rd_be16 (pb + p0 + 0x1c) : 0;
				if (p0 && pn && p0 + pdoff + 2 * (size_t)pn <= plt_sz - 0x20)
				{
					u8 *rgba = eashpg_decode (fmt, b + t0 + doff, bsz - t0 - doff, w, h, pb + p0 + pdoff, pn, pfmt);
					if (rgba)
					{
						char png[PATH_MAX];
						snprintf (png, sizeof (png), "%s.png", arg);
						if (verbose >= 0 || testmode)
							fprintf (stdlog, "%s%sDECODE CAPCOM-TEX:%s %ux%u + PLT0 -> PNG:%s\n",
								verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h, png);
						if (testmode)
							FREE (rgba);
						else
							SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png, 0, true);
					}
				}
			}
			FREE (plt);
		}
	}
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	char name[PATH_MAX];
	snprintf (name, sizeof (name), "%s", base);
	char *dot = strrchr (name, '.');
	if (dot)
		*dot = 0;
	snprintf (name + strlen (name), sizeof (name) - strlen (name), ".brres");
	nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
	if (!entries || !OwnedEntryAdd (entries, 0, name, raw + 0x20, sz - 0x20))
	{
		FREE (entries);
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	FREE (raw);
	return write_owned_entries (arg, basedir, depth, "CAPCOM-TEX", entries, 1);
}

// Capcom MT Framework Wii models (Sengoku Basara 3 ".rmd" = "\0DMR"): a big-endian skeleton / material
// / bone-remap header (bone map @0x30, table of section offsets @0x130) that wraps an ordinary
// BRRES holding the MDL0 model. Its offset is the u32 @0x154 and its size the u32 @0x15c. The BRRES is
// written as "<name>.brres" so the regular BRRES extraction runs on it.
static enumError extract_capcomrmd_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".rmd"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t off = 0, len = 0;
	if (sz > 0x200 && !memcmp (raw, "\0DMR", 4))
	{
		off = rd_be32 (raw + 0x154);
		len = rd_be32 (raw + 0x15c);
		if (!off || off + 0x20 > sz || memcmp (raw + off, "bres", 4))
			off = 0;
		else if (!len || off + len > sz)
			len = sz - off;
	}
	if (!off)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	char name[PATH_MAX];
	snprintf (name, sizeof (name), "%s", base);
	char *dot = strrchr (name, '.');
	if (dot)
		*dot = 0;
	snprintf (name + strlen (name), sizeof (name) - strlen (name), ".brres");
	nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
	if (!entries || !OwnedEntryAdd (entries, 0, name, raw + off, len))
	{
		FREE (entries);
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	FREE (raw);
	return write_owned_entries (arg, basedir, depth, "CAPCOM-RMD", entries, 1);
}

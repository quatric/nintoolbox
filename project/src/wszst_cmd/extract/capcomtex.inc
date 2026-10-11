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

// Capcom MT Framework Wii motion banks (Sengoku Basara 3 ".lmt" = "\0TML"): u16 version @4 (0x38),
// u16 slot count @6, then big-endian u32 motion offsets (0 = empty slot). Each motion is 0x150 bytes
// {u32 track table, u32 track count, u32 frames, u32 loop frame, ...}; tracks are 0x24 bytes
// {u8 compression, u8 usage, u8 type, u8 bone, f32 weight, u32 buffer size, u32 buffer offset, f32[4] ref}.
// The keyframe buffers are not decoded; "<name>.txt" lists the validated motion / track layout.
static enumError extract_capcomlmt_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".lmt"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x10 || memcmp (raw, "\0TML", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = ((uint)raw[6] << 8) | raw[7];
	if (!count || 8 + (size_t)count * 4 > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	// first pass: every non-empty motion and its track table must lie inside the file
	uint motions = 0;
	for (uint i = 0; i < count; i++)
	{
		const u32 mo = rd_be32 (raw + 8 + i * 4);
		if (!mo)
			continue;
		if (mo > sz || sz - mo < 0x150)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		const u32 to = rd_be32 (raw + mo), tc = rd_be32 (raw + mo + 4);
		if (tc && (to > sz || (size_t)tc * 0x24 > sz - to))
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		motions++;
	}
	if (!motions)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t cap = 256, used = 0;
	char *txt = MALLOC (cap);
	if (!txt)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
#define LMT_PRINT(...) \
	do { \
		for (;;) { \
			int n_ = snprintf (txt + used, cap - used, __VA_ARGS__); \
			if (n_ >= 0 && (size_t)n_ < cap - used) { used += n_; break; } \
			char *t_ = REALLOC (txt, cap *= 2); \
			if (!t_) { FREE (txt); FREE (raw); return ERR_OUT_OF_MEMORY; } \
			txt = t_; \
		} \
	} while (0)
	LMT_PRINT ("Capcom LMT version 0x%02x, %u slots, %u motions\n", raw[5], count, motions);
	for (uint i = 0; i < count; i++)
	{
		const u32 mo = rd_be32 (raw + 8 + i * 4);
		if (!mo)
			continue;
		const u32 to = rd_be32 (raw + mo), tc = rd_be32 (raw + mo + 4);
		LMT_PRINT ("motion %u: offset 0x%x, %u tracks, %u frames, loop frame %d\n", i, mo, tc,
			   rd_be32 (raw + mo + 8), (int)rd_be32 (raw + mo + 12));
		for (u32 t = 0; t < tc; t++)
		{
			const u8 *tr = raw + to + t * 0x24;
			const u32 bl = rd_be32 (tr + 8), bo = rd_be32 (tr + 12);
			LMT_PRINT ("  track %u: compression %u, usage %u, type %u, bone %u, buffer 0x%x+%u%s\n", t,
				   tr[0], tr[1], tr[2], tr[3], bo, bl,
				   (bo > sz || bl > sz - bo) ? " (outside file)" : "");
		}
	}
#undef LMT_PRINT
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	char name[PATH_MAX];
	snprintf (name, sizeof (name), "%s", base);
	char *dot = strrchr (name, '.');
	if (dot)
		*dot = 0;
	snprintf (name + strlen (name), sizeof (name) - strlen (name), ".txt");
	nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
	if (!entries || !OwnedEntryAdd (entries, 0, name, (u8 *)txt, used))
	{
		FREE (entries);
		FREE (txt);
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	FREE (txt);
	FREE (raw);
	return write_owned_entries (arg, basedir, depth, "CAPCOM-LMT", entries, 1);
}

// Capcom MT Framework Wii serialized objects (Sengoku Basara 3 ".xfs" = "\0SFX"): u16 version @4 (8),
// u32 class count @8, and the class table at 0x10 + u32@0x10. Each class is {u32 crc, u32 info} with
// info >> 17 members, followed by that many 0x38-byte members {u32 name offset (relative to 0x10), u8 type,
// u8 flags, u16 size, default value bytes}. Names are NUL-terminated strings in the string table. The object
// data after the string table is not decoded; "<name>.txt" lists the validated class / member layout.
static enumError extract_capcomxfs_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".xfs"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x30 || memcmp (raw, "\0SFX", 4) || rd_be16 (raw + 4) != 8)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 classes = rd_be32 (raw + 8);
	const u32 tab = 0x10 + (size_t)rd_be32 (raw + 0x10);
	if (!classes || classes > 0x100 || tab > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	// first pass: every class header, member record and member name must lie inside the file
	size_t p = tab;
	for (u32 c = 0; c < classes; c++)
	{
		if (p > sz || sz - p < 8)
			goto bad;
		const u32 members = rd_be32 (raw + p + 4) >> 17;
		p += 8;
		if ((size_t)members * 0x38 > sz - p)
			goto bad;
		for (u32 m = 0; m < members; m++, p += 0x38)
		{
			const size_t no = 0x10 + (size_t)rd_be32 (raw + p);
			if (no >= sz || !memchr (raw + no, 0, sz - no < 128 ? sz - no : 128))
				goto bad;
		}
	}
	{
		size_t cap = 256, used = 0;
		char *txt = MALLOC (cap);
		if (!txt)
		{
			FREE (raw);
			return ERR_OUT_OF_MEMORY;
		}
#define XFS_PRINT(...) \
	do { \
		for (;;) { \
			int n_ = snprintf (txt + used, cap - used, __VA_ARGS__); \
			if (n_ >= 0 && (size_t)n_ < cap - used) { used += n_; break; } \
			char *t_ = REALLOC (txt, cap *= 2); \
			if (!t_) { FREE (txt); FREE (raw); return ERR_OUT_OF_MEMORY; } \
			txt = t_; \
		} \
	} while (0)
		XFS_PRINT ("Capcom XFS version %u, %u classes\n", rd_be16 (raw + 4), classes);
		p = tab;
		for (u32 c = 0; c < classes; c++)
		{
			const u32 members = rd_be32 (raw + p + 4) >> 17;
			XFS_PRINT ("class %u: crc 0x%08x, %u members\n", c, rd_be32 (raw + p), members);
			p += 8;
			for (u32 m = 0; m < members; m++, p += 0x38)
				XFS_PRINT ("  %s: type 0x%02x, flags 0x%02x, size %u\n",
					   (ccp)raw + 0x10 + rd_be32 (raw + p), raw[p + 4], raw[p + 5], rd_be16 (raw + p + 6));
		}
#undef XFS_PRINT
		ccp base = strrchr (arg, '/');
		base = base ? base + 1 : arg;
		char name[PATH_MAX];
		snprintf (name, sizeof (name), "%s", base);
		char *dot = strrchr (name, '.');
		if (dot)
			*dot = 0;
		snprintf (name + strlen (name), sizeof (name) - strlen (name), ".txt");
		nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
		if (!entries || !OwnedEntryAdd (entries, 0, name, (u8 *)txt, used))
		{
			FREE (entries);
			FREE (txt);
			FREE (raw);
			return ERR_OUT_OF_MEMORY;
		}
		FREE (txt);
		FREE (raw);
		return write_owned_entries (arg, basedir, depth, "CAPCOM-XFS", entries, 1);
	}
bad:
	FREE (raw);
	return ERR_NOTHING_TO_DO;
}

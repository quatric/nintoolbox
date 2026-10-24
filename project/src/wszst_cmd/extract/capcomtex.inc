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
// {u8 bone, u8 joint type, u8 usage (0 rotation, 1 translation, 4 root translation), u8 buffer type,
// f32 weight, u32 buffer size, u32 buffer offset, f32[4] reference (the frame-0 value), u32 extra offset}.
// Buffer types: 1 / 2 hold a constant translation / rotation in the reference and have no buffer; 4 / 5 are
// 16 / 8 bit "bilinear" translations and 7 a 7 bit rotation; 6 is a 14 bit rotation. Every key packs its
// components plus the number of frames up to the next key (0 on the last key). Types 4, 5 and 7 quantize
// each component to [8, 2^bits - 8] and scale it with the 2 x vec4 {multiplier, base} at the extra offset:
// value = base + multiplier * (q - 8) / (2^bits - 16). Type 6 needs no extra data: each of the four 14 bit
// fields is a signed fixed-point value with 12 fractional bits. Keys are interpolated linearly between
// frames. "<name>.txt" lists the validated layout and the decoded keys.
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
			const u32 bl = rd_be32 (tr + 8), bo = rd_be32 (tr + 12), ex = rd_be32 (tr + 32);
			const uint bt = tr[3];
			float ref[4];
			for (uint c = 0; c < 4; c++)
			{
				const u32 b = rd_be32 (tr + 16 + c * 4);
				memcpy (ref + c, &b, 4);
			}
			LMT_PRINT ("  track %u: bone %u, usage %u, joint %u, buffer type %u, buffer 0x%x+%u%s, "
				   "ref %.6f %.6f %.6f %.6f\n", t, tr[0], tr[2], tr[1], bt, bo, bl,
				   (bo > sz || bl > sz - bo) ? " (outside file)" : "", ref[0], ref[1], ref[2],
				   ref[3]);
			if (bt == 1 || bt == 2)
			{
				LMT_PRINT ("    constant %.6f %.6f %.6f", ref[0], ref[1], ref[2]);
				if (bt == 2)
					LMT_PRINT (" %.6f", ref[3]);
				LMT_PRINT ("\n");
				continue;
			}
			// key size and components per type; others are listed only
			const uint ksize = bt == 4 ? 8 : bt == 5 ? 4 : bt == 6 ? 8 : bt == 7 ? 4 : 0;
			if (!ksize || !bl || bo > sz || bl > sz - bo || bl % ksize)
				continue;
			float mult[4] = { 0, 0, 0, 0 }, base[4] = { 0, 0, 0, 0 };
			if (bt != 6)
			{
				if (!ex || ex > sz || sz - ex < 32)
					continue;
				for (uint c = 0; c < 4; c++)
				{
					const u32 m = rd_be32 (raw + ex + c * 4), b = rd_be32 (raw + ex + 16 + c * 4);
					memcpy (mult + c, &m, 4);
					memcpy (base + c, &b, 4);
				}
			}
			uint frame = 0;
			for (uint k = 0; k < bl / ksize; k++)
			{
				const u8 *kp = raw + bo + k * ksize;
				float v[4] = { 0, 0, 0, 0 };
				uint dt = 0, nc = 3;
				if (bt == 4)
				{
					for (uint c = 0; c < 3; c++)
						v[c] = base[c] + mult[c] * ((int)(kp[c * 2] << 8 | kp[c * 2 + 1]) - 8) / 65520.0f;
					dt = kp[6] << 8 | kp[7];
				}
				else if (bt == 5)
				{
					for (uint c = 0; c < 3; c++)
						v[c] = base[c] + mult[c] * ((int)kp[c] - 8) / 240.0f;
					dt = kp[3];
				}
				else if (bt == 6)
				{
					const u64 key = (u64)rd_be32 (kp) << 32 | rd_be32 (kp + 4);
					dt = key >> 56;
					nc = 4;
					for (uint c = 0; c < 4; c++)
					{
						int q = (key >> (42 - 14 * c)) & 0x3fff;
						v[c] = (q >= 0x2000 ? q - 0x4000 : q) / 4096.0f;
					}
				}
				else
				{
					const u32 key = rd_be32 (kp);
					dt = key >> 28;
					nc = 4;
					for (uint c = 0; c < 4; c++)
						v[c] = base[c] + mult[c] * ((int)((key >> (21 - 7 * c)) & 127) - 8) / 112.0f;
				}
				LMT_PRINT ("    frame %u: %.6f %.6f %.6f", frame, v[0], v[1], v[2]);
				if (nc == 4)
					LMT_PRINT (" %.6f", v[3]);
				LMT_PRINT ("\n");
				frame += dt;
			}
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

// Capcom MT Framework Wii attack tables (Sengoku Basara 3 ".atk" = "\0KTA"): u16 version @4 (0x1021),
// u16 count @6, followed by count 176-byte attack records {char name[8], u8 flags[8], f32 damage @0x10, ...}.
// "<name>.txt" lists the validated attack records.
static enumError extract_capcomatk_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".atk"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 8 || memcmp (raw, "\0KTA", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint ver = rd_be16 (raw + 4);
	const uint count = rd_be16 (raw + 6);
	if (!count || 8 + (size_t)count * 176 != sz)
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
#define ATK_PRINT(...) \
	do { \
		for (;;) { \
			int n_ = snprintf (txt + used, cap - used, __VA_ARGS__); \
			if (n_ >= 0 && (size_t)n_ < cap - used) { used += n_; break; } \
			char *t_ = REALLOC (txt, cap *= 2); \
			if (!t_) { FREE (txt); FREE (raw); return ERR_OUT_OF_MEMORY; } \
			txt = t_; \
		} \
	} while (0)
	ATK_PRINT ("Capcom ATK version 0x%04x, %u attacks\n", ver, count);
	for (uint i = 0; i < count; i++)
	{
		const u8 *rec = raw + 8 + i * 176;
		char name[9];
		memcpy (name, rec, 8);
		name[8] = 0;
		for (int j = 7; j >= 0; j--)
		{
			if ((u8)name[j] <= ' ' || (u8)name[j] >= 0x7f)
				name[j] = 0;
			else
				break;
		}
		u32 raw_dmg = rd_be32 (rec + 0x10);
		float dmg = 0.0f;
		memcpy (&dmg, &raw_dmg, sizeof (dmg));
		ATK_PRINT ("attack %u: name \"%s\", damage %.1f, flags 0x%02x%02x%02x%02x, attr 0x%08x\n",
			   i, name[0] ? name : "NONAME", dmg, rec[8], rec[9], rec[10], rec[11], rd_be32 (rec + 12));
	}
#undef ATK_PRINT
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
	return write_owned_entries (arg, basedir, depth, "CAPCOM-ATK", entries, 1);
}

// Capcom MT Framework Wii actor index tables (Sengoku Basara 3 ".idx" = "\0XDI"): u32 count @4,
// followed by count 4-byte mapping IDs, followed by count 20-byte record descriptors (u32[5]).
// "<name>.txt" lists the index mapping table.
static enumError extract_capcomidx_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".idx"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 8 || memcmp (raw, "\0XDI", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 count = rd_be32 (raw + 4);
	if (!count || 8 + (size_t)count * 24 != sz)
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
#define IDX_PRINT(...) \
	do { \
		for (;;) { \
			int n_ = snprintf (txt + used, cap - used, __VA_ARGS__); \
			if (n_ >= 0 && (size_t)n_ < cap - used) { used += n_; break; } \
			char *t_ = REALLOC (txt, cap *= 2); \
			if (!t_) { FREE (txt); FREE (raw); return ERR_OUT_OF_MEMORY; } \
			txt = t_; \
		} \
	} while (0)
	IDX_PRINT ("Capcom IDX %u entries\n", count);
	const size_t rec_base = 8 + (size_t)count * 4;
	for (u32 i = 0; i < count; i++)
	{
		const u32 id = rd_be32 (raw + 8 + i * 4);
		const u8 *rec = raw + rec_base + i * 20;
		IDX_PRINT ("entry %u: id 0x%x (%u), data [0x%x, 0x%x, 0x%x, 0x%x, 0x%x]\n",
			   i, id, id, rd_be32 (rec), rd_be32 (rec + 4), rd_be32 (rec + 8), rd_be32 (rec + 12), rd_be32 (rec + 16));
	}
#undef IDX_PRINT
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
	return write_owned_entries (arg, basedir, depth, "CAPCOM-IDX", entries, 1);
}

// Capcom MT Framework Wii effect link tables (Sengoku Basara 3 ".eco" = "\0OCE"): u16 version @4,
// u16 count @6, u32 table offset @0x10. Followed by count 8-byte descriptors {u32 id, u32 str_offset}
// pointing into NUL-terminated .efl path strings.
// "<name>.txt" lists the linked effect files.
static enumError extract_capcomeco_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".eco"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x14 || memcmp (raw, "\0OCE", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint ver = rd_be16 (raw + 4);
	const uint count = rd_be16 (raw + 6);
	const u32 toff = rd_be32 (raw + 0x10);
	if (!count || toff > sz || 0x14 + (size_t)count * 8 > toff)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	for (uint i = 0; i < count; i++)
	{
		const u32 so = rd_be32 (raw + 0x14 + i * 8 + 4);
		if (so != 0xffffffff && (toff + so >= sz || !memchr (raw + toff + so, 0, sz - (toff + so))))
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	}
	size_t cap = 256, used = 0;
	char *txt = MALLOC (cap);
	if (!txt)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
#define ECO_PRINT(...) \
	do { \
		for (;;) { \
			int n_ = snprintf (txt + used, cap - used, __VA_ARGS__); \
			if (n_ >= 0 && (size_t)n_ < cap - used) { used += n_; break; } \
			char *t_ = REALLOC (txt, cap *= 2); \
			if (!t_) { FREE (txt); FREE (raw); return ERR_OUT_OF_MEMORY; } \
			txt = t_; \
		} \
	} while (0)
	ECO_PRINT ("Capcom ECO version %u, %u effect links\n", ver, count);
	for (uint i = 0; i < count; i++)
	{
		const u32 id = rd_be32 (raw + 0x14 + i * 8);
		const u32 so = rd_be32 (raw + 0x14 + i * 8 + 4);
		if (so == 0xffffffff)
			ECO_PRINT ("effect %u: id %u, unused\n", i, id);
		else
			ECO_PRINT ("effect %u: id %u, path \"%s\"\n", i, id, (ccp)raw + toff + so);
	}
#undef ECO_PRINT
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
	return write_owned_entries (arg, basedir, depth, "CAPCOM-ECO", entries, 1);
}

// Capcom MT Framework Wii effect files (Sengoku Basara 3 ".efl" = "\0LFE"): u32 date @4 (0x20090914),
// u32 payload size @8, u32 count @0x0c, followed by 16-byte parameter/bounding header, followed by
// count 16-byte effect chunk records {u32 d0, u32 d1, u32 d2, u32 d3}.
// "<name>.txt" lists the effect chunks and offsets.
static enumError extract_capcomefl_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".efl"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x20 || memcmp (raw, "\0LFE", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 date = rd_be32 (raw + 4);
	const u32 psz = rd_be32 (raw + 8);
	const u32 count = rd_be32 (raw + 12);
	if (!count || 0x20 + psz != sz || 0x20 + (size_t)count * 16 > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	for (u32 i = 0; i < count; i++)
	{
		const u32 d0 = rd_be32 (raw + 0x20 + i * 16);
		const size_t off = 0x20 + (size_t)(d0 & 0x00ffffff);
		if (off >= sz)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	}
	size_t cap = 256, used = 0;
	char *txt = MALLOC (cap);
	if (!txt)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
#define EFL_PRINT(...) \
	do { \
		for (;;) { \
			int n_ = snprintf (txt + used, cap - used, __VA_ARGS__); \
			if (n_ >= 0 && (size_t)n_ < cap - used) { used += n_; break; } \
			char *t_ = REALLOC (txt, cap *= 2); \
			if (!t_) { FREE (txt); FREE (raw); return ERR_OUT_OF_MEMORY; } \
			txt = t_; \
		} \
	} while (0)
	EFL_PRINT ("Capcom EFL date 0x%08x, %u chunks, payload %u bytes\n", date, count, psz);
	for (u32 i = 0; i < count; i++)
	{
		const u32 d0 = rd_be32 (raw + 0x20 + i * 16);
		const u32 d1 = rd_be32 (raw + 0x20 + i * 16 + 4);
		const u32 d2 = rd_be32 (raw + 0x20 + i * 16 + 8);
		const u32 d3 = rd_be32 (raw + 0x20 + i * 16 + 12);
		EFL_PRINT ("chunk %u: type 0x%02x, offset 0x%x, d1 0x%08x, ref 0x%08x 0x%08x\n",
			   i, d0 >> 24, (uint)(0x20 + (d0 & 0x00ffffff)), d1, d2, d3);
	}
#undef EFL_PRINT
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
	return write_owned_entries (arg, basedir, depth, "CAPCOM-EFL", entries, 1);
}

// Capcom MT Framework Wii scripted resource tables (Sengoku Basara 3 ".scra" = "SCRA"): u16 version @4 (7),
// u16 count @6, followed by count 8-byte resource descriptors {u32 type_hash, u32 resource_hash}.
// "<name>.txt" lists the referenced resource hashes.
static enumError extract_capcomscra_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".scra"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 8 || memcmp (raw, "SCRA", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint ver = rd_be16 (raw + 4);
	const uint count = rd_be16 (raw + 6);
	if (!count || 8 + (size_t)count * 8 != sz)
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
#define SCRA_PRINT(...) \
	do { \
		for (;;) { \
			int n_ = snprintf (txt + used, cap - used, __VA_ARGS__); \
			if (n_ >= 0 && (size_t)n_ < cap - used) { used += n_; break; } \
			char *t_ = REALLOC (txt, cap *= 2); \
			if (!t_) { FREE (txt); FREE (raw); return ERR_OUT_OF_MEMORY; } \
			txt = t_; \
		} \
	} while (0)
	SCRA_PRINT ("Capcom SCRA version %u, %u resource references\n", ver, count);
	for (uint i = 0; i < count; i++)
	{
		const u32 type_hash = rd_be32 (raw + 8 + i * 8);
		const u32 res_hash = rd_be32 (raw + 8 + i * 8 + 4);
		SCRA_PRINT ("ref %u: type 0x%08x, resource 0x%08x\n", i, type_hash, res_hash);
	}
#undef SCRA_PRINT
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
	return write_owned_entries (arg, basedir, depth, "CAPCOM-SCRA", entries, 1);
}

// Capcom MT Framework Wii surround sound parameter tables (Sengoku Basara 3 ".srnd" = "SRND"): u32 version @4 (2),
// u32 count @8, followed by count 140-byte sound records {u32 sound_id, u32 flags, ...}.
// "<name>.txt" lists the surround sound parameters.
static enumError extract_capcomsrnd_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".srnd"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 16 || memcmp (raw, "SRND", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 ver = rd_be32 (raw + 4);
	const u32 count = rd_be32 (raw + 8);
	if (!count || 16 + (size_t)count * 140 != sz)
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
#define SRND_PRINT(...) \
	do { \
		for (;;) { \
			int n_ = snprintf (txt + used, cap - used, __VA_ARGS__); \
			if (n_ >= 0 && (size_t)n_ < cap - used) { used += n_; break; } \
			char *t_ = REALLOC (txt, cap *= 2); \
			if (!t_) { FREE (txt); FREE (raw); return ERR_OUT_OF_MEMORY; } \
			txt = t_; \
		} \
	} while (0)
	SRND_PRINT ("Capcom SRND version %u, %u sound definitions\n", ver, count);
	for (u32 i = 0; i < count; i++)
	{
		const u8 *rec = raw + 16 + i * 140;
		const u32 sound_id = rd_be32 (rec);
		const u32 flags = rd_be32 (rec + 4);
		SRND_PRINT ("sound %u: id 0x%x (%u), flags 0x%08x\n", i, sound_id, sound_id, flags);
	}
#undef SRND_PRINT
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
	return write_owned_entries (arg, basedir, depth, "CAPCOM-SRND", entries, 1);
}

// Capcom MT Framework Wii sound event request tables (Sengoku Basara 3 ".qers" = "QERS"): u32 version @4 (19),
// u32 count @8, followed by count 144-byte sound event records, followed by optional string table.
// "<name>.txt" lists the sound event requests and referenced sound bank paths.
static enumError extract_capcomqers_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".qers"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 128 || memcmp (raw, "QERS", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 ver = rd_be32 (raw + 4);
	const u32 count = rd_be32 (raw + 8);
	if (!count || 128 + (size_t)count * 144 > sz)
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
#define QERS_PRINT(...) \
	do { \
		for (;;) { \
			int n_ = snprintf (txt + used, cap - used, __VA_ARGS__); \
			if (n_ >= 0 && (size_t)n_ < cap - used) { used += n_; break; } \
			char *t_ = REALLOC (txt, cap *= 2); \
			if (!t_) { FREE (txt); FREE (raw); return ERR_OUT_OF_MEMORY; } \
			txt = t_; \
		} \
	} while (0)
	QERS_PRINT ("Capcom QERS version %u, %u sound events\n", ver, count);
	for (u32 i = 0; i < count; i++)
	{
		const u8 *rec = raw + 128 + i * 144;
		const u32 id = rd_be32 (rec + 24);
		const u32 flags = rd_be32 (rec + 28);
		QERS_PRINT ("event %u: id 0x%x, flags 0x%08x\n", i, id, flags);
	}
	const size_t str_base = 128 + (size_t)count * 144;
	if (str_base < sz)
	{
		size_t p = str_base;
		uint bidx = 0;
		while (p < sz)
		{
			while (p < sz && !raw[p])
				p++;
			if (p >= sz)
				break;
			size_t len = strlen ((ccp)raw + p);
			QERS_PRINT ("bank %u: \"%s\"\n", bidx++, (ccp)raw + p);
			p += len + 1;
		}
	}
#undef QERS_PRINT
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
	return write_owned_entries (arg, basedir, depth, "CAPCOM-QERS", entries, 1);
}

// Capcom MT Framework Wii collision tables (Sengoku Basara 3 ".col" = "\0LOC"): u16 version @4 (2),
// u16 count @6, followed by count groups with sub-chunks of collision volumes.
// "<name>.txt" lists the collision groups and shape sub-chunks.
static enumError extract_capcomcol_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".col"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 8 || memcmp (raw, "\0LOC", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint ver = rd_be16 (raw + 4);
	const uint count = rd_be16 (raw + 6);
	if (!count)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t pos = 8;
	for (uint i = 0; i < count; i++)
	{
		if (pos + 16 > sz)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		const u32 subcount = rd_be32 (raw + pos + 8);
		pos += 16 + (size_t)subcount * 64;
	}
	if (pos + 8 != sz)
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
#define COL_PRINT(...) \
	do { \
		for (;;) { \
			int n_ = snprintf (txt + used, cap - used, __VA_ARGS__); \
			if (n_ >= 0 && (size_t)n_ < cap - used) { used += n_; break; } \
			char *t_ = REALLOC (txt, cap *= 2); \
			if (!t_) { FREE (txt); FREE (raw); return ERR_OUT_OF_MEMORY; } \
			txt = t_; \
		} \
	} while (0)
	COL_PRINT ("Capcom COL version %u, %u collision groups\n", ver, count);
	pos = 8;
	for (uint i = 0; i < count; i++)
	{
		const u32 u0 = rd_be32 (raw + pos);
		const u32 u1 = rd_be32 (raw + pos + 4);
		const u32 subcount = rd_be32 (raw + pos + 8);
		const u32 unk = rd_be32 (raw + pos + 12);
		COL_PRINT ("group %u: flags 0x%08x 0x%08x, %u shapes (type 0x%x)\n", i, u0, u1, subcount, unk);
		pos += 16;
		for (uint s = 0; s < subcount; s++)
		{
			const u8 *rec = raw + pos;
			const u32 s_bone = rd_be32 (rec + 12);
			const u32 s_flag = rd_be32 (rec + 16);
			COL_PRINT ("  shape %u: bone 0x%x, flag 0x%x\n", s, s_bone, s_flag);
			pos += 64;
		}
	}
#undef COL_PRINT
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
	return write_owned_entries (arg, basedir, depth, "CAPCOM-COL", entries, 1);
}

// Capcom MT Framework Wii AI decision trees (Sengoku Basara 3 ".thk" = "\0KHT"): u32 version @4,
// u32 count @8, followed by count tree headers pointing to 176-byte behavior node records.
// "<name>.txt" lists the AI decision trees and their behavior node counts.
static enumError extract_capcomthk_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".thk"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 16 || memcmp (raw, "\0KHT", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 ver = rd_be32 (raw + 4);
	const u32 count = rd_be32 (raw + 8);
	if (!count || 16 + (size_t)count * 16 > sz)
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
#define THK_PRINT(...) \
	do { \
		for (;;) { \
			int n_ = snprintf (txt + used, cap - used, __VA_ARGS__); \
			if (n_ >= 0 && (size_t)n_ < cap - used) { used += n_; break; } \
			char *t_ = REALLOC (txt, cap *= 2); \
			if (!t_) { FREE (txt); FREE (raw); return ERR_OUT_OF_MEMORY; } \
			txt = t_; \
		} \
	} while (0)
	THK_PRINT ("Capcom THK version 0x%08x, %u decision trees\n", ver, count);
	for (u32 i = 0; i < count; i++)
	{
		const u8 *entry = raw + 16 + i * 16;
		const u32 u0 = rd_be32 (entry);
		const u32 offset = rd_be32 (entry + 4);
		const u32 nodes = u0 >> 24;
		THK_PRINT ("tree %u: %u nodes, offset 0x%04x\n", i, nodes, offset);
	}
#undef THK_PRINT
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
	return write_owned_entries (arg, basedir, depth, "CAPCOM-THK", entries, 1);
}

// Capcom MT Framework Wii animation/texture SRT tables (Sengoku Basara 3 ".astl" = "ASTL"):
// wraps a NW4R BRRES (holding AnmTexSrt SRT0) at offset @0x144, size @0x140.
// Unwrapped to "<name>.brres".
static enumError extract_capcomastl_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".astl"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x160 || memcmp (raw, "ASTL", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 bres_sz = rd_be32 (raw + 0x140);
	const u32 bres_off = rd_be32 (raw + 0x144);
	if (!bres_off || bres_off + 16 > sz || memcmp (raw + bres_off, "bres", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t len = (bres_sz && bres_off + bres_sz <= sz) ? bres_sz : (sz - bres_off);
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	char name[PATH_MAX];
	snprintf (name, sizeof (name), "%s", base);
	char *dot = strrchr (name, '.');
	if (dot)
		*dot = 0;
	snprintf (name + strlen (name), sizeof (name) - strlen (name), ".brres");
	nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
	if (!entries || !OwnedEntryAdd (entries, 0, name, raw + bres_off, len))
	{
		FREE (entries);
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	FREE (raw);
	return write_owned_entries (arg, basedir, depth, "CAPCOM-ASTL", entries, 1);
}




// Square Enix "SEDBRES " resource packages (".trb" models / skeletons, ".rps" animation sets; Dragon Quest X, Wii),
// little endian: 0x20-byte header {"SEDBRES ", u32 version, u32 flags, u32 file size}, then {u32 record count, u32
// string pool offset, u32 string count, char resource type[4] ("brt", "spr")} and count 16-byte records {u32 key,
// u32 offset, u32 size, u32 class}; offsets are relative to the end of the record table, as is the string pool
// that runs to the end of the file (NUL separated, the "F00\name" resource names then RESOURCE_TYPE / RESOURCE_ID).
// Records of class 0 describe tables inside the pool and are skipped. Every other record is a block that either
// starts with a 0x20-byte "SEDB<tag>" header (txb textures, shd shaders, wrb models, mtb / MCB / ACB motion data,
// SKL skeletons, a nested RES) or is raw data (standard "DDS " files in the .rps sets). "SEDBtxb" blocks hold a
// big-endian DDS-style header (0x80 bytes) and an 8-bit palette-indexed image: a 256 entry {A, B, G, R} palette
// (A is 0..0x80) followed by width * height indices, no mip levels; they are written as "NNN_txb.png". The other
// blocks go to "NNN_<tag>.bin" (nested packages as .trb so they are unpacked again), the pool strings to
// "strings.txt" and the record table to "info.txt". Resource names are not tied to records here (the pool order
// differs from the record order), and the shader / model / motion blocks are not decoded.
static enumError extract_sedbres_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".trb") && !is_ext (arg, ".rps"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 count = sz >= 0x30 ? rd_le32 (raw + 0x20) : 0;
	const size_t base = 0x30 + (size_t)count * 16;
	const u64 pool = sz >= 0x30 ? rd_le32 (raw + 0x24) : 0;
	if (sz < 0x30 || memcmp (raw, "SEDBRES ", 8) || rd_le32 (raw + 0x10) != sz || !count || count > 4096 || base > sz
		|| pool > sz - base)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = CALLOC (count + 2, sizeof (*entries));
	char *info = MALLOC (count * 96 + 256);
	char *strs = MALLOC (8 * (sz - base - (size_t)pool) + 64);
	typedef struct
	{
		uint idx;
		u32 w, h;
		const u8 *pix;
	} tex_t;
	tex_t *tex = CALLOC (count, sizeof (*tex));
	if (!entries || !info || !strs || !tex)
	{
		FREE (entries);
		FREE (info);
		FREE (strs);
		FREE (tex);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char type[5] = {0};
	memcpy (type, raw + 0x2c, 4);
	size_t il = (size_t)snprintf (info, count * 96 + 256, "version %u flags 0x%08x type %.4s records %u strings %u\n# index key offset size class tag\n",
		(uint)rd_le32 (raw + 8), (uint)rd_le32 (raw + 12), type, count, (uint)rd_le32 (raw + 0x28));
	uint n = 0, ntex = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *r = raw + 0x30 + (size_t)i * 16;
		const u64 off = rd_le32 (r + 4), size = rd_le32 (r + 8);
		const u32 cls = rd_le32 (r + 12);
		const bool sedb = size >= 0x20 && off + size <= sz - base && !memcmp (raw + base + off, "SEDB", 4);
		char tag[5] = {0};
		if (sedb)
			memcpy (tag, raw + base + off + 4, 4);
		for (char *c = tag; *c; c++)
			if (*c == ' ')
				*c = 0;
		il += (size_t)snprintf (info + il, 96, "%u %u 0x%x 0x%x %u %s\n", i, (uint)rd_le32 (r), (uint)off, (uint)size, cls,
			sedb ? (*tag ? tag : "?") : "raw");
		if (!cls || !size || off + size > pool)
			continue;
		const u8 *p = raw + base + off;
		char name[48];
		if (sedb && !memcmp (p + 4, "txb\0", 4) && size > 0x20 + 0x80 + 1024 && !memcmp (p + 0x20, "DDS ", 4)
			&& rd_be32 (p + 0x20 + 0x4c) == 0x20 && rd_be32 (p + 0x20 + 0x58) == 8)
		{
			const u32 h = rd_be32 (p + 0x20 + 0xc), w = rd_be32 (p + 0x20 + 0x10);
			if (w && h && w <= 4096 && h <= 4096 && (u64)w * h + 1024 == size - 0x20 - 0x80)
			{
				tex[ntex++] = (tex_t){.idx = i, .w = w, .h = h, .pix = p + 0x20 + 0x80};
				continue;
			}
		}
		if (sedb)
		{
			if (!memcmp (tag, "RES", 4))
				snprintf (name, sizeof (name), "%03u_res.trb", i);
			else
			{
				char lower[5];
				for (int k = 0; k < 5; k++)
					lower[k] = tag[k] >= 'A' && tag[k] <= 'Z' ? tag[k] + 32 : tag[k];
				snprintf (name, sizeof (name), "%03u_%s.bin", i, lower);
			}
		}
		else if (size >= 4 && !memcmp (p, "DDS ", 4))
			snprintf (name, sizeof (name), "%03u.dds", i);
		else
			snprintf (name, sizeof (name), "%03u.bin", i);
		if (!OwnedEntryAdd (entries, n, name, p, (uint)size))
			break;
		n++;
	}

	size_t sl = 0;
	const u8 *s = raw + base + pool, *end = raw + sz;
	for (uint k = 0; s < end; k++)
	{
		const size_t len = strnlen ((ccp)s, (size_t)(end - s));
		if (len)
			sl += (size_t)snprintf (strs + sl, len + 24, "%u\t%s\n", k, (ccp)s);
		s += len + 1;
	}
	if (OwnedEntryAdd (entries, n, "info.txt", (const u8 *)info, (uint)il))
		n++;
	if (sl && OwnedEntryAdd (entries, n, "strings.txt", (const u8 *)strs, (uint)sl))
		n++;
	FREE (info);
	FREE (strs);

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	enumError err = write_owned_entries (arg, basedir, depth, "SEDBRES", entries, n);
	if (!testmode)
		for (uint t = 0; !err && t < ntex; t++)
		{
			u8 *rgba = MALLOC ((size_t)tex[t].w * tex[t].h * 4);
			if (!rgba)
				break;
			const u8 *pal = tex[t].pix, *idx = tex[t].pix + 1024;
			for (size_t px = 0; px < (size_t)tex[t].w * tex[t].h; px++)
			{
				const u8 *e = pal + 4 * (size_t)idx[px];
				rgba[4 * px] = e[3];
				rgba[4 * px + 1] = e[2];
				rgba[4 * px + 2] = e[1];
				rgba[4 * px + 3] = e[0] >= 0x80 ? 0xff : (u8)(e[0] * 2);
			}
			char png[PATH_MAX];
			snprintf (png, sizeof (png), "%s/%s%03u_txb.png", dest, basedir ? basedir : "", tex[t].idx);
			err = SaveDecodedRGBAToPNG (rgba, tex[t].w, tex[t].h, &be_func, png, 0, opt_overwrite);
		}
	else if (ntex && (verbose >= 0 || testmode))
		fprintf (stdlog, "WOULD DECODE SEDBRES: %u textures -> %s/\n", ntex, dest);
	FREE (tex);
	FREE (raw);
	return err;
}

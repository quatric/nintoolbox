// EA "DBPF" version 2.0 resource packages (MySims, MySims Kingdom, MySims Agents ".package"), little-endian:
// "DBPF", u32 major 2, u32 minor, u32 index entry count @0x24, u32 index size @0x2c, u32 index offset @0x40.
// The index is {u32 flags; one constant u32 for each of the bits 0 (type), 1 (group), 2 (instance high) that is
// set; then per entry the non-constant fields of {type, group, instance high, instance low}, offset, size,
// memory size and, in the larger indexes, one more u32}. The entry stride follows from the index size. A member
// is RefPack-compressed when its size (without the 0x80000000 flag) differs from its memory size. Type
// 0x0166038c is a name map {u32 1, u32 count, count * {u64 hash, u32 length, name}} that names the members by
// their instance. Members are written as "NNNN_<name>.<ext>" with textures (type 0x00b2d882, a big-endian header {0x14fe0149, version, data size, ..., u16 width, u16 height @0x1c, GX format @0x20} and
// CMPR / RGBA8 pixels at 0x4c, followed by the mip levels) as ".mstex" files, which become PNGs in decode mode (xx).
typedef struct dbpf_name_t
{
	u64 key;
	char *name;
} dbpf_name_t;

static int dbpf_name_cmp (const void *a, const void *b)
{
	const u64 x = ((const dbpf_name_t *)a)->key, y = ((const dbpf_name_t *)b)->key;
	return x < y ? -1 : x > y;
}

static enumError extract_dbpf_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".package"))
		return ERR_NOTHING_TO_DO;
	FILE *in = fopen (arg, "rb");
	if (!in)
		return ERR_NOTHING_TO_DO;
	u8 hdr[0x48];
	if (!read_abe_at (in, 0, hdr, sizeof (hdr)) || memcmp (hdr, "DBPF", 4) || rd_le32 (hdr + 4) != 2
		|| fseek (in, 0, SEEK_END))
	{
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}
	const long end = ftell (in);
	const u64 fsz = end < 0 ? 0 : (u64)end;
	const uint count = rd_le32 (hdr + 0x24), isz = rd_le32 (hdr + 0x2c);
	const u64 io = rd_le32 (hdr + 0x40);
	u8 *idx = count && count <= 0x100000 && isz >= 8 && isz <= 0x4000000 && io + isz <= fsz ? MALLOC (isz) : 0;
	if (!idx || !read_abe_at (in, io, idx, isz))
	{
		FREE (idx);
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}
	const uint flags = rd_le32 (idx);
	uint nconst = 0, cval[3] = {0};
	for (uint b = 0; b < 3; b++)
		nconst += (flags >> b) & 1;
	const size_t body = (size_t)isz - 4 - 4 * nconst;
	const uint nvar = 4 - nconst, stride = (uint)(body / count / 4);
	bool ok = (flags & ~7u) == 0 && isz >= 4 + 4 * nconst && body % ((size_t)count * 4) == 0 && stride >= nvar + 3
		&& stride <= nvar + 4;
	for (uint b = 0, k = 0; ok && b < 3; b++)
		if ((flags >> b) & 1)
			cval[b] = rd_le32 (idx + 4 + 4 * k++);
	typedef struct
	{
		u32 type, hi, lo, off, size, mem;
	} ent_t;
	ent_t *e = ok ? CALLOC (count, sizeof (*e)) : 0;
	for (uint i = 0; e && i < count && ok; i++)
	{
		const u8 *p = idx + 4 + 4 * nconst + 4 * (size_t)stride * i;
		uint v[4], vi = 0;
		for (uint b = 0; b < 3; b++)
			v[b] = ((flags >> b) & 1) ? cval[b] : rd_le32 (p + 4 * vi++);
		v[3] = rd_le32 (p + 4 * vi++);
		e[i].type = v[0];
		e[i].hi = v[2];
		e[i].lo = v[3];
		e[i].off = rd_le32 (p + 4 * vi);
		e[i].size = rd_le32 (p + 4 * vi + 4) & 0x7fffffff;
		e[i].mem = rd_le32 (p + 4 * vi + 8);
		ok = e[i].off + (u64)e[i].size <= fsz && e[i].mem <= (256u << 20);
	}
	FREE (idx);
	if (!ok || !e)
	{
		FREE (e);
		fclose (in);
		return ok ? ERR_OUT_OF_MEMORY : ERR_NOTHING_TO_DO;
	}

	// name maps
	dbpf_name_t *names = 0;
	uint nnames = 0, cap = 0;
	char *pool = 0;
	for (uint i = 0; i < count; i++)
	{
		if (e[i].type != 0x0166038c || e[i].size < 8 || e[i].size > (16u << 20) || e[i].size != e[i].mem)
			continue;
		u8 *m = MALLOC (e[i].size + 1);
		if (!m || !read_abe_at (in, e[i].off, m, e[i].size))
		{
			FREE (m);
			continue;
		}
		const uint n = rd_le32 (m + 4);
		size_t p = 8;
		for (uint k = 0; k < n && p + 12 <= e[i].size; k++)
		{
			const uint len = rd_le32 (m + p + 8);
			if (len > 0x400 || p + 12 + len > e[i].size)
				break;
			if (nnames == cap)
			{
				cap = cap ? cap * 2 : 256;
				names = REALLOC (names, cap * sizeof (*names));
			}
			char *s = MALLOC (len + 1);
			if (!names || !s)
				break;
			memcpy (s, m + p + 12, len);
			s[len] = 0;
			names[nnames].key = ((u64)rd_le32 (m + p + 4) << 32) | rd_le32 (m + p);
			names[nnames++].name = s;
			p += 12 + len;
		}
		FREE (m);
	}
	(void)pool;
	if (nnames)
		qsort (names, nnames, sizeof (*names), dbpf_name_cmp);

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT DBPF:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			arg, count, dest);
	enumError err = ERR_OK;
	for (uint i = 0; i < count && !err && !testmode; i++)
	{
		u8 *raw = MALLOC (e[i].size ? e[i].size : 1);
		if (!raw || !read_abe_at (in, e[i].off, raw, e[i].size))
		{
			FREE (raw);
			err = ERR_READ_FAILED;
			break;
		}
		u8 *data = raw;
		size_t dsz = e[i].size;
		if (e[i].size != e[i].mem)
		{
			u8 *dec = 0;
			size_t n = 0;
			if (ea_refpack (raw, e[i].size, &dec, &n))
			{
				FREE (raw);
				data = dec;
				dsz = n;
			}
		}
		char nm[PATH_MAX];
		const u64 key = ((u64)e[i].hi << 32) | e[i].lo;
		dbpf_name_t k = {key, 0}, *f = nnames ? bsearch (&k, names, nnames, sizeof (*names), dbpf_name_cmp) : 0;
		if (f)
		{
			snprintf (nm, sizeof (nm), "%s", f->name);
			for (char *c = nm; *c; c++)
				if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || *c == '_'
						|| *c == '-' || *c == '.'))
					*c = '_';
			nm[100] = 0;
		}
		else
			snprintf (nm, sizeof (nm), "%016llx", (unsigned long long)key);
		char path[PATH_MAX];
		if (e[i].type == 0x00b2d882)
			snprintf (path, sizeof (path), "%s/%04u_%s.mstex", dest, i, nm);
		else
			snprintf (path, sizeof (path), "%s/%04u_%s.t%08x.bin", dest, i, nm, e[i].type);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && dsz && fwrite (data, 1, dsz, F.f) != dsz)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", dsz, path);
		ResetFile (&F, opt_preserve);
		FREE (data);
	}
	for (uint i = 0; i < nnames; i++)
		FREE (names[i].name);
	FREE (names);
	FREE (e);
	fclose (in);
	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

static enumError extract_mstex_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".mstex") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint w = sz >= 0x4c ? (raw[0x1c] << 8) | raw[0x1d] : 0, h = sz >= 0x4c ? (raw[0x1e] << 8) | raw[0x1f] : 0;
	const uint fmt = sz >= 0x4c ? rd_be32 (raw + 0x20) : 99;
	if (sz < 0x4c || rd_be32 (raw) != 0x14fe0149 || !w || !h || w > 4096 || h > 4096 || (fmt != 6 && fmt != 14))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	u8 *rgba = eashpg_decode (fmt, raw + 0x4c, sz - 0x4c, w, h, 0, 0, 0);
	FREE (raw);
	if (!rgba)
		return ERR_NOTHING_TO_DO;
	char png[PATH_MAX];
	snprintf (png, sizeof (png), "%s.png", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE MSTEX:%s %ux%u -> PNG:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			arg, w, h, png);
	if (testmode)
	{
		FREE (rgba);
		return ERR_OK;
	}
	return SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png, 0, true);
}

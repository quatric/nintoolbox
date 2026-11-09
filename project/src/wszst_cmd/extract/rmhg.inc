// Grasshopper Manufacture "RMHG" resource container (No More Heroes 1/2 ".rsl": "CHR/TRV001.rsl" ...): little endian {"RMHG", u32 kind (1 =
// textures, 2 = models, 3 = group, 0xcd ...), u32 0x20 (header size), u32 0x4xx build number, 16 zero bytes}, then 32 byte entries {u32 offset (from the
// container start), u32 size, u32 1 when nested, u32 0x427, u32 id, 12 zero bytes}; the first entry's offset marks the end of the entry
// table. An entry holds either another RMHG (extracted as a sub directory "NNN_<id>/") or a leaf (GCT0 texture -> ".bin", GMF2 model -> ".gmf2", GAN2 animation -> ".gan2",
// anything else ".dat"). The entry's id word indexes the root container's name table (header word at +0x10 = offset of {u32 count, u32 offset of
// the u32 string offsets, u32 flags}; strings are XOR 0x8d and end with 0x8d when flags & 1 and the build number exceeds 0x41b: "MODEL",
// "TRV001S.GM2", "TRVA00100.GA2" ... found by tracing the game's loader); members are written under their names (else "NNN_<id>.<ext>").
static bool rmhg_ok (const u8 *p, size_t n)
{
	return n >= 0x20 && !memcmp (p, "RMHG", 4) && rd_le32 (p + 8) == 0x20 && (rd_le32 (p + 12) >> 8) == 4;
}

typedef struct rmhg_names_t { const u8 *base; size_t n; u32 cnt, po, flags; bool xor8d; } rmhg_names_t;

static bool rmhg_name (const rmhg_names_t *t, uint idx, char *out, size_t osz)
{
	if (!t || !t->base || idx >= t->cnt)
		return false;
	const u32 so = rd_le32 (t->base + t->po + 4 * (size_t)idx);
	if (so >= t->n)
		return false;
	size_t i = 0;
	for (size_t p = so; p < t->n && i + 1 < osz; p++, i++)
	{
		u8 c = t->base[p];
		if (t->xor8d)
		{
			if (c == 0x8d)
				break;
			c ^= 0x8d;
		}
		else if (!c)
			break;
		out[i] = c == '\\' || c == ':' || c == '/' || c < 32 ? '_' : (char)c;
	}
	out[i] = 0;
	return i > 0 && strcmp (out, ".") && strcmp (out, "..");
}

static enumError rmhg_walk (ccp arg, ccp dest, ccp basedir, const u8 *p, size_t n, ccp prefix, uint level, const rmhg_names_t *nt)
{
	const u32 first = n >= 0x24 ? rd_le32 (p + 0x20) : 0;
	if (!first)
		return ERR_OK;
	if (first < 0x40 || first > n || (first - 0x20) % 32 || level > 6)
		return ERR_NOTHING_TO_DO;
	const uint cnt = (first - 0x20) / 32;
	for (uint i = 0; i < cnt; i++)
	{
		const u8 *e = p + 0x20 + 32 * i;
		const u32 off = rd_le32 (e), sz = rd_le32 (e + 4);
		if ((u64)off + sz > n || (sz && off < first))
			return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_OK;
	for (uint i = 0; i < cnt && !err; i++)
	{
		const u8 *e = p + 0x20 + 32 * i;
		const u32 off = rd_le32 (e), sz = rd_le32 (e + 4), id = rd_le32 (e + 16);
		const u8 *d = p + off;
		char name[PATH_MAX];
		char nm[256];
		bool named = rmhg_name (nt, id, nm, sizeof (nm));
		for (uint j = 0; named && j < i && cnt <= 4096; j++)		// duplicate name inside this container -> fall back to the numbered form
		{
			char o[256];
			if (rmhg_name (nt, rd_le32 (p + 0x20 + 32 * j + 16), o, sizeof (o)) && !strcmp (o, nm))
				named = false;
		}
		if (rmhg_ok (d, sz))
		{
			if (named)
				snprintf (name, sizeof (name), "%s%s/", prefix, nm);
			else
				snprintf (name, sizeof (name), "%s%03u_%x/", prefix, i, id);
			if (rmhg_walk (arg, dest, basedir, d, sz, name, level + 1, nt))
				err = ERR_NOTHING_TO_DO;
			continue;
		}
		ccp ext = sz >= 4 && !memcmp (d, "GCT0", 4) ? "bin" : sz >= 4 && !memcmp (d, "GMF2", 4) ? "gmf2" : sz >= 4 && !memcmp (d, "GAN2", 4) ? "gan2" : "dat";
		if (named)
			snprintf (name, sizeof (name), "%s%s%s%s", prefix, nm, strchr (nm, '.') ? "" : ".", strchr (nm, '.') ? "" : ext);
		else
			snprintf (name, sizeof (name), "%s%03u_%x.%s", prefix, i, id, ext);
		err = shade_write (arg, dest, basedir, name, d, sz);
	}
	return err;
}

static enumError extract_rmhg_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (rmhg_ok (raw, sz))
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (testmode)
			fprintf (stdlog, "WOULD EXTRACT RMHG:%s -> %s/\n", arg, dest);
		else
		{
			rmhg_names_t nt = { 0 };
			const u32 no = rd_le32 (raw + 0x10);
			if (no && (u64)no + 12 <= sz)
			{
				nt.base = raw + no, nt.n = sz - no, nt.cnt = rd_le32 (raw + no), nt.po = rd_le32 (raw + no + 4), nt.flags = rd_le32 (raw + no + 8);
				nt.xor8d = (nt.flags & 1) && rd_le32 (raw + 12) > 0x41b;
				if (nt.cnt > 100000 || (u64)nt.po + 4 * (u64)nt.cnt > nt.n)
					memset (&nt, 0, sizeof (nt));
			}
			err = rmhg_walk (arg, dest, basedir, raw, sz, "", 0, &nt);
			if (!err && verbose >= 0)
				fprintf (stdlog, "%sEXTRACT RMHG:%s -> %s/\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (raw);
	return err;
}

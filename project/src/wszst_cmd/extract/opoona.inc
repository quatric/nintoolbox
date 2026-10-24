// Opoona (Wii, ArtePiazza) containers.
//
// "SL01" (magic, u32le unpacked size, u32le packed size, then a plain LZO1X
// stream) wraps .btl/.bin/.dat/.map members; the unpacked data is usually an
// "OPKG" package. Packages have no magic: u32le count, u16le 0x20, u16le 0x20,
// then count * 0x20 byte records {u32le offset, u32le size, char name[24]}
// (the stored names are tail-truncated paths such as "racter/p109/p109_01.mdl").
// .mdl and .tex files of the character/ tree are raw packages (model, TPL
// bundle, .tmp material table).

// Unpacks an "SL01" blob of at most 'avail' bytes; *dec (CALLOC'd) is NULL when 'raw' isn't one.
static bool opoona_sl01 (const u8 *raw, size_t avail, u8 **dec, uint *dsz)
{
	*dec = 0;
	*dsz = 0;
	if (avail < 0x14 || memcmp (raw, "SL01", 4))
		return false;
	const u32 usize = rd_le32 (raw + 4), csize = rd_le32 (raw + 8);
	if (!usize || usize > (1u << 28) || csize > avail - 12)
		return false;
	if (DecodeLZO1XGrow (dec, dsz, raw + 12, csize) || *dsz != usize)
	{
		FREE (*dec);
		*dec = 0;
		return false;
	}
	return true;
}

static bool opoona_pkg_ok (const u8 *p, size_t sz)
{
	if (sz < 0x28 || rd_le16 (p + 4) != 0x20 || rd_le16 (p + 6) != 0x20)
		return false;
	const uint n = rd_le32 (p);
	if (!n || n > 0x10000 || 8 + 0x20ull * n > sz)
		return false;
	for (uint i = 0; i < n; i++)
	{
		const u8 *e = p + 8 + 0x20 * i;
		const u32 off = rd_le32 (e), size = rd_le32 (e + 4);
		if (!size)
			continue; // ".mol" manifests carry empty "NULL" slots
		if (off < 8 + 0x20 * n || off > sz || size > sz - off)
			return false;
		if (!e[8])
			return false;
	}
	return true;
}

// Opoona .tpl files are standard Wii TPLs whose magic word was blanked (0..3)
// or wrapped in a "TPLE" bundle: 0x40 byte header {"TPLE", ..., u32be count
// (0x10), u32be stride (0x14), u32be first offset 0x40 (0x18), char name[]
// (0x1c)} followed by count (always 3) blanked-magic TPLs of 'stride' bytes.
static bool opoona_tpl_plain (const u8 *p, size_t sz)
{
	return sz > 0x40 && rd_be32 (p) < 4 && rd_be32 (p + 4) && rd_be32 (p + 4) < 0x100
		&& rd_be32 (p + 8) == 12;
}

static enumError opoona_write_member (ccp arg, ccp dest, ccp basedir, ccp fn, const u8 *p, size_t sz)
{
	const size_t fl = strlen (fn);
	const bool is_tpl = fl > 4 && !strcasecmp (fn + fl - 4, ".tpl");
	if (is_tpl && sz > 0x40 && !memcmp (p, "TPLE", 4))
	{
		const u32 cnt = rd_be32 (p + 0x10), stride = rd_be32 (p + 0x14), off = rd_be32 (p + 0x18);
		if (cnt && cnt <= 64 && stride > 0x40 && off == 0x40 && 0x40 + (u64)cnt * stride <= sz)
		{
			enumError err = ERR_OK;
			for (u32 i = 0; i < cnt && !err; i++)
			{
				u8 *t = MALLOC (stride);
				if (!t)
					return ERR_OUT_OF_MEMORY;
				memcpy (t, p + off + (size_t)i * stride, stride);
				t[0] = 0x00, t[1] = 0x20, t[2] = 0xaf, t[3] = 0x30;
				char nm[80];
				snprintf (nm, sizeof (nm), "%.*s_%u.tpl", (int)(fl - 4), fn, i);
				err = shade_write (arg, dest, basedir, nm, t, stride);
				FREE (t);
			}
			return err;
		}
	}
	if (is_tpl && opoona_tpl_plain (p, sz))
	{
		u8 *t = MALLOC (sz);
		if (!t)
			return ERR_OUT_OF_MEMORY;
		memcpy (t, p, sz);
		t[0] = 0x00, t[1] = 0x20, t[2] = 0xaf, t[3] = 0x30;
		const enumError err = shade_write (arg, dest, basedir, fn, t, (uint)sz);
		FREE (t);
		return err;
	}
	return shade_write (arg, dest, basedir, fn, p, (uint)sz);
}

#include "lib-opoonamodel.h"

// Model package: the "DHDM" chunk stream plus the ".tex" TPL bundle whose decoded
// PNGs (written by the recursion above) the materials point at -> "<stem>.glb".
static void opoona_member_name (const u8 *e, char *fn, size_t fn_size, uint i)
{
	char name[25];
	memcpy (name, e + 8, 24);
	name[24] = 0;
	ccp base = strrchr (name, '/');
	base = base ? base + 1 : name;
	if (!*base || !OwnedNameOk (base))
		snprintf (fn, fn_size, "%04u.bin", i);
	else
		snprintf (fn, fn_size, "%s", base);
}


static void opoona_add_mot_file (model_t *model, ccp path)
{
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (path, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return;
	}
	ccp base = strrchr (path, '/');
	base = base ? base + 1 : path;
	char name[64];
	snprintf (name, sizeof (name), "%s", base);
	char *dot = strrchr (name, '.');
	if (dot)
		*dot = 0;
	OpoonaAddMotion (model, raw, sz, name);
	FREE (raw);
}

static int opoona_strcmp (const void *a, const void *b)
{
	return strcmp (*(char *const *)a, *(char *const *)b);
}

// Clips for a model: "<stem>.mot" next to a model file of the same name
// (character/pNNN/pNNN_01.mdl), and every .mot unpacked from "*.mol.d"
// directories beside the model's own directory (battle packages).
static void opoona_attach_motions (model_t *model, ccp arg, ccp dest)
{
	char dir[PATH_MAX], path[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s", arg);
	char *slash = strrchr (dir, '/');
	char stem[256];
	snprintf (stem, sizeof (stem), "%s", slash ? slash + 1 : dir);
	if (slash)
		*slash = 0;
	else
		strcpy (dir, ".");
	char *dot = strrchr (stem, '.');
	if (dot)
		*dot = 0;
	struct stat st;
	snprintf (path, sizeof (path), "%s/%s.mot", dir, stem);
	if (!stat (path, &st))
		opoona_add_mot_file (model, path);

	char parent[PATH_MAX];
	snprintf (parent, sizeof (parent), "%s", dest);
	slash = strrchr (parent, '/');
	if (!slash)
		return;
	*slash = 0;
	DIR *dd = opendir (parent);
	if (!dd)
		return;
	char *dirs[64];
	int nd = 0;
	for (struct dirent *de; (de = readdir (dd)) && nd < 64;)
	{
		const size_t l = strlen (de->d_name);
		if (l > 6 && !strcmp (de->d_name + l - 6, ".mol.d"))
			dirs[nd++] = STRDUP (de->d_name);
	}
	closedir (dd);
	qsort (dirs, nd, sizeof (*dirs), opoona_strcmp);
	for (int i = 0; i < nd; i++)
	{
		snprintf (path, sizeof (path), "%s/%s", parent, dirs[i]);
		DIR *md = opendir (path);
		char *mots[512];
		int nm = 0;
		for (struct dirent *de; md && (de = readdir (md)) && nm < 512;)
		{
			const size_t l = strlen (de->d_name);
			if (l > 4 && !strcmp (de->d_name + l - 4, ".mot"))
				mots[nm++] = STRDUP (de->d_name);
		}
		if (md)
			closedir (md);
		qsort (mots, nm, sizeof (*mots), opoona_strcmp);
		for (int j = 0; j < nm; j++)
		{
			char mp[PATH_MAX];
			snprintf (mp, sizeof (mp), "%s/%s/%s", parent, dirs[i], mots[j]);
			opoona_add_mot_file (model, mp);
			FREE (mots[j]);
		}
		FREE (dirs[i]);
	}
}

static void opoona_export_model (ccp arg, ccp dest, const u8 *p, size_t sz)
{
	const uint n = rd_le32 (p);
	int mi = -1, ti = -1;
	for (uint i = 0; i < n; i++)
	{
		const u8 *e = p + 8 + 0x20 * i;
		char fn[64];
		opoona_member_name (e, fn, sizeof (fn), i);
		const size_t fl = strlen (fn);
		if (mi < 0 && fl > 4 && !strcasecmp (fn + fl - 4, ".mdl") && IsOpoonaModel (p + rd_le32 (e), rd_le32 (e + 4)))
			mi = (int)i;
		else if (ti < 0 && fl > 4 && !strcasecmp (fn + fl - 4, ".tex"))
			ti = (int)i;
	}
	if (mi < 0)
		return;

	char (*names)[64] = 0;
	uint ntex = 0;
	if (ti >= 0)
	{
		const u8 *te = p + 8 + 0x20 * (size_t)ti;
		const u8 *tp = p + rd_le32 (te);
		const size_t tsz = rd_le32 (te + 4);
		char texdir[64];
		opoona_member_name (te, texdir, sizeof (texdir), (uint)ti);
		if (opoona_pkg_ok (tp, tsz))
		{
			ntex = rd_le32 (tp);
			names = CALLOC (ntex, sizeof (*names));
			for (uint i = 0; names && i < ntex; i++)
			{
				char stem[64], path[PATH_MAX];
				opoona_member_name (tp + 8 + 0x20 * i, stem, sizeof (stem), i);
				size_t l = strlen (stem);
				if (l > 4 && !strcasecmp (stem + l - 4, ".tpl"))
					stem[l - 4] = 0;
				struct stat st;
				snprintf (path, sizeof (path), "%s/%s.d/%s.tpl.png", dest, texdir, stem);
				if (!stat (path, &st))
					snprintf (names[i], 64, "%.40s.d/%.18s.tpl.png", texdir, stem);
				else
				{
					snprintf (path, sizeof (path), "%s/%s.d/%s_0.tpl.png", dest, texdir, stem);
					if (!stat (path, &st))
						snprintf (names[i], 64, "%.40s.d/%.16s_0.tpl.png", texdir, stem);
				}
			}
		}
	}

	const u8 *me = p + 8 + 0x20 * (size_t)mi;
	model_t *model = ParseOpoonaModel (p + rd_le32 (me), rd_le32 (me + 4), names, ntex);
	FREE (names);
	if (!model)
		return;
	char fn[64], out[PATH_MAX];
	opoona_member_name (me, fn, sizeof (fn), (uint)mi);
	fn[strlen (fn) - 4] = 0;
	opoona_attach_motions (model, arg, dest);
	snprintf (out, sizeof (out), "%s/%s.glb", dest, fn);
	ExportModelToGLB (model, out);
	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT OPOONA-MDL:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, out);
	FreeModel (model);
	(void)sz;
}

// A package holding both nested model packages and a ".mol" clip set (battle
// packages): once the clips are unpacked, rebuild the nested models' GLBs with them.
static void opoona_reexport_nested (ccp dest, const u8 *p)
{
	const uint n = rd_le32 (p);
	bool has_mol = false;
	for (uint i = 0; i < n; i++)
	{
		char fn[64];
		opoona_member_name (p + 8 + 0x20 * i, fn, sizeof (fn), i);
		const size_t fl = strlen (fn);
		if (fl > 4 && !strcasecmp (fn + fl - 4, ".mol"))
			has_mol = true;
	}
	if (!has_mol)
		return;
	for (uint i = 0; i < n; i++)
	{
		char fn[64], path[PATH_MAX];
		opoona_member_name (p + 8 + 0x20 * i, fn, sizeof (fn), i);
		const size_t fl = strlen (fn);
		if (fl <= 4 || strcasecmp (fn + fl - 4, ".mdl"))
			continue;
		snprintf (path, sizeof (path), "%s/%s", dest, fn);
		u8 *raw = 0;
		size_t sz = 0;
		if (LoadFileAlloc (path, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		{
			FREE (raw);
			continue;
		}
		if (opoona_pkg_ok (raw, sz))
		{
			char sub[PATH_MAX];
			snprintf (sub, sizeof (sub), "%s.d", path);
			opoona_export_model (path, sub, raw, sz);
		}
		FREE (raw);
	}
}

static enumError opoona_pkg_unpack (ccp arg, ccp basedir, uint depth, const u8 *p, size_t sz)
{
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	const uint n = rd_le32 (p);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT OPOONA-PKG:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n, dest);
	enumError err = ERR_OK;
	for (uint i = 0; i < n && !err; i++)
	{
		const u8 *e = p + 8 + 0x20 * i;
		if (!rd_le32 (e + 4))
			continue;
		char fn[64];
		opoona_member_name (e, fn, sizeof (fn), i);
		err = opoona_write_member (arg, dest, basedir, fn, p + rd_le32 (e), rd_le32 (e + 4));
	}
	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
		opoona_export_model (arg, dest, p, sz);
		opoona_reexport_nested (dest, p);
	}
	return err;
}

// Map ".map" files (RDHF header chunk, optionally SL01 wrapped): a stream of
// {char tag[4], u32 size, data} chunks. "LDMF" lists the map's models as
// 0x20 byte records {u32 abs_offset, u32 unpacked_size, ..., u32 name_offset (0x18)}
// each pointing at an SL01 model package; "XETF" is {u32 count, u32 offsets[count]}
// of SL01 TPLs shared by the models; "MANF" holds the model name strings. The
// remaining chunks (placement, collision, ...) are not interpreted.
static enumError extract_opoona_map_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".map"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *dec = 0;
	size_t sz = 0;
	uint dsz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u8 *d = raw;
	size_t n = sz;
	if (opoona_sl01 (raw, sz, &dec, &dsz))
		d = dec, n = dsz;
	if (n < 0x100 || memcmp (d, "RDHF", 4))
	{
		FREE (dec);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	size_t ldmf = 0, xetf = 0, manf = 0;
	for (size_t p = 0; p + 8 <= n;)
	{
		const u32 csz = rd_be32 (d + p + 4);
		if (csz > n - p - 8)
			break;
		if (!memcmp (d + p, "LDMF", 4))
			ldmf = p;
		else if (!memcmp (d + p, "XETF", 4))
			xetf = p;
		else if (!memcmp (d + p, "MANF", 4))
			manf = p;
		p += 8 + (size_t)csz;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT OPOONA-MAP:%s -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	enumError err = testmode ? ERR_OK : CreatePath (dest, true);
	uint ntex = 0;
	if (xetf && !err && !testmode)
	{
		const size_t csz = rd_be32 (d + xetf + 4);
		ntex = rd_be32 (d + xetf + 8);
		if (ntex > 4096 || 12 + 4 * (size_t)ntex > csz + 8)
			ntex = 0;
		for (uint i = 0; i < ntex && !err; i++)
		{
			const size_t off = rd_be32 (d + xetf + 12 + 4 * (size_t)i);
			if (off >= n)
				continue;
			u8 *t = 0;
			uint tsz = 0;
			char fn[32];
			snprintf (fn, sizeof (fn), "tex_%02u.tpl", i);
			if (opoona_sl01 (d + off, n - off, &t, &tsz))
				err = opoona_write_member (arg, dest, basedir, fn, t, tsz);
			FREE (t);
		}
		if (!err)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}

	if (ldmf && !testmode)
	{
		const size_t csz = rd_be32 (d + ldmf + 4);
		const uint cnt = rd_be32 (d + ldmf + 8);
		char (*names)[64] = ntex ? CALLOC (ntex, sizeof (*names)) : 0;
		for (uint i = 0; names && i < ntex; i++)
		{
			char path[PATH_MAX];
			struct stat st;
			snprintf (path, sizeof (path), "%s/tex_%02u.tpl.png", dest, i);
			if (!stat (path, &st))
				snprintf (names[i], 64, "tex_%02u.tpl.png", i);
		}
		for (uint i = 0; i < cnt && 16 + 0x20 * (size_t)(i + 1) <= csz + 8; i++)
		{
			const u8 *e = d + ldmf + 16 + 0x20 * (size_t)i;
			const size_t off = rd_be32 (e);
			if (off >= n)
				continue;
			u8 *m = 0;
			uint msz = 0;
			if (!opoona_sl01 (d + off, n - off, &m, &msz))
			{
					continue;
			}
			if (opoona_pkg_ok (m, msz))
			{
				const uint pc = rd_le32 (m);
				for (uint k = 0; k < pc; k++)
				{
					const u8 *me = m + 8 + 0x20 * (size_t)k;
					if (!rd_le32 (me + 4) || !IsOpoonaModel (m + rd_le32 (me), rd_le32 (me + 4)))
						continue;
					model_t *model
						= ParseOpoonaModel (m + rd_le32 (me), rd_le32 (me + 4), names, ntex);
					if (!model)
						continue;
					char nm[64];
					const size_t no = rd_be32 (e + 0x18);
					if (manf && no > manf && no < n && memchr (d + no, 0, n - no) && d[no])
					{
						snprintf (nm, sizeof (nm), "%s", (ccp)d + no);
						if (!OwnedNameOk (nm))
							snprintf (nm, sizeof (nm), "model_%03u", i);
					}
					else
						snprintf (nm, sizeof (nm), "model_%03u", i);
					char out[PATH_MAX];
					snprintf (out, sizeof (out), "%s/%s.glb", dest, nm);
					struct stat st;
					if (!stat (out, &st))
						snprintf (out, sizeof (out), "%s/%s_%03u.glb", dest, nm, i);
					ExportModelToGLB (model, out);
					if (verbose >= 0)
						fprintf (stdlog, "%sEXTRACT OPOONA-MDL:%s -> GLB:%s\n", verbose > 0 ? "\n" : "",
							arg, out);
					FreeModel (model);
					break;
				}
			}
			FREE (m);
		}
		FREE (names);
	}
	FREE (dec);
	FREE (raw);
	return err;
}

// system/msg_*.bin message banks: u32le count, count * {u32le id, u32le offset}, then
// strings (offsets ascending, the first one right behind the table). A string is two
// tag bytes (0, a speaker / style code) followed by NUL terminated UTF-8 text.
// Control bytes (< 0x20 except '\n') are written as \xNN; output is "<id>\t<tag>\t<text>" lines.
static enumError extract_opoona_msg_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	if (strncasecmp (base, "msg_", 4))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint cnt = sz >= 12 ? rd_le32 (raw) : 0;
	if (!cnt || cnt > 0x100000 || 4 + 8 * (size_t)cnt > sz || rd_le32 (raw + 8) != 4 + 8 * (size_t)cnt)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t cap = sz * 4 + 32 * (size_t)cnt + 64, used = 0;
	char *out = MALLOC (cap);
	if (!out)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n_out = 0;
	for (uint i = 0; i < cnt; i++)
	{
		const size_t off = rd_le32 (raw + 8 + 8 * (size_t)i);
		if (off + 2 >= sz || !memchr (raw + off + 2, 0, sz - off - 2))
			continue;
		used += snprintf (out + used, cap - used, "%u\t%02x%02x\t", rd_le32 (raw + 4 + 8 * (size_t)i),
			raw[off], raw[off + 1]);
		for (const u8 *c = raw + off + 2; *c && used + 8 < cap; c++)
			if (*c < 0x20 && *c != '\n')
				used += snprintf (out + used, cap - used, "\\x%02x", *c);
			else if (*c == '\n')
				used += snprintf (out + used, cap - used, "\\n");
			else
				out[used++] = (char)*c;
		out[used++] = '\n';
		n_out++;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT OPOONA-MSG:%s (%u strings) -> %s/strings.txt\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n_out, dest);
	const enumError err = shade_write (arg, dest, basedir, "strings.txt", (u8 *)out, (uint)used);
	FREE (out);
	FREE (raw);
	return err;
}

// map/*.atb collision meshes (big-endian): u32 {size, 0x800000, 0x20200000|x, ntri, tri_off,
// vert_off (0x38), normal_off, grid_off, list_off, end}, f32 {1/cell_x, 1/cell_z, min_x, min_z},
// then 12-byte float vertices, 12-byte float normals, ntri * {u16 v0, v1, v2, normal} triangles,
// a 32 x 32 u16 grid of offsets into the 0xFFFF terminated triangle id lists at list_off. A
// region trailer follows `end` and is not interpreted. Written as a Wavefront OBJ.
static enumError extract_opoona_atb_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".atb"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 ntri = sz >= 0x38 ? rd_be32 (raw + 12) : 0, toff = sz >= 0x38 ? rd_be32 (raw + 16) : 0,
		  voff = sz >= 0x38 ? rd_be32 (raw + 20) : 0, noff = sz >= 0x38 ? rd_be32 (raw + 24) : 0;
	if (sz < 0x38 || rd_be32 (raw + 4) != 0x800000 || voff != 0x38 || noff <= voff || toff < noff
		|| (noff - voff) % 12 || (toff - noff) % 12 || ntri > 0x100000 || toff > sz
		|| 8ull * ntri > sz - toff)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint nv = (noff - voff) / 12, nn = (toff - noff) / 12;
	for (uint i = 0; i < ntri; i++)
	{
		const u8 *t = raw + toff + 8 * (size_t)i;
		if (rd_be16 (t) >= nv || rd_be16 (t + 2) >= nv || rd_be16 (t + 4) >= nv || rd_be16 (t + 6) >= nn)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	}
	size_t cap = 64 + 40 * (size_t)(nv + nn) + 40 * (size_t)ntri, used = 0;
	char *out = MALLOC (cap);
	if (!out)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	used += snprintf (out + used, cap - used, "# Opoona collision mesh: %u vertices, %u triangles\n", nv, ntri);
	for (uint i = 0; i < nv + nn; i++)
	{
		const u32 b[3] = { rd_be32 (raw + voff + 12 * (size_t)i), rd_be32 (raw + voff + 12 * (size_t)i + 4),
			rd_be32 (raw + voff + 12 * (size_t)i + 8) };
		float f[3];
		memcpy (f, b, sizeof f);
		used += snprintf (out + used, cap - used, "%s %g %g %g\n", i < nv ? "v" : "vn", f[0], f[1], f[2]);
	}
	for (uint i = 0; i < ntri; i++)
	{
		const u8 *t = raw + toff + 8 * (size_t)i;
		const uint a = rd_be16 (t) + 1, b = rd_be16 (t + 2) + 1, c = rd_be16 (t + 4) + 1,
			   n = rd_be16 (t + 6) + 1;
		used += snprintf (out + used, cap - used, "f %u//%u %u//%u %u//%u\n", a, n, b, n, c, n);
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	char name[PATH_MAX];
	snprintf (name, sizeof (name), "%s.obj", base);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT OPOONA-ATB:%s (%u triangles) -> %s/%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, ntri, dest, name);
	enumError err = shade_write (arg, dest, basedir, name, (u8 *)out, (uint)used);
	FREE (out);
	// Wall outline behind the list area: u32 {size, count, 12}, count * {f32 x1, z1, x2, z2, s16 y_top,
	// y_bottom, u16 angle (65536 = 360 degrees, facing of the wall), u16 flags}: vertical wall quads
	// from (x1, z1) to (x2, z2). The actor collision test (FUN_8005dd74) skips walls whose angle is
	// more than 0x5000 away from the mover's heading, requires y_bottom <= y <= y_top, and ignores
	// walls with (flags & 0xE000) == 0x8000; the wall type is flags & 0xDFFF. Only 0 and 0x2000 | id occur in the
	// 375 retail files: 931 walls carry an event id (1 .. 899 start the script "scp_src/<name>" when the nearest such
	// wall is touched, FUN_80061244; ids from 900 are skipped there).
	const u32 end = rd_be32 (raw + 0x24);
	if (!err && !testmode && end <= sz && sz - end >= 12 && rd_be32 (raw + end + 8) == 12)
	{
		const u32 tsz = rd_be32 (raw + end), nseg = rd_be32 (raw + end + 4);
		if (nseg && nseg < 0x100000 && 12 + 24ull * nseg == tsz && 4ull + tsz <= sz - end)
		{
			const size_t wcap = 64 + 320 * (size_t)nseg;
			char *w = MALLOC (wcap);
			size_t wu = 0;
			if (w)
			{
				wu += snprintf (w + wu, wcap - wu, "# Opoona wall segments: %u\n", nseg);
				for (uint i = 0; i < nseg; i++)
				{
					const u8 *r = raw + end + 12 + 24 * (size_t)i;
					float f[4];
					for (uint c = 0; c < 4; c++)
					{
						const u32 b = rd_be32 (r + 4 * c);
						memcpy (f + c, &b, 4);
					}
					char ev[40] = "";
					if (rd_be16 (r + 22) & 0x2000)
						snprintf (ev, sizeof (ev), " event %u", rd_be16 (r + 22) & 0xdfff);
					wu += snprintf (w + wu, wcap - wu,
						"# %u: angle %.2f flags %04x%s\nv %g %d %g\nv %g %d %g\nv %g %d %g\nv %g %d %g\nf %u %u %u %u\n", i,
						rd_be16 (r + 20) * 360.0 / 65536.0, rd_be16 (r + 22), ev, f[0],
						(int)(s16)rd_be16 (r + 16), f[1], f[2], (int)(s16)rd_be16 (r + 16), f[3], f[2],
						(int)(s16)rd_be16 (r + 18), f[3], f[0], (int)(s16)rd_be16 (r + 18), f[1],
						4 * i + 1, 4 * i + 2, 4 * i + 3, 4 * i + 4);
				}
				snprintf (name, sizeof (name), "%s.walls.obj", base);
				err = shade_write (arg, dest, basedir, name, (u8 *)w, (uint)wu);
				FREE (w);
			}
		}
	}
	FREE (raw);
	return err;
}

// item_param.bin record (444 bytes): u16be id, u16be after_use item, u8 category, char name[32] at 5,
// u32be buy at 36, u32be sell at 40, 8 flag bytes at 44, char tag[32] at 76, u16be value_a / value_b at
// 108 / 110, and seven 32 byte description lines from 216. Other bytes stay in records.tsv.
static size_t opoona_tsv_str (char *out, size_t cap, const u8 *s, size_t max)
{
	size_t used = 0;
	for (size_t k = 0; k < max && s[k] && used + 5 < cap; k++)
		if (s[k] < 0x80)
			out[used++] = s[k] < 0x20 ? ' ' : (char)s[k];
		else
			used += snprintf (out + used, cap - used, "\\x%02x", s[k]);
	return used;
}

static enumError opoona_write_items (ccp arg, ccp basedir, const u8 *raw, uint cnt, size_t rs)
{
	size_t cap = (size_t)cnt * 1024 + 128, used = 0;
	char *out = MALLOC (cap);
	if (!out)
		return ERR_OUT_OF_MEMORY;
	used += snprintf (out, cap, "id\tafter_use\tcategory\tname\tbuy\tsell\tflags\ttag\tvalue_a\tvalue_b\tdescription\n");
	for (uint i = 0; i < cnt; i++)
	{
		const u8 *r = raw + 12 + rs * i;
		used += snprintf (out + used, cap - used, "%u\t%u\t%u\t", rd_be16 (r), rd_be16 (r + 2), r[4]);
		used += opoona_tsv_str (out + used, cap - used, r + 5, 32);
		used += snprintf (out + used, cap - used, "\t%u\t%u\t", rd_be32 (r + 36), rd_be32 (r + 40));
		for (uint k = 0; k < 8; k++)
			used += snprintf (out + used, cap - used, "%02x", r[44 + k]);
		out[used++] = '\t';
		used += opoona_tsv_str (out + used, cap - used, r + 76, 32);
		used += snprintf (out + used, cap - used, "\t%u\t%u\t", rd_be16 (r + 108), rd_be16 (r + 110));
		for (uint k = 0, n = 0; k < 7; k++)
			if (r[216 + 32 * k])
			{
				if (n++)
					out[used++] = ' ';
				used += opoona_tsv_str (out + used, cap - used, r + 216 + 32 * k, 32);
			}
		out[used++] = '\n';
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	const enumError err = shade_write (arg, dest, basedir, "items.tsv", (u8 *)out, (uint)used);
	FREE (out);
	return err;
}

// system/param.bin members (`*_param.bin`, `encounter.bin`): char name[6], u16be count, u32 0,
// then count fixed-size records (the size is implied by the file size). The field layout differs
// per table and is not interpreted; records.tsv lists "index<TAB>text fields<TAB>non-zero u16be
// words as @offset=value", with NUL terminated Shift-JIS / ASCII runs (non-ASCII bytes written as \\xNN).
static enumError extract_opoona_param_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	const size_t bl = strlen (base);
	if (!is_ext (arg, ".bin")
		|| ((bl < 10 || strcasecmp (base + bl - 10, "_param.bin")) && strcasecmp (base, "encounter.bin")))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint cnt = sz > 12 ? rd_be16 (raw + 6) : 0;
	if (!cnt || rd_be32 (raw + 8) || !raw[0] || (sz - 12) % cnt || (sz - 12) / cnt < 4 || (sz - 12) / cnt > 0x1000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	for (uint i = 0; i < 6 && raw[i]; i++)
		if (raw[i] < 0x20 || raw[i] > 0x7e)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	const size_t rs = (sz - 12) / cnt;
	size_t cap = sz * 12 + 64 * (size_t)cnt + 64, used = 0;
	char *out = MALLOC (cap);
	if (!out)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	for (uint r = 0; r < cnt; r++)
	{
		const u8 *rec = raw + 12 + rs * r;
		bool taken[0x1000] = { 0 };
		used += snprintf (out + used, cap - used, "%u\t", r);
		bool first = true;
		for (size_t o = 0; o < rs;)
		{
			size_t e = o;
			bool sj = true;
			while (e < rs && rec[e] && sj)
			{
				const u8 c = rec[e];
				if (c >= 0x20 && c < 0x7f)
					e++;
				else if (c >= 0xa1 && c <= 0xdf)
					e++;
				else if (((c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc)) && e + 1 < rs
					&& rec[e + 1] >= 0x40 && rec[e + 1] != 0x7f && rec[e + 1] <= 0xfc)
					e += 2;
				else
					sj = false;
			}
			if (sj && e - o >= 3 && (e == rs || !rec[e]) && (!o || !rec[o - 1]))
			{
				used += snprintf (out + used, cap - used, "%s@%zu=", first ? "" : " | ", o);
				for (size_t k = o; k < e; k++)
					if (rec[k] < 0x80)
						out[used++] = (char)rec[k] == '\t' ? ' ' : (char)rec[k];
					else
						used += snprintf (out + used, cap - used, "\\x%02x", rec[k]);
				first = false;
				memset (taken + o, 1, e - o);
				o = e + 1;
				continue;
			}
			o++;
		}
		out[used++] = '\t';
		first = true;
		for (size_t o = 0; o + 2 <= rs; o += 2)
		{
			const uint v = rd_be16 (rec + o);
			if (v && !taken[o] && !taken[o + 1])
			{
				used += snprintf (out + used, cap - used, "%s@%zu=%u", first ? "" : " ", o, v);
				first = false;
			}
		}
		out[used++] = '\n';
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT OPOONA-PARAM:%s (%u records of %zu bytes) -> %s/records.tsv\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, cnt, rs, dest);
	enumError err = shade_write (arg, dest, basedir, "records.tsv", (u8 *)out, (uint)used);
	if (!err && !strcasecmp (base, "item_param.bin") && rs == 444)
		err = opoona_write_items (arg, basedir, raw, cnt, rs);
	FREE (out);
	FREE (raw);
	return err;
}

static enumError extract_opoona_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	u8 *dec = 0;
	const u8 *p = raw;
	size_t psz = sz;
	if (sz > 0x14 && !memcmp (raw, "SL01", 4))
	{
		const u32 usize = rd_le32 (raw + 4), csize = rd_le32 (raw + 8);
		uint dsz = 0;
		if (!usize || usize > (1u << 28) || csize > sz - 12
			|| DecodeLZO1XGrow (&dec, &dsz, raw + 12, csize) || dsz != usize)
		{
			FREE (dec);
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		p = dec;
		psz = dsz;
	}
	else if (!is_ext (arg, ".mdl") && !is_ext (arg, ".tex") && !is_ext (arg, ".mol")
		&& !(is_ext (arg, ".bin") && opoona_pkg_ok (raw, sz))
		&& !(is_ext (arg, ".tpl") && (!memcmp (raw, "TPLE", 4) || opoona_tpl_plain (raw, sz))))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	enumError err = ERR_NOTHING_TO_DO;
	if (!dec && is_ext (arg, ".tpl"))
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		ccp base = strrchr (arg, '/');
		base = base ? base + 1 : arg;
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT OPOONA-TPL:%s -> %s/\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, dest);
		err = testmode ? ERR_OK : opoona_write_member (arg, dest, basedir, base, p, psz);
		if (!err && !testmode)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
	else if (opoona_pkg_ok (p, psz))
		err = opoona_pkg_unpack (arg, basedir, depth, p, psz);
	else if (dec)
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		ccp base = strrchr (arg, '/');
		base = base ? base + 1 : arg;
		char name[PATH_MAX];
		snprintf (name, sizeof (name), "%s.dec", base);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT OPOONA-SL01:%s (%zu bytes) -> %s/%s\n",
				verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, psz, dest, name);
		err = shade_write (arg, dest, basedir, name, p, (uint)psz);
	}
	FREE (dec);
	FREE (raw);
	return err;
}

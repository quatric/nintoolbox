// Unreal Engine 1/2 packages ("*.uml", "*.upk", "*.utx", "*.usx", "*.ukx", "*.uax", "*.unr", "*.u", "*.ut2"; Surf's Up "mainwii.uml", package version 0x84): little-endian {0x9E2A83C1, u32 version|licensee<<16, flags, name count/offset, export count/offset, import count/offset}
// with offsets relative to the package start. A file may hold many packages back to back (mainwii.uml holds hundreds); each is parsed at its own tag and written to "<file>.d/pkgNNNN/".
// Names are {u8 len, text NUL, u32 flags}; imports {pkg, class, i32 outer, name} and exports {class, super, i32 outer, name, u32 flags, size, offset} use Unreal "compact index" integers. Every export is written raw to "<Class>/<outer path>/<name>_<n>.bin".
typedef struct upk_ctx_t
{
	const u8 *d;
	size_t sz, p;
	bool ok;
} upk_ctx_t;

static int upk_ci (upk_ctx_t *c)
{
	if (c->p >= c->sz)
	{
		c->ok = false;
		return 0;
	}
	const uint b = c->d[c->p++];
	const bool neg = b & 0x80;
	int v = b & 0x3f;
	if (b & 0x40)
	{
		uint shift = 6;
		for (;;)
		{
			if (c->p >= c->sz || shift > 30)
			{
				c->ok = false;
				return 0;
			}
			const uint x = c->d[c->p++];
			v |= (int)(x & 0x7f) << shift;
			shift += 7;
			if (!(x & 0x80))
				break;
		}
	}
	return neg ? -v : v;
}

static void upk_clean (char *s)
{
	for (; *s; s++)
		if (*s == '/' || *s == '\\' || (u8)*s < 0x20 || *s == ':')
			*s = '_';
}

typedef struct upk_ex_t { int cls, outer; uint name; u32 size, off; } upk_ex_t;
typedef struct upk_im_t { uint name, cls; } upk_im_t;


// UE2 property list reader for textures: collects Format, USize, VSize and the Palette export reference, returns the position after the "None" terminator or 0 on a malformed list
static size_t upk_tex_props (const u8 *raw, size_t sz, size_t p, char (*names)[96], uint nc, uint *fmt, uint *us, uint *vs, int *pal)
{
	upk_ctx_t c = { raw, sz, p, true };
	for (uint guard = 0; guard < 64 && c.ok; guard++)
	{
		const int n = upk_ci (&c);
		if (!c.ok || n < 0 || (uint)n >= nc)
			return 0;
		if (!n)
			return c.p;
		if (c.p >= sz)
			return 0;
		const uint info = raw[c.p++], typ = info & 15, sc = info >> 4 & 7;
		const bool arr = info & 0x80;
		if (typ == 10)
			upk_ci (&c);
		size_t ln = 0;
		if (typ != 3)
		{
			static const uint lens[5] = { 1, 2, 4, 12, 16 };
			if (sc < 5)
				ln = lens[sc];
			else if (sc == 5 && c.p < sz)
				ln = raw[c.p++];
			else if (sc == 6 && c.p + 2 <= sz)
			{
				ln = rd_le16 (raw + c.p);
				c.p += 2;
			}
			else if (sc == 7 && c.p + 4 <= sz)
			{
				ln = rd_le32 (raw + c.p);
				c.p += 4;
			}
			if (arr && c.p < sz)
				c.p += raw[c.p] < 128 ? 1 : raw[c.p] < 192 ? 2 : 4;
		}
		if (c.p + ln > sz)
			return 0;
		const char *nm = names[n];
		if (!strcmp (nm, "Format") && ln >= 1)
			*fmt = raw[c.p];
		else if (!strcmp (nm, "USize") && ln == 4)
			*us = rd_le32 (raw + c.p);
		else if (!strcmp (nm, "VSize") && ln == 4)
			*vs = rd_le32 (raw + c.p);
		else if (!strcmp (nm, "Palette") && typ == 5)
		{
			upk_ctx_t pc = { raw, sz, c.p, true };
			*pal = upk_ci (&pc);
		}
		c.p += ln;
	}
	return 0;
}

// decodes a Texture export (P8 with palette, DXT1, RGBA8) and writes "<png>"
static void upk_texture (const u8 *raw, size_t sz, const upk_ex_t *ex, uint ec, char (*names)[96], uint nc, const upk_ex_t *e, ccp png)
{
	uint fmt = 0, us = 0, vs = 0;
	int pal = 0;
	size_t p = upk_tex_props (raw, sz, e->off, names, nc, &fmt, &us, &vs, &pal);
	if (!p || !us || !vs || us > 4096 || vs > 4096 || (us | vs) & 3 || p + 8 > e->off + e->size)
		return;
	upk_ctx_t c = { raw, sz, p + 8, true };
	upk_ci (&c);
	c.p += 4;
	const uint cnt = (uint)upk_ci (&c);
	if (!c.ok || c.p + cnt > sz)
		return;
	const u8 *dat = raw + c.p;
	u8 *rgba = 0;
	if (fmt == 5 && cnt >= (size_t)us * vs * 4)
	{
		rgba = MALLOC ((size_t)us * vs * 4);
		for (size_t i = 0; i < (size_t)us * vs; i++)
		{
			rgba[i * 4] = dat[i * 4 + 2];
			rgba[i * 4 + 1] = dat[i * 4 + 1];
			rgba[i * 4 + 2] = dat[i * 4];
			rgba[i * 4 + 3] = dat[i * 4 + 3];
		}
	}
	else if (fmt == 3 && cnt >= (size_t)us * vs / 2)
	{
		rgba = MALLOC ((size_t)us * vs * 4);
		for (uint by = 0; by < vs; by += 4)
			for (uint bx = 0; bx < us; bx += 4)
			{
				const u8 *b = dat + ((size_t)(by / 4) * (us / 4) + bx / 4) * 8;
				const uint c0 = rd_le16 (b), c1 = rd_le16 (b + 2);
				u8 col[4][4];
				const uint cc[2] = { c0, c1 };
				for (uint k = 0; k < 2; k++)
				{
					col[k][0] = (cc[k] >> 11 & 31) * 255 / 31;
					col[k][1] = (cc[k] >> 5 & 63) * 255 / 63;
					col[k][2] = (cc[k] & 31) * 255 / 31;
					col[k][3] = 255;
				}
				for (uint k = 0; k < 3; k++)
				{
					if (c0 > c1)
					{
						col[2][k] = (2 * col[0][k] + col[1][k]) / 3;
						col[3][k] = (col[0][k] + 2 * col[1][k]) / 3;
					}
					else
					{
						col[2][k] = (col[0][k] + col[1][k]) / 2;
						col[3][k] = 0;
					}
				}
				col[2][3] = 255;
				col[3][3] = c0 > c1 ? 255 : 0;
				for (uint r = 0; r < 4; r++)
					for (uint x = 0; x < 4; x++)
						memcpy (rgba + ((size_t)(by + r) * us + bx + x) * 4, col[b[4 + r] >> (2 * x) & 3], 4);
			}
	}
	else if (fmt == 0 && cnt >= (size_t)us * vs && pal > 0 && (uint)pal <= ec)
	{
		const upk_ex_t *pe = ex + pal - 1;
		if (pe->size < 1027 || (u64)pe->off + pe->size > sz)
			return;
		upk_ctx_t pc = { raw, sz, pe->off + 1, true };
		const uint n = (uint)upk_ci (&pc);
		if (!pc.ok || n < 1 || n > 256 || pc.p + (size_t)n * 4 > sz)
			return;
		rgba = MALLOC ((size_t)us * vs * 4);
		for (size_t i = 0; i < (size_t)us * vs; i++)
		{
			const uint ix = dat[i] < n ? dat[i] : 0;
			memcpy (rgba + i * 4, raw + pc.p + (size_t)ix * 4, 4);
		}
	}
	if (rgba)
		SaveDecodedRGBAToPNG (rgba, us, vs, &be_func, png, 0, true); // takes ownership of rgba
}

// returns ERR_NOTHING_TO_DO when no valid package starts at base
static enumError upk_one (ccp arg, ccp dest, const u8 *rawb, size_t szb, uint idx, bool multi, size_t *end)
{
	const u8 *raw = rawb;
	const size_t sz = szb;
	enumError err = ERR_NOTHING_TO_DO;
	char (*names)[96] = 0;
	upk_ex_t *ex = 0;
	upk_im_t *im = 0;
	if (sz < 0x40 || rd_le32 (raw) != 0x9e2a83c1 || (rd_le32 (raw + 4) & 0xffff) < 60)
		return ERR_NOTHING_TO_DO;
	const uint nc = rd_le32 (raw + 12), no = rd_le32 (raw + 16), ec = rd_le32 (raw + 20), eo = rd_le32 (raw + 24), ic = rd_le32 (raw + 28), io = rd_le32 (raw + 32);
	if (!nc || nc > 200000 || !ec || ec > 200000 || ic > 200000 || no >= sz || eo >= sz || io > sz)
		return ERR_NOTHING_TO_DO;
	names = CALLOC (nc, sizeof (*names));
	ex = CALLOC (ec, sizeof (*ex));
	im = CALLOC (ic ? ic : 1, sizeof (*im));
	if (!names || !ex || !im)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	upk_ctx_t c = { raw, sz, no, true };
	for (uint i = 0; i < nc && c.ok; i++)
	{
		const uint l = c.p < sz ? raw[c.p++] : 0;
		if (c.p + l + 4 > sz)
		{
			c.ok = false;
			break;
		}
		uint k = 0;
		for (; k < l && k < 95 && raw[c.p + k]; k++)
			names[i][k] = (char)raw[c.p + k];
		names[i][k] = 0;
		upk_clean (names[i]);
		c.p += l + 4;
	}
	size_t pend = c.p;
	c.p = io;
	for (uint i = 0; i < ic && c.ok; i++)
	{
		upk_ci (&c);
		const int cl = upk_ci (&c);
		c.p += 4;
		const int nm = upk_ci (&c);
		im[i].cls = (uint)cl;
		im[i].name = (uint)nm;
	}
	if (c.p > pend)
		pend = c.p;
	c.p = eo;
	for (uint i = 0; i < ec && c.ok; i++)
	{
		ex[i].cls = upk_ci (&c);
		upk_ci (&c);
		if (c.p + 4 > sz)
		{
			c.ok = false;
			break;
		}
		ex[i].outer = (int)rd_le32 (raw + c.p);
		c.p += 4;
		ex[i].name = (uint)upk_ci (&c);
		c.p += 4;
		ex[i].size = (u32)upk_ci (&c);
		ex[i].off = ex[i].size ? (u32)upk_ci (&c) : 0;
	}
	if (!c.ok)
		goto done;
	if (c.p > pend)
		pend = c.p;
	for (uint i = 0; i < ec; i++)
		if (ex[i].size && (size_t)ex[i].off + ex[i].size > pend && (size_t)ex[i].off + ex[i].size <= sz)
			pend = (size_t)ex[i].off + ex[i].size;
	*end = pend;
	err = ERR_OK;
	for (uint i = 0; i < ec && !err && !testmode; i++)
	{
		const upk_ex_t *e = ex + i;
		if (e->size < 2 || (u64)e->off + e->size > sz || e->name >= nc)
			continue;
		const char *cn = "Class";
		if (e->cls < 0)
		{
			const uint ix = (uint)(-e->cls - 1);
			if (ix < ic && im[ix].name < nc)
				cn = names[im[ix].name];
		}
		else if (e->cls > 0 && (uint)e->cls <= ec && ex[e->cls - 1].name < nc)
			cn = names[ex[e->cls - 1].name];
		char outer[PATH_MAX] = "";
		int o = e->outer;
		for (uint hop = 0; o > 0 && (uint)o <= ec && hop < 8; hop++)
		{
			char tmp[PATH_MAX];
			const uint nn = ex[o - 1].name;
			snprintf (tmp, sizeof (tmp), "%s%s%s", nn < nc ? names[nn] : "_", outer[0] ? "/" : "", outer);
			snprintf (outer, sizeof (outer), "%s", tmp);
			o = ex[o - 1].outer;
		}
		char path[PATH_MAX];
		if (multi)
			snprintf (path, sizeof (path), "%s/pkg%04u/%s/%s%s%s_%u.bin", dest, idx, cn, outer, outer[0] ? "/" : "", names[e->name], i);
		else
			snprintf (path, sizeof (path), "%s/%s/%s%s%s_%u.bin", dest, cn, outer, outer[0] ? "/" : "", names[e->name], i);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (raw + e->off, 1, e->size, F.f) != e->size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		ResetFile (&F, opt_preserve);
		if (!err && !strcmp (cn, "Texture"))
		{
			char png[PATH_MAX];
			snprintf (png, sizeof (png), "%.*s.png", (int)strlen (path) - 4, path);
			upk_texture (raw, sz, ex, ec, names, nc, e, png);
		}
	}
done:
	FREE (names);
	FREE (ex);
	FREE (im);
	(void)arg;
	return err;
}

static enumError extract_upk_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir; (void)depth;
	static const char *const exts[] = { ".uml", ".upk", ".utx", ".usx", ".ukx", ".uax", ".unr", ".u", ".ut2", ".uc", ".umd", 0 };
	bool match = false;
	for (uint i = 0; exts[i]; i++)
		if (is_ext (arg, exts[i]))
			match = true;
	if (!match)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 || rd_le32 (raw) != 0x9e2a83c1)
		goto done;
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		uint n = 0;
		size_t p = 0;
		bool announced = false;
		while (p + 0x40 < sz)
		{
			size_t q = p;
			while (q + 4 <= sz && rd_le32 (raw + q) != 0x9e2a83c1)
				q++;
			if (q + 0x40 >= sz)
				break;
			size_t end = 0;
			const enumError e = upk_one (arg, dest, raw + q, sz - q, n, q != 0 || 1, &end);
			if (e == ERR_NOTHING_TO_DO)
			{
				p = q + 4;
				continue;
			}
			if (!announced && (verbose >= 0 || testmode))
			{
				fprintf (stdlog, "%s%sEXTRACT UPK:%s -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
				announced = true;
			}
			if (e)
			{
				err = e;
				break;
			}
			err = ERR_OK;
			n++;
			p = q + (end > 4 ? end : 4);
		}
	}
done:
	FREE (raw);
	return err;
}

// ".wis" block-compressed stream (Cabela's Big Game Hunter 2010 / Dangerous Hunts, Cauldron engine; startup.wis, Maps/*/*.wis, actors, weapons):
// no magic, little endian; a chain of blocks {u32 size (bit 31 = stored raw), payload}, each normally decoding to 0x7ffc bytes, ended by a
// 0xffffffff word and 0xff padding to the sector. Compressed blocks are Okumura LZSS (4096 byte ring pre-zeroed per block, write position 0xfee, flag
// bytes LSB first with 1 = literal, match {b0, b1}: offset = b0 | (b1 & 0xf0) << 4 (ring position), length = (b1 & 15) + 3). The concatenated
// payload is written as "<stem>.bin": maps / actors decode to "<Cauldron Chunk File>" containers, startup.wis to a GUI archive (BRLAN / TPL members
// behind a big endian index); those inner containers are not split yet, except that the Lua 5.1 bytecode scripts embedded in Cauldron chunk files
// ({NUL-terminated name "[Actor - 11]" / "WorldScript", u32 size, 1b 4c 75 61 51 ... chunk}, sizes verified against a full undump) are also written as
// "lua/NNN_<name>.luac"; GX textures embedded in the chunk stream ({u32 size = 16 + mip pyramid (+ up to 0x20 tail), w, h, GX format 6 RGBA8 / 14 CMPR, mip
// count}, then the pyramid; found by a validated scan, level 0 only) are written as "tex/NNN_WxH_fmtF.png". Havok classic packfiles (magic 57 e0 e0 57 10 c0 c0 10, big endian, Havok 6.1.0; the item size word in front equals the packfile's own end, taken from the
// section table) are carved to "hkx/NNN.hkx". Cauldron chunk files (item {ffffffff, id, size LE} + body starting at "<Cauldron Chunk File>"; the body's last u32 is the footer offset, the footer
// {0xcdd44ddc, name bytes, type count, flags(, version > 0x10000)} lists the NUL separated type names (e.g. header / mesh / bone / dummy) and then the object table:
// {u32 bytes, u32 count} + per object 0x18 bytes {type index, parent (-1), u32 kind (0: one u32 payload follows, 1: `count` u32s follow), data offset into the body, count, ...}) are
// split to "chunks/NNN/objects.txt" and "chunks/NNN/OOO_<type>.bin" (object blob = bytes up to the next object's data offset); mesh payload words are
// {0x54 node header size, name bytes, material name bytes, 1, 1, then seven array sizes}. A "startup.wis"-style GUI archive payload (zero u32, BE u32 0x20, data start @0x14, 12-byte records from 0x34: dirs {0x01000000 |
// name offset, parent, end index}, files {name offset, offset (0x20 aligned), size}; names from the NUL strings behind the table; the member starts
// 8 bytes after the offset, 0x20 more when the format magic (TPL / RLAN / RLYT / RFNT) only appears there) is split under "gui/"; BRLAN / BRLYT / BRFNT sizes come from their own headers when the table size is up to 0x40 short).
static bool wis_lzss (u8 *out, uint *olen, const u8 *s, uint n)
{
	static u8 ring[4096];
	memset (ring, 0, sizeof (ring));
	uint r = 4096 - 18, sp = 0, dp = 0;
	while (sp < n)
	{
		const uint f = s[sp++];
		for (uint b = 0; b < 8 && sp < n; b++)
		{
			if (f >> b & 1)
			{
				const u8 c = s[sp++];
				if (dp >= 0x7ffc)
					return false;
				out[dp++] = ring[r] = c, r = (r + 1) & 4095;
			}
			else
			{
				if (sp + 2 > n)
					return false;
				const uint a = s[sp], j = s[sp + 1], off = a | (j & 0xf0) << 4, len = (j & 15) + 3;
				sp += 2;
				if (dp + len > 0x7ffc)
					return false;
				for (uint k = 0; k < len; k++)
				{
					const u8 c = ring[(off + k) & 4095];
					out[dp++] = ring[r] = c, r = (r + 1) & 4095;
				}
			}
		}
	}
	*olen = dp;
	return true;
}

static void wis_textures (ccp arg, ccp dest, ccp basedir, const u8 *o, size_t ol)
{
	uint nt = 0;
	for (size_t i = 8; i + 20 <= ol; i++)
	{
		const u32 sz1 = rd_le32 (o + i), w = rd_le32 (o + i + 4), h = rd_le32 (o + i + 8), f = rd_le32 (o + i + 12), m = rd_le32 (o + i + 16);
		if (w < 16 || h < 16 || w > 2048 || h > 2048 || (w & (w - 1)) || (h & (h - 1)) || (f != 6 && f != 14) || !m || m > 10)
			continue;
		u64 pyr = 0, l0 = 0;
		for (uint l = 0; l < m; l++)
		{
			const u64 lw = (w >> l) ? (w >> l) : 1, lh = (h >> l) ? (h >> l) : 1;
			const u64 b = f == 6 ? ((lw + 3) & ~3ull) * ((lh + 3) & ~3ull) * 4 : ((lw + 7) & ~7ull) * ((lh + 7) & ~7ull) / 2;
			if (!l)
				l0 = b;
			pyr += b;
		}
		if (sz1 < pyr + 16 || sz1 > pyr + 16 + 0x20 || i + 20 + pyr > ol)
			continue;
		u8 *rgba = 0;
		if (DecodeGXTexture_RGBA (&rgba, w, h, f, o + i + 20, (uint)l0, 0, 0, 0) || !rgba)
		{
			FREE (rgba);
			continue;
		}
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%stex/%03u_%ux%u_fmt%u.png", dest, basedir ? basedir : "", nt++, w, h, f);
		SaveDecodedRGBAToPNG (rgba, w, h, &be_func, path, 0, true);
		i += 19 + pyr;
	}
}

static void wis_hkx (ccp arg, ccp dest, ccp basedir, const u8 *o, size_t ol)
{
	static const u8 mg[8] = { 0x57, 0xe0, 0xe0, 0x57, 0x10, 0xc0, 0xc0, 0x10 };
	uint nh = 0;
	for (size_t i = 12; i + 0x70 <= ol; i++)
	{
		if (o[i] != 0x57 || memcmp (o + i, mg, 8))
			continue;
		const u32 ns = rd_be32 (o + i + 0x14);
		if (!ns || ns > 16 || i + 0x40 + 48 * (u64)ns > ol)
			continue;
		u64 end = 0;
		for (uint k = 0; k < ns; k++)
		{
			const u8 *h = o + i + 0x40 + 48 * (size_t)k;
			const u64 e = (u64)rd_be32 (h + 20) + rd_be32 (h + 20 + 24);
			if (e > end)
				end = e;
		}
		if (end < 0x40 + 48 * (u64)ns || i + end > ol)
			continue;
		char fn[64];
		snprintf (fn, sizeof (fn), "hkx/%03u.hkx", nh++);
		if (shade_write (arg, dest, basedir, fn, o + i, (uint)end))
			return;
		i += end - 1;
	}
}

// mesh object -> OBJ: blob = 0x54 byte node header (bbox floats at +0x38..), name, material name, vertex array {s16 BE pos[3], s16 BE normal[3]},
// uv array {2 x u16 BE}, small arrays, then (last `sizeG` bytes) a list of {0x99, u16 count, count x {u16 vertex, u16 normal, u16 uv}} triangle strips.
// Positions are skin/bone-local fixed point; scaled by 1/8192 here.
static void wis_mesh_obj (ccp arg, ccp dest, ccp basedir, uint nc, uint oi, const u8 *bl, u32 bn, const u8 *w)
{
	if (bn < 0x60 || rd_le32 (w) != 0x54)
		return;
	const u32 nl = rd_le32 (w + 4), ml = rd_le32 (w + 8), sa = rd_le32 (w + 20), sg = rd_le32 (w + 44);
	if (rd_le32 (w - 0x18 + 0x10) < 12)
		return;
	const u64 va = 0x54 + (u64)nl + ml;
	if (sa % 12 || va + sa > bn || sg > bn || sg + va + sa > bn)
		return;
	const uint nv = sa / 12;
	const u8 *sp = bl + bn - sg, *se = bl + bn;
	size_t cap = 64 + (size_t)nv * 48 + (size_t)sg * 12;
	char *t = MALLOC (cap);
	size_t tl = snprintf (t, cap, "# %.*s\n", (int)nl > 0 ? (int)nl - 1 : 0, (ccp)bl + 0x54);
	for (uint i = 0; i < nv; i++)
	{
		const u8 *v = bl + va + 12 * (size_t)i;
		tl += snprintf (t + tl, cap - tl, "v %.5f %.5f %.5f\n", (s16)(v[0] << 8 | v[1]) / 8192.0, (s16)(v[2] << 8 | v[3]) / 8192.0, (s16)(v[4] << 8 | v[5]) / 8192.0);
	}
	uint nt = 0;
	while (sp + 3 <= se && sp[0] == 0x99)
	{
		const uint n = sp[1] << 8 | sp[2];
		sp += 3;
		if ((u64)n * 6 > (u64)(se - sp))
			break;
		for (uint i = 0; i + 2 < n && tl + 64 < cap; i++)
		{
			uint a = sp[6 * i] << 8 | sp[6 * i + 1], b2 = sp[6 * (i + 1)] << 8 | sp[6 * (i + 1) + 1], c = sp[6 * (i + 2)] << 8 | sp[6 * (i + 2) + 1];
			if (i & 1)
				b2 ^= c, c ^= b2, b2 ^= c;
			if (a < nv && b2 < nv && c < nv && a != b2 && b2 != c && a != c)
				tl += snprintf (t + tl, cap - tl, "f %u %u %u\n", a + 1, b2 + 1, c + 1), nt++;
		}
		sp += 6 * n;
	}
	if (nt)
	{
		char fn[96];
		snprintf (fn, sizeof (fn), "chunks/%03u/%03u_mesh.obj", nc, oi);
		shade_write (arg, dest, basedir, fn, (const u8 *)t, (uint)tl);
	}
	FREE (t);
}

static void wis_chunks (ccp arg, ccp dest, ccp basedir, const u8 *o, size_t ol)
{
	static const char magic[] = "<Cauldron Chunk File>";
	uint nc = 0;
	for (size_t m = 12; m + 64 < ol; m++)
	{
		if (o[m] != '<' || memcmp (o + m, magic, 21) || rd_le32 (o + m - 12) != 0xffffffff)
			continue;
		const u32 bsz = rd_le32 (o + m - 4);
		if (bsz < 0x40 || m + bsz > ol)
			continue;
		const u8 *b = o + m;
		const u32 fo = rd_le32 (b + bsz - 4);
		if ((u64)fo + 16 + 8 > bsz || rd_le32 (b + fo) != 0xcdd44ddc)
			continue;
		const u32 nb = rd_le32 (b + fo + 4), nt = rd_le32 (b + fo + 8);
		const u64 ot = (u64)fo + 16 + nb;
		if (!nt || nt > 64 || ot + 8 > bsz)
			continue;
		const u32 obytes = rd_le32 (b + ot), no = rd_le32 (b + ot + 4);
		if (no > 100000 || ot + 8 + obytes > bsz)
			continue;
		// walk objects
		const u32 maxo = no;
		u32 *dof = MALLOC (4 * (maxo + 1)), *typ = MALLOC (4 * (maxo + 1)), *p0 = MALLOC (4 * (maxo + 1));
		u64 p = ot + 8;
		uint n = 0;
		for (; n < no && p + 0x18 <= bsz; n++)
		{
			const u32 kind = b[p + 11], cnt = rd_le32 (b + p + 16);
			typ[n] = rd_le32 (b + p);
			dof[n] = rd_le32 (b + p + 12);
			p0[n] = (u32)(p + 0x18);
			p += 0x18 + (kind == 0 ? 4 : kind == 1 ? 4 * (u64)cnt : 0);
		}
		if (n != no || p != ot + 8 + obytes)
		{
			FREE (dof), FREE (typ), FREE (p0);
			continue;
		}
		char names[64][48];
		{
			const u8 *np = b + fo + 16;
			uint k = 0, pos = 0;
			for (; k < nt && pos < nb; k++)
			{
				snprintf (names[k], 48, "%.40s", (ccp)np + pos);
				pos += (uint)strlen ((ccp)np + pos) + 1;
			}
			for (; k < nt; k++)
				snprintf (names[k], 48, "t%u", k);
		}
		const size_t cap = 256 + (size_t)no * 200 + (size_t)obytes * 12;
		char *t = MALLOC (cap);
		size_t tl = snprintf (t, cap, "chunk body %u footer 0x%x types %u objects %u flags 0x%x\n", bsz, fo, nt, no, rd_le32 (b + fo + 12));
		p = ot + 8;
		for (uint i = 0; i < no; i++)
		{
			const u32 kind = b[p + 11], cnt = rd_le32 (b + p + 16), par = rd_le32 (b + p + 4);
			const u32 ty = typ[i] < nt ? typ[i] : 0;
			tl += snprintf (t + tl, cap - tl, "%u type %s parent %d kind %u data 0x%x count %u words", i, names[ty], (int)par, kind, dof[i], cnt);
			const uint nw = kind == 0 ? 1 : kind == 1 ? cnt : 0;
			for (uint w = 0; w < nw && tl + 16 < cap; w++)
				tl += snprintf (t + tl, cap - tl, " %x", rd_le32 (b + p + 0x18 + 4 * (size_t)w));
			tl += snprintf (t + tl, cap - tl, "\n");
			p += 0x18 + 4 * (u64)nw;
		}
		char fn[96];
		snprintf (fn, sizeof (fn), "chunks/%03u/objects.txt", nc);
		shade_write (arg, dest, basedir, fn, (const u8 *)t, (uint)tl);
		FREE (t);
		for (uint i = 0; i < no; i++)
		{
			u32 end = fo;
			for (uint j = 0; j < no; j++)
				if (dof[j] > dof[i] && dof[j] < end)
					end = dof[j];
			const u32 ty = typ[i] < nt ? typ[i] : 0;
			if (ty && dof[i] < end && end <= fo)
			{
				snprintf (fn, sizeof (fn), "chunks/%03u/%03u_%s.bin", nc, i, names[ty]);
				shade_write (arg, dest, basedir, fn, b + dof[i], end - dof[i]);
				if (!strcmp (names[ty], "mesh"))
					wis_mesh_obj (arg, dest, basedir, nc, i, b + dof[i], end - dof[i], b + p0[i]);
			}
		}
		FREE (dof), FREE (typ), FREE (p0);
		nc++;
		m += bsz - 1;
	}
}

static void wis_gui (ccp arg, ccp dest, ccp basedir, const u8 *o, size_t ol)
{
	if (ol < 0x80 || rd_le32 (o) || rd_be32 (o + 0x0c) != 0x20)
		return;
	const u32 ds = rd_be32 (o + 0x14), flag0 = rd_be32 (o + 0x34), end0 = rd_be32 (o + 0x3c);
	if (ds >= ol || ds & 0x1f || flag0 != 0x01000001 || end0 < 3 || end0 > 20000 || 0x34 + 12 * (u64)(end0 - 1) > ds)
		return;
	const u32 ne = end0 - 1, base = 0x34 + 12 * ne;
	char path[PATH_MAX] = "";
	uint ends[32], depth = 0;
	size_t plen[32];
	for (uint j = 1; j <= ne; j++)
	{
		while (depth && j >= ends[depth - 1])
			depth--, path[plen[depth]] = 0;
		const u8 *e = o + 0x34 + 12 * (size_t)(j - 1);
		const u32 w0 = rd_be32 (e), w1 = rd_be32 (e + 4), w2 = rd_be32 (e + 8);
		const u32 no = w0 & 0xffffff;
		if (base + (u64)no >= ds || !memchr (o + base + no, 0, ds - base - no))
			return;
		ccp nm = (ccp)o + base + no;
		if (strlen (nm) > 200 || strchr (nm, '/') || strchr (nm, '\\') || !strcmp (nm, "..") || !strcmp (nm, "."))
			return;
		if (w0 >> 24)
		{
			if (j > 1 && depth < 31 && strlen (path) + strlen (nm) < sizeof (path) - 2)
			{
				plen[depth] = strlen (path);
				ends[depth++] = w2;
				strcat (path, nm), strcat (path, "/");
			}
			else if (j == 1)
				ends[depth] = w2, plen[depth++] = 0;
			(void)w1;
			continue;
		}
		const u32 off = w1, sz = w2;
		if ((u64)off + 8 + sz > ol)
			continue;
		ccp ext = strrchr (nm, '.');
		const u8 *mg = !ext ? 0 : !strcmp (ext, ".tpl") ? (const u8 *)"\x00\x20\xaf\x30" : !strcmp (ext, ".brlan") ? (const u8 *)"RLAN"
			: !strcmp (ext, ".brlyt") ? (const u8 *)"RLYT" : !strcmp (ext, ".brfnt") ? (const u8 *)"RFNT" : 0;
		uint pad = 0;
		if (mg && memcmp (o + off + 8, mg, 4) && (u64)off + 0x28 + sz <= ol && !memcmp (o + off + 0x28, mg, 4))
			pad = 0x20;
		u32 wsz = sz;
		if (mg && mg[0] != 0 && (u64)off + 8 + pad + 12 <= ol)
		{
			const u32 hs = rd_be32 (o + off + 8 + pad + 8);
			if (hs > sz && hs <= sz + 0x40 && (u64)off + 8 + pad + hs <= ol)
				wsz = hs;
		}
		char full[PATH_MAX];
		snprintf (full, sizeof (full), "gui/%s%s", path, nm);
		if (shade_write (arg, dest, basedir, full, o + off + 8 + pad, wsz))
			return;
	}
}

static enumError extract_wis_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0, *out = 0;
	size_t sz = 0, cap = 0, ol = 0;
	if (!is_ext (arg, ".wis") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	size_t p = 0;
	uint nb = 0;
	bool ended = false;
	while (p + 4 <= sz)
	{
		const u32 h = rd_le32 (raw + p);
		if (h == 0xffffffff)
		{
			ended = true;
			break;
		}
		const u32 n = h & 0x7fffffff;
		if (!n || n > 0x8000 || p + 4 + n > sz)
			break;
		if (ol + 0x8000 > cap)
		{
			cap = cap ? cap * 2 : 0x100000;
			out = REALLOC (out, cap);
		}
		uint bl = n;
		if (h >> 31)
			memcpy (out + ol, raw + p + 4, n);
		else if (!wis_lzss (out + ol, &bl, raw + p + 4, n))
			goto done;
		ol += bl;
		p += 4 + n;
		nb++;
	}
	if (!nb || !ended)
		goto done;
	for (size_t i = p; i < sz; i++)
		if (raw[i] != 0xff && raw[i] != 0)
			goto done;
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT WIS:%s (%u blocks, %zu bytes) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nb, ol, dest);
		err = ERR_OK;
		if (!testmode)
		{
			ccp b = strrchr (arg, '/');
			b = b ? b + 1 : arg;
			snprintf (name, sizeof (name), "%s", b);
			char *dot = strrchr (name, '.');
			if (dot)
				*dot = 0;
			strncat (name, ".bin", sizeof (name) - strlen (name) - 1);
			err = shade_write (arg, dest, basedir, name, out, ol);
			if (!err)
				wis_gui (arg, dest, basedir, out, ol);
			if (!err && ol > 0x40 && !memcmp (out + 0xc, "<Cauldron Chunk File>", 21))
				wis_textures (arg, dest, basedir, out, ol);
			if (!err && ol > 0x40 && !memcmp (out + 0xc, "<Cauldron Chunk File>", 21))
				wis_hkx (arg, dest, basedir, out, ol);
			if (!err && ol > 0x40 && !memcmp (out + 0xc, "<Cauldron Chunk File>", 21))
				wis_chunks (arg, dest, basedir, out, ol);
			static const u8 luahdr[12] = { 0x1b, 'L', 'u', 'a', 0x51, 0, 1, 4, 4, 4, 8, 0 };
			uint nl = 0;
			for (size_t i = 5; !err && i + 12 <= ol; i++)
			{
				if (out[i] != 0x1b || memcmp (out + i, luahdr, 12) || out[i - 5])
					continue;
				const u32 ls = rd_le32 (out + i - 4);
				if (ls < 12 || ls > ol - i)
					continue;
				size_t ns = i - 5;
				while (ns > 0 && out[ns - 1] && i - 5 - ns < 40)
					ns--;
				char ln[64], fn[128];
				uint k = 0;
				for (size_t q = ns; q < i - 5 && k < sizeof (ln) - 1; q++)
					if (out[q] != '[' && out[q] != ']')
						ln[k++] = isalnum (out[q]) || out[q] == '.' || out[q] == '-' ? out[q] : '_';
				ln[k] = 0;
				snprintf (fn, sizeof (fn), "lua/%03u_%s.luac", nl++, ln);
				err = shade_write (arg, dest, basedir, fn, out + i, ls);
				i += ls - 1;
			}
		}
	}
done:
	FREE (raw);
	FREE (out);
	return err;
}

// "JBOA" (".aap", Battle of Giants engine): the sprite animation manifest that goes with an ".atb" (SLBT tile data) and a "TTLP" palette. Little endian: {char "JBOA", 6 bytes flags, u32 len + atb path, u32 npal,
// npal * {u32 len + palette path}, u32 nanims, nanims * {u16 3, u32 nframes, nframes * {u32 nobj, nobj * {u16 attr0, u16 attr1, u16 attr2}, 8 bytes}}}. The three u16 are Nintendo DS OAM attributes: attr0 low byte y,
// attr0/attr1 top bits shape/size, attr1 low 9 bits x, attr2 low 10 bits the tile index in 32 byte units of the unpacked SLBT data (tiles of one object follow each other, row by row), attr2 top 4 bits the 16 color bank.
// The bit depth is not stored: a palette of 200 or more colors means 8 bit tiles. Each frame is composed (index 0 transparent) and written as "<name>_<anim>_<frame>.png".
// The sibling ".atb" sits in the same folder and the palette path ("2d/palette/...") is resolved against the "2d" folder found in the manifest's own path.
// Empirical (two samples): 8 bit objects of a raw SLBT (flag 0, trophy.atb) only come out right with wide/tall swapped, an LZ10 one (the 100 All-Time Favorites card_back.atb) does not, so the swap is applied to raw 8 bit data only.
// Manifests with rotation/scaling objects (attr0 bit 8; 3 of 84 in one game) carry extra bytes per frame and are left alone.
static enumError extract_jboa_file (ccp arg, ccp basedir, uint depth)
{
	static const u8 std[4][4][2] = { { {8, 8}, {16, 16}, {32, 32}, {64, 64} }, { {16, 8}, {32, 8}, {32, 16}, {64, 32} }, { {8, 16}, {8, 32}, {16, 32}, {32, 64} }, { {0, 0}, {0, 0}, {0, 0}, {0, 0} } };
	u8 *raw = 0, *atb = 0, *data = 0, *pal = 0, *rgba = 0;
	size_t sz = 0, asz = 0, dn = 0, psz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0x20 || memcmp (raw, "JBOA", 4))
		goto done;
	size_t p = 10;
#define JB_NEED(n) do { if ((size_t)(n) > sz - p) goto done; } while (0)
	JB_NEED (4);
	u32 n = rd_le32 (raw + p);
	p += 4;
	JB_NEED (n);
	char atbname[PATH_MAX] = "";
	{
		const char *b = (ccp)raw + p, *e = b + n, *s = b;
		for (const char *q = b; q < e; q++)
			if (*q == '/')
				s = q + 1;
		if (e - s <= 0 || e - s >= 200)
			goto done;
		snprintf (atbname, sizeof (atbname), "%.*s", (int)(e - s), s);
	}
	p += n;
	JB_NEED (4);
	const u32 npal = rd_le32 (raw + p);
	p += 4;
	if (!npal || npal > 16)
		goto done;
	char palname[256] = "";
	for (u32 i = 0; i < npal; i++)
	{
		JB_NEED (4);
		n = rd_le32 (raw + p);
		p += 4;
		JB_NEED (n);
		if (!i && n < sizeof (palname))
			snprintf (palname, sizeof (palname), "%.*s", (int)n, (ccp)raw + p);
		p += n;
	}
	if (!palname[0])
		goto done;
	JB_NEED (4);
	const u32 nanim = rd_le32 (raw + p);
	p += 4;
	if (!nanim || nanim > 4096)
		goto done;
	// first pass: validate the frame structure and count the frames
	const size_t body = p;
	uint nfr = 0;
	for (u32 a = 0; a < nanim; a++)
	{
		JB_NEED (6);
		const u32 nf = rd_le32 (raw + p + 2);
		p += 6;
		if (nf > 4096 || (nfr += nf) > 20000)
			goto done;
		for (u32 f = 0; f < nf; f++)
		{
			JB_NEED (4);
			const u32 no = rd_le32 (raw + p);
			p += 4;
			if (no > 256)
				goto done;
			JB_NEED (6 * (size_t)no + 8);
			for (u32 o = 0; o < no; o++)
				if (rd_le16 (raw + p + 6 * o) & 0x100)
					goto done;
			p += 6 * no + 8;
		}
	}
	if (!nfr || sz - p > 1)
		goto done;
	// sibling files
	char path[PATH_MAX];
	const char *sl = strrchr (arg, '/');
	snprintf (path, sizeof (path), "%.*s%s", sl ? (int)(sl + 1 - arg) : 0, arg, atbname);
	if (LoadFileAlloc (path, 0, 0, &atb, &asz, 0, 0, 0, false) || !atb)
		goto done;
	bool is_raw;
	if (!slbt_unpack (atb, asz, &data, &dn, &is_raw))
		goto done;
	{
		const char *r = strstr (arg, "/2d/");
		if (r)
			snprintf (path, sizeof (path), "%.*s/%s", (int)(r - arg), arg, palname);
		else if (!strncmp (arg, "2d/", 3))
			snprintf (path, sizeof (path), "%s", palname);
		else
			goto done;
	}
	if (LoadFileAlloc (path, 0, 0, &pal, &psz, 0, 0, 0, false) || !pal || psz < 10 || memcmp (pal, "TTLP", 4))
		goto done;
	uint ncol = rd_le16 (pal + 4) / 2;
	if (ncol > (psz - 8) / 2)
		ncol = (uint)((psz - 8) / 2);
	if (!ncol)
		goto done;
	const uint bpp = ncol >= 200 ? 8 : 4, tb = bpp * 8;
	const bool swap = bpp == 8 && is_raw;
	char base[PATH_MAX];
	snprintf (base, sizeof (base), "%s", sl ? sl + 1 : arg);
	char *dot = strrchr (base, '.');
	if (dot)
		*dot = 0;
	p = body;
	uint written = 0;
	err = ERR_OK;
	for (u32 a = 0; a < nanim && err == ERR_OK; a++)
	{
		const u32 nf = rd_le32 (raw + p + 2);
		p += 6;
		for (u32 f = 0; f < nf && err == ERR_OK; f++)
		{
			const u32 no = rd_le32 (raw + p);
			const u8 *ob = raw + p + 4;
			p += 4 + 6 * no + 8;
			int x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20);
			struct { int x, y, w, h; uint ti, bank; } objs[256];
			uint k = 0;
			for (u32 o = 0; o < no; o++)
			{
				const uint a0 = rd_le16 (ob + 6 * o), a1 = rd_le16 (ob + 6 * o + 2), a2 = rd_le16 (ob + 6 * o + 4);
				if ((a0 >> 8 & 3) == 2 || (a0 >> 14) == 3)
					continue;
				uint shape = a0 >> 14;
				int w = std[shape][a1 >> 14][0], h = std[shape][a1 >> 14][1];
				if (swap && shape)
				{
					const int t = w;
					w = h;
					h = t;
				}
				const int x = (int)(a1 & 0x1ff) - ((a1 & 0x100) ? 512 : 0), y = (int)(a0 & 0xff) - ((a0 & 0x80) ? 256 : 0);
				objs[k].x = x; objs[k].y = y; objs[k].w = w; objs[k].h = h; objs[k].ti = a2 & 0x3ff; objs[k].bank = a2 >> 12;
				if (x < x0) x0 = x;
				if (y < y0) y0 = y;
				if (x + w > x1) x1 = x + w;
				if (y + h > y1) y1 = y + h;
				k++;
			}
			if (!k || x1 - x0 > 1024 || y1 - y0 > 1024)
				continue;
			const uint W = (uint)(x1 - x0), H = (uint)(y1 - y0);
			rgba = CALLOC (4, (size_t)W * H);
			if (!rgba)
			{
				err = ERR_OUT_OF_MEMORY;
				break;
			}
			for (uint i = k; i-- > 0;)
				for (int ty = 0; ty < objs[i].h / 8; ty++)
					for (int tx = 0; tx < objs[i].w / 8; tx++)
					{
						const size_t t0 = (size_t)objs[i].ti * 32 + ((size_t)ty * (objs[i].w / 8) + tx) * tb;
						for (uint py = 0; py < 8; py++)
							for (uint px = 0; px < 8; px++)
							{
								const size_t off = t0 + (bpp == 8 ? py * 8 + px : py * 4 + px / 2);
								if (off >= dn)
									continue;
								uint c = data[off];
								if (bpp == 4)
									c = (px & 1) ? c >> 4 : c & 15;
								if (!c)
									continue;
								c += bpp == 4 ? objs[i].bank * 16 : 0;
								if (c >= ncol)
									continue;
								const uint v = rd_le16 (pal + 8 + 2 * c);
								u8 *q = rgba + 4 * ((size_t)(objs[i].y - y0 + ty * 8 + py) * W + (objs[i].x - x0 + tx * 8 + px));
								q[0] = (v & 31) * 255 / 31;
								q[1] = (v >> 5 & 31) * 255 / 31;
								q[2] = (v >> 10 & 31) * 255 / 31;
								q[3] = 255;
							}
					}
			char name[PATH_MAX + 32];
			snprintf (name, sizeof (name), "%s_%u_%u", base, a, f);
			err = kyl_write_rgba_png (arg, basedir, name, (uint)-1, W, H, rgba);
			FREE (rgba);
			rgba = 0;
			written++;
		}
	}
	if (!written && err == ERR_OK)
		err = ERR_NOTHING_TO_DO;
#undef JB_NEED
done:
	FREE (rgba);
	FREE (pal);
	FREE (data);
	FREE (atb);
	FREE (raw);
	return err;
}

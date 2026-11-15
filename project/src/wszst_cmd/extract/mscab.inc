// Microsoft Cabinet (MSCF) archives, as used by e.g. Tales of Symphonia:
// Dawn of the New World (every `.bin` / `.dat` wraps one file). Stored (0),
// MSZIP (1) and LZX (3) folders are decoded; Quantum folders are skipped.
//   CFHEADER: "MSCF", u32 res, u32 size, u32 res, u32 coffFiles, u32 res,
//     u8 minor, u8 major, u16 nFolders, u16 nFiles, u16 flags, u16 setId,
//     u16 iCab, [flags&4: u16 cbHdrRes, u8 cbFolderRes, u8 cbDataRes, res],
//     [flags&1: prev cab + disk names], [flags&2: next cab + disk names].
//   CFFOLDER: u32 coffCabStart, u16 nData, u16 type, [reserve].
//   CFFILE: u32 size, u32 offInFolder, u16 folder, u16 date, u16 time,
//     u16 attr, name\0.  CFDATA: u32 csum, u16 cb, u16 cbUncomp, [reserve].
// All little-endian. MSZIP blocks are raw deflate after a "CK" marker, each
// primed with the previous 32 KiB of output.
static inline uint cab_u (const u8 *p, uint n)
{
	uint v = 0;
	for (uint i = 0; i < n; i++)
		v |= (uint)p[i] << (8 * i);
	return v;
}

// ---- LZX (as used by CAB; one folder is one continuous stream) ----
typedef struct { const u8 *p, *e; u64 buf; uint left; bool over; } lzx_br_t;
static inline uint lzx_bits (lzx_br_t *b, uint n)
{
	if (!n)
		return 0;
	while (b->left < n)
	{
		uint w = 0;
		if (b->p + 2 <= b->e)
		{
			w = b->p[0] | (b->p[1] << 8);
			b->p += 2;
		}
		else
			b->over = true;
		b->buf = (b->buf << 16) | w;
		b->left += 16;
	}
	b->left -= n;
	return (uint)((b->buf >> b->left) & ((1u << n) - 1));
}

typedef struct { u16 cnt[17]; u16 sym[1056]; uint n; } lzx_tree_t;
static void lzx_build (lzx_tree_t *t, const u8 *lens, uint n)
{
	memset (t->cnt, 0, sizeof (t->cnt));
	t->n = n;
	for (uint i = 0; i < n; i++)
		t->cnt[lens[i]]++;
	u16 offs[17];
	offs[1] = 0;
	for (uint l = 1; l < 16; l++)
		offs[l + 1] = offs[l] + t->cnt[l];
	for (uint i = 0; i < n; i++)
		if (lens[i])
			t->sym[offs[lens[i]]++] = i;
}
static int lzx_sym (lzx_br_t *b, const lzx_tree_t *t)
{
	int code = 0, first = 0, index = 0;
	for (uint l = 1; l <= 16; l++)
	{
		code |= lzx_bits (b, 1);
		const int count = t->cnt[l];
		if (code - count < first)
			return t->sym[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	return -1;
}
static bool lzx_read_lens (lzx_br_t *b, u8 *lens, uint first, uint last)
{
	u8 pl[20];
	lzx_tree_t pt;
	for (uint i = 0; i < 20; i++)
		pl[i] = lzx_bits (b, 4);
	lzx_build (&pt, pl, 20);
	for (uint x = first; x < last;)
	{
		int z = lzx_sym (b, &pt);
		if (z < 0 || b->over)
			return false;
		if (z >= 17)
		{
			uint run;
			if (z == 17)
				run = lzx_bits (b, 4) + 4;
			else if (z == 18)
				run = lzx_bits (b, 5) + 20;
			else
			{
				run = lzx_bits (b, 1) + 4;
				const int y = lzx_sym (b, &pt);
				if (y < 0 || y > 16)
					return false;
				int v = lens[x] - y;
				if (v < 0)
					v += 17;
				if (x + run > last)
					return false;
				while (run--)
					lens[x++] = v;
				continue;
			}
			if (x + run > last)
				return false;
			while (run--)
				lens[x++] = 0;
		}
		else
		{
			int v = lens[x] - z;
			if (v < 0)
				v += 17;
			lens[x++] = v;
		}
	}
	return true;
}

static bool cab_lzx_folder (u8 *out, uint out_size, const u8 *raw, size_t raw_size,
	uint start, uint ndata, uint data_res, uint wbits)
{
	static const u16 slots_tab[7] = { 30, 32, 36, 42, 50, 66, 98 };
	if (wbits < 15 || wbits > 21)
		return false;
	const uint nslots = slots_tab[wbits - 15], nmain = 256 + nslots * 8;
	u32 pbase[98];
	u8 pextra[98];
	for (uint i = 0, base = 0; i < nslots; i++)
	{
		pextra[i] = i < 4 ? 0 : i >= 36 ? 17 : (i - 2) / 2;
		pbase[i] = base;
		base += 1u << pextra[i];
	}
	u8 mainl[256 + 98 * 8] = {0}, lenl[249] = {0}, alnl[8];
	lzx_tree_t mt, lt, at;
	uint R0 = 1, R1 = 1, R2 = 1, btype = 0, bremain = 0, blen = 0, intel_size = 0;
	bool hdr = false, intel_started = false;
	size_t p = start;
	uint o = 0;
	u8 fe8[65536] = {0};
	if (ndata > sizeof (fe8))
		return false;
	for (uint fi = 0; fi < ndata; fi++)
	{
		if (p + 8 + data_res > raw_size)
			return false;
		const uint cb = cab_u (raw + p + 4, 2), fsz = cab_u (raw + p + 6, 2);
		p += 8 + data_res;
		if (p + cb > raw_size || (size_t)o + fsz > out_size)
			return false;
		lzx_br_t b = { raw + p, raw + p + cb, 0, 0, false };
		if (!hdr)
		{
			hdr = true;
			if (lzx_bits (&b, 1))
			{
				intel_size = lzx_bits (&b, 16) << 16;
				intel_size |= lzx_bits (&b, 16);
			}
		}
		const uint fend = o + fsz;
		while (o < fend)
		{
			if (!bremain)
			{
				if (btype == 3 && (blen & 1))
					b.p++;
				btype = lzx_bits (&b, 3);
				blen = lzx_bits (&b, 16) << 8;
				blen |= lzx_bits (&b, 8);
				bremain = blen;
				if (btype == 1 || btype == 2)
				{
					if (btype == 2)
					{
						for (uint i = 0; i < 8; i++)
							alnl[i] = lzx_bits (&b, 3);
						lzx_build (&at, alnl, 8);
					}
					if (!lzx_read_lens (&b, mainl, 0, 256)
						|| !lzx_read_lens (&b, mainl, 256, nmain))
						return false;
					if (mainl[0xe8])
						intel_started = true;
					lzx_build (&mt, mainl, nmain);
					if (!lzx_read_lens (&b, lenl, 0, 249))
						return false;
					lzx_build (&lt, lenl, 249);
				}
				else if (btype == 3)
				{
					intel_started = true;
					if (!b.left)
						lzx_bits (&b, 16);
					b.left = 0;
					b.buf = 0;
					if (b.p + 12 > b.e)
						return false;
					R0 = cab_u (b.p, 4);
					R1 = cab_u (b.p + 4, 4);
					R2 = cab_u (b.p + 8, 4);
					b.p += 12;
				}
				else
					return false;
				if (!blen)
					return false;
			}
			uint run = bremain < fend - o ? bremain : fend - o;
			if (btype == 3)
			{
				if (b.p + run > b.e)
					return false;
				memcpy (out + o, b.p, run);
				b.p += run;
				o += run;
				bremain -= run;
				continue;
			}
			uint used = 0;
			while (used < run)
			{
				const int sym = lzx_sym (&b, &mt);
				if (sym < 0 || b.over)
					return false;
				if (sym < 256)
				{
					out[o++] = sym;
					used++;
					continue;
				}
				uint ml = (sym - 256) & 7;
				const uint slot = (sym - 256) >> 3;
				if (ml == 7)
				{
					const int ls = lzx_sym (&b, &lt);
					if (ls < 0)
						return false;
					ml += ls;
				}
				ml += 2;
				uint off;
				if (slot == 0)
					off = R0;
				else if (slot == 1)
				{
					off = R1;
					R1 = R0;
					R0 = off;
				}
				else if (slot == 2)
				{
					off = R2;
					R2 = R0;
					R0 = off;
				}
				else
				{
					if (slot >= nslots)
						return false;
					off = pbase[slot] - 2;
					const uint ex = pextra[slot];
					if (btype == 2 && ex >= 3)
					{
						off += lzx_bits (&b, ex - 3) << 3;
						const int as = lzx_sym (&b, &at);
						if (as < 0)
							return false;
						off += as;
					}
					else if (ex)
						off += lzx_bits (&b, ex);
					R2 = R1;
					R1 = R0;
					R0 = off;
				}
				if (!off || off > o || ml > run - used)
					return false;
				for (uint k = 0; k < ml; k++, o++)
					out[o] = out[o - off];
				used += ml;
			}
			bremain -= run;
		}
		if (intel_size && intel_started && fsz > 10)
			fe8[fi] = 1;
		p += cb;
	}
	if (o != out_size)
		return false;
	// E8 call translation runs on the finished output; the window itself
	// must stay untranslated for later matches.
	for (uint fi = 0, fs = 0; fi < ndata; fi++, fs += 32768)
	{
		if (!fe8[fi])
			continue;
		u8 *d = out + fs;
		const uint fsz = out_size - fs < 32768 ? out_size - fs : 32768;
		s32 cur = fs;
		for (uint i = 0; i + 10 < fsz;)
		{
			if (d[i] != 0xe8)
			{
				i++;
				cur++;
				continue;
			}
			const s32 a = (s32)cab_u (d + i + 1, 4);
			if (a >= -cur && a < (s32)intel_size)
			{
				const s32 r = a >= 0 ? a - cur : a + (s32)intel_size;
				d[i + 1] = r;
				d[i + 2] = r >> 8;
				d[i + 3] = r >> 16;
				d[i + 4] = r >> 24;
			}
			i += 5;
			cur += 5;
		}
	}
	return true;
}

static bool cab_inflate_folder (u8 *out, uint out_size, const u8 *raw, size_t raw_size,
	uint start, uint ndata, uint type, uint data_res)
{
	size_t p = start;
	uint o = 0;
	for (uint i = 0; i < ndata; i++)
	{
		if (p + 8 + data_res > raw_size)
			return false;
		const uint cb = cab_u (raw + p + 4, 2), ucb = cab_u (raw + p + 6, 2);
		p += 8 + data_res;
		if (p + cb > raw_size || (size_t)o + ucb > out_size)
			return false;
		if (type == 0)
		{
			if (cb != ucb)
				return false;
			memcpy (out + o, raw + p, cb);
		}
		else
		{
			if (cb < 2 || raw[p] != 'C' || raw[p + 1] != 'K')
				return false;
			z_stream z;
			memset (&z, 0, sizeof (z));
			if (inflateInit2 (&z, -15) != Z_OK)
				return false;
			if (o)
				inflateSetDictionary (&z, out + (o > 32768 ? o - 32768 : 0),
					o > 32768 ? 32768 : o);
			z.next_in = (Bytef *)(raw + p + 2);
			z.avail_in = cb - 2;
			z.next_out = out + o;
			z.avail_out = ucb;
			const int r = inflate (&z, Z_FINISH);
			const bool good = (r == Z_STREAM_END || r == Z_OK || r == Z_BUF_ERROR) && z.avail_out == 0;
			inflateEnd (&z);
			if (!good)
				return false;
		}
		o += ucb;
		p += cb;
	}
	return o == out_size;
}

static enumError extract_mscab_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size < 36 || memcmp (raw, "MSCF", 4) || raw[0x19] != 1)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint coff_files = cab_u (raw + 16, 4), nfold = cab_u (raw + 26, 2),
		nfiles = cab_u (raw + 28, 2), flags = cab_u (raw + 30, 2);
	size_t p = 36;
	uint fold_res = 0, data_res = 0;
	if (flags & 4)
	{
		if (p + 4 > raw_size)
			goto bad;
		const uint hr = cab_u (raw + p, 2);
		fold_res = raw[p + 2];
		data_res = raw[p + 3];
		p += 4 + hr;
	}
	for (uint f = flags & 3, i = 0; i < 2; i++)
		if (f & (1u << i))
			for (uint k = 0; k < 2; k++)
			{
				while (p < raw_size && raw[p])
					p++;
				p++;
			}
	if (!nfold || !nfiles || nfold > 4096 || nfiles > 100000 || p > raw_size
		|| coff_files >= raw_size)
		goto bad;

	u8 **fbuf = CALLOC (nfold, sizeof (*fbuf));
	uint *fsize = CALLOC (nfold, sizeof (*fsize));
	bool *fok = CALLOC (nfold, sizeof (*fok));
	const u8 *fent = raw + p;
	const size_t fstep = 8 + fold_res;
	if (!fbuf || !fsize || !fok || p + fstep * nfold > raw_size)
	{
		FREE (fbuf); FREE (fsize); FREE (fok);
		goto bad;
	}
	// size each folder from the files that live in it
	size_t q = coff_files;
	for (uint i = 0; i < nfiles; i++)
	{
		if (q + 16 > raw_size)
			break;
		const uint fl = cab_u (raw + q, 4) + cab_u (raw + q + 4, 4), fo = cab_u (raw + q + 8, 2);
		if (fo < nfold && fl > fsize[fo] && fl < (1u << 30))
			fsize[fo] = fl;
		q += 16;
		while (q < raw_size && raw[q])
			q++;
		q++;
	}
	// a folder's size must cover all CFDATA output, so sum those instead
	for (uint f = 0; f < nfold; f++)
	{
		const u8 *e = fent + fstep * f;
		const uint start = cab_u (e, 4), nd = cab_u (e + 4, 2), type = cab_u (e + 6, 2) & 15;
		if (type > 1 && type != 3 || start >= raw_size)
			continue;
		size_t s = start;
		uint total = 0;
		bool ok = true;
		for (uint i = 0; i < nd && ok; i++)
		{
			if (s + 8 + data_res > raw_size)
			{
				ok = false;
				break;
			}
			total += cab_u (raw + s + 6, 2);
			s += 8 + data_res + cab_u (raw + s + 4, 2);
		}
		if (!ok || total > (1u << 30))
			continue;
		fsize[f] = total;
		fbuf[f] = MALLOC (total ? total : 1);
		fok[f] = fbuf[f] && (type == 3
			? cab_lzx_folder (fbuf[f], total, raw, raw_size, start, nd, data_res, (cab_u (e + 6, 2) >> 8) & 31)
			: cab_inflate_folder (fbuf[f], total, raw, raw_size, start, nd, type, data_res));
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT MSCAB:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, nfiles, dest);

	enumError err = ERR_OK;
	uint n_files = 0;
	q = coff_files;
	for (uint i = 0; i < nfiles && !err; i++)
	{
		if (q + 16 > raw_size)
			break;
		const uint size = cab_u (raw + q, 4), off = cab_u (raw + q + 4, 4), fo = cab_u (raw + q + 8, 2);
		char name[PATH_MAX];
		size_t nl = 0;
		q += 16;
		while (q < raw_size && raw[q] && nl + 1 < sizeof (name))
		{
			const char c = raw[q++];
			name[nl++] = c == '\\' ? '/' : c;
		}
		name[nl] = 0;
		q++;
		const char *n = name;
		while (*n == '/')
			n++;
		if (!*n || strstr (n, "../") || !strcmp (n, "..") || fo >= nfold || !fok[fo]
			|| (size_t)off + size > fsize[fo])
			continue;
		if (!testmode)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", n);
			char *sl = strrchr (path, '/');
			if (sl)
			{
				*sl = 0;
				CreatePath (path, true);
				*sl = '/';
			}
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && size && fwrite (fbuf[fo] + off, 1, size, F.f) != size)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
			ResetFile (&F, opt_preserve);
		}
		n_files++;
	}
	for (uint f = 0; f < nfold; f++)
		FREE (fbuf[f]);
	FREE (fbuf); FREE (fsize); FREE (fok);
	FREE (raw);
	if (!err && !n_files)
		return ERR_NOTHING_TO_DO;
	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
bad:
	FREE (raw);
	return ERR_NOTHING_TO_DO;
}

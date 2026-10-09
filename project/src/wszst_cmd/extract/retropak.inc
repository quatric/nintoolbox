// Retro Studios "PAK" version 2 (Metroid Prime 3: Corruption, Donkey Kong Country Returns):
// {u32 2, u32 0x40, 0x38 bytes hash}, u32 section count + {tag, size} pairs, then 0x40-aligned
// STRG {u32 n; n * {cstr name, fourcc type, u64 id}}, RSHD {u32 n; n * {u32 compressed, fourcc, u64 id,
// u32 size, u32 offset}} and DATA. A compressed resource is "CMPD", u32 blocks, blocks * {u32
// (flags << 29 | compressed size), u32 size}; a block whose two sizes are equal is stored, a zlib stream
// (Donkey Kong Country Returns) is inflated, any other is a run of {u16 length, LZO1X data} pieces of up to
// 0x4000 bytes each. TXTR resources
// (u32 format, u16 w, u16 h, u32 mips, [palette], tiled GX data) become PNG in decode mode.

static long retro_lzo1x (const u8 *ip0, size_t n, u8 *op0, size_t cap)
{
 const u8*ip=ip0,*ie=ip0+n; u8*op=op0,*oe=op0+cap; size_t t; const u8*m;
#define NEED(k) if((size_t)(ie-ip)<(size_t)(k)) goto err
#define OUT(k) if((size_t)(oe-op)<(size_t)(k)) goto err
 NEED(1); t=*ip;
 if(t>17){ip++;t-=17; if(t<4) goto match_next; NEED(t);OUT(t);memcpy(op,ip,t);op+=t;ip+=t; goto first_lit;}
 for(;;){
  NEED(1); t=*ip++;
  if(t>=16) goto match;
  if(t==0){ for(;;){NEED(1); if(*ip) break; t+=255;ip++;} t+=15+*ip++; }
  t+=3; NEED(t);OUT(t);memcpy(op,ip,t);op+=t;ip+=t;
 first_lit:
  NEED(1); t=*ip++;
  if(t>=16) goto match;
  NEED(1); if((size_t)(op-op0)<(size_t)(1+0x800+(t>>2)+(*ip<<2))) goto err;
  m=op-(1+0x800)-(t>>2)-(*ip++<<2); OUT(3); op[0]=m[0];op[1]=m[1];op[2]=m[2];op+=3; goto match_done;
 match:
  for(;;){
  if(t>=64){ NEED(1); m=op-1-((t>>2)&7)-(*ip++<<3); t=(t>>5)-1+0; }
  else if(t>=32){ t&=31; if(!t){for(;;){NEED(1); if(*ip) break; t+=255;ip++;} t+=31+*ip++;} NEED(2); m=op-1-((ip[0]>>2)+(ip[1]<<6)); ip+=2; }
  else if(t>=16){ m=op-((t&8)<<11); t&=7; if(!t){for(;;){NEED(1); if(*ip) break; t+=255;ip++;} t+=7+*ip++;} NEED(2); m-=(ip[0]>>2)+(ip[1]<<6); ip+=2; if(m==op) goto done; m-=0x4000; }
  else { NEED(1); m=op-1-(t>>2)-(*ip++<<2); if(m<op0||m>=op) goto err; OUT(2); op[0]=m[0];op[1]=m[1];op+=2; goto match_done; }
  if(m<op0||m>=op) goto err;
  t+=2; OUT(t); while(t--) *op++=*m++;
  goto match_done;
  }
 match_done:
  NEED(0); t=ip[-2]&3; if(!t) continue;
 match_next:
  NEED(t+1);OUT(t);memcpy(op,ip,t);op+=t;ip+=t; t=*ip++; goto match;
 }
done: return op-op0;
err: return -1;
}

static bool retropak_cmpd (const u8 *p, size_t n, u8 **out, size_t *osz)
{
	if (n < 8 || memcmp (p, "CMPD", 4))
		return false;
	const uint nb = rd_be32 (p + 4);
	if (!nb || nb > 0x10000 || 8 + 8 * (size_t)nb > n)
		return false;
	size_t total = 0;
	for (uint i = 0; i < nb; i++)
		total += rd_be32 (p + 12 + 8 * (size_t)i);
	if (total > ((size_t)256 << 20))
		return false;
	u8 *o = MALLOC (total ? total : 1);
	if (!o)
		return false;
	size_t ip = 8 + 8 * (size_t)nb, op = 0;
	for (uint i = 0; i < nb; i++)
	{
		const u32 c = rd_be32 (p + 8 + 8 * (size_t)i), d = rd_be32 (p + 12 + 8 * (size_t)i);
		const size_t cs = c & 0x1fffffff;
		if (cs > n - ip)
			goto bad;
		if (cs == d)
			memcpy (o + op, p + ip, d);
		else if (cs >= 2 && p[ip] == 0x78 && ((p[ip] << 8 | p[ip + 1]) % 31) == 0)
		{
			// Donkey Kong Country Returns packs its blocks with zlib
			uLongf dl = d;
			if (uncompress (o + op, &dl, p + ip, cs) != Z_OK || dl != d)
				goto bad;
		}
		else
		{
			size_t q = 0, got = 0;
			while (q + 2 <= cs && got < d)
			{
				const size_t l = rd_be16 (p + ip + q);
				if (l > cs - q - 2)
					goto bad;
				const long r = retro_lzo1x (p + ip + q + 2, l, o + op + got, d - got);
				if (r < 0)
					goto bad;
				got += (size_t)r;
				q += 2 + l;
			}
			if (got != d)
				goto bad;
		}
		ip += cs;
		op += d;
	}
	*out = o;
	*osz = total;
	return true;
bad:
	FREE (o);
	return false;
}

static enumError extract_retropak_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pak"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint ns = sz > 0x44 ? rd_be32 (raw + 0x40) : 0;
	if (sz < 0x100 || rd_be32 (raw) != 2 || rd_be32 (raw + 4) != 0x40 || !ns || ns > 8 || 0x44 + 8 * (size_t)ns > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t so[8] = {0}, ss[8] = {0}, pos = (0x44 + 8 * (size_t)ns + 0x3f) & ~(size_t)0x3f;
	const u8 *strg = 0, *rshd = 0, *data = 0;
	size_t strg_n = 0, rshd_n = 0, data_n = 0;
	for (uint i = 0; i < ns; i++)
	{
		ss[i] = rd_be32 (raw + 0x48 + 8 * (size_t)i);
		so[i] = pos;
		if (pos > sz || ss[i] > sz - pos)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		const u8 *tag = raw + 0x44 + 8 * (size_t)i;
		if (!memcmp (tag, "STRG", 4))
			strg = raw + pos, strg_n = ss[i];
		else if (!memcmp (tag, "RSHD", 4))
			rshd = raw + pos, rshd_n = ss[i];
		else if (!memcmp (tag, "DATA", 4))
			data = raw + pos, data_n = ss[i];
		pos = (pos + ss[i] + 0x3f) & ~(size_t)0x3f;
	}
	const uint nres = rshd && rshd_n >= 4 ? rd_be32 (rshd) : 0;
	if (!data || !nres || 4 + 24 * (size_t)nres > rshd_n)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (nres, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	for (uint i = 0; i < nres; i++)
	{
		const u8 *e = rshd + 4 + 24 * (size_t)i;
		const u32 size = rd_be32 (e + 16), off = rd_be32 (e + 20);
		if (off > data_n || size > data_n - off)
			continue;
		char type[5] = {0}, name[PATH_MAX];
		for (int k = 0; k < 4; k++)
			type[k] = e[4 + k] >= 'A' && e[4 + k] <= 'Z' ? e[4 + k] + 32 : (e[4 + k] >= 32 && e[4 + k] < 127 ? e[4 + k] : '_');
		snprintf (name, sizeof (name), "%02x%02x%02x%02x%02x%02x%02x%02x.%s", e[8], e[9], e[10], e[11], e[12], e[13],
			e[14], e[15], type);
		if (strg && strg_n >= 4)
		{
			// STRG: names are cstrings followed by the type and id
			size_t q = 4;
			for (uint k = 0, cnt = rd_be32 (strg); k < cnt && q < strg_n; k++)
			{
				const char *s = (const char *)strg + q;
				const size_t l = strnlen (s, strg_n - q);
				if (q + l + 1 + 12 > strg_n)
					break;
				if (!memcmp (strg + q + l + 5, e + 8, 8))
				{
					snprintf (name, sizeof (name), "%.200s.%s", s, type);
					for (char *c = name; *c; c++)
						if (*c == '/' || *c == '\\')
							*c = '_';
					break;
				}
				q += l + 1 + 12;
			}
		}
		u8 *dec = 0;
		size_t dsz = 0;
		const u8 *src = data + off;
		size_t ssz = size;
		if (rd_be32 (e) && retropak_cmpd (src, size, &dec, &dsz))
			src = dec, ssz = dsz;
		bool dup = false;
		for (uint k = 0; k < n; k++)
			if (!strcmp (entries[k].name, name))
				dup = true;
		if (dup)
			snprintf (name + strlen (name), sizeof (name) - strlen (name), "_%u", i);
		const bool ok = OwnedEntryAdd (entries, n, name, src, (uint)ssz);
		FREE (dec);
		if (ok)
			n++;
	}
	FREE (raw);
	if (!n)
	{
		FREE (entries);
		return ERR_NOTHING_TO_DO;
	}
	return write_owned_entries (arg, basedir, depth, "RETRO-PAK", entries, n);
}

static enumError extract_retrotxtr_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".txtr") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint rf = sz >= 12 ? rd_be32 (raw) : 99, w = sz >= 12 ? rd_be16 (raw + 4) : 0, h = sz >= 12 ? rd_be16 (raw + 6) : 0;
	static const int gx[11] = {0, 1, 2, 3, 8, 9, 10, 4, 5, 6, 14};
	if (rf > 10 || !w || !h || w > 4096 || h > 4096 || gx[rf] == 10)
	{
		// recognizable header but a format without a decoder (C14X2): leave the file alone
		const bool known = rf <= 10 && w && h;
		FREE (raw);
		return known ? ERR_OK : ERR_NOTHING_TO_DO;
	}
	size_t off = 12;
	const u8 *pal = 0;
	uint pn = 0, pfmt = 0;
	if (rf == 4 || rf == 5)
	{
		pn = rf == 4 ? 16 : 256;
		pfmt = sz >= 20 ? rd_be32 (raw + 12) : 9;
		pal = raw + 20;
		off = 20 + 2 * (size_t)pn;
		if (pfmt > 2 || off > sz)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	}
	u8 *rgba = eashpg_decode (gx[rf], raw + off, sz - off, w, h, pal, pn, pfmt);
	FREE (raw);
	if (!rgba)
		return ERR_OK;
	char png[PATH_MAX];
	snprintf (png, sizeof (png), "%s.png", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE RETRO-TXTR:%s %ux%u -> PNG:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			arg, w, h, png);
	if (testmode)
	{
		FREE (rgba);
		return ERR_OK;
	}
	return SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png, 0, true);
}

// Retro "CSMP" sound sample (Metroid Prime 3): "CSMP", u32 1, then {tag, u32 size} chunks
// INFO, PAD and DATA; DATA holds a 0x60-byte DSP-like header (loop points in samples, not nibbles)
// followed by the ADPCM data. Written as a standard mono ".dsp".
static enumError extract_csmp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".csmp"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t p = 8, c = 0, cn = 0;
	if (sz < 0x80 || memcmp (raw, "CSMP", 4) || rd_be32 (raw + 4) != 1)
		p = sz;
	while (p + 8 <= sz)
	{
		const size_t l = rd_be32 (raw + p + 4);
		if (!memcmp (raw + p, "DATA", 4))
		{
			c = p + 8;
			cn = l < sz - c ? l : sz - c;
			break;
		}
		p += 8 + l;
	}
	const u32 samples = c ? rd_be32 (raw + c) : 0;
	const size_t bytes = ((size_t)samples + 13) / 14 * 8;
	if (!c || cn < 0x60 || !samples || bytes > cn - 0x60)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	u8 *out = CALLOC (1, 0x60 + bytes);
	if (!out)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	memcpy (out, raw + c, 0x60);
	memcpy (out + 0x60, raw + c + 0x60, bytes);
#define NIB(s) ((s) / 14 * 16 + 2 + (s) % 14)
	wr_be32 (out + 4, (u32)((samples + 13) / 14 * 16));
	wr_be32 (out + 0x10, NIB (rd_be32 (raw + c + 0x10)));
	wr_be32 (out + 0x14, NIB (rd_be32 (raw + c + 0x14)));
	wr_be32 (out + 0x18, 2);
#undef NIB
	FREE (raw);
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	char name[PATH_MAX];
	snprintf (name, sizeof (name), "%s", base);
	char *dot = strrchr (name, '.');
	if (dot)
		*dot = 0;
	snprintf (name + strlen (name), sizeof (name) - strlen (name), ".dsp");
	nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
	const bool ok = entries && OwnedEntryAdd (entries, 0, name, out, (uint)(0x60 + bytes));
	FREE (out);
	if (!ok)
	{
		FREE (entries);
		return ERR_OUT_OF_MEMORY;
	}
	return write_owned_entries (arg, basedir, depth, "CSMP", entries, 1);
}

// Retro "STRG" string table, version 3 (Metroid Prime 3): 0x87654321, u32 3, u32 languages, u32
// strings, u32 names, u32 name table size, names * {u32 name offset (from 0x18), u32 string index} + name
// cstrings, languages * fourcc, then per language {u32 size, strings * u32 offset} and a shared
// blob of {u32 length, UTF-8 text + NUL} records. One "<LANG>.txt" per language ("index<TAB>name<TAB>text").
static enumError extract_retrostrg_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".strg"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint nl = sz >= 0x18 ? rd_be32 (raw + 8) : 0, ns = sz >= 0x18 ? rd_be32 (raw + 12) : 0;
	const uint nn = sz >= 0x18 ? rd_be32 (raw + 16) : 0;
	const size_t nts = sz >= 0x18 ? rd_be32 (raw + 20) : 0;
	if (sz < 0x18 || rd_be32 (raw) != 0x87654321 || rd_be32 (raw + 4) != 3 || !nl || nl > 16 || !ns || ns > 0x10000
		|| nn > 0x10000 || 8 * (size_t)nn > nts || 0x18 + nts + 4 * (size_t)nl > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t langs = 0x18 + nts, tabs = langs + 4 * (size_t)nl;
	const size_t blob = tabs + (4 + 4 * (size_t)ns) * nl;
	if (blob > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (nl, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	for (uint l = 0; l < nl; l++)
	{
		const u8 *t = raw + tabs + (4 + 4 * (size_t)ns) * l + 4;
		size_t cap = 4096, o = 0;
		char *txt = MALLOC (cap);
		if (!txt)
			break;
		for (uint i = 0; i < ns; i++)
		{
			const size_t off = rd_be32 (t + 4 * (size_t)i);
			if (blob + off + 4 > sz)
				continue;
			const size_t len = rd_be32 (raw + blob + off);
			if (!len || len > sz - blob - off - 4)
				continue;
			const char *nm = "";
			for (uint k = 0; k < nn; k++)
				if (rd_be32 (raw + 0x18 + 8 * (size_t)k + 4) == i && 0x18 + rd_be32 (raw + 0x18 + 8 * (size_t)k) < sz)
				{
					nm = (const char *)raw + 0x18 + rd_be32 (raw + 0x18 + 8 * (size_t)k);
					break;
				}
			if (o + len * 2 + strnlen (nm, 256) + 32 > cap)
			{
				cap = (o + len * 2 + 300) * 2;
				char *nt = REALLOC (txt, cap);
				if (!nt)
					break;
				txt = nt;
			}
			o += snprintf (txt + o, cap - o, "%u\t%.255s\t", i, nm);
			for (size_t k = 0; k + 1 < len; k++)
			{
				const char c = (char)raw[blob + off + 4 + k];
				if (c == '\n')
					txt[o++] = '\\', txt[o++] = 'n';
				else if (c == '\t')
					txt[o++] = '\\', txt[o++] = 't';
				else if (c != '\r')
					txt[o++] = c;
			}
			txt[o++] = '\n';
		}
		char name[16];
		snprintf (name, sizeof (name), "%.4s.txt", raw + langs + 4 * (size_t)l);
		for (char *c = name; *c; c++)
			if (*c == '/')
				*c = '_';
		if (OwnedEntryAdd (entries, n, name, (const u8 *)txt, (uint)o))
			n++;
		FREE (txt);
	}
	FREE (raw);
	if (!n)
	{
		FREE (entries);
		return ERR_NOTHING_TO_DO;
	}
	return write_owned_entries (arg, basedir, depth, "RETRO-STRG", entries, n);
}

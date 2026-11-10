// ".wis" block-compressed stream (Cabela's Big Game Hunter 2010 / Dangerous Hunts, Cauldron engine; startup.wis, Maps/*/*.wis, actors, weapons):
// no magic, little endian; a chain of blocks {u32 size (bit 31 = stored raw), payload}, each normally decoding to 0x7ffc bytes, ended by a
// 0xffffffff word and 0xff padding to the sector. Compressed blocks are Okumura LZSS (4096 byte ring pre-zeroed per block, write position 0xfee, flag
// bytes LSB first with 1 = literal, match {b0, b1}: offset = b0 | (b1 & 0xf0) << 4 (ring position), length = (b1 & 15) + 3). The concatenated
// payload is written as "<stem>.bin": maps / actors decode to "<Cauldron Chunk File>" containers, startup.wis to a GUI archive (BRLAN / TPL members
// behind a big endian index); those inner containers are not split yet, except that the Lua 5.1 bytecode scripts embedded in Cauldron chunk files
// ({NUL-terminated name "[Actor - 11]" / "WorldScript", u32 size, 1b 4c 75 61 51 ... chunk}, sizes verified against a full undump) are also written as
// "lua/NNN_<name>.luac". A "startup.wis"-style GUI archive payload (zero u32, BE u32 0x20, data start @0x14, 12-byte records from 0x34: dirs {0x01000000 |
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

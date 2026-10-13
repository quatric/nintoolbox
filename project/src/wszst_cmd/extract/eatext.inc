// EA Montreal text containers (Boogie SuperStar, Wii): ".loc" string tables and ".sub" subtitle tracks.
//
// ".loc": little-endian chunks "LOCH" (header, size @4), "LOCI" {size, count, count {u16 id, u16 index}} and
// "LOCL" {size, u32, count u32 offsets relative to LOCL, UTF-16LE NUL-terminated strings}. Written
// to "<file>.d/strings.txt" as "id<TAB>text" (id -1 for strings no id refers to; newlines / tabs escaped).
//
// ".sub": "ST00" records {u32 size of the whole record, u32 start, u32 end, u32 width @+0xc, ... "CC\0\0" and a UTF-16LE
// caption}. Written to "<file>.d/subtitles.txt" as "start<TAB>end<TAB>text".
static void eatext_put16 (char *out, size_t *len, const u8 *s, size_t n)
{
	for (size_t i = 0; i + 1 < n; i += 2)
	{
		uint c = s[i] | s[i + 1] << 8;
		if (!c)
			break;
		if (c >= 0xd800 && c < 0xdc00 && i + 3 < n)
		{
			const uint d = s[i + 2] | s[i + 3] << 8;
			if (d >= 0xdc00 && d < 0xe000)
			{
				c = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00);
				i += 2;
			}
		}
		if (c == '\n' || c == '\t' || c == '\\' || c == '\r')
		{
			out[(*len)++] = '\\';
			out[(*len)++] = c == '\n' ? 'n' : c == '\t' ? 't' : c == '\r' ? 'r' : '\\';
		}
		else if (c < 0x80)
			out[(*len)++] = (char)c;
		else if (c < 0x800)
		{
			out[(*len)++] = (char)(0xc0 | c >> 6);
			out[(*len)++] = (char)(0x80 | (c & 63));
		}
		else if (c < 0x10000)
		{
			out[(*len)++] = (char)(0xe0 | c >> 12);
			out[(*len)++] = (char)(0x80 | (c >> 6 & 63));
			out[(*len)++] = (char)(0x80 | (c & 63));
		}
		else
		{
			out[(*len)++] = (char)(0xf0 | c >> 18);
			out[(*len)++] = (char)(0x80 | (c >> 12 & 63));
			out[(*len)++] = (char)(0x80 | (c >> 6 & 63));
			out[(*len)++] = (char)(0x80 | (c & 63));
		}
	}
}

static enumError extract_eatext_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if ((!is_ext (arg, ".loc") && !is_ext (arg, ".sub")) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char *out = MALLOC (sz * 4 + 4096);
	if (!out)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	size_t len = 0;
	ccp fn = 0;
	uint n_items = 0;
	if (is_ext (arg, ".loc") && sz > 0x20 && !memcmp (raw, "LOCH", 4))
	{
		fn = "strings.txt";
		size_t p = get_le32 (raw + 4), ci = 0, cl = 0;
		for (; p + 8 <= sz;)
		{
			const size_t csz = get_le32 (raw + p + 4);
			if (!memcmp (raw + p, "LOCI", 4))
				ci = p;
			else if (!memcmp (raw + p, "LOCL", 4))
				cl = p;
			if (csz < 8 || csz > sz - p)
				break;
			p += csz;
		}
		if (cl && cl + 16 <= sz)
		{
			const uint count = get_le32 (raw + cl + 12);
			const size_t base = cl, tab = cl + 16;
			uint nid = ci ? (get_le32 (raw + ci + 4) - 12) / 4 : 0; // the count field is one short
			if (count && count < 0x100000 && tab + 4 * (size_t)count <= sz)
				for (uint i = 0; i < count; i++)
				{
					size_t a = base + get_le32 (raw + tab + 4 * i), b = i + 1 < count ? base + get_le32 (raw + tab + 4 * i + 4) : cl + get_le32 (raw + cl + 4);
					if (b > sz)
						b = sz;
					if (a > sz || b > sz || b < a)
						continue;
					int id = -1;
					for (uint k = 0; k < nid && ci + 12 + 4 * (size_t)k + 4 <= sz; k++)
						if (rd_le16 (raw + ci + 12 + 4 * k + 2) == i && (id < 0 || !i))
							id = (int)rd_le16 (raw + ci + 12 + 4 * k);
					len += (size_t)snprintf (out + len, 32, "%d\t", id);
					eatext_put16 (out, &len, raw + a, b - a);
					out[len++] = '\n';
					n_items++;
				}
		}
	}
	else if (is_ext (arg, ".sub") && sz > 8 && !memcmp (raw, "ST00", 4))
	{
		fn = "subtitles.txt";
		for (size_t p = 0; p + 8 <= sz && !memcmp (raw + p, "ST00", 4);)
		{
			const size_t rs = get_le32 (raw + p + 4) - 8; // the size counts the whole record
			if (rs > sz - p - 8 || rs < 8 || get_le32 (raw + p + 4) < 8)
				break;
			const u8 *r = raw + p + 8;
			size_t t = 0;
			for (; t + 4 <= rs; t++)
				if (!memcmp (r + t, "CC\0\0", 4))
					break;
			if (t + 4 < rs)
			{
				len += (size_t)snprintf (out + len, 64, "%u\t%u\t", get_le32 (r), get_le32 (r + 4));
				eatext_put16 (out, &len, r + t + 4, rs - t - 4);
				out[len++] = '\n';
				n_items++;
			}
			p += 8 + rs;
		}
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (fn && n_items)
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT EA TEXT:%s (%u items) -> %s/%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, n_items, dest, fn);
		err = ERR_OK;
		if (!testmode)
			err = shade_write (arg, dest, basedir, fn, out, (uint)len);
	}
	FREE (out);
	FREE (raw);
	return err;
}

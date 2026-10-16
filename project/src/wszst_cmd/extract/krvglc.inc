// Legend of Spyro: Dawn of the Dragon (Wii) language ".KRV" tables (NNGLC.KRV): little endian, {u32 5, u32 12, ..}
// followed by id tables and runs of length-prefixed strings {u32 chars including the terminator, UTF-16LE text, 0}.
// The runs are found by scanning and written as "strings.txt" (one "index: text" per line, newlines escaped).
static bool krvglc_valid (const u8 *d, size_t sz, size_t o)
{
	if (o + 4 > sz)
		return false;
	const u32 len = rd_le32 (d + o);
	if (len < 1 || len > 4000 || o + 4 + 2 * (size_t)len > sz || rd_le16 (d + o + 2 * len + 2))
		return false;
	for (u32 i = 0; i + 1 < len; i++)
	{
		const uint c = rd_le16 (d + o + 4 + 2 * i);
		if (c < 0x20 && c != '\n' && c != '\r' && c != '\t')
			return false;
	}
	return true;
}

static size_t krvglc_chain (const u8 *d, size_t sz, size_t o, uint *count, u8 **out, size_t *olen, size_t *ocap)
{
	uint n = 0;
	for (;;)
	{
		// strings may be separated by a u32 id
		if (!krvglc_valid (d, sz, o) && n && krvglc_valid (d, sz, o + 4))
			o += 4;
		if (!krvglc_valid (d, sz, o))
			break;
		const u32 len = rd_le32 (d + o);
		// UTF-8 encode (BMP only, surrogates dropped) with escaped newlines
		char line[16 * 1024];
		size_t ll = (size_t)snprintf (line, sizeof (line), "%u: ", *count + n);
		for (u32 i = 0; i + 1 < len && ll + 8 < sizeof (line); i++)
		{
			const uint c = rd_le16 (d + o + 4 + 2 * i);
			if (c == '\n')
				ll += (size_t)snprintf (line + ll, 3, "\\n");
			else if (c == '\r')
				ll += (size_t)snprintf (line + ll, 3, "\\r");
			else if (c == '\t')
				ll += (size_t)snprintf (line + ll, 3, "\\t");
			else if (c < 0x80)
				line[ll++] = (char)c;
			else if (c < 0x800)
			{
				line[ll++] = (char)(0xc0 | (c >> 6));
				line[ll++] = (char)(0x80 | (c & 63));
			}
			else if (c < 0xd800 || c >= 0xe000)
			{
				line[ll++] = (char)(0xe0 | (c >> 12));
				line[ll++] = (char)(0x80 | ((c >> 6) & 63));
				line[ll++] = (char)(0x80 | (c & 63));
			}
		}
		line[ll++] = '\n';
		if (*olen + ll > *ocap)
		{
			u8 *np = REALLOC (*out, (*ocap = (*olen + ll) * 2));
			if (!np)
				return 0;
			*out = np;
		}
		memcpy (*out + *olen, line, ll);
		*olen += ll;
		n++;
		o += 4 + 2 * (size_t)len;
	}
	*count += n;
	return n >= 3 ? o : 0;
}

static enumError extract_krvglc_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".krv"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x80 || rd_le32 (raw) != 5 || rd_le32 (raw + 4) != 12)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	u8 *out = 0;
	size_t olen = 0, ocap = 0;
	uint count = 0;
	for (size_t o = 0x40; o + 8 <= sz;)
	{
		const uint c0 = count;
		const size_t l0 = olen;
		const size_t e = krvglc_chain (raw, sz, o, &count, &out, &olen, &ocap);
		if (e)
			o = e;
		else
		{
			// drop what a rejected run (fewer than 3 strings) appended
			count = c0;
			olen = l0;
			o += 4;
		}
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (out && count)
	{
		nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
		if (entries && OwnedEntryAdd (entries, 0, "strings.txt", out, olen))
			err = write_owned_entries (arg, basedir, depth, "KRV-GLC", entries, 1);
		else
		{
			FREE (entries);
			err = ERR_OUT_OF_MEMORY;
		}
	}
	FREE (out);
	FREE (raw);
	return err;
}

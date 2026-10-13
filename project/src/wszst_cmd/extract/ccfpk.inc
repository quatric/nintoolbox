// CyberConnect2-style "fpack" .fpk archives (Naruto Shippuden: Clash of Ninja Revolution III, Tatsunoko vs.
// Capcom; not the Traveller's Tales .fpk). Big-endian: u32 id, u32 entry count, u32 table offset (0x10), u32 file
// size, then 0x30-byte entries {char name[0x24], u32 offset, u32 stored size, u32 unpacked size} (names are
// relative paths). A member whose stored and unpacked sizes are equal is stored as is; otherwise it is Sega PRS
// with the bits read most significant first: flag 1 = literal; 0 then 1 = long copy (big-endian u16, offset in
// the top 13 bits minus 0x2000, length in the low 3 bits plus 2, or a length byte plus 1 when those are 0 and 0
// ends the stream); 0 then 0 = short copy (two length bits plus 2, one offset byte minus 0x100).
static bool ccfpk_unprs (const u8 *src, size_t sn, u8 *dst, size_t dn)
{
	size_t p = 0, o = 0;
	uint ctl = 0, bits = 0;
#define CC_BIT(v)                                                                                                  \
	do                                                                                                         \
	{                                                                                                          \
		if (!bits)                                                                                         \
		{                                                                                                  \
			if (p >= sn)                                                                               \
				return false;                                                                      \
			ctl = src[p++];                                                                            \
			bits = 8;                                                                                  \
		}                                                                                                  \
		(v) = (ctl >> 7) & 1;                                                                              \
		ctl = (ctl << 1) & 0xff;                                                                           \
		bits--;                                                                                            \
	} while (0)
	for (;;)
	{
		uint b;
		CC_BIT (b);
		if (b)
		{
			if (p >= sn || o >= dn)
				return false;
			dst[o++] = src[p++];
			continue;
		}
		size_t n;
		int off;
		CC_BIT (b);
		if (b)
		{
			if (p + 2 > sn)
				return false;
			const uint a = src[p] << 8 | src[p + 1];
			p += 2;
			off = (int)(a >> 3) - 0x2000;
			n = a & 7;
			if (!n)
			{
				if (p >= sn)
					return false;
				n = src[p++];
				if (!n)
					break;
				n++;
			}
			else
				n += 2;
		}
		else
		{
			uint b1, b2;
			CC_BIT (b1);
			CC_BIT (b2);
			n = (size_t)(b1 << 1 | b2) + 2;
			if (p >= sn)
				return false;
			off = (int)src[p++] - 256;
		}
		if (off >= 0 || (size_t)-off > o || n > dn - o)
			return false;
		for (; n; n--, o++)
			dst[o] = dst[o + off];
	}
#undef CC_BIT
	return o == dn;
}

static enumError extract_ccfpk_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".fpk"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 0x10 ? rd_be32 (raw + 4) : 0;
	if (!count || count > 65536 || rd_be32 (raw + 8) != 0x10 || rd_be32 (raw + 12) != sz
		|| 0x10 + 0x30 * (size_t)count > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (count, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	bool ok = true;
	for (uint i = 0; i < count && ok; i++)
	{
		const u8 *e = raw + 0x10 + 0x30 * i;
		const size_t off = rd_be32 (e + 0x24), cs = rd_be32 (e + 0x28), us = rd_be32 (e + 0x2c);
		if (off > sz || cs > sz - off || us > 0x20000000)
		{
			ok = false;
			break;
		}
		char name[0x25];
		memcpy (name, e, 0x24);
		name[0x24] = 0;
		if (!valid_sarc_path (name))
			snprintf (name, sizeof (name), "file%05u", i);
		u8 *buf = 0;
		const u8 *data = raw + off;
		size_t dsz = cs;
		if (cs != us)
		{
			buf = MALLOC (us ? us : 1);
			if (!buf || !ccfpk_unprs (raw + off, cs, buf, us))
			{
				FREE (buf);
				ok = false;
				break;
			}
			data = buf;
			dsz = us;
		}
		ok = OwnedEntryAdd (entries, n, name, data, (uint)dsz);
		FREE (buf);
		if (ok)
			n++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (ok && n)
		err = write_owned_entries (arg, basedir, depth, "FPK", entries, n);
	else
		ResetOwnedEntries (entries, n);
	FREE (raw);
	return err;
}

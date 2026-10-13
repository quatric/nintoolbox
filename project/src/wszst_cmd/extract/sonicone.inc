// Sonic Team ".one" archives (Sonic and the Black Knight, Wii). Big-endian: u32 entry count, u32 table offset
// (0x10), u32 header size (first data offset), u32 0xffffffff, then 0x30-byte entries {char name[0x20],
// u32 index, u32 offset, u32 packed size, u32 unpacked size}. Members are Sega PRS compressed (control bytes
// read LSB first, 1 = literal; 0 1 = long copy with a 13-bit offset and 3-bit / extra-byte length; 0 0 =
// short copy with a 2-bit length and one offset byte) and written as "<name>" in "<file>.d/".
static bool sonicone_unprs (const u8 *src, size_t sn, u8 *dst, size_t dn)
{
	size_t p = 0, o = 0;
	uint ctl = 0, bits = 0;
#define S1_BIT(v)                                                                                                  \
	do                                                                                                         \
	{                                                                                                          \
		if (!bits)                                                                                         \
		{                                                                                                  \
			if (p >= sn)                                                                               \
				return false;                                                                      \
			ctl = src[p++];                                                                            \
			bits = 8;                                                                                  \
		}                                                                                                  \
		(v) = ctl & 1;                                                                                     \
		ctl >>= 1;                                                                                         \
		bits--;                                                                                            \
	} while (0)
	while (o < dn)
	{
		uint b;
		S1_BIT (b);
		if (b)
		{
			if (p >= sn)
				return false;
			dst[o++] = src[p++];
			continue;
		}
		size_t n;
		int off;
		S1_BIT (b);
		if (b)
		{
			if (p + 2 > sn)
				return false;
			const uint a = src[p] | (src[p + 1] << 8);
			p += 2;
			if (!a)
				break;
			n = a & 7;
			off = (int)(a >> 3) - 0x2000;
			if (!n)
			{
				if (p >= sn)
					return false;
				n = (size_t)src[p++] + 1;
			}
			else
				n += 2;
		}
		else
		{
			uint b1, b2;
			S1_BIT (b1);
			S1_BIT (b2);
			n = (size_t)(b1 << 1 | b2) + 2;
			if (p >= sn)
				return false;
			off = (int)src[p++] - 256;
		}
		if (off >= 0 || (size_t)-off > o)
			return false;
		for (; n && o < dn; n--, o++)
			dst[o] = dst[o + off];
	}
#undef S1_BIT
	return o == dn;
}

static enumError extract_sonicone_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".one"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 0x40 ? rd_be32 (raw) : 0;
	if (!count || count > 4096 || rd_be32 (raw + 4) != 0x10 || rd_be32 (raw + 12) != 0xffffffff
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
		if (off > sz || cs > sz - off || us > 0x10000000)
		{
			ok = false;
			break;
		}
		char name[0x21];
		memcpy (name, e, 0x20);
		name[0x20] = 0;
		for (char *p = name; *p; p++)
			if (*p == '/' || *p == '\\' || (u8)*p < 0x20)
				*p = '_';
		if (!*name)
			snprintf (name, sizeof (name), "file%04u", i);
		for (uint dup = 1, k = 0; k < n; k++)
			if (!strcmp (entries[k].name, name))
			{
				char t[0x40];
				snprintf (t, sizeof (t), "%s-%u", name, dup++);
				snprintf (name, sizeof (name), "%s", t);
				k = (uint)-1;
			}
		u8 *buf = MALLOC (us ? us : 1);
		if (!buf)
		{
			ok = false;
			break;
		}
		if (!sonicone_unprs (raw + off, cs, buf, us))
		{
			FREE (buf);
			ok = false;
			break;
		}
		ok = OwnedEntryAdd (entries, n, name, buf, (uint)us);
		FREE (buf);
		if (ok)
			n++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (ok && n)
		err = write_owned_entries (arg, basedir, depth, "ONE", entries, n);
	else
		ResetOwnedEntries (entries, n);
	FREE (raw);
	return err;
}

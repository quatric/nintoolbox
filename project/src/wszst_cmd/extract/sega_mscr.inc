// SEGA "MSCR" script binaries (Sakura Wars: So Long, My Love "*.msX", little endian). {"MSCR", u32 payload size, u32 header size 0x20 (0x10 in two files, without the hash), u32 flags (0x400 = compressed), 16-byte hash}, then for the
// compressed form u32 unpacked size, u32 0x8000, 8 x 0xff and the stream at header size + 0x10. The stream (found in the game's decoder, "Fail Melt") is a byte LZ with every byte and flag bit inverted:
// flags are read LSB first from an inverted byte; flag 1 = literal; else flag 1 = long match {b0, b1, (b2)} (len = b0 & 7, 0 = b2 + 1 else + 2; offset = b1 * 0x20 + (b0 >> 3) - 0x2000, both 0 ends),
// flag 0 = short match (2 flag bits = length - 2, one byte offset - 0x100). The window is 8 KiB. Written as the plain ".msb" script binary (header with flags 0, then the unpacked body).
static enumError extract_sega_mscr_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".msX"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x24 || memcmp (c, "MSCR", 4) || (rd_le32 (c + 8) != 0x20 && rd_le32 (c + 8) != 0x10) || !(rd_le32 (c + 0xc) & 0x400))
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	const size_t hs = rd_le32 (c + 8), cap = hs + 0x4000000;
	u8 *o = MALLOC (cap);
	memcpy (o, c, hs);
	memset (o + 0xc, 0, 4);
	static u8 win[0x2000];
	memset (win, 0, sizeof (win));
	size_t p = hs + 0x10, n = 0;
	uint cnt = 1, fl = 0;
	bool ok = false, bad = false;
#define MSCR_BIT(r)                                  \
	do                                               \
	{                                                \
		if (!--cnt)                                  \
		{                                            \
			if (p >= cs)                             \
			{                                        \
				bad = true;                          \
				break;                               \
			}                                        \
			fl = (~c[p++]) & 0xff;                   \
			cnt = 8;                                 \
		}                                            \
		r = fl & 1;                                  \
		fl >>= 1;                                    \
	} while (0)
#define MSCR_PUT(v)                                  \
	do                                               \
	{                                                \
		if (hs + n >= cap)                         \
		{                                            \
			bad = true;                              \
			break;                                   \
		}                                            \
		const u8 vv = (v);                           \
		win[n & 0x1fff] = vv;                        \
		o[hs + n++] = vv;                          \
	} while (0)
	while (!bad)
	{
		uint f = 0;
		MSCR_BIT (f);
		if (bad)
			break;
		if (f)
		{
			if (p >= cs)
				break;
			MSCR_PUT ((~c[p++]) & 0xff);
			continue;
		}
		MSCR_BIT (f);
		if (bad)
			break;
		uint len;
		long off;
		if (!f)
		{
			uint a = 0, b = 0;
			MSCR_BIT (a);
			MSCR_BIT (b);
			if (bad || p >= cs)
				break;
			len = b + a * 2 + 2;
			off = (long)((~c[p++]) & 0xff) - 0x100;
		}
		else
		{
			if (p + 2 > cs)
				break;
			const uint b0 = (~c[p]) & 0xff, b1 = (~c[p + 1]) & 0xff;
			p += 2;
			if (!b0 && !b1)
			{
				ok = true;
				break;
			}
			len = b0 & 7;
			off = (long)b1 * 0x20 + (b0 >> 3) - 0x2000;
			if (!len)
			{
				if (p >= cs)
					break;
				len = ((~c[p++]) & 0xff) + 1;
			}
			else
				len += 2;
		}
		long s = off + (long)n;
		for (uint k = 0; k < len && !bad; k++, s++)
			MSCR_PUT (win[s & 0x1fff]);
	}
#undef MSCR_BIT
#undef MSCR_PUT
	enumError err = ERR_NOTHING_TO_DO;
	if (ok && n)
	{
		const size_t total = hs + n;
		wr_le32 (o + 4, (u32)(total - hs - 0x10 > total ? 0 : total - hs - 0x10));
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".msb");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SEGA-MSCR:%s (%zu bytes) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest, name);
		err = shade_write (arg, dest, basedir, name, o, (uint)total);
	}
	FREE (o);
	FREE (c);
	return err;
}

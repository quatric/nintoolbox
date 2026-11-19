// The "Melt" decompressor of the SEGA/War Drum engine (Sakura Wars: So Long, My Love; found in the game's chunk loader): an LZ with every byte and every flag bit inverted. Flags are read LSB first from an
// inverted byte; flag 1 = literal; else flag 1 = long match {b0, b1, (b2)} (len = b0 & 7, 0 = b2 + 1 else + 2; offset = b1 * 0x20 + (b0 >> 3) - 0x2000, both bytes 0 ends the stream), flag 0 = short
// match (2 flag bits = length - 2, one byte, offset - 0x100). 8 KiB window, initially zero. Returns true when the end marker was reached; *nout is the number of bytes written to o (at most cap).
static bool sega_melt (const u8 *c, size_t cs, size_t p, u8 *o, size_t cap, size_t *nout)
{
	static u8 win[0x2000];
	memset (win, 0, sizeof (win));
	size_t n = 0;
	uint cnt = 1, fl = 0;
	bool ok = false, bad = false;
#define MELT_BIT(r)                                  \
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
#define MELT_PUT(v)                                  \
	do                                               \
	{                                                \
		if (n >= cap)                                \
		{                                            \
			bad = true;                              \
			break;                                   \
		}                                            \
		const u8 vv = (v);                           \
		win[n & 0x1fff] = vv;                        \
		o[n++] = vv;                                 \
	} while (0)
	while (!bad)
	{
		uint f = 0;
		MELT_BIT (f);
		if (bad)
			break;
		if (f)
		{
			if (p >= cs)
				break;
			MELT_PUT ((~c[p++]) & 0xff);
			continue;
		}
		MELT_BIT (f);
		if (bad)
			break;
		uint len;
		long off;
		if (!f)
		{
			uint a = 0, b = 0;
			MELT_BIT (a);
			MELT_BIT (b);
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
			MELT_PUT (win[s & 0x1fff]);
	}
#undef MELT_BIT
#undef MELT_PUT
	*nout = n;
	return ok;
}

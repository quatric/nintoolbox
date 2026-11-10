// "GVX0" event script (".pvx": Power Punch, Atari's Wii boxing game; voice/camera/effect/entrance timelines), little endian:
// {"GVX0", u32 version (0x116), u32 flag, u32 0x28, u32 table A start (0x30), B start, C start, D start, u32 file size x2 (the second
// presumably a payload size), then at 0x28 {u32 node count, u32 1}. A = count 8-byte nodes {u32 flags, u32 index}; B = 24-byte records
// {u16 kind, u16 flags, u32 arg x5}; C = 16-byte records; D = 4-byte-aligned parameter blocks separated by 0xfffffffe (floats, -1
// "unset" words and NUL-terminated strings: sound bank path, cue names). The record semantics are not decoded; "gvx.txt" lists every
// section as words (floats annotated) and "strings.txt" the embedded strings.
static enumError extract_gvx_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x30 || memcmp (raw, "GVX0", 4) || rd_le32 (raw + 4) != 0x116 || rd_le32 (raw + 12) != 0x28 || rd_le32 (raw + 16) != 0x30)
		goto done;
	{
		const uint a = rd_le32 (raw + 16), b = rd_le32 (raw + 20), c = rd_le32 (raw + 24), d = rd_le32 (raw + 28), n = rd_le32 (raw + 0x28);
		if (rd_le32 (raw + 32) != sz || a + 8 * (u64)n != b || b > c || c > d || d > sz || ((b | c | d) & 3))
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT GVX0:%s (%u nodes) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		if (!testmode)
		{
			const uint cap = 128 + (uint)sz * 24;
			char *t = MALLOC (cap), *s = MALLOC (sz * 2 + 16);
			uint tl = 0, sl = 0;
			tl += snprintf (t + tl, cap - tl, "GVX0 version 0x%x flag %u nodes %u size %zu\n", rd_le32 (raw + 4), rd_le32 (raw + 8), n, sz);
			static const struct { ccp name; uint step; } sec[3] = { { "A", 8 }, { "B", 24 }, { "C", 16 } };
			const uint start[4] = { a, b, c, d };
			for (uint k = 0; k < 4; k++)
			{
				const uint end = k < 3 ? start[k + 1] : (uint)sz, step = k < 3 ? sec[k].step : 4;
				tl += snprintf (t + tl, cap - tl, "\n[%s] 0x%x..0x%x\n", k < 3 ? sec[k].name : "D", start[k], end);
				for (uint o = start[k]; o < end; o += step)
				{
					tl += snprintf (t + tl, cap - tl, "%05x", o);
					for (uint w = 0; w < step && o + w + 4 <= end; w += 4)
					{
						const u32 v = rd_le32 (raw + o + w);
						const uint ex = (v >> 23) & 0xff;
						if (v == 0xffffffff)
							tl += snprintf (t + tl, cap - tl, " -1");
						else if (v == 0xfffffffe)
							tl += snprintf (t + tl, cap - tl, " END");
						else if (k == 3 && ex >= 0x70 && ex <= 0x8f && (v & 0x7fffff))
						{
							float f;
							memcpy (&f, &v, 4);
							tl += snprintf (t + tl, cap - tl, " %g", f);
						}
						else
							tl += snprintf (t + tl, cap - tl, " %x", v);
					}
					tl += snprintf (t + tl, cap - tl, "\n");
				}
			}
			for (uint o = d; o < sz;)
			{
				uint e = o;
				while (e < sz && raw[e] >= 0x20 && raw[e] < 0x7f)
					e++;
				if (e - o >= 8 && e < sz && !raw[e])
				{
					sl += snprintf (s + sl, sz * 2 + 16 - sl, "0x%x %s\n", o, (ccp)raw + o);
					o = e + 1;
				}
				else
					o = e + 1;
			}
			err = shade_write (arg, dest, basedir, "gvx.txt", (const u8 *)t, tl);
			if (!err && sl)
				err = shade_write (arg, dest, basedir, "strings.txt", (const u8 *)s, sl);
			FREE (t);
			FREE (s);
		}
	}
done:
	FREE (raw);
	return err;
}

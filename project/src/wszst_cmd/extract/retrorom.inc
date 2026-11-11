// Nintendo cartridge ROM images: NES (iNES ".nes"), Game Boy / Color (".gb", ".gbc"), Game Boy Advance (".gba"), Super Nintendo (".sfc", ".smc") and
// Nintendo 64 (".z64" / ".v64" / ".n64"). Every ROM gets "<file>.d/info.txt" (header fields and checksum verdicts). NES ROMs are also split into
// "prg.bin" and "chr.bin" and the CHR tiles are drawn to "chr.png" (16 tiles per row, 2 bit planes as grey levels); SNES ROMs lose a 512 byte copier header
// ("rom.sfc") and N64 ROMs are normalised to the big-endian ".z64" byte order ("rom.z64").
typedef struct retro_info_t { char buf[2048]; size_t n; } retro_info_t;

static void retro_info (retro_info_t *r, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	if (r->n < sizeof (r->buf))
		r->n += vsnprintf (r->buf + r->n, sizeof (r->buf) - r->n, fmt, ap);
	va_end (ap);
	if (r->n > sizeof (r->buf) - 1)
		r->n = sizeof (r->buf) - 1;
}

static void retro_text (char *out, const u8 *p, uint n)
{
	uint k = 0;
	for (uint i = 0; i < n; i++)
		out[k++] = p[i] >= 32 && p[i] < 127 ? p[i] : p[i] ? '?' : ' ';
	while (k && out[k - 1] == ' ')
		k--;
	out[k] = 0;
}

static enumError extract_retrorom_file (ccp arg, ccp basedir, uint depth)
{
	const bool nes = is_ext (arg, ".nes"), gb = is_ext (arg, ".gb") || is_ext (arg, ".gbc"), gba = is_ext (arg, ".gba");
	const bool snes = is_ext (arg, ".sfc") || is_ext (arg, ".smc");
	const bool n64 = is_ext (arg, ".z64") || is_ext (arg, ".v64") || is_ext (arg, ".n64");
	if ((!nes && !gb && !gba && !snes && !n64) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	retro_info_t info = {{0}, 0};
	char title[64];
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	u8 *extra = 0;
	size_t extra_n = 0;
	ccp extra_name = 0;
	u8 *rgba = 0;
	uint rgba_w = 0, rgba_h = 0;
	ccp extra2_name = 0;
	const u8 *extra2 = 0;
	size_t extra2_n = 0;
	bool ok = false;

	if (nes && sz >= 16 && !memcmp (raw, "NES\x1a", 4))
	{
		const uint prg = raw[4] * 16384u, chr = raw[5] * 8192u, f6 = raw[6], f7 = raw[7];
		const uint mapper = (f6 >> 4) | (f7 & 0xf0);
		const bool v2 = (f7 & 0x0c) == 8;
		const size_t off = 16 + (f6 & 4 ? 512 : 0);
		if (off + prg + chr <= sz)
		{
			ok = true;
			retro_info (&info, "format: iNES%s\nmapper: %u\nprg rom: %u bytes\nchr rom: %u bytes\nmirroring: %s\nbattery: %s\ntrainer: %s\n", v2 ? " 2.0" : "", mapper, prg, chr,
				f6 & 8 ? "four screen" : f6 & 1 ? "vertical" : "horizontal", f6 & 2 ? "yes" : "no", f6 & 4 ? "yes" : "no");
			extra = raw + off; extra_n = prg; extra_name = "prg.bin";
			if (chr)
			{
				extra2 = raw + off + prg; extra2_n = chr; extra2_name = "chr.bin";
				const uint tiles = chr / 16, cols = 16, rows = (tiles + cols - 1) / cols;
				rgba_w = cols * 8; rgba_h = rows * 8;
				rgba = CALLOC (1, (size_t)rgba_w * rgba_h * 4);
				for (uint t = 0; rgba && t < tiles; t++)
					for (uint y = 0; y < 8; y++)
						for (uint x = 0; x < 8; x++)
						{
							const u8 *tp = extra2 + t * 16;
							const uint v = (tp[y] >> (7 - x) & 1) | (tp[y + 8] >> (7 - x) & 1) << 1;
							u8 *px = rgba + (((size_t)(t / cols) * 8 + y) * rgba_w + (t % cols) * 8 + x) * 4;
							px[0] = px[1] = px[2] = v * 85;
							px[3] = 255;
						}
			}
		}
	}
	else if (gb && sz >= 0x150 && raw[0x104] == 0xce && raw[0x105] == 0xed)
	{
		ok = true;
		retro_text (title, raw + 0x134, raw[0x143] & 0x80 ? 11 : 16);
		uint x = 0;
		for (uint i = 0x134; i <= 0x14c; i++)
			x = x - raw[i] - 1;
		retro_info (&info, "format: Game Boy%s\ntitle: %s\ncgb flag: 0x%02x\nsgb flag: 0x%02x\ncartridge type: 0x%02x\nrom size code: 0x%02x (%u KiB)\nram size code: 0x%02x\n"
			"header checksum: %s\n", raw[0x143] & 0x80 ? " Color" : "", title, raw[0x143], raw[0x146], raw[0x147], raw[0x148], 32u << (raw[0x148] & 15), raw[0x149],
			(x & 255) == raw[0x14d] ? "ok" : "BAD");
	}
	else if (gba && sz >= 0xc0 && raw[0xb2] == 0x96)
	{
		ok = true;
		retro_text (title, raw + 0xa0, 12);
		uint x = 0;
		for (uint i = 0xa0; i <= 0xbc; i++)
			x += raw[i];
		x = (uint)(-(int)(x + 0x19)) & 255;
		char code[8], maker[4];
		retro_text (code, raw + 0xac, 4);
		retro_text (maker, raw + 0xb0, 2);
		retro_info (&info, "format: Game Boy Advance\ntitle: %s\ngame code: %s\nmaker code: %s\nversion: %u\nheader checksum: %s\n", title, code, maker, raw[0xbc],
			x == raw[0xbd] ? "ok" : "BAD");
	}
	else if (snes && sz >= 0x8000)
	{
		const size_t hdr = sz % 0x8000 == 0x200 ? 0x200 : 0;
		const u8 *rom = raw + hdr;
		const size_t rsz = sz - hdr;
		int best = -1;
		const size_t cands[2] = {0x7fc0, 0xffc0};
		for (uint c = 0; c < 2; c++)
		{
			if (cands[c] + 0x30 > rsz)
				continue;
			const u8 *h = rom + cands[c];
			if ((uint)(h[0x1c] | h[0x1d] << 8) + (uint)(h[0x1e] | h[0x1f] << 8) == 0xffff)
				if (best < 0 || (h[0x15] & 1) == (c == 1))
					best = c;
		}
		if (best >= 0)
		{
			const u8 *h = rom + cands[best];
			retro_text (title, h, 21);
			ok = true;
			retro_info (&info, "format: Super Nintendo (%s)\ntitle: %s\nmap mode: 0x%02x\ncartridge type: 0x%02x\nrom size code: 0x%02x (%u KiB)\nsram size code: 0x%02x\n"
				"region: 0x%02x\nversion: %u\nchecksum: 0x%04x (complement ok)\ncopier header: %s\n", best ? "HiROM" : "LoROM", title, h[0x15], h[0x16], h[0x17], 1u << h[0x17], h[0x18], h[0x19],
				h[0x1b], h[0x1e] | h[0x1f] << 8, hdr ? "yes (removed in rom.sfc)" : "no");
			if (hdr)
			{
				extra = raw + hdr; extra_n = rsz; extra_name = "rom.sfc";
			}
		}
	}
	else if (n64 && sz >= 0x1000)
	{
		const u8 *m = raw;
		int order = !memcmp (m, "\x80\x37\x12\x40", 4) ? 0 : !memcmp (m, "\x37\x80\x40\x12", 4) ? 1 : !memcmp (m, "\x40\x12\x37\x80", 4) ? 2 : -1;
		if (order >= 0)
		{
			u8 *z = MALLOC (sz & ~3u);
			if (z)
			{
				const size_t n = sz & ~3u;
				for (size_t i = 0; i < n; i += 4)
				{
					if (order == 0) { z[i] = raw[i]; z[i + 1] = raw[i + 1]; z[i + 2] = raw[i + 2]; z[i + 3] = raw[i + 3]; }
					else if (order == 1) { z[i] = raw[i + 1]; z[i + 1] = raw[i]; z[i + 2] = raw[i + 3]; z[i + 3] = raw[i + 2]; }
					else { z[i] = raw[i + 3]; z[i + 1] = raw[i + 2]; z[i + 2] = raw[i + 1]; z[i + 3] = raw[i]; }
				}
				ok = true;
				retro_text (title, z + 0x20, 20);
				char code[8];
				retro_text (code, z + 0x3b, 4);
				retro_info (&info, "format: Nintendo 64 (%s byte order)\ntitle: %s\ngame code: %s\nregion: %c\nversion: %u\ncrc1: 0x%08x\ncrc2: 0x%08x\n",
					order == 0 ? "big endian" : order == 1 ? "byte swapped" : "little endian", title, code, z[0x3e] >= 32 ? z[0x3e] : '?', z[0x3f], rd_be32 (z + 0x10), rd_be32 (z + 0x14));
				if (order)
				{
					extra = z; extra_n = n; extra_name = "rom.z64";
				}
				else
					FREE (z);
			}
		}
	}
	if (ok)
	{
		err = ERR_OK;
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT ROM-INFO:%s -> %s%s%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, basedir ? "/" : "", basedir ? basedir : "");
		if (!testmode)
		{
			err = shade_write (arg, dest, basedir, "info.txt", (u8 *)info.buf, (uint)info.n);
			if (!err && extra_name)
				err = shade_write (arg, dest, basedir, extra_name, extra, (uint)extra_n);
			if (!err && extra2_name)
				err = shade_write (arg, dest, basedir, extra2_name, extra2, (uint)extra2_n);
			if (!err && rgba)
				err = kyl_write_rgba_png (arg, basedir, "chr", (uint)-1, rgba_w, rgba_h, rgba);
		}
	}
	if (extra && extra_name && n64)
		FREE (extra);
	FREE (rgba);
	FREE (raw);
	return err;
}

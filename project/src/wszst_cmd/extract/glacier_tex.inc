// IO Interactive Glacier texture banks (".TEX" inside the scene ".zip" packs of Mini Ninjas, Wii;
// big endian). Header {u32 table offset, u32 end, 0, 0x04000000}, then from 0x10 one entry per
// texture, each starting on a 16-byte boundary: {u32 size (from the entry start to the end of the
// last mip), char format[4] twice, u32 id, u16 width, u16 height, u32 mip count, u32 0, f32 1.0,
// u32 0, NUL-terminated name ("Locations/All_Levels/Wood/Wood_Bamboo_01_ALL", may be empty), then
// per mip {u32 size, data}}. A "PALN" entry is followed by its palette {u32 count, count x {A, B,
// G, R}}. Entries whose two format words differ are id groups {u32 count, count x u32 id}. The
// texture area ends at the table offset (runtime offset tables, all zero). Pixel data is LINEAR
// (not GX-tiled): DXT1 is PC DXT1 with big-endian colour words, 15A3 = RGB5A3, 16A0 = RGB565, IA8 =
// {alpha, intensity}, IA4 = alpha:intensity nibbles, I4 high nibble first, I8, RGBA and palette
// entries are {A, B, G, R}, PALN = 8-bit palette indices. Checked on all 12,367 textures of the 59
// Mini Ninjas banks. The base level of every texture is written as
// "<file>.d/<name>.png" with --decode.
#include "lib-bntx.h"
#include "lib-tera.h"

static inline u8 gtex5 (uint v)
{
	return (u8)(v << 3 | v >> 2);
}

static bool glacier_tex_fmt_ok (const u8 *p)
{
	static const char *fmts[]
		= { "DXT1", "15A3", "16A0", "IA4 ", "IA8 ", "I4  ", "I8  ", "RGBA", "PALN" };
	if (memcmp (p, p + 4, 4))
		return false;
	for (uint i = 0; i < sizeof (fmts) / sizeof (*fmts); i++)
		if (!memcmp (p, fmts[i], 4))
			return true;
	return false;
}

// Returns the needed byte count of the base level for a supported format, 0 if unknown.
static size_t glacier_tex_need (const u8 *fc, uint w, uint h)
{
	const size_t px = (size_t)w * h;
	if (!memcmp (fc, "DXT1", 4))
		return (size_t)((w + 3) / 4) * ((h + 3) / 4) * 8;
	if (!memcmp (fc, "15A3", 4) || !memcmp (fc, "16A0", 4) || !memcmp (fc, "IA8 ", 4))
		return px * 2;
	if (!memcmp (fc, "IA4 ", 4) || !memcmp (fc, "I8  ", 4) || !memcmp (fc, "PALN", 4))
		return px;
	if (!memcmp (fc, "I4  ", 4))
		return (px + 1) / 2;
	if (!memcmp (fc, "RGBA", 4))
		return px * 4;
	return 0;
}

static void glacier_tex_decode (
	const u8 *fc, const u8 *d, uint w, uint h, const u8 *pal, uint npal, u8 *rgba)
{
	const size_t px = (size_t)w * h;
	if (!memcmp (fc, "DXT1", 4))
	{
		for (uint by = 0; by < (h + 3) / 4; by++)
			for (uint bx = 0; bx < (w + 3) / 4; bx++)
			{
				const u8 *s = d + ((size_t)by * ((w + 3) / 4) + bx) * 8;
				u8 blk[8] = { s[1], s[0], s[3], s[2], s[4], s[5], s[6], s[7] }, out[64];
				decode_bc1_block (blk, out, true);
				for (uint y = 0; y < 4; y++)
					for (uint x = 0; x < 4; x++)
						if (bx * 4 + x < w && by * 4 + y < h)
							memcpy (rgba + (((size_t)(by * 4 + y) * w) + bx * 4 + x) * 4,
								out + (y * 4 + x) * 4, 4);
			}
		return;
	}
	for (size_t i = 0; i < px; i++)
	{
		u8 *o = rgba + i * 4;
		if (!memcmp (fc, "15A3", 4))
		{
			const uint v = rd_be16 (d + 2 * i);
			if (v & 0x8000)
				o[0] = gtex5 (v >> 10 & 31), o[1] = gtex5 (v >> 5 & 31), o[2] = gtex5 (v & 31),
				o[3] = 255;
			else
				o[0] = (u8)((v >> 8 & 15) * 17), o[1] = (u8)((v >> 4 & 15) * 17),
				o[2] = (u8)((v & 15) * 17), o[3] = (u8)((v >> 12 & 7) * 255 / 7);
		}
		else if (!memcmp (fc, "16A0", 4))
		{
			const uint v = rd_be16 (d + 2 * i);
			o[0] = gtex5 (v >> 11), o[1] = (u8)((v >> 5 & 63) << 2 | (v >> 5 & 63) >> 4),
			o[2] = gtex5 (v & 31), o[3] = 255;
		}
		else if (!memcmp (fc, "IA8 ", 4))
			o[0] = o[1] = o[2] = d[2 * i + 1], o[3] = d[2 * i];
		else if (!memcmp (fc, "IA4 ", 4))
			o[0] = o[1] = o[2] = (u8)((d[i] & 15) * 17), o[3] = (u8)((d[i] >> 4) * 17);
		else if (!memcmp (fc, "I8  ", 4))
			o[0] = o[1] = o[2] = d[i], o[3] = 255;
		else if (!memcmp (fc, "I4  ", 4))
			o[0] = o[1] = o[2] = (u8)(((d[i / 2] >> (i & 1 ? 0 : 4)) & 15) * 17), o[3] = 255;
		else if (!memcmp (fc, "RGBA", 4))
			o[0] = d[4 * i + 3], o[1] = d[4 * i + 2], o[2] = d[4 * i + 1], o[3] = d[4 * i];
		else // PALN
		{
			const u8 *c = pal + 4 * (d[i] < npal ? d[i] : 0);
			o[0] = c[3], o[1] = c[2], o[2] = c[1], o[3] = c[0];
		}
	}
}

static bool is_glacier_tex (const u8 *raw, size_t sz)
{
	if (sz < 0x50)
		return false;
	const u32 table = rd_be32 (raw), end = rd_be32 (raw + 4), first = rd_be32 (raw + 0x10);
	return table >= 0x50 && table <= end && end <= sz && !rd_be32 (raw + 8)
		&& rd_be32 (raw + 12) == 0x04000000 && first >= 0x30 && 0x10 + (size_t)first <= table
		&& glacier_tex_fmt_ok (raw + 0x14);
}

static enumError extract_glacier_tex_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".tex") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 head[0x50];
	FILE *hf = fopen (arg, "rb");
	if (!hf)
		return ERR_NOTHING_TO_DO;
	const bool hok = fread (head, 1, sizeof head, hf) == sizeof head;
	fclose (hf);
	if (!hok || !is_glacier_tex (head, UINT_MAX))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (!is_glacier_tex (raw, sz))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	const size_t table = rd_be32 (raw);
	uint ntex = 0, wrote = 0, used = 0, cap = 0;
	char **names = 0;
	enumError err = ERR_OK;
	for (size_t o = 0x10; o + 0x30 <= table && !err;)
	{
		const u32 esz = rd_be32 (raw + o);
		if (!esz)
			break;
		if (!glacier_tex_fmt_ok (raw + o + 4))
		{
			// id group {u32 count, count x u32 id}
			if (esz > 0x10000 || o + 4 + 4 * (size_t)esz > table)
				break;
			o = (o + 4 + 4 * (size_t)esz + 15) & ~(size_t)15;
			continue;
		}
		if (esz < 0x2c || o + esz > table)
			break;
		const u8 *e = raw + o, *fc = e + 4;
		const uint w = rd_be16 (e + 16), h = rd_be16 (e + 18), mips = rd_be32 (e + 20);
		const u8 *nm = e + 0x24, *nend = memchr (nm, 0, raw + o + esz - nm);
		if (!nend || !w || !h || w > 8192 || h > 8192 || !mips || mips > 16)
			break;
		size_t p = nend + 1 - raw, base = 0, base_n = 0;
		bool ok = true;
		for (uint m = 0; m < mips && ok; m++)
		{
			if (p + 4 > o + esz)
			{
				ok = false;
				break;
			}
			const size_t ms = rd_be32 (raw + p);
			if (p + 4 + ms > o + esz)
				ok = false;
			else if (!m)
				base = p + 4, base_n = ms;
			p += 4 + ms;
		}
		const u8 *pal = 0;
		uint npal = 0;
		const bool paln = !memcmp (fc, "PALN", 4);
		if (ok && paln)
		{
			npal = p + 4 <= o + esz ? rd_be32 (raw + p) : 0;
			ok = npal && npal <= 256 && p + 4 + 4 * (size_t)npal <= o + esz;
			pal = raw + p + 4;
			p += 4 + 4 * (size_t)npal;
		}
		if (!ok || p != o + esz)
			break;
		ntex++;
		const size_t need = glacier_tex_need (fc, w, h);
		if (need && base_n >= need)
		{
			u8 *rgba = CALLOC ((size_t)w * h, 4);
			u8 *png = 0;
			size_t png_n = 0;
			if (rgba)
			{
				glacier_tex_decode (fc, raw + base, w, h, pal, npal, rgba);
				EncodePngMem (rgba, w, h, 4, 8, &png, &png_n);
			}
			if (png)
			{
				char name[PATH_MAX];
				const size_t nl = (size_t)(nend - nm);
				if (nl && nl < 400 && valid_sarc_path ((ccp)nm))
					snprintf (name, sizeof (name), "%s.png", (ccp)nm);
				else
					snprintf (name, sizeof (name), "tex_%u.png", rd_be32 (e + 12));
				// names repeat inside a bank (shared textures, case-insensitive file system)
				bool dup = false;
				for (uint i = 0; i < used && !dup; i++)
					dup = !strcasecmp (names[i], name);
				if (dup)
				{
					char *dot = strrchr (name, '.');
					snprintf (
						dot, sizeof (name) - (size_t)(dot - name), "_%u.png", rd_be32 (e + 12));
				}
				if (used == cap)
				{
					cap = cap ? 2 * cap : 64;
					char **nn = REALLOC (names, cap * sizeof (*names));
					if (nn)
						names = nn;
					else
						cap = used;
				}
				if (used < cap)
					names[used++] = STRDUP (name);
				if (!wrote && (verbose >= 0 || testmode))
					fprintf (stdlog, "%s%sDECODE GLACIER-TEX:%s -> %s/\n", verbose > 0 ? "\n" : "",
						testmode ? "WOULD " : "", arg, dest);
				if (!testmode)
					err = shade_write (arg, dest, basedir, name, png, (uint)png_n);
				wrote++;
			}
			FREE (png);
			FREE (rgba);
		}
		o = (o + esz + 15) & ~(size_t)15;
	}
	for (uint i = 0; i < used; i++)
		FREE (names[i]);
	FREE (names);
	FREE (raw);
	if (!wrote)
		return ntex ? ERR_OK : ERR_NOTHING_TO_DO;
	if (verbose > 0)
		fprintf (stdlog, "  %u of %u textures written\n", wrote, ntex);
	return err;
}

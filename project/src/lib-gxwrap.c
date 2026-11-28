#include "lib-gf3ds.h"
#include "lib-mtmob.h"
#include "lib-dc2.h"
#include <utime.h>
#include "lib-gxwrap.h"
#include "lib-retro-txtr.h"
#include "lib-excite.h"
#include "lib-lz10.h"
#include "lib-rhgroove.h"
#include "lib-zstd.h"

typedef struct gxw_t
{
	uint gx, w, h;
	size_t doff;
	size_t avail; // 0: up to the end of the file
	uint levels; // 0: as many as fit
	bool tga; // a Truevision TGA (RGBA <-> BGR(A)/gray), not GX tiles
	bool ivt; // ACB Total .ivt: BGRA8 / BGR8 / LA8 / L8 pixels XOR-scrambled with a short key (ncol = key variant, bpp = bytes per pixel)
	bool apx; // Capcom Zack and Wiki .apx: 24 bit linear RGB behind a 0x20 byte header
	bool imy; // Nippon Ichi IMY: palette + tiles, rewritten as an uncompressed (flags bit 0) stream
	bool ptexa; // PTEX kind 2: CMPR colour plane followed by an I4 alpha plane
	bool flip; // rows are stored bottom-up
	bool ltex; // Qubic LTEX: RGB565 / RGBA8888 pixels, optionally LZ11 packed (bpp = bytes per pixel)
	bool ntft; // NITRO raw texels (.ntft) with a sibling .ntfp palette; itype = format, ncol = palette colours
	uint rw, rh; // size of the PNG when the stored texture is padded (Ubisoft WSP)
	bool msk; // Ubisoft .msk: 8 bit mask (grey = r + g + b / 3)
	bool tan; // Ubisoft .tan frame (C8, shared RGB565 palette; doff = frame data, clut = palette)
	bool txm; // Namco .txm directory: the texels live in the sibling .txv (doff / avail refer to it)
	bool tklt; // Battle of Giants tile set ("TKLT" plain, "VNKT" LZ10 packed), palette from a TTLP file below a 2d folder
	bool nbfc; // DS raw tile dump (.nbfc / .char) edited through its sibling palette and map
	bool dst1; // DS "DST1" sprite sheet: RGB555 palette, 2/4/8 bit indices (doff = pixels, clut = palette)
	bool fmpp; // Where's Wally .ppm (FMPP): w / h = image size, doff = unused (the file is unpacked, edited and packed again)
	bool glac; // IOI Glacier .TEX entry (doff = base level, itype = its bytes, ctype = format fourcc, clut = palette (A,B,G,R), ncol = colours)
	bool pica; // 3DS PICA200 texture (GFTexture / MT TEX): gx = PICA format 0-13, doff = texels, avail = their bytes
	bool grph; // EA DS GRPH image (doff = GIMG block, clut = first palette colour, ncol = colours, tga.. = see reinject_grph; itype = palette base, bpp = 1 for a sprite frame)
	bool rtex; // NW4R .rtex XML export (doff = start of the hex words, avail = their text length, itype = data bytes, bpp = bytes per word)
	bool shpg; // EA SHPG entry (a dbl-style record inside the unpacked archive; RefPack wrapped files are re-packed as literal runs)
	bool dbl; // Avalanche .dbl / .mdb texture record (doff = pixels, clut = palette (RGB565), ncol = palette entries, itype = bytes available, gx = GX format)
	bool oibb; // AxEngine OIBB .pak TXTR object (doff = object, gx = format 6 / 8 / 9, bpp = palette bytes, itype = data bytes)
	bool rwpi; // RenderWare platform-independent texture (.rws chunk 0x23): doff = pixels, itype = stride, bpp = depth, clut = palette (RGBA), ncol = entries
	bool tim2; // a single-image PS2 TIM2 (tga fields: w, h, doff = pixels)
	uint bpp;
	uint itype, ctype, ncol;
	size_t clut;
} gxw_t;

static uint gxw_be16 (const u8 *p) { return p[0] << 8 | p[1]; }
static uint gxw_le16 (const u8 *p) { return p[0] | p[1] << 8; }
static uint gxw_be32 (const u8 *p) { return (uint)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint gxw_le32 (const u8 *p) { return (uint)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]; }

static u8 *load (ccp path, size_t *size);
static enumError store (ccp path, const u8 *data, size_t size);

static uint gxw_idx; // image number for multi-image files ("foo.dbt.2.png"), set by the repack code
void GXWrapSetIndex (uint idx) { gxw_idx = idx; }

static char gxw_name[256]; // member name for containers addressed by name ("NAME.tex.png" inside "foo.lmp.d")
void GXWrapSetName (ccp name) { snprintf (gxw_name, sizeof (gxw_name), "%s", name ? name : ""); }

static bool has_ext (ccp path, ccp ext)
{
	const size_t a = strlen (path), b = strlen (ext);
	return a > b && !strcasecmp (path + a - b, ext);
}

static void gx_block (uint gx, uint *bw, uint *bh, uint *bits)
{
	static const uint tab[15] = { 4, 8, 8, 16, 16, 16, 32, 0, 0, 0, 0, 0, 0, 0, 4 };
	*bits = tab[gx];
	*bw = gx == 0 || gx == 14 || gx == 1 || gx == 2 ? 8 : 4;
	*bh = gx == 0 || gx == 14 ? 8 : 4;
}

static bool gx_ok (uint gx) { return gx <= 6 || gx == 14; }

static size_t level_size (uint gx, uint w, uint h)
{
	uint bw, bh, bits;
	gx_block (gx, &bw, &bh, &bits);
	return (size_t)((w + bw - 1) / bw) * bw * ((h + bh - 1) / bh) * bh * bits / 8;
}

// Truevision TGA with a foreign extension, accepted like extract_tgaimg_file() does
static bool locate_tga (const u8 *raw, size_t sz, ccp path, gxw_t *g)
{
	if (has_ext (path, ".png") || has_ext (path, ".tga") || sz < 0x20 || raw[1]
		|| (raw[2] != 2 && raw[2] != 3 && raw[2] != 10 && raw[2] != 11) || raw[3] || raw[4]
		|| raw[5] || raw[6] || raw[7])
		return false;
	const uint w = raw[12] | raw[13] << 8, h = raw[14] | raw[15] << 8, bpp = raw[16], type = raw[2];
	if (!w || !h || w > 8192 || h > 8192 || (bpp != 8 && bpp != 24 && bpp != 32)
		|| (type == 2 || type == 10 ? bpp == 8 : bpp != 8))
		return false;
	const size_t npx = (size_t)w * h, bytes = bpp / 8, off = 18 + raw[0];
	if (type < 9)
	{
		if (off + npx * bytes > sz || sz - off - npx * bytes > 0x2000)
			return false;
	}
	else
	{
		size_t p = off, n = 0;
		while (n < npx && p < sz)
		{
			const uint c = raw[p++], run = (c & 0x7f) + 1;
			if (n + run > npx)
				break;
			p += c & 0x80 ? bytes : (size_t)run * bytes;
			if (p > sz)
				return false;
			n += run;
		}
		if (n != npx || sz - p > 0x2000)
			return false;
	}
	g->tga = true, g->w = w, g->h = h, g->bpp = bpp, g->doff = off;
	return true;
}

static bool locate_dst1 (const u8 *raw, size_t sz, gxw_t *g)
{
	if (sz < 0x14 || memcmp (raw, "DST1", 4))
		return false;
	const uint w = gxw_le16 (raw + 0xc), h = gxw_le16 (raw + 0xe), nc = gxw_le16 (raw + 0x10);
	const uint bpp = nc <= 4 ? 2 : nc <= 16 ? 4 : 8;
	const size_t o = 0x12 + 2 * (size_t)nc;
	if (!w || !h || !nc || nc > 256 || w * bpp % 8 || o + (size_t)w * h * bpp / 8 != sz)
		return false;
	g->dst1 = true, g->w = w, g->h = h, g->ncol = nc, g->bpp = bpp, g->doff = o, g->clut = 0x12;
	return true;
}

// PS2 TIM2 with one image (see extract_tim2png_file); only the top mip level is handled
static bool locate_tim2 (const u8 *raw, size_t sz, ccp path, gxw_t *g)
{
	if (!has_ext (path, ".tm2") || sz < 0x40 || memcmp (raw, "TIM2", 4) || !gxw_le16 (raw + 6) || gxw_idx >= gxw_le16 (raw + 6))
		return false;
	size_t off = 0x10; // images follow each other, each one {header, pixels, clut} of "total" bytes
	for (uint k = 0; k < gxw_idx; k++)
	{
		if (off + 0x30 > sz)
			return false;
		off += gxw_le32 (raw + off);
	}
	if (off + 0x30 > sz)
		return false;
	const u8 *h = raw + off;
	const size_t total = gxw_le32 (h), clutsz = gxw_le32 (h + 4), imgsz = gxw_le32 (h + 8);
	const uint hs = gxw_le16 (h + 12), ncol = gxw_le16 (h + 14), ctype = h[18], itype = h[19];
	const uint w = gxw_le16 (h + 20), ht = gxw_le16 (h + 22);
	if (total < hs || off + total > sz || hs < 0x30 || !w || !ht || w > 4096 || ht > 4096 || itype < 1
		|| itype > 5)
		return false;
	const uint bpp = itype == 1 ? 16 : itype == 2 ? 24 : itype == 3 ? 32 : itype == 4 ? 4 : 8;
	const uint cbpp = (ctype & 3) == 1 ? 16 : (ctype & 3) == 2 ? 24 : (ctype & 3) == 3 ? 32 : 0;
	if ((u64)w * ht * bpp / 8 > imgsz || hs + imgsz + clutsz > total
		|| (itype >= 4 && (!cbpp || (u64)ncol * cbpp / 8 > clutsz)))
		return false;
	g->tim2 = true, g->w = w, g->h = ht, g->bpp = itype >= 4 ? cbpp : bpp, g->itype = itype;
	g->ctype = ctype, g->ncol = ncol, g->doff = off + hs, g->clut = off + hs + imgsz;
	return true;
}

// first image of a TPL starting at raw+toff
static bool locate_tpl (const u8 *raw, size_t sz, size_t toff, gxw_t *g)
{
	if (toff + 0x20 > sz || gxw_be32 (raw + toff) != 0x0020af30)
		return false;
	const size_t ih = toff + gxw_be32 (raw + toff + 0xc);
	if (ih + 0x24 > sz)
		return false;
	g->h = gxw_be16 (raw + ih), g->w = gxw_be16 (raw + ih + 2), g->gx = gxw_be32 (raw + ih + 4);
	g->doff = toff + gxw_be32 (raw + ih + 8);
	g->levels = raw[ih + 0x22] + 1u; // max_lod + 1
	return true;
}

// sibling file of a ".nbfc" ("foo.nbfp", "foo.NBFP"), loaded with the same lookup the decoder uses
static u8 *load_sibling (ccp path, ccp ext, size_t *sz)
{
	char buf[PATH_MAX];
	ccp dot = strrchr (path, '.');
	if (!dot || (size_t)(dot - path) + strlen (ext) >= sizeof (buf))
		return 0;
	for (uint up = 0; up < 2; up++)
	{
		snprintf (buf, sizeof (buf), "%.*s", (int)(dot - path), path);
		char *q = buf + strlen (buf);
		for (ccp e = ext; *e; e++)
			*q++ = up ? toupper ((u8)*e) : *e;
		*q = 0;
		u8 *d = load (buf, sz);
		if (d)
			return d;
	}
	return 0;
}

static bool locate_nbfc (const u8 *raw, size_t sz, ccp path, gxw_t *g)
{
	(void)raw;
	const bool ch = has_ext (path, ".char");
	if ((!ch && !has_ext (path, ".nbfc")) || sz < 32 || sz > 0x400000)
		return false;
	size_t psz;
	u8 *pal = load_sibling (path, ch ? ".pal" : ".nbfp", &psz);
	const bool ok = pal && psz >= 2 && psz <= 0x200 && !(psz & 1);
	FREE (pal);
	g->nbfc = ok;
	return ok;
}

static const u8 ivt_key[3][7] = { { 0x99, 0x44, 0x87 }, { 0xba, 0x8c, 0xab, 0x9e, 0x99 }, { 0xb0, 0x8f, 0xbe, 0x93, 0x9a, 0x8d, 0xb2 } };
static const uint ivt_klen[3] = { 3, 5, 7 };

// same size / geometry deduction as the decoder (extract/acbivt.inc)
static bool locate_ivt (const u8 *raw, size_t sz, gxw_t *g)
{
	static const u8 hdr[3][6] = { { 0x2f, 0xcd, 0x1a, 0x36, 0xd8, 0x2f }, { 0x2f, 0x33, 0x11, 0x04, 0x02, 0x31 }, { 0x39, 0x37, 0x0e, 0x35, 0x11, 0x1a } };
	static const uint base[5] = { 0, 229, 37, 101, 165 };
	if (sz < 16 || raw[1] > 2 || memcmp (raw + 2, hdr[raw[1]], 6))
		return false;
	const u64 nb = sz - 8;
	for (uint b = 4; b >= 1; b--)
	{
		if (nb % b)
			continue;
		const u64 cand = nb / b;
		if (cand & (cand - 1))
			continue;
		uint sb = 0;
		while (((u64)1 << sb) < cand)
			sb++;
		const int t = 8 * (int)sb - (int)((raw[0] - base[b]) & 255);
		if (t < 0 || t % 7 || t / 7 > (int)sb || t / 7 > 13 || sb - t / 7 > 13)
			continue;
		g->ivt = true, g->bpp = b, g->ncol = raw[1], g->w = 1u << (t / 7), g->h = 1u << (sb - t / 7), g->doff = 8;
		return true;
	}
	return false;
}

static bool locate_apx (const u8 *raw, size_t sz, gxw_t *g)
{
	if (sz <= 0x20 || gxw_le32 (raw) != sz || gxw_le16 (raw + 12) != 24 || gxw_le32 (raw + 8) || gxw_le16 (raw + 18) != 1)
		return false;
	const uint w = gxw_le16 (raw + 14), h = gxw_le16 (raw + 16);
	if (!w || !h || gxw_le32 (raw + 4) != w * h * 3 || 0x20 + (size_t)w * h * 3 != sz)
		return false;
	g->apx = true, g->w = w, g->h = h, g->doff = 0x20;
	return true;
}

// ".PTX" raw GX data described by the sibling ".PTXH" header (Around the World)
static bool locate_ptx (ccp path, size_t sz, gxw_t *g)
{
	size_t hs;
	u8 *h = load_sibling (path, ".ptxh", &hs);
	bool ok = false;
	if (h && hs >= 0x28 && !memcmp (h + 8, "PATX WII", 8))
	{
		const uint bpp = gxw_be32 (h + 0x20);
		g->gx = !memcmp (h + 0x1c, "1TXD", 4) && bpp == 4 ? 14 : !memcmp (h + 0x1c, " 3A5", 4) && bpp == 16 ? 5
			: !memcmp (h + 0x1c, " 565", 4) && bpp == 16 ? 4 : !memcmp (h + 0x1c, "ABGR", 4) && bpp == 32 ? 6 : 99;
		g->w = gxw_be16 (h + 0x18), g->h = gxw_be16 (h + 0x1a), g->doff = 0, g->levels = 1;
		ok = g->gx != 99 && !((g->w | g->h) & 3) && sz >= (size_t)g->w * g->h * bpp / 8;
	}
	FREE (h);
	return ok;
}

static bool locate_imy (const u8 *raw, size_t sz, gxw_t *g)
{
	if (sz < 0x24 || memcmp (raw, "IMY", 4))
		return false;
	const uint stride = gxw_le16 (raw + 8), flags = raw[10], bpp = raw[11], h = gxw_le16 (raw + 12), npal = gxw_le16 (raw + 14);
	const uint w = bpp == 4 ? stride * 2 : bpp == 8 ? stride : bpp == 32 ? stride / 4 : 0;
	if (!w || !h || w > 0x400 || h > 0x1000 || (bpp != 32 && (!npal || npal > 256)) || (bpp == 32 && (npal || stride % 4))
		|| 0x20 + (size_t)npal * 2 > sz)
		return false;
	g->imy = true, g->w = w, g->h = h, g->bpp = bpp, g->ncol = npal, g->itype = flags, g->doff = 0x20 + (size_t)npal * 2;
	return true;
}

static bool locate_ltex (const u8 *raw, size_t sz, gxw_t *g)
{
	if (sz <= 0x18 || memcmp (raw, "LTEX", 4))
		return false;
	const uint fmt = raw[8] | raw[9] << 8, w = raw[10] | raw[11] << 8, h = raw[12] | raw[13] << 8;
	const uint bpp = fmt == 17 ? 4 : fmt == 14 || fmt == 15 ? 2 : 0;
	if (!bpp || !w || !h || w > 8192 || h > 8192)
		return false;
	g->ltex = true, g->w = w, g->h = h, g->bpp = bpp, g->doff = 0x18;
	return true;
}

enum { NTF_DIRECT = 1, NTF_I2, NTF_A5I3, NTF_I4, NTF_A3I5, NTF_I8, NTF_4X4 };

// same format deduction as the decoder (extract/ntfraw.inc); with an ".ntfi" it is the block compressed 4x4 format
static bool locate_ntft (const u8 *raw, size_t sz, ccp path, gxw_t *g)
{
	(void)raw;
	if (!has_ext (path, ".ntft") || sz < 8 || sz > 0x400000)
		return false;
	size_t psz = 0, isz = 0;
	u8 *pal = load_sibling (path, ".ntfp", &psz), *idx = load_sibling (path, ".ntfi", &isz);
	const bool has_pal = pal != 0 && psz <= 0x10000, has_idx = idx != 0;
	FREE (pal);
	FREE (idx);
	if (!has_pal)
		psz = 0;
	const uint ncol = (uint)psz / 2;
	g->ntft = true, g->ncol = ncol, g->bpp = (uint)sz;
	g->itype = has_idx ? NTF_4X4 : !ncol ? NTF_DIRECT : psz <= 8 ? NTF_I2 : psz == 16 ? NTF_A5I3 : psz <= 32 ? NTF_I4 : psz == 64 ? NTF_A3I5 : NTF_I8;
	g->w = g->h = 1; // the real size is the PNG's
	return true;
}

// ".dat" DST\0 image banks (Japanese quiz games): entries laid end to end; entry gxw_idx is edited like a DST1 sheet
static bool locate_dst0 (const u8 *raw, size_t sz, gxw_t *g)
{
	if (sz < 0x40 || memcmp (raw, "DST\0001.", 6))
		return false;
	size_t o = 0, sel = 0;
	uint n = 0;
	const uint want = isdigit ((u8)gxw_name[0]) ? (uint)strtoul (gxw_name, 0, 10) : gxw_idx; // "NNN_name.png"
	for (; o + 0x40 <= sz && !memcmp (raw + o, "DST\0001.", 6); n++)
	{
		const u8 *h = raw + o;
		const size_t es = gxw_le32 (h + 8), w = gxw_le16 (h + 12), hh = gxw_le16 (h + 14), ps = gxw_le32 (h + 16), xs = gxw_le32 (h + 24),
			po = gxw_le32 (h + 28), xo = gxw_le32 (h + 32);
		const size_t bpp = w && hh ? xs * 8 / (w * hh) : 0;
		if (es < 0x40 || es > sz - o || (bpp != 2 && bpp != 4 && bpp != 8) || xs * 8 != w * hh * bpp || ps > 512 || ps % 2
			|| po + ps > es || xo + xs > es)
			break;
		if (n == want)
		{
			g->dst1 = true, g->itype = 1, g->w = (uint)w, g->h = (uint)hh, g->bpp = (uint)bpp, g->ncol = (uint)(ps / 2);
			g->doff = o + xo, g->clut = o + po;
			sel = 1;
		}
		o += es;
	}
	return o == sz && n && sel && g->ncol;
}

static bool locate_tklt (const u8 *raw, size_t sz, gxw_t *g)
{
	const bool lz = sz >= 0x30 && !memcmp (raw, "VNKT", 4);
	if (sz < 0x30 || (!lz && memcmp (raw, "TKLT", 4)))
		return false;
	const size_t n4 = gxw_le16 (raw + 4), n8 = gxw_le16 (raw + 6), pl = gxw_le32 (raw + 0xd), data = 0x11 + pl;
	if (!(n4 + n8) || pl < 8 || pl > 200 || data + 4 > sz || (!lz && data + n4 * 32 + n8 * 64 != sz))
		return false;
	g->tklt = true, g->w = 128, g->h = (uint)((n4 + n8 + 15) / 16) * 8;
	return true;
}

// Fizzwizzle "OIBB" object pack: texture number gxw_idx (in decoder order) is a TXTR object {tag, size, data bytes, ptr, w, h, 1, fmt, 1, palette bytes}
static bool locate_oibb (const u8 *raw, size_t sz, gxw_t *g)
{
	if (sz < 0x100 || sz > 0x20000000 || memcmp (raw, "OIBB", 4))
		return false;
	uint n = 0;
	for (size_t o = 0x20; o + 0x40 <= sz; o += 4)
	{
		if (memcmp (raw + o, "TXTR", 4))
			continue;
		const uint osz = gxw_be32 (raw + o + 4), ds = gxw_be32 (raw + o + 8), w = gxw_be32 (raw + o + 0x10), h = gxw_be32 (raw + o + 0x14),
			   fmt = gxw_be32 (raw + o + 0x1c), pb = gxw_be32 (raw + o + 0x24);
		if (!w || !h || w > 4096 || h > 4096 || (fmt != 6 && fmt != 8 && fmt != 9) || pb > 512 || osz != 0x40 + pb + (u64)ds || o + osz > sz)
			continue;
		const uint tw = fmt == 6 ? 4 : 8, th = fmt == 8 ? 8 : 4, pw = (w + tw - 1) / tw * tw, ph = (h + th - 1) / th * th;
		const size_t need = fmt == 6 ? (size_t)pw * ph * 4 : fmt == 8 ? (size_t)pw * ph / 2 : (size_t)pw * ph;
		if (need > ds || (fmt != 6 && !pb))
			continue;
		if (n++ == gxw_idx)
		{
			g->oibb = true, g->doff = o, g->gx = fmt, g->w = w, g->h = h, g->bpp = pb, g->itype = ds;
			return true;
		}
		o += osz - 4;
	}
	return false;
}

// Avalanche "DB_TEXTURE_SET" record number gxw_idx (see extract/avdbl.inc)
static bool locate_dbl (const u8 *raw, size_t sz, gxw_t *g)
{
	if (sz < 0x40 || gxw_be32 (raw + 0x10) != 0x100)
		return false;
	for (size_t p = 0x20; p + 10 <= sz;)
	{
		const uint type = gxw_be16 (raw + p), fl = gxw_be16 (raw + p + 2);
		const size_t size = gxw_be32 (raw + p + 4), hl = fl & 8 ? (fl & 2 ? 0x40 : 0x20) : (fl & 2 ? 0x2a : 10);
		if (p + hl > sz || size > sz - p - hl)
			break;
		const size_t D = p + hl;
		if (type == 0x82 && size >= 0x60)
		{
			const uint n = gxw_be32 (raw + D);
			if (gxw_idx < n && gxw_idx < 1024 && 0x18 + 0x48 * (size_t)(gxw_idx + 1) <= size)
			{
				const u8 *r = raw + D + 0x18 + 0x48 * (size_t)gxw_idx;
				const uint fmt = gxw_be32 (r + 0x20) & 0xff, w = gxw_be16 (r + 0x30), h = gxw_be16 (r + 0x32);
				const size_t po = gxw_be32 (r + 0x2c), pal = gxw_be32 (r + 0x3c);
				const uint pn = gxw_be16 (r + 0x40) * gxw_be16 (r + 0x42);
				const bool paletted = fmt == 8 || fmt == 9;
				if (!w || !h || w > 2048 || h > 2048 || po >= size || !(fmt <= 6 || fmt == 8 || fmt == 9 || fmt == 14)
					|| fmt == 7 || (paletted && (!pn || pn > 256 || pal + 2 * (size_t)pn > size)))
					return false;
				g->dbl = true, g->bpp = 1, g->gx = fmt, g->w = w, g->h = h, g->doff = D + po, g->itype = (uint)(size - po), g->clut = D + pal, g->ncol = pn;
				return true;
			}
			return false;
		}
		p = D + size;
	}
	return false;
}

// EA RefPack ("10 fb" / "90 fb"): the unpacked bytes of a stream without the compressed-size field; NULL otherwise
static u8 *gxw_refpack (const u8 *s, size_t n, size_t *osz)
{
	if (n < 5 || s[1] != 0xfb || (s[0] & 0x7f) != 0x10)
		return 0;
	const uint nb = s[0] & 0x80 ? 4 : 3;
	size_t size = 0, p = 2 + nb;
	if (p > n)
		return 0;
	for (uint i = 0; i < nb; i++)
		size = size << 8 | s[2 + i];
	if (!size || size > (size_t)256 << 20)
		return 0;
	u8 *o = MALLOC (size);
	size_t q = 0;
	while (o && p < n && q < size)
	{
		const uint a = s[p++];
		size_t lit = 0, len = 0, dist = 0;
		if (a < 0x80)
		{
			if (p + 1 > n)
				break;
			lit = a & 3, len = ((a & 0x1c) >> 2) + 3, dist = ((a & 0x60) << 3) + s[p] + 1, p++;
		}
		else if (a < 0xc0)
		{
			if (p + 2 > n)
				break;
			lit = s[p] >> 6, len = (a & 0x3f) + 4, dist = ((s[p] & 0x3f) << 8) + s[p + 1] + 1, p += 2;
		}
		else if (a < 0xe0)
		{
			if (p + 3 > n)
				break;
			lit = a & 3, len = ((a & 0x0c) << 6) + s[p + 2] + 5, dist = ((a & 0x10) << 12) + (s[p] << 8) + s[p + 1] + 1, p += 3;
		}
		else
			lit = a < 0xfc ? ((a & 0x1f) << 2) + 4 : a & 3;
		if (lit > n - p || lit > size - q)
			break;
		memcpy (o + q, s + p, lit), p += lit, q += lit;
		if (a >= 0xe0)
		{
			if (a >= 0xfc)
				break;
			continue;
		}
		if (dist > q || len > size - q)
			break;
		for (size_t i = 0; i < len; i++, q++)
			o[q] = o[q - dist];
	}
	if (!o || q != size)
		return FREE (o), (u8 *)0;
	*osz = size;
	return o;
}

// a RefPack stream made of literal runs only (any decoder reads it); hdr = first byte of the original header
static u8 *gxw_refpack_literals (u8 hdr, const u8 *d, size_t n, size_t *osz)
{
	const uint nb = hdr & 0x80 ? 4 : 3;
	u8 *o = MALLOC (2 + nb + n + n / 112 + 8);
	if (!o)
		return 0;
	size_t p = 0;
	o[p++] = hdr, o[p++] = 0xfb;
	for (uint i = 0; i < nb; i++)
		o[p++] = (u8)(n >> (8 * (nb - 1 - i)));
	size_t q = 0;
	while (n - q >= 4)
	{
		size_t run = (n - q) & ~(size_t)3;
		if (run > 112)
			run = 112;
		o[p++] = (u8)(0xe0 + run / 4 - 1);
		memcpy (o + p, d + q, run), p += run, q += run;
	}
	o[p++] = (u8)(0xfc + (n - q));
	memcpy (o + p, d + q, n - q), p += n - q;
	*osz = p;
	return o;
}

// EA SHPG entry gxw_idx: record chain {code 0x10 + GX format, u24 size, u16 w, u16 h, ..., pixels at +0x10} followed by an optional 0x3N palette record
static bool locate_shpg (const u8 *raw, size_t sz, gxw_t *g)
{
	if (sz < 0x20 || memcmp (raw, "SHPG", 4))
		return false;
	const uint count = gxw_be32 (raw + 8);
	if (!count || count > 4096 || 16 + 8 * (size_t)count > sz || gxw_idx >= count)
		return false;
	const size_t off = gxw_be32 (raw + 16 + 8 * (size_t)gxw_idx + 4);
	if (off + 16 > sz)
		return false;
	const uint code = raw[off], fmt = code - 0x10, w = gxw_be16 (raw + off + 4), h = gxw_be16 (raw + off + 6);
	const size_t rec = (size_t)raw[off + 1] << 16 | raw[off + 2] << 8 | raw[off + 3];
	if (code < 0x10 || fmt > 14 || fmt == 7 || (fmt > 6 && fmt != 8 && fmt != 9 && fmt != 14) || rec < 16 || off + rec > sz || !w || !h || w > 2048 || h > 2048)
		return false;
	size_t pal = 0;
	uint pn = 0, pfmt = 0;
	if (off + rec + 16 <= sz && (raw[off + rec] & 0xf0) == 0x30)
	{
		const size_t prec = (size_t)raw[off + rec + 1] << 16 | raw[off + rec + 2] << 8 | raw[off + rec + 3];
		if (prec > 16 && off + rec + prec <= sz)
			pal = off + rec + 16, pn = (uint)(prec - 16) / 2, pfmt = raw[off + rec] & 3;
	}
	if ((fmt == 8 || fmt == 9) && !pal)
		return false;
	g->shpg = g->dbl = true, g->gx = fmt, g->w = w, g->h = h, g->doff = off + 16, g->itype = (uint)(rec - 16), g->clut = pal, g->ncol = pn, g->bpp = pfmt;
	return true;
}

// Rainbow Studios ".gct" (Cars): {2, format 41 CMPR / 58 CI8, [256, 256 RGB5A3 entries], count, w, h, count * {w, h, size, data}}; the last surface is the base level
static bool locate_gct (const u8 *raw, size_t sz, gxw_t *g)
{
	if (sz < 0x28 || gxw_be32 (raw) != 2)
		return false;
	const uint fmt = gxw_be32 (raw + 4);
	size_t o = 12, pal = 0;
	if (fmt == 58)
	{
		if (gxw_be32 (raw + 8) != 256)
			return false;
		pal = o, o += 512;
	}
	else if (fmt != 41)
		return false;
	if (o + 12 > sz)
		return false;
	const uint n = gxw_be32 (raw + o), w = gxw_be32 (raw + o + 4), h = gxw_be32 (raw + o + 8);
	o += 12;
	if (!n || n > 16 || !w || !h || w > 4096 || h > 4096)
		return false;
	size_t at = 0;
	uint bw = 0, bh = 0, bs = 0;
	for (uint i = 0; i < n; i++)
	{
		if (o + 12 > sz)
			return false;
		bw = gxw_be32 (raw + o), bh = gxw_be32 (raw + o + 4), bs = gxw_be32 (raw + o + 8);
		o += 12;
		if (!bw || !bh || bw > 4096 || bh > 4096 || o + bs > sz)
			return false;
		at = o, o += bs;
	}
	if (bw != w || bh != h)
		return false;
	g->dbl = true, g->bpp = 2, g->gx = fmt == 41 ? 14 : 9, g->w = w, g->h = h, g->doff = at, g->itype = bs, g->clut = pal, g->ncol = pal ? 256 : 0;
	return true;
}

// NW4R "nw4r_3dif" .rtex: <image format= width= height= data_unit= data_size=> followed by hex words (see extract/rtex.inc)
static bool locate_rtex (const u8 *raw, size_t sz, gxw_t *g)
{
	if (sz < 64 || sz > 0x20000000)
		return false;
	char *t = MALLOC (sz + 1);
	if (!t)
		return false;
	memcpy (t, raw, sz);
	t[sz] = 0;
	bool ok = false;
	char *im = strstr (t, "<image "), *gt = im ? strchr (im, '>') : 0, *end = gt ? strstr (gt, "</image>") : 0;
	if (im && gt && end && strstr (t, "<nw4r_3dif"))
	{
		*gt = 0;
		char fmts[16] = "", pal[8] = "", unit[8] = "";
		uint w = 0, h = 0, dsz = 0;
		const char *p;
		if ((p = strstr (im, "format=\"")))
			sscanf (p + 8, "%15[^\"]", fmts);
		if ((p = strstr (im, "palette_name=\"")))
			sscanf (p + 14, "%7[^\"]", pal);
		if ((p = strstr (im, "data_unit=\"")))
			sscanf (p + 11, "%7[^\"]", unit);
		if ((p = strstr (im, "width=\"")))
			w = (uint)strtoul (p + 7, 0, 10);
		if ((p = strstr (im, "height=\"")))
			h = (uint)strtoul (p + 8, 0, 10);
		if ((p = strstr (im, "data_size=\"")))
			dsz = (uint)strtoul (p + 11, 0, 10);
		static const struct { ccp name; uint fmt; } F[] = { { "i4", 0 }, { "i8", 1 }, { "ia4", 2 }, { "ia8", 3 }, { "rgb565", 4 }, { "rgb5a3", 5 }, { "rgba8", 6 }, { "cmpr", 14 } };
		int fmt = -1;
		for (uint i = 0; i < sizeof (F) / sizeof (*F); i++)
			if (!strcmp (F[i].name, fmts))
				fmt = (int)F[i].fmt;
		const uint ub = !strcmp (unit, "u32") ? 4 : !strcmp (unit, "u16") ? 2 : !strcmp (unit, "u8") ? 1 : 0;
		if (fmt >= 0 && !*pal && ub && w && h && w <= 4096 && h <= 4096 && dsz && dsz <= 0x10000000 && dsz % ub == 0 && level_size ((uint)fmt, w, h) <= dsz)
		{
			g->rtex = true, g->gx = (uint)fmt, g->w = w, g->h = h, g->itype = dsz, g->bpp = ub;
			g->doff = (size_t)(gt + 1 - t), g->avail = (size_t)(end - gt - 1);
			ok = true;
		}
	}
	FREE (t);
	return ok;
}

// Avalanche ".tbb" (Cars 2): the texels of texture gxw_idx of the sibling ".thb" {u32 count, count * {record, tbb offset, bytes}, records with w / h / format at +12}
static bool locate_tbb (const u8 *raw, size_t sz, ccp path, gxw_t *g)
{
	size_t tn;
	u8 *t = load_sibling (path, ".thb", &tn);
	bool ok = false;
	if (t && tn >= 48)
	{
		const uint n = gxw_be32 (t);
		if (n && n <= 256 && 4 + 12 * (size_t)n <= tn && gxw_idx < n)
		{
			const u8 *e = t + 4 + 12 * (size_t)gxw_idx;
			const size_t rec = gxw_be32 (e), off = gxw_be32 (e + 4), bytes = gxw_be32 (e + 8);
			if (rec + 32 <= tn && off + bytes <= sz)
			{
				const uint w = gxw_be16 (t + rec + 12), h = gxw_be16 (t + rec + 14), fmt = gxw_be16 (t + rec + 16);
				if (w && h && w <= 4096 && h <= 4096 && gx_ok (fmt) && level_size (fmt, w, h) <= bytes)
				{
					g->gx = fmt, g->w = w, g->h = h, g->doff = off, g->avail = bytes;
					ok = true;
				}
			}
		}
	}
	FREE (t);
	return ok;
}

// EA "GRPH" .dat (see extract/grph.inc): image number n of the file is the PNG "<stem>.png" (0) or "<stem>_n.png"; plain GIMG blocks and sprite frames (pieces)
static bool grph_image_ok (const u8 *raw, size_t sz, size_t g, uint *w, uint *h, uint *bpp, bool *tiled)
{
	if (g + 16 > sz || memcmp (raw + g, "GIMG", 4))
		return false;
	const size_t bl = gxw_le32 (raw + g + 4);
	*w = gxw_le16 (raw + g + 8), *h = gxw_le16 (raw + g + 10), *bpp = raw[g + 15], *tiled = raw[g + 14] == 6;
	return bl >= 16 && g + bl <= sz && *w && *h && (*bpp == 4 || *bpp == 8) && !(*tiled && (*w % 8 || *h % 8)) && (size_t)*w * *h * *bpp / 8 + 16 <= bl;
}

static bool locate_grph (const u8 *raw, size_t sz, ccp path, gxw_t *g)
{
	if (sz < 0x30 || memcmp (raw, "GRPH", 4) || gxw_le32 (raw + 8) != sz)
		return false;
	ccp bs = strrchr (path, '/');
	char stem[256];
	snprintf (stem, sizeof (stem), "%s", bs ? bs + 1 : path);
	char *dot = strrchr (stem, '.');
	if (dot)
		*dot = 0;
	const size_t sl = strlen (stem);
	uint want = 0;
	if (!strncmp (gxw_name, stem, sl) && gxw_name[sl] == '_' && isdigit ((u8)gxw_name[sl + 1]))
		want = (uint)strtoul (gxw_name + sl + 1, 0, 10);
	else if (strcmp (gxw_name, stem))
		return false;
	size_t pal = 0, pn = 0, pbase = 0;
	uint idx = 0;
	for (size_t o = gxw_le16 (raw + 0x0c) ? gxw_le16 (raw + 0x0c) : 0x10; o + 16 <= sz;)
	{
		const size_t bl = gxw_le32 (raw + o + 4);
		if (bl < 16 || o + bl > sz)
			break;
		if (!memcmp (raw + o, "GPAL", 4))
		{
			pn = (bl - 16) / 2;
			pbase = gxw_le16 (raw + o + 12) < 256 ? gxw_le16 (raw + o + 12) : 0;
			pal = o + 16;
		}
		else if (!memcmp (raw + o, "GIMG", 4) && pal)
		{
			uint w = 0, h = 0, bpp = 0;
			bool tiled = false;
			const bool sprite = raw[o + 14] == 0x14 && bl >= 32;
			if (sprite)
				w = gxw_le32 (raw + o + 20), h = gxw_le32 (raw + o + 24);
			if (sprite ? w && h && w <= 1024 && h <= 1024 : grph_image_ok (raw, sz, o, &w, &h, &bpp, &tiled))
			{
				if (idx++ == want)
				{
					g->grph = true, g->doff = o, g->clut = pal, g->ncol = (uint)pn, g->itype = (uint)pbase, g->bpp = sprite, g->w = w, g->h = h;
					return true;
				}
			}
		}
		o += bl;
	}
	return false;
}

// Midway (TNA Impact!) ".tim": BE {@0x20 w, h, bpp 8 / 32, data bytes, data offset, colours, palette offset}; 8 bpp is C8 with the palette in two 512 byte planes {G,R} / {A,B}
static bool locate_mtim (const u8 *raw, size_t sz, gxw_t *g)
{
	if (sz <= 0x40)
		return false;
	const uint w = gxw_be32 (raw + 0x20), h = gxw_be32 (raw + 0x24), bpp = gxw_be32 (raw + 0x28);
	const size_t dsz = gxw_be32 (raw + 0x2c), off = gxw_be32 (raw + 0x30), npal = gxw_be32 (raw + 0x34), poff = gxw_be32 (raw + 0x38);
	if (!w || !h || w > 4096 || h > 4096 || (bpp != 8 && bpp != 32) || dsz != (size_t)w * h * bpp / 8 || off < 0x40 || off + dsz > sz
		|| (bpp == 8 && (npal != 256 || poff + 1024 > sz)) || (w & 7) || (h & 3))
		return false;
	g->dbl = true, g->bpp = 3, g->gx = bpp == 8 ? 9 : 6, g->w = w, g->h = h, g->doff = off, g->itype = (uint)dsz, g->clut = poff, g->ncol = bpp == 8 ? 256 : 0;
	return true;
}

// "10 Minute Solution" .tplc: LZSS (flag bytes LSB first, 1 = literal, 0 = u16 LE {pos << 4 | len - 3} into a zero filled 4096 byte ring) over {u32 BE size, TPL}
static u8 *tplc_open (const u8 *d, size_t n, size_t *dn, gxw_t *g)
{
	if (n < 8 || d[0] != 0xff)
		return 0;
	const size_t size = (size_t)d[1] << 24 | d[2] << 16 | d[3] << 8 | d[4];
	if (size < 0x30 || size > 0x4000000)
		return 0;
	u8 *out = MALLOC (size), *ring = CALLOC (1, 4096);
	size_t o = 0, p = 0;
	bool bad = !out || !ring;
	while (!bad && o < size && p < n)
	{
		const uint f = d[p++];
		for (uint i = 0; i < 8 && o < size && !bad; i++)
		{
			if (f >> i & 1)
			{
				if (p >= n)
					bad = true;
				else
					ring[o & 4095] = out[o] = d[p++], o++;
			}
			else if (p + 2 > n)
				bad = true;
			else
			{
				const uint t = d[p] | d[p + 1] << 8, pos = t >> 4, len = (t & 15) + 3;
				p += 2;
				for (uint k = 0; k < len && o < size; k++, o++)
					ring[o & 4095] = out[o] = ring[(pos + k) & 4095];
			}
		}
	}
	FREE (ring);
	if (bad || o != size || memcmp (out, d + 1, 4) || gxw_be32 (out + 4) != 0x0020af30)
		return FREE (out), (u8 *)0;
	const u8 *t = out + 4;
	const size_t tn = size - 4;
	const uint ih = gxw_be32 (t + 0xc);
	if (tn < 0x20 || gxw_be32 (t + 4) < 1 || ih + 0xc > tn)
		return FREE (out), (u8 *)0;
	const uint h = gxw_be16 (t + ih), w = gxw_be16 (t + ih + 2), fmt = gxw_be32 (t + ih + 4);
	const size_t doff = gxw_be32 (t + ih + 8);
	if (!w || !h || !gx_ok (fmt) || doff + level_size (fmt, w, h) > tn)
		return FREE (out), (u8 *)0;
	memset (g, 0, sizeof (*g));
	g->gx = fmt, g->w = w, g->h = h, g->doff = 4 + doff, g->avail = tn - doff;
	g->levels = 1;
	*dn = size;
	return out;
}

// literal-only stream for the same container: one 0xff flag byte per eight literals
static u8 *tplc_pack (const u8 *d, size_t n, size_t *on)
{
	u8 *o = MALLOC (n + n / 8 + 16);
	if (!o)
		return 0;
	size_t p = 0;
	for (size_t i = 0; i < n; i += 8)
	{
		const size_t run = n - i < 8 ? n - i : 8;
		o[p++] = run == 8 ? 0xff : (u8)((1u << run) - 1);
		memcpy (o + p, d + i, run), p += run;
	}
	*on = p;
	return o;
}

// --- PICA200 texel encoders (inverse of DecodePicaTexture / decode_etc1_tiled) ---
static const int pica_mod[8][4] = { { 2, 8, -2, -8 }, { 5, 17, -5, -17 }, { 9, 29, -9, -29 }, { 13, 42, -13, -42 }, { 18, 60, -18, -60 }, { 24, 80, -24, -80 }, { 33, 106, -33, -106 }, { 47, 183, -47, -183 } };

static int pica_c255 (int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

// best table / indices for the pixels of one half block around the (8 bit) base colour; returns the squared error
static uint etc1_half (const u8 *px[8], const int base[3], uint *table, uint *idx)
{
	uint best = ~0u;
	for (uint t = 0; t < 8; t++)
	{
		uint err = 0, ix[8];
		for (uint i = 0; i < 8; i++)
		{
			uint be = ~0u;
			for (uint m = 0; m < 4; m++)
			{
				uint e = 0;
				for (uint k = 0; k < 3; k++)
				{
					const int d = pica_c255 (base[k] + pica_mod[t][m]) - px[i][k];
					e += (uint)(d * d);
				}
				if (e < be)
					be = e, ix[i] = m;
			}
			err += be;
		}
		if (err < best)
		{
			best = err, *table = t;
			memcpy (idx, ix, sizeof ix);
		}
	}
	return best;
}

// one ETC1 colour block as the 3DS stores it (64 bit value little endian); p = 16 RGBA pixels row-major
static void etc1_encode_block (const u8 *p, u8 out[8])
{
	u64 bestv = 0;
	uint beste = ~0u;
	for (uint flip = 0; flip < 2; flip++)
	{
		const u8 *half[2][8];
		uint pos[2][8], n[2] = { 0, 0 };
		for (uint y = 0; y < 4; y++)
			for (uint x = 0; x < 4; x++)
			{
				const uint s = flip ? y >= 2 : x >= 2;
				half[s][n[s]] = p + 4 * (y * 4 + x), pos[s][n[s]++] = x * 4 + y;
			}
		int avg[2][3];
		for (uint s = 0; s < 2; s++)
			for (uint k = 0; k < 3; k++)
			{
				uint sum = 0;
				for (uint i = 0; i < 8; i++)
					sum += half[s][i][k];
				avg[s][k] = (int)((sum + 4) / 8);
			}
		for (uint diff = 0; diff < 2; diff++)
		{
			int q[2][3], b[2][3];
			bool ok = true;
			for (uint s = 0; s < 2; s++)
				for (uint k = 0; k < 3; k++)
				{
					q[s][k] = diff ? (avg[s][k] * 31 + 127) / 255 : (avg[s][k] + 8) / 17;
					b[s][k] = diff ? q[s][k] << 3 | q[s][k] >> 2 : q[s][k] * 17;
				}
			if (diff)
				for (uint k = 0; k < 3; k++)
					if (q[1][k] - q[0][k] < -4 || q[1][k] - q[0][k] > 3)
						ok = false;
			if (!ok)
				continue;
			uint t[2], ix[2][8], err = 0;
			for (uint s = 0; s < 2; s++)
				err += etc1_half (half[s], b[s], &t[s], ix[s]);
			if (err >= beste)
				continue;
			u64 v = (u64)flip << 32 | (u64)diff << 33 | (u64)t[0] << 37 | (u64)t[1] << 34;
			if (diff)
				for (uint k = 0; k < 3; k++)
					v |= (u64)q[0][k] << (59 - 8 * k) | (u64)(q[1][k] - q[0][k] & 7) << (56 - 8 * k);
			else
				for (uint k = 0; k < 3; k++)
					v |= (u64)q[0][k] << (60 - 8 * k) | (u64)q[1][k] << (56 - 8 * k);
			for (uint s = 0; s < 2; s++)
				for (uint i = 0; i < 8; i++)
					v |= (u64)(ix[s][i] >> 1) << (pos[s][i] + 16) | (u64)(ix[s][i] & 1) << pos[s][i];
			beste = err, bestv = v;
		}
	}
	for (uint i = 0; i < 8; i++)
		out[i] = (u8)(bestv >> (8 * i));
}

// rgba: pw x ph pixels (multiples of 8); returns false when the surface needs more than cap bytes
static bool pica_encode (uint fmt, const u8 *rgba, uint pw, uint ph, u8 *out, size_t cap)
{
	if (fmt == 12 || fmt == 13)
	{
		const uint bw = pw / 4, bh = ph / 4, bsz = fmt == 13 ? 16 : 8;
		if ((size_t)bw * bh * bsz > cap)
			return false;
		for (uint by = 0; by < bh; by++)
			for (uint bx = 0; bx < bw; bx++)
			{
				u8 px[64];
				for (uint y = 0; y < 4; y++)
					memcpy (px + 16 * y, rgba + 4 * ((size_t)(by * 4 + y) * pw + bx * 4), 16);
				u8 *o = out + (size_t)(((by / 2) * (bw / 2) + bx / 2) * 4 + ((bx & 1) | (by & 1) << 1)) * bsz;
				if (fmt == 13)
				{
					memset (o, 0, 8);
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 4; x++)
						{
							const uint pp = x * 4 + y, a = (px[4 * (y * 4 + x) + 3] + 8) / 17;
							o[pp >> 1] |= (u8)(a << (pp & 1 ? 4 : 0));
						}
					o += 8;
				}
				etc1_encode_block (px, o);
			}
		return true;
	}
	static const uint bpp[12] = { 4, 3, 2, 2, 2, 2, 2, 1, 1, 1, 0, 0 };
	if (fmt > 11 || (size_t)pw * ph * (fmt >= 10 ? 1 : bpp[fmt]) / (fmt >= 10 ? 2 : 1) > cap)
		return false;
	for (uint y = 0; y < ph; y++)
		for (uint x = 0; x < pw; x++)
		{
			const uint pos = ((y / 8) * (pw / 8) + x / 8) * 64 + ((x & 1) | (y & 1) << 1 | (x & 2) << 1 | (y & 2) << 2 | (x & 4) << 2 | (y & 4) << 3);
			const u8 *s = rgba + 4 * ((size_t)y * pw + x);
			const uint lum = (s[0] + s[1] + s[2]) / 3;
			if (fmt >= 10)
			{
				const uint v = ((fmt == 10 ? lum : s[3]) + 8) / 17;
				out[pos >> 1] = (u8)((out[pos >> 1] & (pos & 1 ? 0x0f : 0xf0)) | (pos & 1 ? v << 4 : v));
				continue;
			}
			u8 *p = out + (size_t)pos * bpp[fmt];
			uint c;
			switch (fmt)
			{
			case 0: p[0] = s[0], p[1] = s[1], p[2] = s[2], p[3] = s[3]; break;
			case 1: p[0] = s[0], p[1] = s[1], p[2] = s[2]; break;
			case 2: c = ((s[0] * 31 + 127) / 255) << 11 | ((s[1] * 31 + 127) / 255) << 6 | ((s[2] * 31 + 127) / 255) << 1 | (s[3] >= 128); p[0] = (u8)c, p[1] = (u8)(c >> 8); break;
			case 3: c = ((s[0] * 31 + 127) / 255) << 11 | ((s[1] * 63 + 127) / 255) << 5 | ((s[2] * 31 + 127) / 255); p[0] = (u8)c, p[1] = (u8)(c >> 8); break;
			case 4: c = ((s[0] + 8) / 17) << 12 | ((s[1] + 8) / 17) << 8 | ((s[2] + 8) / 17) << 4 | (s[3] + 8) / 17; p[0] = (u8)c, p[1] = (u8)(c >> 8); break;
			case 5: p[0] = (u8)lum, p[1] = s[3]; break;
			case 6: p[0] = s[0], p[1] = s[1]; break;
			case 7: p[0] = (u8)lum; break;
			case 8: p[0] = s[3]; break;
			default: p[0] = (u8)((lum + 8) / 17 | (s[3] + 8) / 17 << 4); break;
			}
		}
	return true;
}

// Jakers! Kart Racing ".dct": GX CMPR / RGBA8 top mip behind the DC2 header (smaller mips are left as they are)
static bool locate_dct (const u8 *raw, size_t sz, gxw_t *g)
{
	uint w, h, gx;
	size_t doff, avail;
	if (sz > UINT_MAX || !DCTInfo (raw, (uint)sz, &w, &h, &gx, &doff, &avail))
		return false;
	g->gx = gx, g->w = w, g->h = h, g->doff = doff, g->avail = avail, g->levels = 1;
	return true;
}

// GFTexture (3DS): the texels follow a 0x80 byte header (see lib-gf3ds.c); Capcom MT Framework Mobile "TEX\\0" (see lib-mtmob.c) is
// recognised by its stricter header check and keeps its 16 / 20 byte header
static bool locate_pica (const u8 *raw, size_t sz, gxw_t *g)
{
	uint w, h, f;
	size_t doff, avail;
	if (!GFTextureInfo (raw, sz, &w, &h, &f, &doff, &avail) && !MTTEXInfo (raw, sz, &w, &h, &f, &doff, &avail))
		return false;
	g->pica = true, g->gx = f, g->w = w, g->h = h, g->doff = doff, g->avail = avail;
	return true;
}

// IOI Glacier (Mini Ninjas) ".TEX" bank: the entry whose name (or "tex_<id>", or "<name>_<id>") is gxw_name; linear pixels, see extract/glacier_tex.inc
static bool gl_fmt_ok (const u8 *p)
{
	static const char *const f[] = { "DXT1", "15A3", "16A0", "IA4 ", "IA8 ", "I4  ", "I8  ", "RGBA", "PALN" };
	if (memcmp (p, p + 4, 4))
		return false;
	for (uint i = 0; i < sizeof f / sizeof *f; i++)
		if (!memcmp (p, f[i], 4))
			return true;
	return false;
}

static bool locate_glacier (const u8 *raw, size_t sz, gxw_t *g)
{
	if (sz < 0x50 || !gxw_name[0])
		return false;
	const size_t table = gxw_be32 (raw), end = gxw_be32 (raw + 4), first = gxw_be32 (raw + 0x10);
	if (!(table >= 0x50 && table <= end && end <= sz && !gxw_be32 (raw + 8) && gxw_be32 (raw + 12) == 0x04000000 && first >= 0x30 && 0x10 + first <= table
		&& gl_fmt_ok (raw + 0x14)))
		return false;
	for (size_t o = 0x10; o + 0x30 <= table;)
	{
		const size_t esz = gxw_be32 (raw + o);
		if (!esz)
			break;
		if (!gl_fmt_ok (raw + o + 4))
		{
			if (esz > 0x10000 || o + 4 + 4 * esz > table)
				break;
			o = (o + 4 + 4 * esz + 15) & ~(size_t)15;
			continue;
		}
		if (esz < 0x2c || o + esz > table)
			break;
		const u8 *e = raw + o, *nm = e + 0x24, *nend = memchr (nm, 0, raw + o + esz - nm);
		const uint w = gxw_be16 (e + 16), h = gxw_be16 (e + 18), mips = gxw_be32 (e + 20), id = gxw_be32 (e + 12);
		if (!nend || !w || !h || w > 8192 || h > 8192 || !mips || mips > 16)
			break;
		size_t p = (size_t)(nend + 1 - raw), base = 0, base_n = 0;
		bool ok = true;
		for (uint m = 0; m < mips && ok; m++)
		{
			if (p + 4 > o + esz)
			{
				ok = false;
				break;
			}
			const size_t ms = gxw_be32 (raw + p);
			if (p + 4 + ms > o + esz)
				ok = false;
			else if (!m)
				base = p + 4, base_n = ms;
			p += 4 + ms;
		}
		const bool paln = !memcmp (e + 4, "PALN", 4);
		uint npal = 0;
		size_t pal = 0;
		if (ok && paln)
		{
			npal = p + 4 <= o + esz ? gxw_be32 (raw + p) : 0;
			ok = npal && npal <= 256 && p + 4 + 4 * (size_t)npal <= o + esz;
			pal = p + 4;
			p += 4 + 4 * (size_t)npal;
		}
		if (!ok || p != o + esz)
			break;
		char a[400], b[420], c[48];
		snprintf (a, sizeof a, "%.*s", (int)(nend - nm) < 400 ? (int)(nend - nm) : 399, (ccp)nm);
		snprintf (c, sizeof c, "tex_%u", id);
		snprintf (b, sizeof b, "%s_%u", a, id);
		if (!strcasecmp (gxw_name, a) && nend != nm ? true : (!strcasecmp (gxw_name, c) || !strcasecmp (gxw_name, b)))
		{
			const uint fc = gxw_be32 (e + 4);
			size_t need = 0;
			const size_t px = (size_t)w * h;
			need = fc == 0x44585431 ? (size_t)((w + 3) / 4) * ((h + 3) / 4) * 8 : (fc == 0x49342020) ? (px + 1) / 2 : (fc == 0x52474241) ? px * 4
				: (fc == 0x31354133 || fc == 0x31364130 || fc == 0x49413820) ? px * 2 : px;
			if (base_n >= need)
			{
				g->glac = true, g->doff = base, g->itype = (uint)base_n, g->w = w, g->h = h, g->ctype = fc, g->clut = pal, g->ncol = npal;
				return true;
			}
			return false;
		}
		o = (o + esz + 15) & ~(size_t)15;
	}
	return false;
}

// RenderWare GameCube / Wii ".txd" (see extract/rw_txd_gc.inc): texture number = leading digits of the PNG name "NNNN_<name>"
static bool locate_rwtxd (const u8 *raw, size_t sz, gxw_t *g)
{
	if (sz < 0x30 || gxw_le32 (raw) != 0x16 || gxw_le32 (raw + 0xc) != 1 || gxw_le32 (raw + 0x10) != 4 || gxw_le32 (raw + 0x1c) != 0x15 || !isdigit ((u8)gxw_name[0]))
		return false;
	const uint want = (uint)strtoul (gxw_name, 0, 10), count = gxw_le16 (raw + 0x18);
	size_t p = 0x1c;
	for (uint k = 0; k < count && p + 0x24 < sz; k++)
	{
		const size_t tsz = gxw_le32 (raw + p + 4);
		if (gxw_le32 (raw + p) != 0x15 || p + 12 + tsz > sz)
			break;
		const u8 *st = raw + p + 12;
		const size_t ssz = gxw_le32 (st + 4), b = p + 24;
		p += 12 + tsz;
		if (k != want)
			continue;
		if (gxw_le32 (st) != 1 || ssz < 0x6c || b + ssz > sz || gxw_be32 (raw + b) != 6)
			return false;
		const uint rf = gxw_be32 (raw + b + 0x58), w = gxw_be16 (raw + b + 0x5c), h = gxw_be16 (raw + b + 0x5e), depth = raw[b + 0x60], gx = raw[b + 0x62];
		const bool pal = gx == 8 || gx == 9;
		if (!w || !h || w > 4096 || h > 4096 || !(gx <= 6 || pal || gx == 14) || gx == 7)
			return false;
		const uint npal = pal ? (depth == 4 ? 16u : 256u) : 0;
		if (0x6c + npal * 2 > ssz)
			return false;
		g->dbl = true, g->bpp = (rf & 0xf00) == 0x200 ? 1 : 2, g->gx = gx, g->w = w, g->h = h, g->doff = b + 0x6c + npal * 2, g->itype = (uint)(ssz - 0x6c - npal * 2);
		g->clut = b + 0x6c, g->ncol = npal;
		return true;
	}
	return false;
}

// RenderWare platform-independent texture dictionary (".rws", see extract/rw_clump.inc): texture = trailing digits of the PNG name "<name>_NNN" (of the first 0x23 chunk)
static bool locate_rwpi (const u8 *raw, size_t sz, gxw_t *g)
{
	char nm[256];
	snprintf (nm, sizeof (nm), "%s", gxw_name);
	size_t n = strlen (nm);
	if (n > 4 && !strcasecmp (nm + n - 4, ".png"))
		nm[n -= 4] = 0;
	while (n && isdigit ((u8)nm[n - 1]))
		n--;
	if (!nm[n])
		return false;
	const uint want = (uint)strtoul (nm + n, 0, 10);
	for (size_t o = 0; o + 12 <= sz;)
	{
		const size_t s = gxw_le32 (raw + o + 4);
		if (s > sz - o - 12)
			return false;
		if (gxw_le32 (raw + o) != 0x23 || s < 4)
		{
			o += 12 + s;
			continue;
		}
		const size_t end = o + 12 + s;
		size_t p = o + 16;
		const uint cnt = gxw_le16 (raw + o + 12);
		for (uint i = 0; i < cnt && p + 4 <= end; i++)
		{
			const uint nl = gxw_le32 (raw + p);
			p += 4;
			size_t im = 0;
			bool first = true;
			for (uint l = 0; l < nl && p + 12 <= end; l++)
			{
				const size_t is = gxw_le32 (raw + p + 4);
				if (gxw_le32 (raw + p) != 0x18 || is > end - p - 12)
					return false;
				if (first)
					im = p, first = false;
				p += 12 + is;
			}
			if (p + 12 > end || gxw_le32 (raw + p) != 6 || gxw_le32 (raw + p + 4) > end - p - 12)
				return false;
			p += 12 + gxw_le32 (raw + p + 4);
			if (i != want)
				continue;
			if (!im || gxw_le32 (raw + im + 12) != 1 || gxw_le32 (raw + im + 16) < 16)
				return false;
			const uint w = gxw_le32 (raw + im + 24), h = gxw_le32 (raw + im + 28), depth = gxw_le32 (raw + im + 32), stride = gxw_le32 (raw + im + 36);
			const size_t isz = gxw_le32 (raw + im + 4) - 28, npal = depth <= 8 ? (size_t)1 << depth : 0;
			if (!w || !h || w > 4096 || h > 4096 || (depth != 4 && depth != 8 && depth != 32) || stride < (w * depth + 7) / 8 || (size_t)h * stride + npal * 4 > isz)
				return false;
			g->rwpi = true, g->w = w, g->h = h, g->bpp = depth, g->itype = stride, g->ncol = (uint)npal;
			g->doff = im + 40, g->clut = im + 40 + (size_t)h * stride;
			return true;
		}
		o = end;
	}
	return false;
}

// Where's Wally "FMPP" .ppm: header 0x28 + FastLZ level 2 stream (see extract/fmpp.inc); unpacked: palette planes, alpha image, 8 bit colour image in 8x4 tiles
static bool fmpp_unpack (const u8 *in, size_t n, u8 *out, size_t outsz)
{
	if (!n || (in[0] >> 5) != 1)
		return false;
	const u8 *ip = in, *end = in + n;
	size_t op = 0;
	u32 ctrl = *ip++ & 31;
	for (;;)
	{
		if (ctrl >= 32)
		{
			u32 len = (ctrl >> 5) - 1;
			size_t ofs = (ctrl & 31) << 8;
			if (len == 6)
			{
				u32 code;
				do
				{
					if (ip >= end)
						return false;
					code = *ip++;
					len += code;
				} while (code == 255);
			}
			if (ip >= end)
				return false;
			u32 code = *ip++;
			size_t back = ofs + code + 1;
			if (code == 255 && ofs == (31 << 8))
			{
				if (ip + 2 > end)
					return false;
				back = (size_t)((ip[0] << 8) + ip[1]) + 8191 + 1;
				ip += 2;
			}
			len += 3;
			if (back > op || len > outsz - op)
				return false;
			for (u32 i = 0; i < len; i++, op++)
				out[op] = out[op - back];
		}
		else
		{
			const u32 run = ctrl + 1;
			if (run > (size_t)(end - ip) || run > outsz - op)
				return false;
			memcpy (out + op, ip, run);
			ip += run, op += run;
		}
		if (ip >= end)
			break;
		ctrl = *ip++;
	}
	return op == outsz;
}

static bool locate_fmpp (const u8 *raw, size_t sz, ccp path, gxw_t *g)
{
	if (!has_ext (path, ".ppm") || sz < 0x30 || memcmp (raw, "FMPP", 4) || gxw_be32 (raw + 0x24) != 0x28)
		return false;
	const uint w = gxw_be16 (raw + 0x10), h = gxw_be16 (raw + 0x12), pw = gxw_be16 (raw + 0x14), ph = gxw_be16 (raw + 0x16);
	const size_t usz = gxw_be32 (raw + 0x1c), psz = gxw_be32 (raw + 0x20);
	if (!w || !h || pw < w || ph < h || pw % 8 || ph % 4 || usz < 0x40 || usz > 0x10000000 || psz > sz - 0x28)
		return false;
	u8 *out = MALLOC (usz);
	bool ok = out && fmpp_unpack (raw + 0x28, psz, out, usz);
	if (ok)
	{
		const size_t csz = gxw_be32 (out + 0x18), coff = gxw_be32 (out + 0x1c), plane = (size_t)pw * ph;
		ok = csz == plane && coff <= usz && plane <= usz - coff && gxw_be32 (out + 8) + 512 <= usz && gxw_be32 (out + 12) + 512 <= usz;
	}
	FREE (out);
	if (ok)
		g->fmpp = true, g->w = w, g->h = h, g->rw = pw, g->rh = ph;
	return ok;
}

static enumError reinject_fmpp (const Image_t *img, const u8 *raw, size_t sz, const gxw_t *g, ccp path)
{
	if (img->width != g->w || img->height != g->h)
		return ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the image is %ux%u: %s\n", img->width, img->height, g->w, g->h, path);
	const size_t usz = gxw_be32 (raw + 0x1c), psz = gxw_be32 (raw + 0x20);
	u8 *out = MALLOC (usz);
	if (!out || !fmpp_unpack (raw + 0x28, psz, out, usz))
		return FREE (out), ERR_NOTHING_TO_DO;
	const size_t pal1 = gxw_be32 (out + 8), pal2 = gxw_be32 (out + 12), asz = gxw_be32 (out + 0x10), aoff = gxw_be32 (out + 0x14), coff = gxw_be32 (out + 0x1c),
		     plane = (size_t)g->rw * g->rh;
	const bool alpha = asz == plane && aoff <= usz && plane <= usz - aoff;
	for (uint y = 0; y < g->h; y++)
		for (uint x = 0; x < g->w; x++)
		{
			const u8 *p = img->data + 4 * ((size_t)y * img->xwidth + x);
			const size_t pos = ((size_t)(y / 4) * (g->rw / 8) + x / 8) * 32 + (y % 4) * 8 + x % 8;
			uint best = 0, bd = ~0u;
			for (uint c = 0; c < 256; c++)
			{
				const int dr = (int)out[pal1 + 2 * c] - p[0], dg = (int)out[pal1 + 2 * c + 1] - p[1], db = (int)out[pal2 + 2 * c + 1] - p[2];
				const uint d = (uint)(dr * dr + dg * dg + db * db);
				if (d < bd)
					bd = d, best = c;
			}
			out[coff + pos] = (u8)best;
			if (alpha)
				out[aoff + pos] = p[3];
		}
	// literal-only FastLZ level 2: the first control byte carries the level bits
	u8 *z = MALLOC (usz + usz / 32 + 64);
	if (!z)
		return FREE (out), ERR_OUT_OF_MEMORY;
	size_t zn = 0;
	for (size_t i = 0; i < usz; i += 32)
	{
		const size_t run = usz - i < 32 ? usz - i : 32;
		z[zn++] = (u8)((i ? 0 : 0x20) | (run - 1));
		memcpy (z + zn, out + i, run), zn += run;
	}
	u8 *file = MALLOC (0x28 + zn);
	if (!file)
		return FREE (out), FREE (z), ERR_OUT_OF_MEMORY;
	memcpy (file, raw, 0x28);
	file[0x20] = (u8)(zn >> 24), file[0x21] = (u8)(zn >> 16), file[0x22] = (u8)(zn >> 8), file[0x23] = (u8)zn;
	memcpy (file + 0x28, z, zn);
	const enumError e = store (path, file, 0x28 + zn);
	FREE (out), FREE (z), FREE (file);
	(void)sz;
	return e;
}

static bool locate (const u8 *raw, size_t sz, ccp path, gxw_t *g)
{
	memset (g, 0, sizeof (*g));
	if (locate_tga (raw, sz, path, g) || locate_tim2 (raw, sz, path, g) || locate_dst1 (raw, sz, g) || locate_dst0 (raw, sz, g) || (has_ext (path, ".pak") && locate_oibb (raw, sz, g)) || (has_ext (path, ".gct") && locate_gct (raw, sz, g)) || (has_ext (path, ".tim") && locate_mtim (raw, sz, g)) || (has_ext (path, ".dat") && gxw_name[0] && locate_grph (raw, sz, path, g)) || (has_ext (path, ".tbb") && locate_tbb (raw, sz, path, g)) || (has_ext (path, ".rtex") && locate_rtex (raw, sz, g)) || locate_shpg (raw, sz, g) || ((has_ext (path, ".dbl") || has_ext (path, ".mdb")) && locate_dbl (raw, sz, g)) || locate_nbfc (raw, sz, path, g) || locate_tklt (raw, sz, g) || (has_ext (path, ".ivt") && locate_ivt (raw, sz, g)) || (has_ext (path, ".apx") && locate_apx (raw, sz, g)) || (has_ext (path, ".imy") && locate_imy (raw, sz, g)) || (has_ext (path, ".ltex") && locate_ltex (raw, sz, g)) || locate_ntft (raw, sz, path, g) || (has_ext (path, ".tex") && locate_glacier (raw, sz, g)) || (has_ext (path, ".txd") && locate_rwtxd (raw, sz, g)) || (has_ext (path, ".rws") && locate_rwpi (raw, sz, g)) || locate_fmpp (raw, sz, path, g) || (has_ext (path, ".dct") && locate_dct (raw, sz, g)) || locate_pica (raw, sz, g))
		return true;
	if (sz >= 0x40 && !memcmp (raw, "GCT0", 4))
	{
		g->gx = gxw_be32 (raw + 4), g->w = gxw_be16 (raw + 8), g->h = gxw_be16 (raw + 10), g->doff = 0x40;
	}
	else if (has_ext (path, ".wit") && sz >= 0x20)
	{
		for (uint hb = 0; hb < 8; hb += 4)
		{
			const uint w = gxw_le32 (raw + hb), h = gxw_le32 (raw + hb + 4), fmt = gxw_le32 (raw + hb + 8);
			const uint lv = gxw_le32 (raw + hb + 12);
			if (!w || !h || w > 4096 || h > 4096 || fmt > 14 || !gx_ok (fmt) || !lv || lv > 16)
				continue; // (the Cabela's CMPR variant with fmt 0x800 has two chains: not re-encoded)
			g->gx = fmt, g->w = w, g->h = h, g->doff = hb ? 0x18 : 0x10, g->levels = lv;
			break;
		}
	}
	else if (has_ext (path, ".ctx") && sz >= 0x50 && !gxw_be32 (raw) && gxw_be32 (raw + 12) == 0x34)
	{
		g->w = gxw_be16 (raw + 4), g->h = gxw_be16 (raw + 6), g->gx = gxw_be16 (raw + 8), g->doff = 0x40;
		g->levels = gxw_be16 (raw + 10);
		if (!g->levels || g->levels > 16)
			return false;
	}
	else if (has_ext (path, ".gaftx") && sz >= 0x44)
	{
		const uint fmt = gxw_be32 (raw + 0xc);
		g->w = gxw_be16 (raw), g->h = gxw_be16 (raw + 2), g->doff = gxw_be32 (raw + 0x24);
		g->gx = fmt == 18 ? 14 : fmt == 8 ? 6 : fmt == 9 ? 3 : fmt == 11 ? 4 : 99;
		g->levels = gxw_be32 (raw + 0x38);
		if (gxw_be16 (raw + 4) != g->w || gxw_be16 (raw + 6) != g->h || gxw_be32 (raw + 8) != 0x10001)
			return false;
	}
	else if (has_ext (path, ".s3t") && sz >= 0x40 && !gxw_be32 (raw + 12) && !gxw_be32 (raw + 16))
		g->gx = gxw_be32 (raw), g->w = gxw_be32 (raw + 4), g->h = gxw_be32 (raw + 8), g->doff = 0x20, g->levels = 1;
	else if (has_ext (path, ".5a3") && sz == 192 * 64 * 2)
		g->gx = 5, g->w = 192, g->h = 64, g->doff = 0, g->levels = 1; // headerless Wii Chess save banner
	else if (has_ext (path, ".iwi") && sz > 0x28 && !memcmp (raw, "IWi", 3) && (raw[3] == 11 || raw[3] == 7)
		&& raw[4] == 11)
	{
		g->gx = 14, g->w = raw[6] | raw[7] << 8, g->h = raw[8] | raw[9] << 8, g->doff = raw[3] == 7 ? 0x1c : 0x20;
		g->levels = 1;
		if (!g->w || !g->h || g->w > 4096 || g->h > 4096
			|| sz - g->doff != (size_t)((g->w + 7) / 8 * 8) * ((g->h + 7) / 8 * 8) / 2)
			return false;
	}
	else if (has_ext (path, ".a2") && sz >= 0x30 && !gxw_be32 (raw + 16) && !gxw_be32 (raw + 20))
	{
		g->gx = gxw_be32 (raw), g->w = gxw_be32 (raw + 4), g->h = gxw_be32 (raw + 8), g->doff = 0x1c;
		g->levels = gxw_be32 (raw + 12);
		if (!g->levels || g->levels > 16)
			return false;
	}
	else if (has_ext (path, ".bct") && sz >= 0x50 && raw[0] == 6)
	{
		g->w = gxw_be16 (raw + 4), g->h = gxw_be16 (raw + 6), g->gx = 14, g->doff = 0x30;
		g->avail = gxw_be32 (raw + 0x1c);
		if ((g->w & 7) || (g->h & 7))
			return false;
	}
	else if (has_ext (path, ".txw") && sz >= 0x80 && gxw_be32 (raw) == 0xafaf0002
		&& gxw_be32 (raw + 0x0c) == 0xbeefface)
	{
		const uint type = raw[0x12] << 8 | raw[0x13];
		g->w = raw[0x14] << 8 | raw[0x15], g->h = raw[0x16] << 8 | raw[0x17], g->gx = 14, g->doff = 0x40;
		g->levels = 1;
		if (type != 2 && type != 4)
			return false;
	}
	else if (has_ext (path, ".d4") && sz >= 0x40 && !gxw_be16 (raw + 6) && gxw_be32 (raw + 8) >= 0x0c
		&& gxw_be32 (raw + 8) <= 0x100 && !(gxw_be32 (raw + 8) & 3))
	{
		g->gx = gxw_be16 (raw), g->w = gxw_be16 (raw + 2), g->h = gxw_be16 (raw + 4), g->doff = gxw_be32 (raw + 8);
		g->levels = 1; // Paon (Donkey Kong: Barrel Blast): one image, no mips
	}
	else if (has_ext (path, ".wii") && sz >= 0x14 && !memcmp (raw, "\x12\xb9\xb0\xa1", 4)
		&& (raw[10] == 2 || raw[10] == 3 || raw[10] == 5))
	{
		g->gx = raw[10] == 5 ? 14 : raw[10] == 3 ? 5 : 4, g->w = gxw_be16 (raw + 6), g->h = gxw_be16 (raw + 8);
		g->doff = 0x13, g->levels = 1; // Pet Vet: the .wii1 ... mip files are not touched
	}
	else if (sz >= 0x20 && !memcmp (raw, "_NGCBM01", 8))
	{
		g->w = gxw_be16 (raw + 8), g->h = gxw_be16 (raw + 10), g->gx = gxw_be16 (raw + 12), g->doff = gxw_be32 (raw + 20);
		g->levels = raw[14] ? raw[14] : 1;
		if (g->doff < 0x20)
			return false;
	}
	else if (has_ext (path, ".mwt") && sz >= 0x60 && !memcmp (raw, "GDATAVERSION", 12))
	{
		if (!locate_tpl (raw, sz, 0x40, g))
			return false;
	}
	else if (has_ext (path, ".hvt") && sz >= 0x18 && !memcmp (raw, " IVH", 4)
		&& (gxw_be32 (raw + 4) == 2 || gxw_be32 (raw + 4) == 6))
	{
		const uint pb = gxw_be32 (raw + 0x14);
		g->gx = !memcmp (raw + 8, "S3TW", 4) && pb == 4 ? 14 : !memcmp (raw + 8, "ARGB", 4) && pb == 32 ? 6
			: !memcmp (raw + 8, "G8A8", 4) && pb == 16 ? 3 : !memcmp (raw + 8, "GRY8", 4) && pb == 8 ? 1 : 99;
		g->w = gxw_be32 (raw + 0xc), g->h = gxw_be32 (raw + 0x10), g->doff = 0x18, g->levels = 1;
	}
	else if (has_ext (path, ".etx") && sz >= 0x30)
	{
		const uint f = gxw_be32 (raw + 4);
		g->gx = f == 0x100000 ? 6 : f == 0x120000 ? 4 : f == 0x150000 ? 1 : 99;
		g->w = gxw_be16 (raw), g->h = gxw_be16 (raw + 2), g->doff = 8, g->levels = 1;
	}
	else if (has_ext (path, ".txe") && sz >= 0x20 && raw[4] == 0 && raw[5] < 8)
	{
		static const u8 gx[8] = { 4, 14, 5, 0, 1, 2, 3, 6 }; // Pikmin's own format enum
		g->gx = gx[raw[5]], g->w = raw[0] << 8 | raw[1], g->h = raw[2] << 8 | raw[3], g->doff = 0x20, g->levels = 1;
	}
	else if (has_ext (path, ".tex") && sz >= 0x20 && gxw_be32 (raw + 12) + 0x20 == sz
		&& (gxw_be32 (raw) == 0x4c || gxw_be32 (raw) == 0x4b || !gxw_be32 (raw)))
	{
		g->gx = gxw_be32 (raw) == 0x4c ? 14 : gxw_be32 (raw) == 0x4b ? 3 : 6;
		g->w = gxw_be32 (raw + 4), g->h = gxw_be32 (raw + 8), g->doff = 0x20, g->levels = (gxw_be32 (raw + 0x14) >> 16) + 1;
		if ((g->w & (g->w - 1)) || (g->h & (g->h - 1)) || g->levels > 16)
			return false;
	}
	else if (has_ext (path, ".gvr") && sz >= 0x20)
	{
		// optional GCIX chunk, then "GVRT": u32 size, 2 pad, u8 palette format << 4 | flags, u8 data format, BE16 w, h
		const size_t o = !memcmp (raw, "GCIX", 4) ? 8 + (size_t)gxw_le32 (raw + 4) : 0;
		if (o + 0x10 > sz || memcmp (raw + o, "GVRT", 4) || (raw[o + 10] & 0x0e))
			return false;
		const uint f = raw[o + 11];
		g->gx = f <= 6 ? f : f == 0xe ? 14 : 99; // indexed (8, 9) and external palette textures are not re-encoded
		g->w = gxw_be16 (raw + o + 12), g->h = gxw_be16 (raw + o + 14), g->doff = o + 0x10, g->levels = raw[o + 10] & 1 ? 0 : 1;
	}
	else if (has_ext (path, ".aig") && sz > 0x40 && !memcmp (raw, "ALIG", 4) && raw[8] == 'G')
	{
		static const struct { const char *tag; uint fmt; } fm[] = { { "GIA4", 2 }, { "G5A3", 5 }, { "G565", 4 }, { "GC32", 6 }, { "GCCP", 14 }, { "GACC", 14 } };
		g->gx = 99;
		for (uint i = 0; i < sizeof (fm) / sizeof (fm[0]); i++)
			if (!memcmp (raw + 8, fm[i].tag, 4))
				g->gx = fm[i].fmt;
		g->w = gxw_le16 (raw + 0x10), g->h = gxw_le16 (raw + 0x12), g->doff = 0x40, g->levels = 1; // One Piece: Unlimited Adventure
	}
	else if (has_ext (path, ".dbt") && sz >= 0x180 && gxw_be32 (raw) == 3 && gxw_be32 (raw + 4) == 8 && gxw_be32 (raw + 0x108) == 14)
	{
		g->w = gxw_be16 (raw + 0x104), g->h = gxw_be16 (raw + 0x106), g->gx = 14, g->levels = 1;
		const size_t one = (size_t)((g->w + 7) / 8) * ((g->h + 7) / 8) * 32;
		g->doff = 0x180 + gxw_idx * one;
		if (g->w < 8 || g->h < 8 || g->w > 2048 || g->h > 2048 || g->doff + one > sz)
			return false;
	}
	else if (has_ext (path, ".wiitex") && sz >= 0x100 && gxw_be32 (raw) == 2 && gxw_be32 (raw + 4) == sz - 0x10
		&& gxw_be32 (raw + 8) == 0x10 && gxw_be32 (raw + 12) == 8)
	{
		// the decoder finds the size by matching the CMPR mip chain against the file size: do the same
		for (uint pass = 0; pass < 2 && !g->w; pass++)
			for (uint w = 2048; w >= 8 && !g->w; w >>= 1)
				for (uint h = 2048; h >= 8 && !g->w; h >>= 1)
				{
					u64 sum = 0;
					uint lw = w, lh = h;
					for (uint lv = 1; lv <= 12 && (pass || w == h) && !g->w; lv++)
					{
						sum += (u64)((lw + 7) / 8) * ((lh + 7) / 8) * 32;
						lw = lw > 1 ? lw >> 1 : 1, lh = lh > 1 ? lh >> 1 : 1;
						if (0x50 + sum <= sz && sz - 0x50 - sum <= 8)
							g->w = w, g->h = h, g->gx = 14, g->doff = 0x50, g->levels = lv;
					}
				}
		if (!g->w)
			return false;
	}
	else if (has_ext (path, ".ptx"))
	{
		if (!locate_ptx (path, sz, g))
			return false;
	}
	else if (has_ext (path, ".btf") && sz >= 0x100 && !memcmp (raw, "FTBG", 4))
	{
		// chunks {"DXTG", u32 size, header with BE w/h and the reversed format name, GX pixels as the last bytes}
		static const struct { const char *name; uint gx, bits; } fm[] = { { "A8R8G8B8", 6, 32 }, { "I8", 1, 8 }, { "IA4", 2, 8 }, { "IA8", 3, 16 }, { "RGB565", 4, 16 }, { "RGB5A3", 5, 16 }, { "I4", 0, 4 } };
		size_t p = 12;
		for (uint idx = 0, n = gxw_le32 (raw + 8); idx <= gxw_idx && idx < n && idx < 256 && p + 8 <= sz; idx++)
		{
			const size_t csz = gxw_le32 (raw + p + 4), c = p + 8;
			p = c + csz;
			if (csz < 0x80 || p > sz)
				return false;
			if (idx != gxw_idx)
				continue;
			char name[9] = { 0 };
			for (uint i = 0; i < 8; i++)
				name[i] = (char)raw[c + 0x37 - i];
			const char *f = name;
			while (!*f && f < name + 8)
				f++;
			for (uint i = 0; i < sizeof (fm) / sizeof (fm[0]); i++)
				if (!strcmp (f, fm[i].name))
				{
					g->gx = fm[i].gx, g->w = gxw_be32 (raw + c + 0x20), g->h = gxw_be32 (raw + c + 0x24), g->levels = 1;
					g->doff = p - (size_t)g->w * g->h * fm[i].bits / 8;
				}
			if (!g->w || g->w > 2048 || g->h > 2048 || g->doff < c + 0x70)
				return false; // (palette formats P4 / P8 are not re-encoded)
			break;
		}
		if (!g->w)
			return false;
	}
	else if (has_ext (path, ".lmp") && sz > 0x40 && gxw_name[0] && gxw_be16 (raw + 2) == 1)
	{
		// texture lump {u16 count, u16 1}, 24 byte entries {0, name offset, 0, data offset, size, 0}; each texture has a 0x80 byte header
		const uint n = gxw_be16 (raw);
		if (!n || n > 4096 || 4 + (size_t)n * 24 > sz)
			return false;
		for (uint i = 0; i < n && !g->w; i++)
		{
			const u8 *e = raw + 4 + 24 * i;
			const size_t noff = gxw_be32 (e + 4), off = gxw_be32 (e + 12), size = gxw_be32 (e + 16);
			if (noff >= sz || off + size > sz || size <= 0x80 || gxw_be32 (e) || gxw_be32 (e + 8))
				continue;
			char nm[256];
			snprintf (nm, sizeof (nm), "%.200s", (ccp)raw + noff);
			const uint w = gxw_be16 (raw + off), h = gxw_be16 (raw + off + 2), tail = gxw_be16 (raw + off + 6);
			const uint fmt = tail == 0x8080 ? 6 : tail == 0x0004 ? 14 : 99;
			if (strcmp (nm, gxw_name) || fmt == 99 || !w || !h)
				continue;
			g->gx = fmt, g->w = w, g->h = h, g->doff = off + 0x80, g->levels = 1;
			if (size - 0x80 != level_size (fmt, w, h))
				g->w = 0;
		}
		if (!g->w)
			return false;
	}
	else if (has_ext (path, ".tpc") && sz >= 36 && gxw_name[0] && strspn (gxw_name, "0123456789") == strlen (gxw_name)
		&& !raw[0] && !raw[2] && !raw[3] && raw[1] && 4 + (size_t)raw[1] * 32 <= sz && strtoul (gxw_name, 0, 10) < raw[1])
	{
		// Little League texture pack: 32 byte entries {BE32 texel offset, palette offset, h, w, ..., format at 0x17}; only direct colour formats
		const u8 *e = raw + 4 + 32 * strtoul (gxw_name, 0, 10);
		const uint f = e[0x17];
		g->gx = f == 4 ? 4 : f == 5 ? 5 : f == 6 ? 6 : 99, g->w = gxw_be16 (e + 10), g->h = gxw_be16 (e + 8), g->doff = gxw_be32 (e), g->levels = 1;
		if (g->gx == 99 || !g->w || !g->h || g->w > 2048 || g->h > 2048 || g->doff + level_size (g->gx, g->w, g->h) > sz)
			return false;
	}
	else if (has_ext (path, ".wii") && sz > 0x40 && !memcmp (raw, "WII!", 4) && gxw_be32 (raw + 4) == 2 && gxw_be32 (raw + 32) == 0x40 && !gxw_be32 (raw + 12) && gxw_name[0]
		&& isdigit ((u8)gxw_name[0]) && strtoul (gxw_name, 0, 10) < gxw_be32 (raw + 8))
	{
		// Star Trek: Conquest texture-only bundle: 0x18 byte records {name[4], w, h, kind, 0, size, offset} from 0x40
		const u8 *r = raw + 0x40 + 0x18 * strtoul (gxw_name, 0, 10);
		if (r + 0x18 > raw + sz)
			return false;
		const uint kind = gxw_be32 (r + 8);
		g->w = gxw_be16 (r + 4), g->h = gxw_be16 (r + 6), g->doff = gxw_be32 (r + 20), g->levels = 1;
		g->gx = kind == 0 ? 6 : kind == 1 ? 4 : 5;
		if (kind > 2 || !g->w || !g->h || gxw_be32 (r + 12) || gxw_be32 (r + 16) != g->w * g->h * (kind ? 2u : 4u)
			|| g->doff + gxw_be32 (r + 16) > sz)
			return false;
	}
	else if (has_ext (path, ".kwtp") && sz >= 0x180 && !memcmp (raw, "kwtp02", 6))
	{
		if (!locate_tpl (raw, sz, gxw_be32 (raw + 0x18), g))
			return false;
	}
	else if (has_ext (path, ".d3dtx") && sz >= 0x100 && !memcmp (raw, "ERTM", 4))
	{
		// Telltale (Wii): metastream whose body embeds a one-image TPL running to the end of the file
		for (size_t i = 4; i + 0x40 < sz && !g->w; i++)
			if (gxw_be32 (raw + i) == 0x0020af30 && gxw_be32 (raw + i + 4) == 1 && gxw_be32 (raw + i + 8) == 0xc)
				locate_tpl (raw, sz, i, g);
		if (!g->w)
			return false;
	}
	else if ((has_ext (path, ".pte") || has_ext (path, ".pfi")) && sz >= 0x100 && gxw_be32 (raw) == 13)
	{
		// Pixel Entertainment: a header, then an ordinary TPL somewhere in the first 0x400 bytes
		for (size_t i = 0x20; i + 0x40 < sz && i < 0x400 && !g->w; i++)
			if (gxw_be32 (raw + i) == 0x0020af30 && gxw_be32 (raw + i + 4) == 1 && gxw_be32 (raw + i + 8) == 0x0c)
				locate_tpl (raw, sz, i, g);
		if (!g->w)
			return false;
	}
	else if (sz >= 0x40 && !memcmp (raw, "TXG ", 4))
	{
		// EA texture group: chunks {tag, BE32 size} HEAD / TXHE (88 byte records) / CLHE / TXDA; the base level of record gxw_idx is re-encoded
		size_t he = 0, he_size = 0, da = 0, da_size = 0;
		for (size_t p = 8; p + 8 <= sz;)
		{
			const size_t cs = gxw_be32 (raw + p + 4);
			if (cs > sz - p - 8)
				break;
			if (!memcmp (raw + p, "TXHE", 4))
				he = p + 8, he_size = cs;
			else if (!memcmp (raw + p, "TXDA", 4))
				da = p + 8, da_size = cs;
			p += 8 + cs;
		}
		if (!he || !da || he_size % 88 || gxw_idx >= he_size / 88)
			return false;
		const u8 *r = raw + he + 88 * gxw_idx;
		const uint f = r[0x48], off = gxw_be32 (r + 0x10);
		g->w = gxw_be16 (r + 0x40), g->h = gxw_be16 (r + 0x42), g->gx = f, g->levels = 1, g->doff = da + off;
		if ((f != 14 && f != 5 && f != 0) || !g->w || !g->h || g->w > 2048 || g->h > 2048 || ((g->w | g->h) & 7)
			|| off > da_size || level_size (f, g->w, g->h) > da_size - off)
			return false;
	}
	else if (sz >= 0x3c && !memcmp (raw, "PTEX", 4) && gxw_be32 (raw + 12) == 0x44332211 && gxw_name[0] && isdigit ((u8)gxw_name[0])
		&& strtoul (gxw_name, 0, 10) < gxw_be32 (raw + 0x18) && gxw_be32 (raw + 0x18) <= 256)
	{
		// Virtua Tennis 2009 texture bundle: 0x20 byte records from 0x1c {hash, kind, used w, h, w, h, offset - 0x10, ...}
		const u8 *r = raw + 0x1c + 0x20 * strtoul (gxw_name, 0, 10);
		const uint kind = gxw_be32 (r + 4);
		g->w = gxw_be16 (r + 12), g->h = gxw_be16 (r + 14), g->gx = 14, g->levels = 1, g->doff = (size_t)gxw_be32 (r + 16) + 0x10;
		g->ptexa = kind == 2;
		if ((kind != 0 && kind != 2) || !g->w || !g->h || ((g->w | g->h) & 7)
			|| g->doff + (kind == 2 ? (size_t)g->w * g->h : (size_t)g->w * g->h / 2) > sz)
			return false;
	}
	else if (sz >= 0x20 && !memcmp (raw, "TTPL", 4))
	{
		if (!locate_tpl (raw, sz, 8, g)) // Petz: {"TTPL", u32 size} around an ordinary TPL
			return false;
	}
	else if (sz >= 0x40 && !memcmp (raw, "TEXL", 4))
	{
		// Petz Cranium Kabookii: 0x19 byte header {..., BE16 w, h at 0x10}, raw CMPR / RGBA8 (or CMPR + I4 alpha) pixels, 8 byte trailer
		g->w = raw[0x10] << 8 | raw[0x11], g->h = raw[0x12] << 8 | raw[0x13], g->doff = 0x19, g->levels = 1;
		const size_t body = sz - 0x19 - 8;
		if (!g->w || !g->h || ((g->w | g->h) & 7))
			return false;
		if (body == (size_t)g->w * g->h / 2 || body == (size_t)g->w * g->h)
			g->gx = 14, g->ptexa = body == (size_t)g->w * g->h;
		else if (body == (size_t)g->w * g->h * 4)
			g->gx = 6;
		else
			return false;
	}
	else if (has_ext (path, ".tex") && sz >= 0x18 && gxw_be32 (raw) >> 16 == 0x0402 && gxw_be32 (raw + 20) == 1
		&& (gxw_be32 (raw + 4) == 11 || gxw_be32 (raw + 4) == 13) && gxw_be32 (raw + 8) == sz - 0x18)
	{
		// International Athletics: direct colour only (the C4 / C8 variants carry a palette)
		g->gx = gxw_be32 (raw + 4) == 11 ? 5 : 14, g->w = gxw_be32 (raw + 12), g->h = gxw_be32 (raw + 16), g->doff = 0x18, g->levels = 1, g->flip = true;
	}
	else if (has_ext (path, ".bgst3") && sz >= 0x40 && !memcmp (raw, "BGST", 4))
	{
		// Kirby's Epic Yarn background tiles: count CMPR tiles of w * h / 2 bytes run to the end of the file
		g->w = gxw_be32 (raw + 8), g->h = gxw_be32 (raw + 12), g->gx = 14, g->levels = 1;
		const size_t count = gxw_be32 (raw + 0x18), data = gxw_be32 (raw + 0x2c), tsz = (size_t)g->w * g->h / 2;
		if (g->w < 8 || g->w > 2048 || g->h < 8 || g->h > 2048 || ((g->w | g->h) & 7) || !count || count > 4096
			|| data + count * tsz != sz || gxw_idx >= count)
			return false;
		g->doff = data + gxw_idx * tsz;
	}
	else if (has_ext (path, ".pvr") && sz >= 0x20 && !memcmp (raw, "XIBG", 4) && !memcmp (raw + 0x10, "TRVP", 4))
	{
		// Radirgy Noa / Illvelo: GBIX + PVRT with GX data; CMPR (0x4e) and RGB565 (0x45, pixel format 1) are re-encoded
		const uint kind = raw[0x1a], pf = raw[0x1b];
		g->w = gxw_be16 (raw + 0x1c), g->h = gxw_be16 (raw + 0x1e), g->doff = 0x20, g->levels = 1;
		g->gx = kind == 0x4e ? 14 : kind == 0x45 && pf == 1 ? 4 : 99;
		if ((g->w | g->h) & 7)
			return false;
	}
	else if (has_ext (path, ".wsp") && sz >= 0x24)
	{
		// Ubisoft Imagine sprite (inside its LZSS wrapper): LE {GX format 4 / 5 / 6, w, h}, 0xcc padding to 0x20, tiles for w, h rounded up to 4
		g->gx = gxw_le32 (raw), g->rw = gxw_le32 (raw + 4), g->rh = gxw_le32 (raw + 8), g->doff = 0x20, g->levels = 1;
		g->w = (g->rw + 3) & ~3u, g->h = (g->rh + 3) & ~3u;
		for (uint i = 12; i < 0x20; i++)
			if (raw[i] != 0xcc)
				return false;
		if (g->gx < 4 || g->gx > 6 || !g->rw || !g->rh || g->rw > 4096 || g->rh > 4096)
			return false;
	}
	else if (has_ext (path, ".msk") && sz >= 0x19)
	{
		g->w = gxw_le32 (raw), g->h = gxw_le32 (raw + 4);
		for (uint i = 8; i < 24; i++)
			if (raw[i] != 0xcc)
				return false;
		if (!g->w || !g->h || g->w > 8192 || g->h > 8192 || (u64)g->w * g->h != sz - 24)
			return false;
		g->msk = true, g->doff = 24;
	}
	else if (has_ext (path, ".tan") && sz >= 0x230 && gxw_name[0])
	{
		const uint nf = gxw_be32 (raw), w = gxw_be32 (raw + 4), h = gxw_be32 (raw + 8);
		const uint fr = !strncmp (gxw_name, "frame_", 6) ? (uint)strtoul (gxw_name + 6, 0, 10) : ~0u;
		if (!nf || nf > 1024 || gxw_be32 (raw + 12) || !w || !h || w > 2048 || h > 2048 || (w & 7) || (h & 3)
			|| sz != 0x220 + (size_t)nf * w * h || fr >= nf)
			return false;
		g->tan = true, g->w = w, g->h = h, g->doff = 0x220 + (size_t)fr * w * h, g->clut = 16;
	}
	else if (has_ext (path, ".txm") && sz >= 0x30 && gxw_be32 (raw) == 0x20000 && gxw_name[0])
	{
		// Tales of: BE directory, 0x1c byte entries from 0x14 {flags, w, h, mips, GX format, name offset, data offset}; match by name
		const uint count = gxw_be32 (raw + 12);
		char want[64];
		snprintf (want, sizeof (want), "%s", gxw_name);
		const size_t wl = strlen (want);
		if (wl > 4 && !strcasecmp (want + wl - 4, ".tpl"))
			want[wl - 4] = 0;
		if (!count || count > 4096 || 0x14 + (size_t)count * 0x1c > sz)
			return false;
		for (uint i = 0; i < count && !g->w; i++)
		{
			const u8 *e = raw + 0x14 + (size_t)i * 0x1c;
			char nm[64];
			uint k = 0;
			const size_t np = 0x14 + (size_t)i * 0x1c + 20 + gxw_be32 (e + 20);
			if (np < sz)
				for (; k < 60 && np + k < sz && raw[np + k] >= 0x20 && raw[np + k] < 0x7f; k++)
					nm[k] = raw[np + k] == '/' || raw[np + k] == '\\' ? '_' : raw[np + k];
			if (!k)
				k = (uint)snprintf (nm, sizeof (nm), "tex_%03u", i);
			else
				nm[k] = 0;
			if (strcmp (nm, want))
				continue;
			const uint fmt = gxw_be32 (e + 16) & 0xffff;
			if (!gx_ok (fmt) || fmt > 14)
				return false;
			g->txm = true, g->gx = fmt, g->w = gxw_be32 (e + 4), g->h = gxw_be32 (e + 8), g->doff = gxw_be32 (e + 24), g->levels = 1;
			g->avail = level_size (fmt, g->w, g->h);
			if (!g->w || !g->h || g->w > 4096 || g->h > 4096)
				return false;
		}
		if (!g->w)
			return false;
		return true;
	}
	else if ((has_ext (path, ".rlt") || has_ext (path, ".res") || has_ext (path, ".dmn")) && sz > 0x40 && gxw_name[0])
	{
		// Next Level Games texture banks: PTLG ".rlt" or 0x20-based ".res" / ".dmn"; entries {hash, offset, size}; "<file>.<hash>.png"
		ccp hs = strrchr (gxw_name, '.');
		const u32 hash = (u32)strtoul (hs ? hs + 1 : gxw_name, 0, 16);
		const bool ptlg = !memcmp (raw, "PTLG", 4);
		if (!ptlg && gxw_be32 (raw) != 0x20)
			return false;
		const uint n = gxw_be32 (raw + 4);
		const size_t hd = ptlg ? 0x10 : 0x20, es = ptlg ? 16 : 12, base = ptlg ? hd + (size_t)n * 16 : 0;
		if (!n || n >= 0x10000 || hd + (size_t)n * es > sz)
			return false;
		for (uint i = 0; i < n && !g->w; i++)
		{
			const u8 *e = raw + hd + (size_t)i * es;
			if (gxw_be32 (e) != hash)
				continue;
			const size_t off = ptlg ? base + gxw_be32 (e + 4) : (size_t)gxw_be32 (e + 4) * 0x20, size = gxw_be32 (e + 8) & 0x0fffffff;
			if (off > sz || size > sz - off || size < 0x40)
				continue;
			const u8 *p = raw + off;
			const uint mips = gxw_be32 (p), kind = gxw_be32 (p + 4);
			if (mips < 1 || mips > 12 || kind < 1 || kind > 3)
				continue;
			g->gx = kind == 2 ? 14 : kind == 1 ? 5 : 6, g->w = gxw_be16 (p + 0xe), g->h = gxw_be16 (p + 0x10), g->levels = mips;
			g->doff = off + 0x20, g->avail = size - 0x20;
		}
		if (!g->w)
			return false;
	}
	else if ((has_ext (path, ".idd") || has_ext (path, ".eff")) && sz >= 0x30 && (!memcmp (raw, "IDD", 4) || !memcmp (raw, "EFF", 4)))
	{
		// MadWorld: section table, "TEX" section with {texture record, palette header} pairs; only direct colour formats are re-encoded
		size_t tex = 0;
		for (uint k = 0; k < 32 && 8 + 8 * (size_t)k + 8 <= sz; k++)
		{
			const size_t off = gxw_be32 (raw + 12 + 8 * k);
			if (gxw_be32 (raw + 8 + 8 * k) != k || off < 0x10 || off + 0x20 > sz)
				break;
			if (!tex && !memcmp (raw + off, "TEX", 4))
				tex = off;
		}
		if (!tex)
			return false;
		const size_t B = tex + gxw_be32 (raw + tex + 12);
		if (B + 0xc > sz)
			return false;
		const uint n = gxw_be32 (raw + B + 4), tab = gxw_be32 (raw + B + 8);
		if (!n || n > 512 || B + tab + 8 * (size_t)n > sz || gxw_idx >= n)
			return false;
		const size_t rec = gxw_be32 (raw + B + tab + 8 * gxw_idx);
		if (B + rec + 0x10 > sz)
			return false;
		g->w = gxw_be16 (raw + B + rec), g->h = gxw_be16 (raw + B + rec + 2), g->gx = gxw_be32 (raw + B + rec + 4);
		g->doff = B + gxw_be32 (raw + B + rec + 8), g->levels = 1;
		if (g->gx == 8 || g->gx == 9 || g->gx == 10 || g->w > 2048 || g->h > 2048)
			return false;
	}
	else if (has_ext (path, ".tgs") && sz >= 0x60 && !memcmp (raw, "tgs\0", 4))
	{
		if (!locate_tpl (raw, sz, gxw_be32 (raw + 0x20), g))
			return false;
	}
	else if (has_ext (path, ".pix") && sz >= 0x100 && !memcmp (raw, "FPIX", 4))
	{
		size_t o = 0;
		for (size_t q = 0x10; q + 0x20 < sz && q < 0x400 && !o; q += 4)
			if (gxw_be32 (raw + q) == 0x0020af30)
				o = q;
		if (!o || !locate_tpl (raw, sz, o, g))
			return false;
	}
	else
		return false;

	if (!g->avail || g->avail > sz - g->doff)
		g->avail = g->doff < sz ? sz - g->doff : 0;
	return g->gx < 15 && gx_ok (g->gx) && g->w && g->h && g->w <= 4096 && g->h <= 4096
		&& g->doff < sz && level_size (g->gx, g->w, g->h) <= g->avail;
}

static u8 *load (ccp path, size_t *size)
{
	*size = 0;
	FILE *f = fopen (path, "rb");
	if (!f)
		return 0;
	u8 *buf = 0;
	if (!fseeko (f, 0, SEEK_END))
	{
		const off_t n = ftello (f);
		if (n > 0 && n <= 0x8000000 && !fseeko (f, 0, SEEK_SET) && (buf = MALLOC ((size_t)n)))
		{
			if (fread (buf, 1, (size_t)n, f) == (size_t)n)
				*size = (size_t)n;
			else
				FREE (buf), buf = 0;
		}
	}
	fclose (f);
	return buf;
}

// Rhythm Heaven Groove ".tex" (RGBA8 only), plain or zstd packed: returns the plain surface + trailer (MALLOC'd) or 0
static u8 *rhtex_open (ccp path, const u8 *raw, size_t sz, size_t *pn, bool *zst)
{
	if (!has_ext (path, ".tex") || sz < 4)
		return 0;
	u8 *plain = 0;
	uint n = 0;
	*zst = !memcmp (raw, "\x28\xb5\x2f\xfd", 4);
	if (*zst)
	{
		if (DecodeZSTD (&plain, &n, raw, (uint)sz))
			return 0;
	}
	else if ((plain = MALLOC (sz)) != 0)
		memcpy (plain, raw, sz), n = (uint)sz;
	if (plain && IsRHGrooveTex (plain, n) && plain[n - 0x1000] == 99)
	{
		*pn = n;
		return plain;
	}
	FREE (plain);
	return 0;
}

bool GXWrapIsFile (ccp path)
{
	size_t sz;
	u8 *raw = load (path, &sz);
	gxw_t g;
	size_t dsz;
	u8 *dec = raw ? gxw_refpack (raw, sz, &dsz) : 0;
	u8 *tp = raw && !dec && has_ext (path, ".tplc") ? tplc_open (raw, sz, &dsz, &g) : 0;
	size_t rn;
	bool rz;
	u8 *rh = raw ? rhtex_open (path, raw, sz, &rn, &rz) : 0;
	const bool ok = raw && (rh || tp || (dec ? locate_shpg (dec, dsz, &g) : locate (raw, sz, path, &g)));
	FREE (raw);
	FREE (dec);
	FREE (tp);
	FREE (rh);
	return ok;
}

// encode the base level and then every box-filtered level that still fits into avail bytes
static void encode_chain (const gxw_t *g, const u8 *rgba, u8 *out, size_t avail)
{
	uint w = g->w, h = g->h;
	u8 *cur = MALLOC ((size_t)w * h * 4);
	if (!cur)
		return;
	memcpy (cur, rgba, (size_t)w * h * 4);
	for (uint level = 0; !g->levels || level < g->levels; level++)
	{
		const size_t n = level_size (g->gx, w, h);
		if (n > avail)
			break;
		GXEncodeRGBA (g->gx, w, h, cur, out);
		out += n, avail -= n;
		if (w == 1 && h == 1)
			break;
		const uint nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
		u8 *next = MALLOC ((size_t)nw * nh * 4);
		if (!next)
			break;
		for (uint y = 0; y < nh; y++)
			for (uint x = 0; x < nw; x++)
				for (uint c = 0; c < 4; c++)
				{
					uint sum = 0, cnt = 0;
					for (uint dy = 0; dy < (h > 1 ? 2u : 1u); dy++)
						for (uint dx = 0; dx < (w > 1 ? 2u : 1u); dx++, cnt++)
							sum += cur[((size_t)(y * 2 + dy) * w + x * 2 + dx) * 4 + c];
					next[((size_t)y * nw + x) * 4 + c] = (u8)((sum + cnt / 2) / cnt);
				}
		FREE (cur);
		cur = next, w = nw, h = nh;
	}
	FREE (cur);
}

static enumError store (ccp path, const u8 *data, size_t size)
{
	char tmp[PATH_MAX];
	snprintf (tmp, sizeof (tmp), "%s.gxw-tmp", path);
	FILE *f = fopen (tmp, "wb");
	if (!f)
		return ERR_CANT_CREATE;
	const bool bad = fwrite (data, 1, size, f) != size;
	if (fclose (f) || bad || rename (tmp, path))
	{
		unlink (tmp);
		return ERR_WRITE_FAILED;
	}
	return ERR_OK;
}

static u8 tim2_alpha (uint a, uint bits) { return bits == 1 ? (a >= 128) : a >= 128 ? 0x80 : a / 2; }

// direct colour types are re-encoded; indexed types pick the nearest entry of the file's own CLUT
static enumError reinject_tim2 (const Image_t *img, u8 *raw, const gxw_t *g)
{
	u8 *pix = raw + g->doff;
	for (uint y = 0; y < g->h; y++)
		for (uint x = 0; x < g->w; x++)
		{
			const uint i = y * g->w + x;
			const u8 *p = img->data + 4 * ((size_t)y * img->xwidth + x);
			if (g->itype < 4)
			{
				u8 *o = pix + (size_t)i * g->bpp / 8;
				if (g->bpp == 16)
				{
					const uint v = (p[0] * 31 + 127) / 255 | (p[1] * 31 + 127) / 255 << 5
						| (p[2] * 31 + 127) / 255 << 10 | tim2_alpha (p[3], 1) << 15;
					o[0] = v, o[1] = v >> 8;
				}
				else
				{
					o[0] = p[0], o[1] = p[1], o[2] = p[2];
					if (g->bpp == 32)
						o[3] = tim2_alpha (p[3], 8);
				}
				continue;
			}
			uint best = 0, best_d = ~0u;
			const uint cb = g->bpp / 8;
			for (uint c = 0; c < g->ncol && best_d; c++)
			{
				const u8 *e = raw + g->clut + (size_t)c * g->bpp / 8; // physical CLUT position
				u8 col[4] = { 0, 0, 0, 255 };
				if (g->bpp == 16)
				{
					const uint v = e[0] | e[1] << 8;
					col[0] = (v & 31) * 255 / 31, col[1] = (v >> 5 & 31) * 255 / 31;
					col[2] = (v >> 10 & 31) * 255 / 31, col[3] = v & 0x8000 ? 255 : 0;
				}
				else
				{
					col[0] = e[0], col[1] = e[1], col[2] = e[2];
					if (cb == 4)
						col[3] = e[3] >= 0x80 ? 255 : e[3] * 2;
				}
				uint d = 0;
				for (uint k = 0; k < 4; k++)
					d += (uint)((int)col[k] - (int)p[k]) * (uint)((int)col[k] - (int)p[k]);
				if (d < best_d)
					best_d = d, best = c;
			}
			// 8 bit CLUTs are block swizzled (swapping bits 3 and 4 is its own inverse)
			uint store_idx = best;
			if (g->itype == 5 && !(g->ctype & 0x80) && g->ncol >= 256)
				store_idx = (best & 0x18) == 8 || (best & 0x18) == 0x10 ? best ^ 0x18 : best;
			if (g->itype == 4)
				pix[i >> 1] = (u8)((pix[i >> 1] & (i & 1 ? 0x0f : 0xf0)) | store_idx << ((i & 1) * 4));
			else
				pix[i] = (u8)store_idx;
		}
	return ERR_OK;
}

// index 0 is the transparent key; other pixels take the nearest palette colour
static void reinject_dst1 (const Image_t *img, u8 *raw, const gxw_t *g)
{
	u8 pal[256][3];
	for (uint c = 0; c < g->ncol; c++)
	{
		const uint v = gxw_le16 (raw + g->clut + 2 * c);
		pal[c][0] = (v & 31) * 255 / 31, pal[c][1] = (v >> 5 & 31) * 255 / 31, pal[c][2] = (v >> 10 & 31) * 255 / 31;
	}
	u8 *pix = raw + g->doff;
	memset (pix, 0, (size_t)g->w * g->h * g->bpp / 8);
	for (uint y = 0; y < g->h; y++)
		for (uint x = 0; x < g->w; x++)
		{
			const u8 *p = img->data + 4 * ((size_t)y * img->xwidth + x);
			uint best = 0, best_d = ~0u;
			for (uint c = g->itype && g->bpp == 8 ? 0 : 1; (p[3] >= 128 || (g->itype && g->bpp == 8)) && c < g->ncol; c++)
			{
				uint d = 0;
				for (uint k = 0; k < 3; k++)
					d += (uint)(((int)pal[c][k] - p[k]) * ((int)pal[c][k] - p[k]));
				if (d < best_d)
					best_d = d, best = c;
			}
			const size_t bit = ((size_t)y * g->w + x) * g->bpp;
			pix[bit / 8] |= (u8)(best << (bit & 7));
		}
}

static uint nbf_c555 (const u8 *p) { uint c = gxw_le16 (p); return (c & 31) | (c >> 5 & 31) << 8 | (c >> 10 & 31) << 16; }

// Mirror of the decoder (extract/nbfraw.inc): every map cell (or tile, without a map) owns 8x8 pixels of the
// sheet; each pixel becomes the nearest colour of its palette bank. Tiles shared by several cells take the last one.
static enumError reinject_nbfc (const Image_t *img, u8 *raw, size_t sz, ccp path)
{
	const bool ch = has_ext (path, ".char");
	size_t psz = 0, msz = 0;
	u8 *pal = load_sibling (path, ch ? ".pal" : ".nbfp", &psz);
	u8 *map = load_sibling (path, ch ? ".scr" : ".nbfs", &msz);
	enumError e = ERR_OK;
	if (!pal)
		return ERR_NOTHING_TO_DO;
	if (map && msz >= 4 && !memcmp (map, "VSCR", 4))
		FREE (map), map = 0, msz = 0;
	const uint ncol = (uint)psz / 2, nent = (uint)msz / 2;
	bool banks = false;
	for (uint i = 0; i < nent; i++)
		banks |= gxw_le16 (map + 2 * i) >> 12 != 0;
	const uint bpp = ncol <= 16 || banks ? 4 : 8, tb = bpp * 8;
	uint wt, ht;
	if (nent >= 16 && nent <= 4096)
	{
		wt = nent % 32 == 0 ? 32 : nent % 16 == 0 ? 16 : 0;
		ht = wt ? nent / wt : 0;
	}
	else
		FREE (map), map = 0, wt = 32, ht = ((uint)(sz / tb) + 31) / 32;
	if (!ht || ht > 128)
		e = ERR_NOTHING_TO_DO;
	else if (img->width != wt * 8 || img->height != ht * 8)
		e = ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the tile sheet is %ux%u: %s\n", img->width,
			img->height, wt * 8, ht * 8, path);
	for (uint i = 0; !e && i < wt * ht; i++)
	{
		const uint en = map ? gxw_le16 (map + 2 * i) : i, t = map ? en & 0x3ff : i, hf = map ? en >> 10 & 1 : 0,
			vf = map ? en >> 11 & 1 : 0, bk = map ? en >> 12 : 0;
		for (uint y = 0; y < 8; y++)
			for (uint x = 0; x < 8; x++)
			{
				const uint sy = vf ? 7 - y : y, sx = hf ? 7 - x : x;
				const size_t o = (size_t)t * tb + (bpp == 4 ? sy * 4 + sx / 2 : sy * 8 + sx);
				if (o >= sz)
					continue;
				const u8 *p = img->data + 4 * ((size_t)((i / wt) * 8 + y) * img->xwidth + (i % wt) * 8 + x);
				uint best = 0, best_d = ~0u;
				for (uint v = 0; v < (bpp == 4 ? 16u : 256u); v++)
				{
					uint ci = v + (bpp == 4 ? bk * 16 : 0);
					if (ci >= ncol)
						ci = v;
					if (ci >= ncol)
						continue;
					const uint c = nbf_c555 (pal + 2 * ci);
					uint d = 0;
					for (uint k = 0; k < 3; k++)
					{
						const int dv = (int)((c >> (8 * k) & 31) * 255 / 31) - p[k];
						d += (uint)(dv * dv);
					}
					if (d < best_d)
						best_d = d, best = v;
				}
				if (bpp == 4)
					raw[o] = (u8)(sx & 1 ? (raw[o] & 15) | best << 4 : (raw[o] & 0xf0) | best);
				else
					raw[o] = (u8)best;
			}
	}
	FREE (pal);
	FREE (map);
	return e;
}

// Tile 0 and every transparent pixel use index 0; 4 bit tiles are drawn with palette bank 0, 8 bit tiles with the whole palette.
// VNKT is rebuilt around a fresh LZ10 stream, TKLT is patched in place; either way the palette file stays as it is.
static enumError reinject_tklt (const Image_t *img, const u8 *raw, size_t sz, ccp path, u8 **out, size_t *outn)
{
	const bool lz = !memcmp (raw, "VNKT", 4);
	const size_t n4 = gxw_le16 (raw + 4), n8 = gxw_le16 (raw + 6), pl = gxw_le32 (raw + 0xd), data = 0x11 + pl, bytes = n4 * 32 + n8 * 64;
	ccp r = !strncmp (path, "2d/", 3) ? path - 1 : strstr (path, "/2d/");
	if (!r)
		return ERR_NOTHING_TO_DO;
	char pp[PATH_MAX];
	snprintf (pp, sizeof (pp), "%.*s%s%.*s", r < path ? 0 : (int)(r - path), path, r < path ? "" : "/", (int)pl, (ccp)raw + 0x11);
	size_t psz;
	u8 *pal = load (pp, &psz);
	if (!pal || psz < 10 || memcmp (pal, "TTLP", 4))
	{
		FREE (pal);
		return ERR_NOTHING_TO_DO;
	}
	uint ncol = gxw_le16 (pal + 4) / 2;
	if (ncol > (psz - 8) / 2)
		ncol = (uint)(psz - 8) / 2;
	u8 *t = CALLOC (1, bytes);
	if (!t)
		return FREE (pal), ERR_OUT_OF_MEMORY;
	for (size_t i = 0; i < n4 + n8; i++)
	{
		const bool b8 = i >= n4;
		u8 *tile = b8 ? t + n4 * 32 + (i - n4) * 64 : t + i * 32;
		for (uint y = 0; y < 8; y++)
			for (uint x = 0; x < 8; x++)
			{
				const u8 *p = img->data + 4 * ((size_t)((i / 16) * 8 + y) * img->xwidth + (i % 16) * 8 + x);
				uint best = 0, best_d = ~0u;
				for (uint c = 1; p[3] >= 128 && c < (b8 ? 256u : 16u) && c < ncol; c++)
				{
					const uint v = gxw_le16 (pal + 8 + 2 * c);
					uint d = 0;
					for (uint k = 0; k < 3; k++)
					{
						const int dv = (int)((v >> (5 * k) & 31) * 255 / 31) - p[k];
						d += (uint)(dv * dv);
					}
					if (d < best_d)
						best_d = d, best = c;
				}
				if (b8)
					tile[y * 8 + x] = (u8)best;
				else
					tile[y * 4 + x / 2] |= (u8)(x & 1 ? best << 4 : best);
			}
	}
	FREE (pal);
	enumError e = ERR_OK;
	if (!lz)
	{
		*out = MALLOC (sz), *outn = sz;
		if (*out)
			memcpy (*out, raw, data), memcpy (*out + data, t, bytes);
		else
			e = ERR_OUT_OF_MEMORY;
	}
	else
	{
		u8 *z = 0;
		uint zn = 0;
		e = EncodeLZ10LZ11 (&z, &zn, t, (uint)bytes, false);
		if (!e && (*out = MALLOC (data + 4 + zn)))
		{
			memcpy (*out, raw, data);
			memcpy (*out + data, &(u8[4]){ (u8)zn, (u8)(zn >> 8), (u8)(zn >> 16), (u8)(zn >> 24) }, 4);
			memcpy (*out + data + 4, z, zn);
			*outn = data + 4 + zn;
		}
		else if (!e)
			e = ERR_OUT_OF_MEMORY;
		FREE (z);
	}
	FREE (t);
	return e;
}

static void reinject_ivt (const Image_t *img, u8 *raw, const gxw_t *g)
{
	const u8 *key = ivt_key[g->ncol];
	const uint kl = ivt_klen[g->ncol];
	for (size_t i = 0; i < (size_t)g->w * g->h; i++)
	{
		const u8 *p = img->data + 4 * ((i / g->w) * img->xwidth + i % g->w);
		const u8 lum = (u8)((p[0] + p[1] + p[2]) / 3);
		const u8 v[4] = { g->bpp < 3 ? lum : p[2], g->bpp == 2 ? p[3] : p[1], p[0], p[3] };
		for (uint c = 0; c < g->bpp; c++)
			raw[g->doff + i * g->bpp + c] = v[c] ^ key[(i * g->bpp + c) % kl];
	}
}

static void reinject_apx (const Image_t *img, u8 *raw, const gxw_t *g)
{
	for (size_t i = 0; i < (size_t)g->w * g->h; i++)
		memcpy (raw + g->doff + 3 * i, img->data + 4 * ((i / g->w) * img->xwidth + i % g->w), 3);
}

static enumError reinject_imy (const Image_t *img, const u8 *raw, const gxw_t *g, u8 **out, size_t *outn)
{
	const uint bpp = g->bpp, tw = bpp == 32 ? 4 : 8, th = bpp == 4 ? 8 : 4, stride = gxw_le16 (raw + 8);
	const size_t total = (size_t)stride * g->h, hd = g->doff;
	u8 pal[256][4];
	for (uint i = 0; i < g->ncol; i++)
	{
		const uint v = raw[0x20 + 2 * i] << 8 | raw[0x21 + 2 * i];
		if (!(g->itype & 0x60))
			pal[i][0] = (v >> 11 & 31) * 255 / 31, pal[i][1] = (v >> 5 & 63) * 255 / 63, pal[i][2] = (v & 31) * 255 / 31, pal[i][3] = 255;
		else if (v & 0x8000)
			pal[i][0] = (v >> 10 & 31) * 255 / 31, pal[i][1] = (v >> 5 & 31) * 255 / 31, pal[i][2] = (v & 31) * 255 / 31, pal[i][3] = 255;
		else
			pal[i][0] = (v >> 8 & 15) * 17, pal[i][1] = (v >> 4 & 15) * 17, pal[i][2] = (v & 15) * 17, pal[i][3] = (v >> 12 & 7) * 255 / 7;
	}
	const uint pw = (g->w + tw - 1) / tw * tw, ph = (g->h + th - 1) / th * th;
	u8 *buf = CALLOC (1, (size_t)pw * ph * bpp / 8 + total + 64);
	if (!buf)
		return ERR_OUT_OF_MEMORY;
	u8 *s = buf;
	for (uint ty = 0; ty < ph; ty += th)
		for (uint tx = 0; tx < pw; tx += tw)
		{
			if (bpp == 32)
			{
				for (uint k = 0; k < 16; k++)
				{
					const uint x = tx + k % 4, y = ty + k / 4;
					const u8 *p = x < g->w && y < g->h ? img->data + 4 * ((size_t)y * img->xwidth + x) : (const u8[4]){ 0, 0, 0, 0 };
					s[2 * k] = p[3], s[2 * k + 1] = p[0], s[32 + 2 * k] = p[1], s[33 + 2 * k] = p[2];
				}
				s += 64;
				continue;
			}
			for (uint y = 0; y < th; y++)
				for (uint x = 0; x < tw; x++)
				{
					uint best = 0, best_d = ~0u;
					if (tx + x < g->w && ty + y < g->h)
					{
						const u8 *p = img->data + 4 * ((size_t)(ty + y) * img->xwidth + tx + x);
						for (uint c = 0; c < g->ncol; c++)
						{
							uint d = 0;
							for (uint k = 0; k < 4; k++)
								d += (uint)(((int)pal[c][k] - p[k]) * ((int)pal[c][k] - p[k]));
							if (d < best_d)
								best_d = d, best = c;
						}
					}
					if (bpp == 8)
						*s++ = (u8)best;
					else
					{
						*s = (u8)(x & 1 ? (*s & 0xf0) | best : best << 4);
						s += x & 1;
					}
				}
		}
	*outn = hd + total;
	*out = MALLOC (*outn);
	if (!*out)
		return FREE (buf), ERR_OUT_OF_MEMORY;
	memcpy (*out, raw, hd);
	(*out)[10] |= 1; // stored uncompressed
	memcpy (*out + hd, buf, total);
	FREE (buf);
	return ERR_OK;
}

// the LZ11 packed variant (byte 6 == 0x10) is packed again: {u32 with the low 24 bits = stream length, LZ11 stream}
static enumError reinject_ltex (const Image_t *img, const u8 *raw, size_t sz, const gxw_t *g, u8 **out, size_t *outn)
{
	const size_t n = (size_t)g->w * g->h * g->bpp;
	u8 *pix = MALLOC (n);
	if (!pix)
		return ERR_OUT_OF_MEMORY;
	for (size_t i = 0; i < (size_t)g->w * g->h; i++)
	{
		const u8 *p = img->data + 4 * ((i / g->w) * img->xwidth + i % g->w);
		if (g->bpp == 4)
			memcpy (pix + 4 * i, p, 4);
		else
		{
			const uint v = (p[0] >> 3) << 11 | (p[1] >> 2) << 5 | p[2] >> 3;
			pix[2 * i] = (u8)v, pix[2 * i + 1] = (u8)(v >> 8);
		}
	}
	u8 *z = 0;
	uint zn = 0;
	enumError e = ERR_OK;
	if (raw[6] == 0x10)
		e = EncodeLZ10LZ11 (&z, &zn, pix, (uint)n, true);
	const size_t body = raw[6] == 0x10 ? 4 + (size_t)zn : n;
	if (!e && (*out = MALLOC (0x18 + body)))
	{
		memcpy (*out, raw, 0x18);
		if (raw[6] == 0x10)
		{
			const uint hv = (sz > 0x1c ? raw[0x1b] : 0) << 24 | zn;
			(*out)[0x18] = (u8)hv, (*out)[0x19] = (u8)(hv >> 8), (*out)[0x1a] = (u8)(hv >> 16), (*out)[0x1b] = (u8)(hv >> 24);
			memcpy (*out + 0x1c, z, zn);
		}
		else
			memcpy (*out + 0x18, pix, n);
		*outn = 0x18 + body;
	}
	else if (!e)
		e = ERR_OUT_OF_MEMORY;
	FREE (z);
	FREE (pix);
	return e;
}

// 4x4 block texture: per block two endpoint colours (mode 3, or mode 1 with a transparent texel) or up to four exact colours (mode 2),
// written as a fresh ".ntfp" (block palettes, shared when identical) and ".ntfi"
static enumError reinject_ntft4 (const Image_t *img, u8 *raw, size_t sz, ccp path)
{
	const uint w = img->width, h = img->height;
	if (!w || !h || (w | h) & 3 || (size_t)w * h != sz * 4)
		return ERROR0 (ERR_INVALID_IFORM, "PNG has %ux%u pixels but the texture holds %zu: %s\n", w, h, sz * 4, path);
	const size_t nb = sz / 4;
	u16 *pal = MALLOC (nb * 8 + 8);
	u8 *idx = MALLOC (nb * 2 + 2);
	if (!pal || !idx)
		return FREE (pal), FREE (idx), ERR_OUT_OF_MEMORY;
	size_t np = 0;
#define C5(c, k) ((c) >> (5 * (k)) & 31)
	for (size_t b = 0; b < nb; b++)
	{
		const uint bx = (uint)(b % (w / 4)) * 4, by = (uint)(b / (w / 4)) * 4;
		uint col[16], opq[16], nopq = 0, ntr = 0;
		for (uint k = 0; k < 16; k++)
		{
			const u8 *p = img->data + 4 * ((size_t)(by + k / 4) * img->xwidth + bx + k % 4);
			col[k] = (p[0] >> 3) | (p[1] >> 3) << 5 | (p[2] >> 3) << 10;
			if (p[3] >= 128)
				opq[nopq++] = k;
			else
				ntr++, col[k] = 0x10000;
		}
		uint c[4] = { 0, 0, 0, 0 }, mode = 3, nd = 0;
		for (uint i = 0; i < nopq; i++)
		{
			uint j = 0;
			while (j < nd && c[j] != col[opq[i]])
				j++;
			if (j == nd && nd < 4)
				c[nd++] = col[opq[i]];
			else if (j == nd)
				nd = 5; // too many colours
		}
		uint e0 = 0, e1 = 0;
		if (nd > 4 || (nd > 3 && ntr))
		{
			uint lo = ~0u, hi = 0;
			for (uint i = 0; i < nopq; i++)
			{
				const uint v = col[opq[i]], l = 2 * C5 (v, 0) + 5 * C5 (v, 1) + C5 (v, 2);
				if (l < lo)
					lo = l, e0 = v;
				if (l >= hi)
					hi = l, e1 = v;
			}
			mode = ntr ? 1 : 3;
			c[0] = e0, c[1] = e1;
			nd = 2;
		}
		else if (ntr)
			mode = nd <= 3 ? 0 : 1;
		else
			mode = 2;
		if (mode == 1 && nd < 2)
			mode = 0;
		// palette entries as the decoder will see them
		uint pc[4] = { c[0], c[1], c[2], c[3] }, ca[4] = { 1, 1, 1, 1 };
		const uint r0 = C5 (c[0], 0), g0 = C5 (c[0], 1), b0 = C5 (c[0], 2), r1 = C5 (c[1], 0), g1 = C5 (c[1], 1), b1 = C5 (c[1], 2);
		if (mode == 0)
			pc[3] = 0, ca[3] = 0;
		else if (mode == 1)
			pc[2] = (r0 + r1) / 2 | (g0 + g1) / 2 << 5 | (b0 + b1) / 2 << 10, pc[3] = 0, ca[3] = 0;
		else if (mode == 3)
		{
			pc[2] = (r0 * 5 + r1 * 3) / 8 | (g0 * 5 + g1 * 3) / 8 << 5 | (b0 * 5 + b1 * 3) / 8 << 10;
			pc[3] = (r0 * 3 + r1 * 5) / 8 | (g0 * 3 + g1 * 5) / 8 << 5 | (b0 * 3 + b1 * 5) / 8 << 10;
		}
		u32 bits = 0;
		for (uint k = 0; k < 16; k++)
		{
			uint t = 3;
			if (col[k] < 0x10000)
			{
				uint bd = ~0u;
				for (uint j = 0; j < 4; j++)
				{
					if (!ca[j] || ((mode == 0 || mode == 2) && j >= nd))
						continue;
					uint d = 0;
					for (uint q = 0; q < 3; q++)
					{
						const int dv = (int)C5 (pc[j], q) - (int)C5 (col[k], q);
						d += (uint)(dv * dv);
					}
					if (d < bd)
						bd = d, t = j;
				}
			}
			bits |= (u32)t << (2 * k);
		}
		raw[4 * b] = (u8)bits, raw[4 * b + 1] = (u8)(bits >> 8), raw[4 * b + 2] = (u8)(bits >> 16), raw[4 * b + 3] = (u8)(bits >> 24);
		// reuse an identical entry group (words of two colours)
		const uint nw = mode == 0 ? 3 : mode == 2 ? 4 : 2;
		size_t at = (size_t)-1;
		for (size_t q = 0; q + nw <= np && at == (size_t)-1; q += 2)
		{
			uint j = 0;
			while (j < nw && pal[q + j] == c[j])
				j++;
			if (j == nw)
				at = q;
		}
		if (at == (size_t)-1)
		{
			at = np;
			for (uint j = 0; j < 4; j++)
				pal[np + j] = j < nw ? (u16)c[j] : 0;
			np += 4;
		}
		if (at / 2 > 0x3fff)
		{
			FREE (pal), FREE (idx);
			return ERROR0 (ERR_INVALID_IFORM, "Too many distinct block colours for a 4x4 texture: %s\n", path);
		}
		const uint iv = (uint)(at / 2) | mode << 14;
		idx[2 * b] = (u8)iv, idx[2 * b + 1] = (u8)(iv >> 8);
	}
#undef C5
	u8 *pb = MALLOC (np * 2 + 2);
	char sib[PATH_MAX];
	enumError e = pb ? ERR_OK : ERR_OUT_OF_MEMORY;
	for (size_t i = 0; i < np && pb; i++)
		pb[2 * i] = (u8)pal[i], pb[2 * i + 1] = (u8)(pal[i] >> 8);
	if (!e)
	{
		snprintf (sib, sizeof (sib), "%.*s.ntfp", (int)strlen (path) - 5, path);
		e = store (sib, pb, np * 2);
	}
	if (!e)
	{
		snprintf (sib, sizeof (sib), "%.*s.ntfi", (int)strlen (path) - 5, path);
		e = store (sib, idx, nb * 2);
	}
	FREE (pb), FREE (pal), FREE (idx);
	return e;
}

static enumError reinject_ntft (const Image_t *img, u8 *raw, size_t sz, const gxw_t *g, ccp path)
{
	const uint f = g->itype;
	if (f == NTF_4X4)
		return reinject_ntft4 (img, raw, sz, path);
	const size_t px = f == NTF_DIRECT ? sz / 2 : f == NTF_I2 ? sz * 4 : f == NTF_I4 ? sz * 2 : sz;
	if ((size_t)img->width * img->height != px)
		return ERROR0 (ERR_INVALID_IFORM, "PNG has %ux%u pixels but the texture holds %zu: %s\n", img->width, img->height, px, path);
	size_t psz;
	u8 *pal = load_sibling (path, ".ntfp", &psz);
	if (!pal && f != NTF_DIRECT)
		return ERR_NOTHING_TO_DO;
	memset (raw, 0, sz);
	for (size_t i = 0; i < px; i++)
	{
		const u8 *p = img->data + 4 * ((i / img->width) * img->xwidth + i % img->width);
		if (f == NTF_DIRECT)
		{
			const uint v = (p[0] >> 3) | (p[1] >> 3) << 5 | (p[2] >> 3) << 10 | (p[3] >= 128 ? 0x8000u : 0);
			raw[2 * i] = (u8)v, raw[2 * i + 1] = (u8)(v >> 8);
			continue;
		}
		uint best = 0, best_d = ~0u;
		for (uint c = 0; c < (f == NTF_A5I3 ? 8u : f == NTF_A3I5 ? 32u : f == NTF_I2 ? 4u : f == NTF_I4 ? 16u : 256u) && c < g->ncol; c++)
		{
			const uint v = gxw_le16 (pal + 2 * c);
			uint d = 0;
			for (uint k = 0; k < 3; k++)
			{
				const int dv = (int)((v >> (5 * k) & 31) * 255 / 31) - p[k];
				d += (uint)(dv * dv);
			}
			if (d < best_d)
				best_d = d, best = c;
		}
		switch (f)
		{
		case NTF_I2: raw[i / 4] |= (u8)(best << (2 * (i & 3))); break;
		case NTF_I4: raw[i / 2] |= (u8)(best << (4 * (i & 1))); break;
		case NTF_A5I3: raw[i] = (u8)(best | ((p[3] * 31 + 127) / 255) << 3); break;
		case NTF_A3I5: raw[i] = (u8)(best | ((p[3] * 7 + 127) / 255) << 5); break;
		default: raw[i] = (u8)best; break;
		}
	}
	FREE (pal);
	return ERR_OK;
}

// Okumura LZSS as used by the Ubisoft Imagine wrapper (21 43 65 87, compressed size, size, stream): 4096 byte ring pre-filled with
// 0x20 and written from 4078, control bit 1 = literal, 0 = {pos & 0xff, pos >> 4 & 0xf0 | len - 3}; matches look at most 4095 bytes back
static u8 *lzss_pack (const u8 *d, size_t n, size_t *outn)
{
	u8 *out = MALLOC (12 + n + n / 8 + 16);
	int *head = MALLOC (65536 * sizeof (int)), *prev = MALLOC ((n + 1) * sizeof (int));
	if (!out || !head || !prev)
		return FREE (out), FREE (head), FREE (prev), (u8 *)0;
	memset (head, 0xff, 65536 * sizeof (int));
	size_t o = 12, ctl = 0, i = 0;
	uint bit = 8;
	while (i < n)
	{
		if (bit == 8)
			ctl = o++, out[ctl] = 0, bit = 0;
		uint best = 0, bpos = 0;
		if (i + 2 < n)
		{
			const uint hv = d[i] << 8 ^ d[i + 1] << 4 ^ d[i + 2];
			int c = head[hv & 0xffff];
			for (uint tries = 0; c >= 0 && i - (size_t)c < 4096 && tries < 64; tries++, c = prev[c])
			{
				uint l = 0;
				while (l < 18 && i + l < n && d[c + l] == d[i + l])
					l++;
				if (l > best)
					best = l, bpos = (uint)((4078 + c) & 4095);
				if (l == 18)
					break;
			}
		}
		const size_t step = best >= 3 ? best : 1;
		if (best >= 3)
			out[o++] = (u8)bpos, out[o++] = (u8)((bpos >> 4 & 0xf0) | (best - 3));
		else
			out[ctl] |= (u8)(1 << bit), out[o++] = d[i];
		bit++;
		for (size_t k = 0; k < step; k++, i++)
			if (i + 2 < n)
			{
				const uint hv = (d[i] << 8 ^ d[i + 1] << 4 ^ d[i + 2]) & 0xffff;
				prev[i] = head[hv], head[hv] = (int)i;
			}
	}
	FREE (head);
	FREE (prev);
	memcpy (out, "\x21\x43\x65\x87", 4);
	const size_t cs = o - 12;
	for (uint k = 0; k < 4; k++)
		out[4 + k] = (u8)(cs >> (8 * k)), out[8 + k] = (u8)(n >> (8 * k));
	*outn = o;
	return out;
}

// "foo.wsp.d/foo.wsp" is the unpacked copy of the wrapper "foo.wsp": when it is newer, pack it again into the wrapper
void GXWrapRepackLZSS (ccp root)
{
	const size_t rl = strlen (root);
	if (rl < 3 || strcmp (root + rl - 2, ".d"))
		return;
	char parent[PATH_MAX], inner[PATH_MAX];
	snprintf (parent, sizeof (parent), "%.*s", (int)(rl - 2), root);
	ccp bs = strrchr (parent, '/');
	snprintf (inner, sizeof (inner), "%s/%s", root, bs ? bs + 1 : parent);
	struct stat sp, si;
	if (stat (parent, &sp) || stat (inner, &si) || !S_ISREG (sp.st_mode) || !S_ISREG (si.st_mode) || si.st_mtime <= sp.st_mtime)
		return;
	size_t pn, in;
	u8 *p = load (parent, &pn), *d = load (inner, &in);
	if (p && d && pn >= 12 && !memcmp (p, "\x21\x43\x65\x87", 4))
	{
		size_t zn;
		u8 *z = lzss_pack (d, in, &zn);
		if (z && !store (parent, z, zn))
		{
			struct utimbuf t = { si.st_atime, si.st_mtime };
			utime (parent, &t);
		}
		FREE (z);
	}
	FREE (p);
	FREE (d);
}

static void reinject_msk (const Image_t *img, u8 *raw, const gxw_t *g)
{
	for (size_t i = 0; i < (size_t)g->w * g->h; i++)
	{
		const u8 *p = img->data + 4 * ((i / g->w) * img->xwidth + i % g->w);
		raw[g->doff + i] = (u8)((p[0] + p[1] + p[2]) / 3);
	}
}

// C8 frame in 8x4 tiles against the file's RGB565 palette (256 entries from 0x10)
static void reinject_tan (const Image_t *img, u8 *raw, const gxw_t *g)
{
	u8 pal[256][3];
	for (uint c = 0; c < 256; c++)
	{
		const uint v = gxw_be16 (raw + g->clut + 2 * c);
		pal[c][0] = (v >> 11 & 31) * 255 / 31, pal[c][1] = (v >> 5 & 63) * 255 / 63, pal[c][2] = (v & 31) * 255 / 31;
	}
	u8 *o = raw + g->doff;
	for (uint ty = 0; ty < g->h; ty += 4)
		for (uint tx = 0; tx < g->w; tx += 8)
			for (uint y = 0; y < 4; y++)
				for (uint x = 0; x < 8; x++)
				{
					const u8 *p = img->data + 4 * ((size_t)(ty + y) * img->xwidth + tx + x);
					uint best = 0, best_d = ~0u;
					for (uint c = 0; c < 256; c++)
					{
						uint d = 0;
						for (uint k = 0; k < 3; k++)
							d += (uint)(((int)pal[c][k] - p[k]) * ((int)pal[c][k] - p[k]));
						if (d < best_d)
							best_d = d, best = c;
					}
					*o++ = (u8)best;
				}
}

// "foo.GVRs.d/p.gvr" / "foo.gts.d/one.gvr": members of a texture set that were re-encoded in place (same size) are copied back into
// the container ("foo.GVRs" = Sonic Team set, "foo.gts" = Sega gts1)
void GXWrapRepackSet (ccp root)
{
	const size_t rl = strlen (root);
	if (rl < 3 || strcmp (root + rl - 2, ".d"))
		return;
	char parent[PATH_MAX];
	snprintf (parent, sizeof (parent), "%.*s", (int)(rl - 2), root);
	struct stat sp;
	if (stat (parent, &sp) || !S_ISREG (sp.st_mode))
		return;
	size_t sz;
	u8 *raw = load (parent, &sz);
	if (!raw)
		return;
	const bool gts = sz >= 0x50 && !memcmp (raw + 4, "gts1", 4), gvrs = has_ext (parent, ".gvrs") && sz >= 8;
	const uint count = gts ? gxw_be16 (raw + 8) : gvrs ? raw[0] << 8 | raw[1] : 0;
	bool changed = false;
	time_t newest = 0;
	size_t np = 4 + 4 * (size_t)count; // GVRs name table
	for (uint i = 0; i < count && i < 4096 && (gts || gvrs); i++)
	{
		size_t off, len, hdr;
		char base[0x41] = "", name[0x60], path[PATH_MAX];
		if (gts)
		{
			off = gxw_be32 (raw + 0xc + 4 * i);
			if (off == 0xffffffff)
				continue;
			if (off + 0x40 > sz)
				break;
			hdr = off + 0x40, len = gxw_be32 (raw + off);
			memcpy (base, raw + off + 4, 0x3c);
			base[0x3c] = 0;
		}
		else
		{
			off = gxw_be32 (raw + 4 + 4 * i);
			if (off + 0x20 > sz)
				break;
			hdr = off, len = 0x18 + (size_t)gxw_le32 (raw + off + 0x14);
			if (!(raw[2] << 8 | raw[3]))
			{
				size_t e = np;
				while (e < sz && raw[e])
					e++;
				memcpy (base, raw + np, e - np > 0x40 ? 0x40 : e - np);
				np = e + 1;
			}
		}
		if (hdr + len > sz)
			len = sz - hdr;
		for (char *p = base; *p; p++)
			if (*p == '/' || *p == '\\' || (u8)*p < 0x20)
				*p = '_';
		if (!*base)
			snprintf (base, sizeof (base), "tex%03u", i);
		snprintf (name, sizeof (name), "%s.gvr", base);
		snprintf (path, sizeof (path), "%s/%s", root, name);
		struct stat sm;
		size_t mn;
		if (stat (path, &sm) || sm.st_mtime <= sp.st_mtime)
			continue;
		u8 *m = load (path, &mn);
		if (m && mn == len)
			memcpy (raw + hdr, m, len), changed = true, newest = sm.st_mtime > newest ? sm.st_mtime : newest;
		FREE (m);
	}
	if (changed && !store (parent, raw, sz))
	{
		struct utimbuf t = { sp.st_atime, newest };
		utime (parent, &t);
	}
	FREE (raw);
}

// Opoona keeps its TPLs with a blanked magic word, alone or three at a time in a "TPLE" bundle; the unpacked copies in "foo.tpl.d" carry the
// real magic. A re-encoded copy of the same size goes back with everything but the original magic word.
void GXWrapRepackOpoona (ccp root)
{
	const size_t rl = strlen (root);
	if (rl < 7 || strcmp (root + rl - 6, ".tpl.d"))
		return;
	char parent[PATH_MAX];
	snprintf (parent, sizeof (parent), "%.*s", (int)(rl - 2), root);
	struct stat sp;
	size_t sz;
	u8 *raw = stat (parent, &sp) || !S_ISREG (sp.st_mode) ? 0 : load (parent, &sz);
	if (!raw)
		return;
	ccp bs = strrchr (parent, '/');
	ccp fn = bs ? bs + 1 : parent;
	const size_t fl = strlen (fn);
	bool changed = false;
	time_t newest = 0;
	const bool tple = sz > 0x40 && !memcmp (raw, "TPLE", 4);
	const uint cnt = tple ? gxw_be32 (raw + 0x10) : 1, stride = tple ? gxw_be32 (raw + 0x14) : (uint)sz, off = tple ? gxw_be32 (raw + 0x18) : 0;
	if (sz > 0x40 && (!tple || (cnt && cnt <= 64 && stride > 0x40 && off == 0x40 && 0x40 + (size_t)cnt * stride <= sz))
		&& (tple || (gxw_be32 (raw) < 4 && gxw_be32 (raw + 4) && gxw_be32 (raw + 4) < 0x100 && gxw_be32 (raw + 8) == 12)))
		for (uint i = 0; i < cnt; i++)
		{
			char path[PATH_MAX];
			if (tple)
				snprintf (path, sizeof (path), "%s/%.*s_%u.tpl", root, (int)(fl - 4), fn, i);
			else
				snprintf (path, sizeof (path), "%s/%s", root, fn);
			struct stat sm;
			size_t mn;
			if (stat (path, &sm) || sm.st_mtime <= sp.st_mtime)
				continue;
			u8 *m = load (path, &mn);
			u8 *dst = raw + off + (size_t)i * stride;
			if (m && mn == stride)
				memcpy (dst + 4, m + 4, stride - 4), changed = true, newest = sm.st_mtime > newest ? sm.st_mtime : newest;
			else if (m && mn > 0x30 && gxw_be32 (m + 4) == 1 && gxw_be32 (m + 8) == 12)
			{
				// the encoder lays a single image out compactly: move just the texels when the image header matches
				const uint mi = gxw_be32 (m + 12), pi = gxw_be32 (dst + 12);
				if (mi + 16 <= mn && pi + 16 <= stride && !memcmp (m + mi, dst + pi, 8))
				{
					const uint md = gxw_be32 (m + mi + 8), pd = gxw_be32 (dst + pi + 8);
					const size_t n = mn - md < stride - pd ? mn - md : stride - pd;
					if (md < mn && pd < stride)
						memcpy (dst + pd, m + md, n), changed = true, newest = sm.st_mtime > newest ? sm.st_mtime : newest;
				}
			}
			FREE (m);
		}
	if (changed && !store (parent, raw, sz))
	{
		struct utimbuf t = { sp.st_atime, newest };
		utime (parent, &t);
	}
	FREE (raw);
}

// NITRO-CHARACTER sources: an edited "foo.ncg.d/foo.ncgr" (tiles) or "foo.ncl.d/foo.nclr" (palette) goes back into the CHAR / PALT block of
// "foo.ncg" / "foo.ncl" when the data size is unchanged.
void GXWrapRepackNcfam (ccp root)
{
	const size_t rl = strlen (root);
	if (rl < 7 || (strcmp (root + rl - 6, ".ncg.d") && strcmp (root + rl - 6, ".ncl.d")))
		return;
	const bool chr = root[rl - 3] == 'g';
	char parent[PATH_MAX], path[PATH_MAX];
	snprintf (parent, sizeof (parent), "%.*s", (int)(rl - 2), root);
	ccp bs = strrchr (parent, '/');
	ccp fn = bs ? bs + 1 : parent;
	snprintf (path, sizeof (path), "%s/%.*s%s", root, (int)strlen (fn) - 4, fn, chr ? ".ncgr" : ".nclr");
	struct stat sp, sm;
	if (stat (parent, &sp) || !S_ISREG (sp.st_mode) || stat (path, &sm) || sm.st_mtime <= sp.st_mtime)
		return;
	size_t sz, mn;
	u8 *raw = load (parent, &sz), *m = load (path, &mn);
	if (raw && m && sz > 0x30 && mn > 0x30 && !memcmp (raw, chr ? "NCCG" : "NCCL", 4) && !memcmp (raw + 16, chr ? "CHAR" : "PALT", 4))
	{
		// converted copy: data size at +40 (NCGR) or +32 (NCLR), data at +48 / +40; source: data at +0x20
		const uint n = gxw_le32 (m + (chr ? 40 : 32)), mo = chr ? 48 : 40, sn = gxw_le32 (raw + 20) - (chr ? 0x14 : 0x10);
		bool ok = n && mo + (size_t)n <= mn && 0x20 + (size_t)sn <= sz && sn && gxw_le32 (raw + 20) == sn + (chr ? 0x14 : 0x10);
		if (ok && chr && n == 2 * sn)
		{
			// the PNG import wrote 8 bit tiles for a 4 bit source: pack them back when every index fits
			for (uint i = 0; i < n && ok; i++)
				ok = m[mo + i] < 16;
			for (uint i = 0; i < sn && ok; i++)
				raw[0x20 + i] = m[mo + 2 * i] | m[mo + 2 * i + 1] << 4;
		}
		else
			ok = ok && n == sn && memcpy (raw + 0x20, m + mo, n);
		if (ok && !store (parent, raw, sz))
		{
			struct utimbuf t = { sp.st_atime, sm.st_mtime };
			utime (parent, &t);
		}
	}
	FREE (raw);
	FREE (m);
}

// Starbreeze "compiledtextures.xtc": an edited "foo.xtc.d/tex_NNNN_WxH.tpl" (re-encoded CMPR of the same size) goes back over the top mip.
// shortcut: the smaller mips keep the old picture, regenerate them when a game is seen to sample them.
void GXWrapRepackXtc (ccp root)
{
	const size_t rl = strlen (root);
	if (rl < 7 || strcmp (root + rl - 6, ".xtc.d"))
		return;
	char parent[PATH_MAX];
	snprintf (parent, sizeof (parent), "%.*s", (int)(rl - 2), root);
	struct stat sp;
	size_t sz;
	u8 *raw = stat (parent, &sp) || !S_ISREG (sp.st_mode) ? 0 : load (parent, &sz);
	if (!raw)
		return;
	bool changed = false;
	time_t newest = 0;
	uint n = 0;
	if (sz > 0x100 && !memcmp (raw, "MOS DATAFILE2.0", 15))
		for (size_t p = 0x30; p + 0x30 < sz;)
		{
			const uint o0 = gxw_le32 (raw + p);
			bool ok = o0 >= p + 8 && o0 - p <= 0x40 && !((o0 - p) & 3) && (size_t)o0 + 0x28 <= sz;
			const u8 *hd = raw + (ok ? o0 : 0);
			const uint w = ok ? gxw_le32 (hd + 12) : 0, h = ok ? gxw_le32 (hd + 16) : 0;
			ok = ok && gxw_le32 (hd) == 0x200 && gxw_le32 (hd + 4) == 0x5000 && gxw_le32 (hd + 36) == 0x10 && w && !(w & (w - 1)) && h
				&& !(h & (h - 1)) && w <= 4096 && h <= 4096;
			size_t end = 0;
			if (ok)
			{
				const uint last = gxw_le32 (raw + p + (size_t)((o0 - p) / 4 - 1) * 4);
				ok = (size_t)last + 0x28 <= sz && last >= o0;
				end = ok ? (size_t)last + 0x28 + gxw_le32 (raw + last + 28) : 0;
				ok = ok && end <= sz;
			}
			if (!ok)
			{
				p += 4;
				continue;
			}
			const size_t dl = (size_t)w * h / 2;
			char path[PATH_MAX];
			struct stat sm;
			snprintf (path, sizeof (path), "%s/tex_%04u_%ux%u.tpl", root, n, w, h);
			if (gxw_le32 (hd + 28) >= dl && w >= 8 && h >= 8 && !stat (path, &sm) && sm.st_mtime > sp.st_mtime)
			{
				size_t mn;
				u8 *m = load (path, &mn);
				if (m && mn > 0x30 && gxw_be32 (m + 4) == 1 && gxw_be32 (m + 8) == 12)
				{
					const uint mi = gxw_be32 (m + 12);
					if (mi + 16 <= mn)
					{
						const uint md = gxw_be32 (m + mi + 8);
						if ((m[mi] << 8 | m[mi + 1]) == h && (m[mi + 2] << 8 | m[mi + 3]) == w && gxw_be32 (m + mi + 4) == 14
							&& md <= mn && mn - md >= dl)
							memcpy ((u8 *)hd + 0x28, m + md, dl), changed = true, newest = sm.st_mtime > newest ? sm.st_mtime : newest;
					}
				}
				FREE (m);
			}
			n++;
			p = end;
		}
	if (changed && !store (parent, raw, sz))
	{
		struct utimbuf t = { sp.st_atime, newest };
		utime (parent, &t);
	}
	FREE (raw);
}

// C4 (8x8 tiles) / C8 (8x4 tiles) texels of an RGBA image padded to whole tiles, each matched to the nearest palette colour
static void enc_paletted (bool c8, const u8 *rgba, uint pw, uint ph, const u8 (*pc)[4], uint nc, u8 *s)
{
	const uint tw = 8, th = c8 ? 4 : 8;
	for (uint ty = 0; ty < ph; ty += th)
		for (uint tx = 0; tx < pw; tx += tw)
			for (uint y = 0; y < th; y++)
				for (uint x = 0; x < tw; x++)
				{
					const u8 *p = rgba + 4 * ((size_t)(ty + y) * pw + tx + x);
					uint best = 0, bd = ~0u;
					for (uint c = 0; c < nc; c++)
					{
						uint d = 0;
						for (uint k = 0; k < 4; k++)
						{
							const int dv = (int)pc[c][k] - p[k];
							d += (uint)(dv * dv);
						}
						if (d < bd)
							bd = d, best = c;
					}
					if (c8)
						*s++ = (u8)best;
					else if (x & 1)
						s[-1] |= (u8)best;
					else
						*s++ = (u8)(best << 4);
				}
}

// the PNG padded to whole tiles by repeating its last row / column
static u8 *pad_rgba (const Image_t *img, uint pw, uint ph)
{
	u8 *rgba = CALLOC (4, (size_t)pw * ph);
	for (uint y = 0; rgba && y < ph; y++)
		for (uint x = 0; x < pw; x++)
			memcpy (rgba + 4 * ((size_t)y * pw + x), img->data + 4 * ((size_t)(y < img->height ? y : img->height - 1) * img->xwidth + (x < img->width ? x : img->width - 1)), 4);
	return rgba;
}

static enumError reinject_oibb (const Image_t *img, u8 *raw, const gxw_t *g, ccp path)
{
	const uint fmt = g->gx, w = g->w, h = g->h, tw = fmt == 6 ? 4 : 8, th = fmt == 8 ? 8 : 4, pw = (w + tw - 1) / tw * tw, ph = (h + th - 1) / th * th;
	if (img->width != w || img->height != h)
		return ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the texture is %ux%u: %s\n", img->width, img->height, w, h, path);
	u8 *rgba = pad_rgba (img, pw, ph);
	if (!rgba)
		return ERR_OUT_OF_MEMORY;
	u8 *pal = raw + g->doff + 0x40, *pix = pal + g->bpp;
	if (fmt == 6)
		GXEncodeRGBA (6, pw, ph, rgba, pix);
	else
	{
		u8 pc[256][4];
		const uint nc = g->bpp / 2 > (fmt == 8 ? 16u : 256u) ? (fmt == 8 ? 16u : 256u) : g->bpp / 2;
		for (uint i = 0; i < nc; i++)
		{
			const uint v = gxw_be16 (pal + 2 * i);
			if (v & 0x8000)
				pc[i][0] = (v >> 10 & 31) * 255 / 31, pc[i][1] = (v >> 5 & 31) * 255 / 31, pc[i][2] = (v & 31) * 255 / 31, pc[i][3] = 255;
			else
				pc[i][0] = (v >> 8 & 15) * 17, pc[i][1] = (v >> 4 & 15) * 17, pc[i][2] = (v & 15) * 17, pc[i][3] = (v >> 12 & 7) * 255 / 7;
		}
		memset (pix, 0, g->itype);
		enc_paletted (fmt == 9, rgba, pw, ph, pc, nc, pix);
	}
	FREE (rgba);
	return ERR_OK;
}

// Avalanche texture: GX formats 0-6 / 14 straight, C4 / C8 against the record's RGB565 palette; only the base level is written
static enumError reinject_dbl (const Image_t *img, u8 *raw, const gxw_t *g, ccp path)
{
	const uint fmt = g->gx, bw = fmt == 3 || fmt == 4 || fmt == 5 || fmt == 6 ? 4 : 8, bh = fmt == 0 || fmt == 8 || fmt == 14 ? 8 : fmt == 1 || fmt == 2 || fmt == 9 ? 4 : 4;
	if (img->width != g->w || img->height != g->h)
		return ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the texture is %ux%u: %s\n", img->width, img->height, g->w, g->h, path);
	const uint pw = (g->w + bw - 1) / bw * bw, ph = (g->h + bh - 1) / bh * bh;
	const size_t need = fmt == 8 ? (size_t)pw * ph / 2 : fmt == 9 ? (size_t)pw * ph : level_size (fmt, pw, ph);
	if (need > g->itype)
		return ERR_NOTHING_TO_DO;
	u8 *rgba = pad_rgba (img, pw, ph);
	if (!rgba)
		return ERR_OUT_OF_MEMORY;
	if (fmt == 8 || fmt == 9)
	{
		u8 pc[256][4];
		const uint nc = g->ncol > (fmt == 8 ? 16u : 256u) ? (fmt == 8 ? 16u : 256u) : g->ncol;
		for (uint i = 0; i < nc; i++)
		{
			const uint v = gxw_be16 (raw + g->clut + 2 * i);
			if (g->bpp == 3) // Midway: {G,R} plane then {A,B} plane
				pc[i][0] = raw[g->clut + 2 * i + 1], pc[i][1] = raw[g->clut + 2 * i], pc[i][2] = raw[g->clut + 512 + 2 * i + 1], pc[i][3] = raw[g->clut + 512 + 2 * i];
			else if (g->bpp == 0) // IA8
				pc[i][0] = pc[i][1] = pc[i][2] = v & 255, pc[i][3] = v >> 8;
			else if (g->bpp == 1) // RGB565
				pc[i][0] = (v >> 11 & 31) * 255 / 31, pc[i][1] = (v >> 5 & 63) * 255 / 63, pc[i][2] = (v & 31) * 255 / 31, pc[i][3] = 255;
			else if (v & 0x8000)
				pc[i][0] = (v >> 10 & 31) * 255 / 31, pc[i][1] = (v >> 5 & 31) * 255 / 31, pc[i][2] = (v & 31) * 255 / 31, pc[i][3] = 255;
			else
				pc[i][0] = (v >> 8 & 15) * 17, pc[i][1] = (v >> 4 & 15) * 17, pc[i][2] = (v & 15) * 17, pc[i][3] = (v >> 12 & 7) * 255 / 7;
		}
		memset (raw + g->doff, 0, need);
		enc_paletted (fmt == 9, rgba, pw, ph, pc, nc, raw + g->doff);
	}
	else
		GXEncodeRGBA (fmt, pw, ph, rgba, raw + g->doff);
	FREE (rgba);
	return ERR_OK;
}

// nearest GPAL colour (red in the high bits) for an RGBA pixel; transparent pixels are index 0
static uint grph_index (const u8 *raw, const gxw_t *g, const u8 *p)
{
	if (p[3] < 128)
		return 0;
	uint best = 0, bd = ~0u;
	for (uint i = 0; i < g->ncol; i++)
	{
		const uint c = g->itype + i;
		if (!c || c > 255)
			continue;
		const uint v = gxw_le16 (raw + g->clut + 2 * i);
		const int dr = (int)((v >> 10 & 31) * 255 / 31) - p[0], dg = (int)((v >> 5 & 31) * 255 / 31) - p[1], db = (int)((v & 31) * 255 / 31) - p[2];
		const uint d = (uint)(dr * dr + dg * dg + db * db);
		if (d < bd)
			bd = d, best = c;
	}
	return best;
}

static void grph_put (u8 *gm, uint w, uint bpp, bool tiled, size_t x, size_t y, uint c)
{
	size_t i = y * w + x;
	if (tiled)
		i = ((y / 8) * (w / 8) + x / 8) * 64 + (y % 8) * 8 + x % 8;
	if (bpp == 8)
		gm[16 + i] = (u8)c;
	else
		gm[16 + i / 2] = (u8)((gm[16 + i / 2] & (i & 1 ? 0x0f : 0xf0)) | (i & 1 ? c << 4 : c & 15));
}

static enumError reinject_grph (const Image_t *img, u8 *raw, size_t sz, const gxw_t *g, ccp path)
{
	if (img->width != g->w || img->height != g->h)
		return ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the image is %ux%u: %s\n", img->width, img->height, g->w, g->h, path);
	const size_t o = g->doff, bl = gxw_le32 (raw + o + 4);
	const u8 *px = img->data;
	if (!g->bpp)
	{
		uint w, h, bpp;
		bool tiled;
		if (!grph_image_ok (raw, sz, o, &w, &h, &bpp, &tiled))
			return ERR_NOTHING_TO_DO;
		for (uint y = 0; y < h; y++)
			for (uint x = 0; x < w; x++)
				grph_put (raw + o, w, bpp, tiled, x, y, grph_index (raw, g, px + 4 * ((size_t)y * img->xwidth + x)));
		return ERR_OK;
	}
	const uint np = gxw_le32 (raw + o + 28);
	size_t gp = o + 16 + gxw_le32 (raw + o + 16);
	for (uint k = 0; k < np && o + 32 + 8 * k + 8 <= o + bl; k++)
	{
		uint pw, ph, pb;
		bool pt;
		if (!grph_image_ok (raw, sz, gp, &pw, &ph, &pb, &pt) || gp + gxw_le32 (raw + gp + 4) > o + bl)
			break;
		const uint ox = gxw_le16 (raw + o + 34 + 8 * k) & 0x1ff, oy = gxw_le16 (raw + o + 32 + 8 * k) & 0xff;
		for (uint y = 0; y < ph && oy + y < g->h; y++)
			for (uint x = 0; x < pw && ox + x < g->w; x++)
				grph_put (raw + gp, pw, pb, pt, x, y, grph_index (raw, g, px + 4 * ((size_t)(oy + y) * img->xwidth + ox + x)));
		gp += gxw_le32 (raw + gp + 4);
	}
	return ERR_OK;
}

static enumError reinject_glacier (const Image_t *img, u8 *raw, const gxw_t *g, ccp path)
{
	const uint w = g->w, h = g->h, fc = g->ctype;
	if (img->width != w || img->height != h)
		return ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the texture is %ux%u: %s\n", img->width, img->height, w, h, path);
	u8 *d = raw + g->doff;
	const size_t px = (size_t)w * h;
	memset (d, 0, g->itype < px * 4 ? g->itype : px * 4);
	if (fc == 0x44585431) // DXT1: GX CMPR blocks with PC (LSB first) index bytes, in linear block order
	{
		const uint pw = (w + 7) & ~7u, ph = (h + 7) & ~7u;
		u8 *rgba = pad_rgba (img, pw, ph), *tiled = rgba ? MALLOC (level_size (14, pw, ph)) : 0;
		if (!tiled)
			return FREE (rgba), ERR_OUT_OF_MEMORY;
		GXEncodeRGBA (14, pw, ph, rgba, tiled);
		const uint bw = (w + 3) / 4, bh = (h + 3) / 4;
		for (uint by = 0; by < bh; by++)
			for (uint bx = 0; bx < bw; bx++)
			{
				const u8 *s = tiled + (size_t)((by / 2) * (pw / 8) + bx / 2) * 32 + (size_t)((by & 1) * 2 + (bx & 1)) * 8;
				u8 *o = d + ((size_t)by * bw + bx) * 8;
				memcpy (o, s, 4);
				for (uint r = 0; r < 4; r++)
					o[4 + r] = (u8)((s[4 + r] >> 6 & 3) | (s[4 + r] >> 4 & 3) << 2 | (s[4 + r] >> 2 & 3) << 4 | (s[4 + r] & 3) << 6);
			}
		FREE (rgba), FREE (tiled);
		return ERR_OK;
	}
	for (size_t i = 0; i < px; i++)
	{
		const u8 *p = img->data + 4 * ((i / w) * img->xwidth + i % w);
		const uint lum = (p[0] + p[1] + p[2]) / 3;
		uint v;
		switch (fc)
		{
		case 0x31354133: // 15A3
			v = p[3] == 255 ? 0x8000 | ((p[0] * 31 + 127) / 255) << 10 | ((p[1] * 31 + 127) / 255) << 5 | ((p[2] * 31 + 127) / 255)
				: ((p[3] * 7 + 127) / 255) << 12 | ((p[0] + 8) / 17) << 8 | ((p[1] + 8) / 17) << 4 | (p[2] + 8) / 17;
			d[2 * i] = (u8)(v >> 8), d[2 * i + 1] = (u8)v;
			break;
		case 0x31364130: // 16A0
			v = ((p[0] * 31 + 127) / 255) << 11 | ((p[1] * 63 + 127) / 255) << 5 | ((p[2] * 31 + 127) / 255);
			d[2 * i] = (u8)(v >> 8), d[2 * i + 1] = (u8)v;
			break;
		case 0x49413820: d[2 * i] = p[3], d[2 * i + 1] = (u8)lum; break; // IA8
		case 0x49413420: d[i] = (u8)((p[3] + 8) / 17 << 4 | (lum + 8) / 17); break; // IA4
		case 0x49382020: d[i] = (u8)lum; break; // I8
		case 0x49342020: d[i / 2] |= (u8)((lum + 8) / 17 << (i & 1 ? 0 : 4)); break; // I4
		case 0x52474241: d[4 * i] = p[3], d[4 * i + 1] = p[2], d[4 * i + 2] = p[1], d[4 * i + 3] = p[0]; break; // RGBA stored {A, B, G, R}
		default: // PALN
		{
			uint best = 0, bd = ~0u;
			for (uint c = 0; c < g->ncol; c++)
			{
				const u8 *e = raw + g->clut + 4 * c;
				uint dd = 0;
				const int dv[4] = { (int)e[3] - p[0], (int)e[2] - p[1], (int)e[1] - p[2], (int)e[0] - p[3] };
				for (uint k = 0; k < 4; k++)
					dd += (uint)(dv[k] * dv[k]);
				if (dd < bd)
					bd = dd, best = c;
			}
			d[i] = (u8)best;
		}
		}
	}
	return ERR_OK;
}

enumError GXWrapReinject (Image_t *img, ccp dest)
{
	size_t sz;
	u8 *raw = load (dest, &sz);
	gxw_t g;
	if (raw && has_ext (dest, ".tplc"))
	{
		size_t dn;
		u8 *dec = tplc_open (raw, sz, &dn, &g);
		enumError e = dec ? ERR_OK : ERR_NOTHING_TO_DO;
		if (dec)
		{
			Transform2XIMG (img);
			if (img->iform != IMG_X_RGB)
				e = ERR_NOTHING_TO_DO;
			else if (img->width != g.w || img->height != g.h)
				e = ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the texture is %ux%u: %s\n", img->width, img->height, g.w, g.h, dest);
			else
			{
				const uint pw = (g.w + 7) / 8 * 8, ph = (g.h + 7) / 8 * 8;
				u8 *rgba = pad_rgba (img, pw, ph);
				if (!rgba)
					e = ERR_OUT_OF_MEMORY;
				else
				{
					g.w = pw, g.h = ph;
					encode_chain (&g, rgba, dec + g.doff, g.avail);
					FREE (rgba);
					size_t pn;
					u8 *z = tplc_pack (dec, dn, &pn);
					e = z ? store (dest, z, pn) : ERR_OUT_OF_MEMORY;
					FREE (z);
				}
			}
			FREE (dec);
		}
		FREE (raw);
		return e;
	}
	if (raw)
	{
		size_t rn;
		bool rz;
		u8 *rh = rhtex_open (dest, raw, sz, &rn, &rz);
		if (rh)
		{
			Transform2XIMG (img);
			const uint w = gxw_le16 (rh + rn - 0x1000 + 4), h = gxw_le16 (rh + rn - 0x1000 + 6);
			enumError e = ERR_OK;
			if (img->iform != IMG_X_RGB)
				e = ERR_NOTHING_TO_DO;
			else if (img->width != w || img->height != h)
				e = ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the texture is %ux%u: %s\n", img->width, img->height, w, h, dest);
			else
			{
				u8 *rgba = pad_rgba (img, w, h);
				if (!rgba || !EncodeRHGrooveTexRGBA (rh, rn, rgba, w, h))
					e = ERR_NOTHING_TO_DO;
				else if (rz)
				{
					u8 *z = 0;
					uint zn = 0;
					e = EncodeZSTD (&z, &zn, rh, (uint)rn, 0) ? ERR_NOTHING_TO_DO : store (dest, z, zn);
					FREE (z);
				}
				else
					e = store (dest, rh, rn);
				FREE (rgba);
			}
			FREE (rh);
			FREE (raw);
			return e;
		}
	}
	u8 *packed = 0, hdr0 = raw && sz ? raw[0] : 0;
	size_t psz = 0;
	if (raw && (packed = gxw_refpack (raw, sz, &psz)) != 0)
	{
		if (hdr0 & 1) // a compressed-size field follows the header: not rewritten
		{
			FREE (raw);
			FREE (packed);
			return ERR_NOTHING_TO_DO;
		}
		FREE (raw);
		raw = packed, sz = psz, packed = (u8 *)1;
	}
	if (!raw || !locate (raw, sz, dest, &g))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char txv[PATH_MAX];
	if (g.txm) // the texels are in "foo.txv" beside "foo.txm"
	{
		snprintf (txv, sizeof (txv), "%s", dest);
		txv[strlen (txv) - 1] = 'v';
		FREE (raw);
		raw = load (txv, &sz);
		dest = txv;
		if (!raw || g.doff + g.avail > sz)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	}
	if (g.fmpp)
	{
		Transform2XIMG (img);
		enumError e = img->iform != IMG_X_RGB ? ERR_NOTHING_TO_DO : reinject_fmpp (img, raw, sz, &g, dest);
		FREE (raw);
		return e;
	}
	if (g.glac)
	{
		Transform2XIMG (img);
		enumError e = img->iform != IMG_X_RGB ? ERR_NOTHING_TO_DO : reinject_glacier (img, raw, &g, dest);
		if (!e)
			e = store (dest, raw, sz);
		FREE (raw);
		return e;
	}
	if (g.pica)
	{
		Transform2XIMG (img);
		enumError e = ERR_OK;
		if (img->iform != IMG_X_RGB)
			e = ERR_NOTHING_TO_DO;
		else if (img->width != g.w || img->height != g.h)
			e = ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the texture is %ux%u: %s\n", img->width, img->height, g.w, g.h, dest);
		else
		{
			const uint pw = (g.w + 7) & ~7u, ph = (g.h + 7) & ~7u;
			u8 *rgba = pad_rgba (img, pw, ph);
			if (!rgba)
				e = ERR_OUT_OF_MEMORY;
			else if (!pica_encode (g.gx, rgba, pw, ph, raw + g.doff, g.avail))
				e = ERR_NOTHING_TO_DO;
			else
				e = store (dest, raw, sz);
			FREE (rgba);
		}
		FREE (raw);
		return e;
	}
	if (g.rwpi)
	{
		Transform2XIMG (img);
		enumError e = ERR_OK;
		if (img->iform != IMG_X_RGB)
			e = ERR_NOTHING_TO_DO;
		else if (img->width != g.w || img->height != g.h)
			e = ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the texture is %ux%u: %s\n", img->width, img->height, g.w, g.h, dest);
		else
		{
			u8 *rgba = pad_rgba (img, g.w, g.h);
			if (!rgba)
				e = ERR_OUT_OF_MEMORY;
			else
			{
				for (uint y = 0; y < g.h; y++)
					for (uint x = 0; x < g.w; x++)
					{
						const u8 *c = rgba + ((size_t)y * g.w + x) * 4;
						u8 *row = raw + g.doff + (size_t)y * g.itype;
						if (g.bpp == 32)
						{
							memcpy (row + 4 * x, c, 4);
							continue;
						}
						// paletted: the palette stays, each pixel takes the nearest entry
						uint best = 0, bd = ~0u;
						for (uint k = 0; k < g.ncol; k++)
						{
							const u8 *pc = raw + g.clut + 4 * k;
							uint d = 0;
							for (int q = 0; q < 4; q++)
								d += (uint)(((int)pc[q] - c[q]) * ((int)pc[q] - c[q]));
							if (d < bd)
								bd = d, best = k;
						}
						if (g.bpp == 8)
							row[x] = (u8)best;
						else if (x & 1)
							row[x / 2] = (u8)((row[x / 2] & 0x0f) | best << 4);
						else
							row[x / 2] = (u8)((row[x / 2] & 0xf0) | best);
					}
				e = store (dest, raw, sz);
			}
			FREE (rgba);
		}
		FREE (raw);
		return e;
	}
	if (g.grph)
	{
		Transform2XIMG (img);
		enumError e = img->iform != IMG_X_RGB ? ERR_NOTHING_TO_DO : reinject_grph (img, raw, sz, &g, dest);
		if (!e)
			e = store (dest, raw, sz);
		FREE (raw);
		return e;
	}
	if (g.rtex)
	{
		Transform2XIMG (img);
		enumError e = ERR_OK;
		if (img->iform != IMG_X_RGB)
			e = ERR_NOTHING_TO_DO;
		else if (img->width != g.w || img->height != g.h)
			e = ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the texture is %ux%u: %s\n", img->width, img->height, g.w, g.h, dest);
		else
		{
			u8 *rgba = pad_rgba (img, (g.w + 7) / 8 * 8, (g.h + 7) / 8 * 8), *data = CALLOC (1, (size_t)g.itype + 8);
			const uint pw = (g.w + 7) / 8 * 8, ph = (g.h + 7) / 8 * 8;
			g.w = pw, g.h = ph;
			char *txt = MALLOC ((size_t)g.itype / g.bpp * (2 * g.bpp + 1) + 16);
			if (!rgba || !data || !txt)
				e = ERR_OUT_OF_MEMORY;
			else
			{
				encode_chain (&g, rgba, data, g.itype);
				size_t n = 0;
				txt[n++] = '\r', txt[n++] = '\n', txt[n++] = '\t';
				for (uint i = 0; i < g.itype; i += g.bpp)
				{
					uint v = 0;
					for (uint k = 0; k < g.bpp; k++)
						v = v << 8 | data[i + k];
					n += (size_t)sprintf (txt + n, g.bpp == 4 ? "%08x" : g.bpp == 2 ? "%04x" : "%02x", v);
					txt[n++] = i + g.bpp < g.itype ? ' ' : '\r';
				}
				txt[n - 1] == '\r' ? (void)(txt[n++] = '\n') : (void)0;
				u8 *out = MALLOC (sz + n + 16);
				if (!out)
					e = ERR_OUT_OF_MEMORY;
				else
				{
					memcpy (out, raw, g.doff);
					memcpy (out + g.doff, txt, n);
					memcpy (out + g.doff + n, raw + g.doff + g.avail, sz - g.doff - g.avail);
					e = store (dest, out, g.doff + n + (sz - g.doff - g.avail));
					FREE (out);
				}
			}
			FREE (rgba), FREE (data), FREE (txt);
		}
		FREE (raw);
		return e;
	}
	if (g.dbl)
	{
		Transform2XIMG (img);
		enumError e = img->iform != IMG_X_RGB ? ERR_NOTHING_TO_DO : reinject_dbl (img, raw, &g, dest);
		if (!e && packed)
		{
			size_t on;
			u8 *o = gxw_refpack_literals (hdr0, raw, sz, &on);
			e = o ? store (dest, o, on) : ERR_OUT_OF_MEMORY;
			FREE (o);
		}
		else if (!e)
			e = store (dest, raw, sz);
		FREE (raw);
		return e;
	}
	if (g.oibb)
	{
		Transform2XIMG (img);
		enumError e = img->iform != IMG_X_RGB ? ERR_NOTHING_TO_DO : reinject_oibb (img, raw, &g, dest);
		if (!e)
			e = store (dest, raw, sz);
		FREE (raw);
		return e;
	}
	if (g.imy)
	{
		enumError e = ERR_OK;
		u8 *out = 0;
		size_t outn = 0;
		Transform2XIMG (img);
		if (img->iform != IMG_X_RGB)
			e = ERR_NOTHING_TO_DO;
		else if (img->width != g.w || img->height != g.h)
			e = ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the image is %ux%u: %s\n", img->width, img->height, g.w, g.h, dest);
		else
			e = reinject_imy (img, raw, &g, &out, &outn);
		if (!e)
			e = store (dest, out, outn);
		FREE (out);
		FREE (raw);
		return e;
	}
	if (g.ltex)
	{
		enumError e = ERR_OK;
		u8 *out = 0;
		size_t outn = 0;
		Transform2XIMG (img);
		if (img->iform != IMG_X_RGB)
			e = ERR_NOTHING_TO_DO;
		else if (img->width != g.w || img->height != g.h)
			e = ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the image is %ux%u: %s\n", img->width, img->height, g.w, g.h, dest);
		else
			e = reinject_ltex (img, raw, sz, &g, &out, &outn);
		if (!e)
			e = store (dest, out, outn);
		FREE (out);
		FREE (raw);
		return e;
	}
	if (g.ntft)
	{
		enumError e = ERR_OK;
		Transform2XIMG (img);
		if (img->iform != IMG_X_RGB)
			e = ERR_NOTHING_TO_DO;
		else
			e = reinject_ntft (img, raw, sz, &g, dest);
		if (!e)
			e = store (dest, raw, sz);
		FREE (raw);
		return e;
	}
	if (g.tklt)
	{
		enumError e = ERR_OK;
		u8 *out = 0;
		size_t outn = 0;
		Transform2XIMG (img);
		if (img->iform != IMG_X_RGB)
			e = ERR_NOTHING_TO_DO;
		else if (img->width != g.w || img->height != g.h)
			e = ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the tile sheet is %ux%u: %s\n", img->width,
				img->height, g.w, g.h, dest);
		else
			e = reinject_tklt (img, raw, sz, dest, &out, &outn);
		if (!e)
			e = store (dest, out, outn);
		FREE (out);
		FREE (raw);
		return e;
	}
	if (g.nbfc)
	{
		enumError e = ERR_OK;
		Transform2XIMG (img);
		if (img->iform != IMG_X_RGB)
			e = ERR_NOTHING_TO_DO;
		else
			e = reinject_nbfc (img, raw, sz, dest);
		if (!e)
			e = store (dest, raw, sz);
		FREE (raw);
		return e;
	}
	if (g.tim2 || g.dst1 || g.ivt || g.apx || g.msk || g.tan)
	{
		enumError e = ERR_OK;
		Transform2XIMG (img);
		if (img->iform != IMG_X_RGB)
			e = ERR_NOTHING_TO_DO;
		else if (img->width != g.w || img->height != g.h)
			e = ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the image is %ux%u: %s\n", img->width,
				img->height, g.w, g.h, dest);
		else if (g.msk)
			reinject_msk (img, raw, &g);
		else if (g.tan)
			reinject_tan (img, raw, &g);
		else if (g.apx)
			reinject_apx (img, raw, &g);
		else if (g.ivt)
			reinject_ivt (img, raw, &g);
		else if (g.dst1)
			reinject_dst1 (img, raw, &g);
		else
			e = reinject_tim2 (img, raw, &g);
		if (!e)
			e = store (dest, raw, sz);
		FREE (raw);
		return e;
	}
	if (g.tga)
	{
		enumError e = ERR_OK;
		Transform2XIMG (img);
		if (img->iform != IMG_X_RGB)
			e = ERR_NOTHING_TO_DO;
		else if (img->width != g.w || img->height != g.h)
			e = ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the TGA is %ux%u: %s\n", img->width,
				img->height, g.w, g.h, dest);
		else
		{
			// header and image id are kept; pixels are written uncompressed (type 2 / 3)
			const uint bytes = g.bpp / 8;
			const size_t out_size = g.doff + (size_t)g.w * g.h * bytes;
			u8 *out = CALLOC (1, out_size);
			if (!out)
				e = ERR_OUT_OF_MEMORY;
			else
			{
				memcpy (out, raw, g.doff);
				out[2] = bytes == 1 ? 3 : 2;
				for (uint y = 0; y < g.h; y++)
				{
					const uint dy = (raw[17] & 0x20) ? y : g.h - 1 - y;
					for (uint x = 0; x < g.w; x++)
					{
						const u8 *p = img->data + 4 * ((size_t)y * img->xwidth + x);
						u8 *o = out + g.doff + ((size_t)dy * g.w + x) * bytes;
						if (bytes == 1)
							o[0] = (u8)((p[0] + p[1] + p[2]) / 3);
						else
						{
							o[0] = p[2], o[1] = p[1], o[2] = p[0];
							if (bytes == 4)
								o[3] = p[3];
						}
					}
				}
				e = store (dest, out, out_size);
				FREE (out);
			}
		}
		FREE (raw);
		return e;
	}
	Transform2XIMG (img);
	enumError err = ERR_OK;
	if (img->iform != IMG_X_RGB)
		err = ERR_NOTHING_TO_DO;
	else if (img->width != (g.rw ? g.rw : g.w) || img->height != (g.rw ? g.rh : g.h))
		err = ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the texture is %ux%u: %s\n", img->width,
			img->height, g.rw ? g.rw : g.w, g.rw ? g.rh : g.h, dest);
	else
	{
		u8 *rgba = MALLOC ((size_t)g.w * g.h * 4);
		if (!rgba)
			err = ERR_OUT_OF_MEMORY;
		else
		{
			for (uint y = 0; y < g.h; y++)
				for (uint x = 0; x < g.w; x++)
				{
					const uint sx = x < (g.rw ? g.rw : g.w) ? x : (g.rw ? g.rw : g.w) - 1, sy = g.rw && y >= g.rh ? g.rh - 1 : g.flip ? g.h - 1 - y : y;
					memcpy (rgba + 4 * ((size_t)y * g.w + x), img->data + 4 * ((size_t)sy * img->xwidth + sx), 4);
				}
			if (g.ptexa)
			{
				u8 *a = raw + g.doff + (size_t)g.w * g.h / 2;
				for (uint by = 0; by < g.h; by += 8)
					for (uint bx = 0; bx < g.w; bx += 8)
						for (uint y = 0; y < 8; y++)
							for (uint x = 0; x < 8; x += 2)
							{
								const u8 *p = rgba + 4 * ((size_t)(by + y) * g.w + bx + x);
								*a++ = (u8)(((p[3] + 8) / 17) << 4 | (p[7] + 8) / 17);
							}
				for (size_t i = 0; i < (size_t)g.w * g.h; i++)
					rgba[4 * i + 3] = 255; // the colour plane has no transparent mode
			}
			encode_chain (&g, rgba, raw + g.doff, g.avail);
			FREE (rgba);
			err = store (dest, raw, sz);
		}
	}
	FREE (raw);
	return err;
}

enumError GXWrapSyncFromTPL (ccp tpl_path)
{
	char wrap[PATH_MAX];
	snprintf (wrap, sizeof (wrap), "%s", tpl_path);
	if (!has_ext (wrap, ".tpl"))
		return ERR_NOTHING_TO_DO;
	wrap[strlen (wrap) - 4] = 0;
	size_t wsz, tsz;
	u8 *w = load (wrap, &wsz), *t = load (tpl_path, &tsz);
	gxw_t g;
	enumError err = ERR_NOTHING_TO_DO;
	if (w && t && tsz > 0x20 && gxw_be32 (t) == 0x0020af30 && locate (w, wsz, wrap, &g) && !g.tga)
	{
		const uint ih = gxw_be32 (t + 0xc), n = level_size (g.gx, g.w, g.h);
		const size_t doff = ih && ih + 0xc <= tsz ? gxw_be32 (t + ih + 8) : 0;
		if (ih && ih + 0xc <= tsz && gxw_be32 (t + ih + 4) == g.gx && doff && doff + n <= tsz
			&& gxw_be16 (t + ih) == g.h && gxw_be16 (t + ih + 2) == g.w)
		{
			// regenerate the mip chain from the new base level, then keep the base bytes exactly
			u8 *rgba = 0;
			if (!DecodeGXTexture_RGBA (&rgba, g.w, g.h, g.gx, t + doff, n, 0, 0, 0) && rgba)
				encode_chain (&g, rgba, w + g.doff, g.avail);
			FREE (rgba);
			memcpy (w + g.doff, t + doff, n);
			err = store (wrap, w, wsz);
		}
	}
	FREE (w);
	FREE (t);
	return err;
}

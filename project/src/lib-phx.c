// SPDX-License-Identifier: GPL-2.0+
// Kirby Air Riders "phx" engine: PARC catalogs over PDAT packs, and PTEX textures.
#include "lib-phx.h"
#include "lib-archive-util.h"
#include "lib-bntx.h"
#include "lib-sha256.h"
#include "lib-zstd.h"
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

#define PARC_MAX_PACKS 64
#define PARC_LOC_SIZE 64
#define PARC_BLOCK_SIZE 48
#define PARC_MAX_BLOCK (1u << 30)

bool IsPARC (const u8 *data, uint size)
{
	return data && size >= 0x40 && !memcmp (data, "PARC", 4) && le32 (data + 4) >= 1
		&& le32 (data + 4) <= 16 && le32 (data + 0x3c) >= 1 && le32 (data + 0x3c) <= PARC_MAX_PACKS
		&& le32 (data + 0x38) == 12 + le32 (data + 0x3c) * 32;
}

typedef struct parc_t
{
	const u8 *data;
	size_t size;
	uint n_packs;
	const u8 *pack_hash; // [n_packs][32]
	uint n_assets;
	const u8 *ids; // [n_assets] {u64 id, u32 ?, u32 index}
	const u8 *rec_offs; // [n_assets] u32, relative to rec_base
	size_t rec_base, rec_end;
	uint n_locs;
	const u8 *locs; // [n_locs] 64-byte records
} parc_t;

static bool parc_scan (parc_t *pc, const u8 *data, size_t size)
{
	memset (pc, 0, sizeof (*pc));
	if (!IsPARC (data, size > UINT_MAX ? UINT_MAX : (uint)size))
		return false;
	pc->data = data;
	pc->size = size;
	pc->n_packs = le32 (data + 0x3c);
	pc->pack_hash = data + 0x44;

	size_t p = 0x38 + le32 (data + 0x38);
	if (p + 8 > size)
		return false;
	const u32 s1 = le32 (data + p), n = le32 (data + p + 4);
	if ((u64)n * 16 + 8 != s1 || p + s1 > size || !n)
		return false;
	pc->n_assets = n;
	pc->ids = data + p + 8;
	p += s1;

	// The sections between the ID map and the asset records (secondary keys, name
	// tables) are not needed. The asset-record section is the one whose count is the
	// asset count and whose end starts the location table, which ends the file.
	for (size_t q = p; q + 12 <= size; q += 4)
	{
		const u32 sz = le32 (data + q);
		if (le32 (data + q + 4) != n || le32 (data + q + 8) != 0 || sz < 8 + (u64)n * 4
			|| q + sz + 8 > size)
			continue;
		const size_t l = q + sz;
		const u32 lsz = le32 (data + l), lcnt = le32 (data + l + 4);
		if ((u64)lcnt * PARC_LOC_SIZE + 8 != lsz || l + lsz != size)
			continue;
		pc->rec_offs = data + q + 8;
		pc->rec_base = q + 8 + (size_t)n * 4;
		pc->rec_end = l;
		pc->n_locs = lcnt;
		pc->locs = data + l + 8;
		return true;
	}
	return false;
}

// Returns a pointer to asset I's record and its block count, or NULL.
static const u8 *parc_record (const parc_t *pc, uint i, uint *n_blocks)
{
	const size_t r = pc->rec_base + le32 (pc->rec_offs + (size_t)i * 4);
	if (r + 24 > pc->rec_end)
		return 0;
	const u64 nb = le64 (pc->data + r);
	if (nb > 4096 || r + 24 + nb * PARC_BLOCK_SIZE + 16 > pc->rec_end)
		return 0;
	*n_blocks = (uint)nb;
	return pc->data + r;
}

typedef struct parc_ctx_t
{
	parc_t pc;
	FILE *pack[PARC_MAX_PACKS];
	u64 pack_size[PARC_MAX_PACKS];
	u8 *stored;
	size_t stored_cap;
	u8 *buf;
	size_t buf_cap;
} parc_ctx_t;

static bool grow (u8 **p, size_t *cap, size_t need)
{
	if (need <= *cap)
		return true;
	size_t c = *cap ? *cap : 1 << 20;
	while (c < need)
		c *= 2;
	FREE (*p);
	*p = MALLOC (c);
	*cap = c;
	return *p != 0;
}

// Loads location LI decoded into ctx->buf; returns its size or -1.
static s64 parc_load (parc_ctx_t *c, u64 li)
{
	if (li >= c->pc.n_locs)
		return -1;
	const u8 *l = c->pc.locs + li * PARC_LOC_SIZE;
	const u32 pack = le32 (l), codec = le32 (l + 4);
	const u64 off = le64 (l + 8), stored = le64 (l + 16), size = le64 (l + 24);
	if (pack >= c->pc.n_packs || !c->pack[pack] || codec > 1 || stored > PARC_MAX_BLOCK
		|| size > PARC_MAX_BLOCK || off > c->pack_size[pack] || stored > c->pack_size[pack] - off
		|| (codec == 0 && stored != size))
		return -1;
	if (!grow (&c->buf, &c->buf_cap, size + 1))
		return -1;
	u8 *dst = codec ? (grow (&c->stored, &c->stored_cap, stored + 1) ? c->stored : 0) : c->buf;
	if (!dst || fseeko (c->pack[pack], (off_t)off, SEEK_SET)
		|| fread (dst, 1, stored, c->pack[pack]) != stored)
		return -1;
	if (codec)
	{
		uint written = 0;
		if (DecodeZSTDpart (c->buf, (uint)size, &written, c->stored, (uint)stored) || written != size)
			return -1;
	}
	return (s64)size;
}

// The record type: a flatbuffer's file identifier at +4, else a 4-character magic at 0.
static void parc_type (const u8 *d, size_t n, char *type, size_t type_size)
{
	const u8 *t = 0;
	if (n >= 8 && (!memcmp (d, "MsgStdBn", 8) || !memcmp (d, "MsgPrjBn", 8)))
	{
		snprintf (type, type_size, "%s", d[3] == 'S' ? "msbt" : "msbp"); // plain LMS text
		return;
	}
	if (n >= 8 && le32 (d) >= 8 && le32 (d) < n && isalnum (d[4]) && isalnum (d[5]) && isalnum (d[6])
		&& isalnum (d[7]))
		t = d + 4;
	else if (n >= 4 && isalpha (d[0]) && isalnum (d[1]) && isalnum (d[2]) && isalnum (d[3]))
		t = d;
	if (!t)
	{
		snprintf (type, type_size, "bin");
		return;
	}
	for (uint i = 0; i < 4; i++)
		type[i] = tolower (t[i]);
	type[4] = 0;
}

static void parc_block_suffix (const u8 *blk, char *out, size_t out_size)
{
	char tag[5];
	uint k = 0;
	for (uint i = 0; i < 4 && blk[16 + i]; i++)
		if (isalnum (blk[16 + i]))
			tag[k++] = tolower (blk[16 + i]);
	tag[k] = 0;
	const u32 sub = le32 (blk + 20);
	if (sub)
		snprintf (out, out_size, "%s%u", k ? tag : "blk", sub);
	else
		snprintf (out, out_size, "%s", k ? tag : "blk");
}

static bool parc_save (ccp path, const u8 *d, size_t n)
{
	FILE *f = fopen (path, "wb");
	if (!f)
		return false;
	const bool ok = fwrite (d, 1, n, f) == n;
	return !fclose (f) && ok;
}

static void parc_open_packs (parc_ctx_t *c, ccp arg)
{
	char dir[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s", arg);
	char *slash = strrchr (dir, '/');
	if (slash)
		*slash = 0;
	else
		snprintf (dir, sizeof (dir), ".");

	DIR *dh = opendir (dir);
	if (!dh)
		return;
	struct dirent *de;
	while ((de = readdir (dh)) != 0)
	{
		const size_t len = strlen (de->d_name);
		if (len < 5 || strcasecmp (de->d_name + len - 4, ".dat"))
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s", dir, de->d_name);
		FILE *f = fopen (path, "rb");
		u8 h[0x40];
		if (!f)
			continue;
		const u32 idx = fread (h, 1, sizeof (h), f) == sizeof (h) && !memcmp (h, "PDAT", 4)
			? le32 (h + 8)
			: ~0u;
		struct stat st;
		if (idx < c->pc.n_packs && !c->pack[idx] && !memcmp (h + 0x18, c->pc.pack_hash + idx * 32, 32)
			&& !fstat (fileno (f), &st))
		{
			c->pack[idx] = f;
			c->pack_size[idx] = (u64)st.st_size;
		}
		else
			fclose (f);
	}
	closedir (dh);
}

enumError ExtractPARCArchive (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 1u << 30, 2, 0, false))
		return ERR_NOTHING_TO_DO;
	parc_ctx_t c;
	memset (&c, 0, sizeof (c));
	if (!parc_scan (&c.pc, raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	parc_open_packs (&c, arg);

	uint found = 0;
	for (uint i = 0; i < c.pc.n_packs; i++)
		found += c.pack[i] != 0;

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PARC:%s (%u assets, %u blocks, %u/%u PDAT packs) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, c.pc.n_assets, c.pc.n_locs, found,
			c.pc.n_packs, dest);

	enumError err = ERR_OK;
	if (found < c.pc.n_packs)
		err = ERROR0 (ERR_WARNING,
			"PARC: %u of %u PDAT packs missing beside %s (or their hashes differ); their assets are skipped\n",
			c.pc.n_packs - found, c.pc.n_packs, arg);
	if (testmode)
		goto done;
	CreatePath (dest, true);

	uint written = 0, failed = 0;
	char made_types[64][8];
	uint n_made = 0;
	for (uint i = 0; i < c.pc.n_assets; i++)
	{
		uint idx = le32 (c.pc.ids + (size_t)i * 16 + 12);
		const u64 id = le64 (c.pc.ids + (size_t)i * 16);
		uint nb = 0;
		const u8 *rec = idx < c.pc.n_assets ? parc_record (&c.pc, idx, &nb) : 0;
		if (!rec || !nb)
			continue;

		// primary block: HEAD (textures, fonts), else Main / MAIN / BIN, else the first
		static const char *const prefer[] = { "HEAD", "Main", "MAIN", "BIN" };
		uint prim = 0;
		for (uint pi = 0, done_p = 0; pi < 4 && !done_p; pi++)
			for (uint b = 0; b < nb; b++)
				if (!strncmp ((ccp)rec + 24 + b * PARC_BLOCK_SIZE + 16, prefer[pi], 4))
				{
					prim = b;
					done_p = 1;
					break;
				}

		const u8 *pb = rec + 24 + prim * PARC_BLOCK_SIZE;
		s64 n = parc_load (&c, le64 (pb + 24));
		if (n < 0)
		{
			failed++;
			continue;
		}
		char type[8];
		parc_type (c.buf, (size_t)n, type, sizeof (type));

		char tdir[PATH_MAX], path[PATH_MAX];
		snprintf (tdir, sizeof (tdir), "%s/%s", dest, type);
		bool made = false;
		for (uint k = 0; k < n_made && !made; k++)
			made = !strcmp (made_types[k], type);
		if (!made)
		{
			CreatePath (tdir, true);
			if (n_made < 64)
				snprintf (made_types[n_made++], sizeof (made_types[0]), "%s", type);
		}

		snprintf (path, sizeof (path), "%s/%016llx.%s", tdir, (unsigned long long)id, type);
		if (!parc_save (path, c.buf, (size_t)n))
			failed++;
		for (uint b = 0; b < nb; b++)
		{
			if (b == prim)
				continue;
			const u8 *blk = rec + 24 + b * PARC_BLOCK_SIZE;
			char suf[16];
			parc_block_suffix (blk, suf, sizeof (suf));
			n = parc_load (&c, le64 (blk + 24));
			snprintf (path, sizeof (path), "%s/%016llx.%s.%s", tdir, (unsigned long long)id, type, suf);
			if (n < 0 || !parc_save (path, c.buf, (size_t)n))
				failed++;
		}
		written++;
		if (verbose > 0 && !(written % 10000))
			fprintf (stdlog, "  - %u/%u assets\n", written, c.pc.n_assets);
	}
	if (failed)
		err = ERROR0 (ERR_WARNING, "PARC: %u blocks could not be read: %s\n", failed, arg);

done:
	for (uint i = 0; i < PARC_MAX_PACKS; i++)
		if (c.pack[i])
			fclose (c.pack[i]);
	FREE (c.buf);
	FREE (c.stored);
	FREE (raw);
	return err;
}

///////////////////////////////////////////////////////////////////////////////
// PTEX

// Minimal, bounds-checked flatbuffer table access.
static bool fb_table (const u8 *d, uint n, uint tpos, uint *vt, uint *vlen)
{
	if ((u64)tpos + 4 > n)
		return false;
	const s32 so = (s32)le32 (d + tpos);
	const s64 v = (s64)tpos - so;
	if (v < 0 || v + 4 > n)
		return false;
	*vt = (uint)v;
	*vlen = le16 (d + v);
	return *vlen >= 4 && (u64)*vt + *vlen <= n;
}

// Absolute position of field FIELD of the table at TPOS, or 0 when absent.
static uint fb_field (const u8 *d, uint n, uint tpos, uint field, uint width)
{
	uint vt, vlen;
	if (!fb_table (d, n, tpos, &vt, &vlen) || 4 + field * 2 + 2 > vlen)
		return 0;
	const uint o = le16 (d + vt + 4 + field * 2);
	return o && (u64)tpos + o + width <= n ? tpos + o : 0;
}

typedef struct ptex_info_t
{
	uint width, height, depth, mips, dim, format;
} ptex_info_t;

bool IsPTEX (const u8 *data, uint size)
{
	return data && size >= 0x20 && !memcmp (data + 4, "PTEX", 4) && le32 (data) < size;
}

static bool ptex_info (const u8 *d, uint n, ptex_info_t *pi)
{
	if (!IsPTEX (d, n))
		return false;
	const uint root = le32 (d);
	const uint f3 = fb_field (d, n, root, 3, 4);
	if (!f3)
		return false;
	const u64 info = (u64)f3 + le32 (d + f3);
	if (info >= n)
		return false;
	uint p;
	memset (pi, 0, sizeof (*pi));
	pi->width = (p = fb_field (d, n, (uint)info, 1, 4)) ? le32 (d + p) : 0;
	pi->height = (p = fb_field (d, n, (uint)info, 2, 4)) ? le32 (d + p) : 0;
	pi->depth = (p = fb_field (d, n, (uint)info, 3, 2)) ? le16 (d + p) : 1;
	pi->mips = (p = fb_field (d, n, (uint)info, 4, 2)) ? le16 (d + p) : 1;
	pi->dim = (p = fb_field (d, n, (uint)info, 5, 1)) ? d[p] : 0;
	pi->format = (p = fb_field (d, n, (uint)info, 6, 1)) ? d[p] : 0;
	return pi->width && pi->height && pi->width <= 16384 && pi->height <= 16384;
}

typedef struct ptex_fmt_t
{
	u8 code;
	u16 bntx; // BNTX format word (format << 8 | type)
	u8 bw, bh, bpb; // block size in pixels and bytes per block
	ccp name;
} ptex_fmt_t;

// The engine's own format enum. Checked by decoding real textures and by matching every
// texture's body size against its Tegra layout: the BC block is 0x18..0x23 (BC4/BC5 have no
// signed variants), ASTC UNORM 4x4..12x12 is 0x29..0x36 and ASTC sRGB 0x37..0x44.
#define ASTC(code, w, h, fmt, type, nm) { code, (fmt) << 8 | (type), w, h, 16, nm }
static const ptex_fmt_t ptex_formats[] = {
	{ 0x02, 0x0b06, 1, 1, 4, "RGBA8_SRGB" },
	{ 0x0b, 0x1505, 1, 1, 8, "RGBA16F" },
	{ 0x15, 0x1905, 1, 1, 16, "RGBA32F" },
	{ 0x18, 0x1a01, 4, 4, 8, "BC1" },
	{ 0x19, 0x1a06, 4, 4, 8, "BC1_SRGB" },
	{ 0x1a, 0x1b01, 4, 4, 16, "BC2" },
	{ 0x1b, 0x1b06, 4, 4, 16, "BC2_SRGB" },
	{ 0x1c, 0x1c01, 4, 4, 16, "BC3" },
	{ 0x1d, 0x1c06, 4, 4, 16, "BC3_SRGB" },
	{ 0x1e, 0x1d01, 4, 4, 8, "BC4" },
	{ 0x1f, 0x1e01, 4, 4, 16, "BC5" },
	{ 0x20, 0x1f0a, 4, 4, 16, "BC6H_UF16" },
	{ 0x21, 0x1f0b, 4, 4, 16, "BC6H_SF16" },
	{ 0x22, 0x2001, 4, 4, 16, "BC7" },
	{ 0x23, 0x2006, 4, 4, 16, "BC7_SRGB" },
	ASTC (0x29, 4, 4, 0x2d, 1, "ASTC_4x4"),
	ASTC (0x2a, 5, 4, 0x2e, 1, "ASTC_5x4"),
	ASTC (0x2b, 5, 5, 0x2f, 1, "ASTC_5x5"),
	ASTC (0x2c, 6, 5, 0x30, 1, "ASTC_6x5"),
	ASTC (0x2d, 6, 6, 0x31, 1, "ASTC_6x6"),
	ASTC (0x2e, 8, 5, 0x32, 1, "ASTC_8x5"),
	ASTC (0x2f, 8, 6, 0x33, 1, "ASTC_8x6"),
	ASTC (0x30, 8, 8, 0x34, 1, "ASTC_8x8"),
	ASTC (0x31, 10, 5, 0x35, 1, "ASTC_10x5"),
	ASTC (0x32, 10, 6, 0x36, 1, "ASTC_10x6"),
	ASTC (0x33, 10, 8, 0x37, 1, "ASTC_10x8"),
	ASTC (0x34, 10, 10, 0x38, 1, "ASTC_10x10"),
	ASTC (0x35, 12, 10, 0x39, 1, "ASTC_12x10"),
	ASTC (0x36, 12, 12, 0x3a, 1, "ASTC_12x12"),
	ASTC (0x37, 4, 4, 0x2d, 6, "ASTC_4x4_SRGB"),
	ASTC (0x38, 5, 4, 0x2e, 6, "ASTC_5x4_SRGB"),
	ASTC (0x39, 5, 5, 0x2f, 6, "ASTC_5x5_SRGB"),
	ASTC (0x3a, 6, 5, 0x30, 6, "ASTC_6x5_SRGB"),
	ASTC (0x3b, 6, 6, 0x31, 6, "ASTC_6x6_SRGB"),
	ASTC (0x3c, 8, 5, 0x32, 6, "ASTC_8x5_SRGB"),
	ASTC (0x3d, 8, 6, 0x33, 6, "ASTC_8x6_SRGB"),
	ASTC (0x3e, 8, 8, 0x34, 6, "ASTC_8x8_SRGB"),
	ASTC (0x3f, 10, 5, 0x35, 6, "ASTC_10x5_SRGB"),
	ASTC (0x40, 10, 6, 0x36, 6, "ASTC_10x6_SRGB"),
	ASTC (0x41, 10, 8, 0x37, 6, "ASTC_10x8_SRGB"),
	ASTC (0x42, 10, 10, 0x38, 6, "ASTC_10x10_SRGB"),
	ASTC (0x43, 12, 10, 0x39, 6, "ASTC_12x10_SRGB"),
	ASTC (0x44, 12, 12, 0x3a, 6, "ASTC_12x12_SRGB"),
	{ 0, 0, 0, 0, 0, 0 },
};
#undef ASTC

static const ptex_fmt_t *ptex_format (uint code)
{
	for (uint i = 0; ptex_formats[i].name; i++)
		if (ptex_formats[i].code == code)
			return ptex_formats + i;
	return 0;
}

// Size of a block-linear surface (all mips, all layers) for level-0 block height BHL.
// Each mip lowers the block height while the level fits in half of it; layers are
// padded to the level-0 block size.
static u64 ptex_bl_size (const ptex_info_t *pi, const ptex_fmt_t *f, uint bhl, bool pad_last)
{
	u64 total = 0;
	for (uint m = 0; m < pi->mips && m < 17; m++)
	{
		const uint w = pi->width >> m ? pi->width >> m : 1, h = pi->height >> m ? pi->height >> m : 1;
		const u64 wb = (w + f->bw - 1) / f->bw, hb = (h + f->bh - 1) / f->bh;
		uint b = bhl;
		while (b > 0 && hb <= (8u << (b - 1)))
			b--;
		const u64 pitch = (wb * f->bpb + 63) / 64 * 64;
		const u64 gob_rows = 8u << b;
		total += pitch * ((hb + gob_rows - 1) / gob_rows * gob_rows);
	}
	const u64 align = 512u << bhl;
	const u64 stride = (total + align - 1) / align * align;
	const uint layers = pi->depth ? pi->depth : 1;
	return stride * (layers - 1) + (pad_last ? stride : total);
}

// Most textures use the usual Tegra level-0 block height; some (UI ASTC 6x6 among
// them) were laid out with a smaller one. Pick the one whose size is the body's.
static uint ptex_block_height (const ptex_info_t *pi, const ptex_fmt_t *f, uint body_size)
{
	const uint rows = (pi->height + f->bh - 1) / f->bh;
	uint std = 0;
	while (std < 4 && (8u << std) < rows)
		std++;
	for (int pass = 0; pass < 2; pass++)
		for (uint b = 0; b <= 5; b++)
		{
			if (!pass && b != std)
				continue;
			if (ptex_bl_size (pi, f, b, false) == body_size || ptex_bl_size (pi, f, b, true) == body_size)
				return b;
		}
	return std;
}

enumError DecodePTEX_RGBA (u8 **dest, uint *width, uint *height, const u8 *head, uint head_size,
	const u8 *body, uint body_size, ccp *format_name)
{
	ptex_info_t pi;
	if (!ptex_info (head, head_size, &pi))
		return ERR_INVALID_DATA;
	const ptex_fmt_t *f = ptex_format (pi.format);
	if (format_name)
		*format_name = f ? f->name : 0;
	if (!f)
		return ERROR0 (ERR_INVALID_IFORM, "PTEX: unknown texture format 0x%02x (%ux%u)\n", pi.format,
			pi.width, pi.height);

	const uint bhl = ptex_block_height (&pi, f, body_size);

	u64 mip0 = 0;
	bntx_texture_t syn;
	memset (&syn, 0, sizeof (syn));
	syn.name = "ptex";
	syn.width = pi.width;
	syn.height = pi.height;
	syn.depth = 1;
	syn.array_count = 1;
	syn.format = f->bntx;
	syn.tile_mode = 0;
	syn.block_height_log2 = bhl;
	syn.is_orin = true; // Switch 2 GOB layout
	syn.n_mips = 1;
	syn.data = body;
	syn.data_size = body_size;
	syn.mip_offsets = &mip0;

	bntx_t bntx;
	memset (&bntx, 0, sizeof (bntx));
	bntx.data = body;
	bntx.size = body_size;
	bntx.n_textures = 1;
	bntx.textures = &syn;
	return DecodeBNTX_Mip_RGBA (dest, width, height, &bntx, 0, 0);
}

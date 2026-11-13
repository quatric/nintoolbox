// SPDX-License-Identifier: GPL-2.0+
// Kirby Air Riders "phx" engine: PARC catalogs over PDAT packs, and PTEX textures.
#include "lib-phx.h"
#include "lib-archive-util.h"
#include "lib-bntx.h"
#include "lib-sha256.h"
#include "lib-zstd.h"
#include "lib-aes.h"
#include <math.h>
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

// Material binding collected while a PARC catalog is extracted (see phx_bind_* below).
typedef struct phx_bind_t
{
	u64 *slot; // PMSH material-slot hashes
	uint n_slot, cap_slot;
	u64 *mat; // PMTR: {material id, first ref, ref count} triples
	uint n_mat, cap_mat;
	u64 *ref; // PMTR texture refs
	uint n_ref, cap_ref;
	u64 *tex; // PTEX: {id, width << 1 | srgb}
	uint n_tex, cap_tex;
	uint *pfab; // asset indexes of Pfab records
	uint n_pfab, cap_pfab;
	u64 *skel; // PSKS: {joint name hash, skeleton id}
	uint n_skel, cap_skel;
} phx_bind_t;

static void phx_bind_collect (phx_bind_t *b, ccp type, u64 id, uint asset, const u8 *d, uint n);
static void phx_bind_finish (phx_bind_t *b, parc_ctx_t *c, ccp dest);

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
	phx_bind_t bind;
	memset (&bind, 0, sizeof (bind));
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

		// primary block: HEAD (textures, fonts), else Main / MAIN / BIN, else the first. A HEAD that
		// is not a typed record (meshes carry 12 zero bytes there) gives way to the next candidate.
		static const char *const prefer[] = { "HEAD", "Main", "MAIN", "BIN" };
		uint prim = 0;
		char type[8] = "";
		s64 n = -1;
		for (uint pi = 0; pi < 4 && !*type; pi++)
			for (uint b = 0; b < nb; b++)
				if (!strncmp ((ccp)rec + 24 + b * PARC_BLOCK_SIZE + 16, prefer[pi], 4))
				{
					const s64 bn = parc_load (&c, le64 (rec + 24 + b * PARC_BLOCK_SIZE + 24));
					if (bn < 0)
						break;
					char bt[8];
					parc_type (c.buf, (size_t)bn, bt, sizeof (bt));
					if (strcmp (bt, "bin") || pi == 3)
					{
						prim = b;
						n = bn;
						snprintf (type, sizeof (type), "%s", bt);
					}
					break;
				}
		if (!*type)
		{
			// nothing typed: keep the first preferred (else first) block as before
			for (uint pi = 0, found = 0; pi < 4 && !found; pi++)
				for (uint b = 0; b < nb; b++)
					if (!strncmp ((ccp)rec + 24 + b * PARC_BLOCK_SIZE + 16, prefer[pi], 4))
					{
						prim = b;
						found = 1;
						break;
					}
			n = parc_load (&c, le64 (rec + 24 + prim * PARC_BLOCK_SIZE + 24));
			if (n < 0)
			{
				failed++;
				continue;
			}
			parc_type (c.buf, (size_t)n, type, sizeof (type));
		}

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

		phx_bind_collect (&bind, type, id, idx, c.buf, (uint)n);
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
	phx_bind_finish (&bind, &c, dest);
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
	{ 0x01, 0x0b01, 1, 1, 4, "RGBA8" },
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

// 3D textures (colour-grading LUTs: 33x33x33 RGBA8) are block linear in x, y and z on the
// Orin GOB layout. Block height and depth are not stored; take the smallest block height whose
// layout fills the body exactly. Output: the depth slices side by side (width * depth x height).
static uint ptex_gob_orin (uint xb, uint y)
{
	return ((xb % 64) / 32) * 256 + ((y % 8) / 4) * 128 + ((xb % 32) / 16) * 64 + (y % 4) * 16 + (xb % 16);
}

static enumError ptex_decode_3d (u8 **dest, uint *width, uint *height, const ptex_info_t *pi,
	const ptex_fmt_t *f, const u8 *body, uint body_size)
{
	if (f->bw != 1 || f->bh != 1 || f->bpb != 4 || pi->depth > 512 || pi->width * pi->depth > 65536)
		return ERROR0 (ERR_INVALID_IFORM, "PTEX: 3D %s textures are not supported (%ux%ux%u)\n", f->name,
			pi->width, pi->height, pi->depth);
	const uint W = pi->width, H = pi->height, D = pi->depth, gw = (W * 4 + 63) / 64;
	uint bh = 0, bd = 0;
	for (uint h = 1; h <= 16 && !bh; h <<= 1)
		for (uint dz = 32; dz >= 1; dz >>= 1)
		{
			const u64 gh = (H + 8 * h - 1) / (8 * h), gd = (D + dz - 1) / dz;
			if ((u64)gw * gh * gd * 512 * h * dz == body_size)
			{
				bh = h;
				bd = dz;
				break;
			}
		}
	if (!bh)
		return ERROR0 (ERR_INVALID_DATA, "PTEX: no 3D block layout fits %u bytes (%ux%ux%u)\n", body_size, W, H, D);
	const uint gh = (H + 8 * bh - 1) / (8 * bh);
	u8 *out = MALLOC ((size_t)W * D * H * 4);
	if (!out)
		return ERR_OUT_OF_MEMORY;
	const size_t blk = (size_t)512 * bh * bd;
	for (uint z = 0; z < D; z++)
		for (uint y = 0; y < H; y++)
			for (uint x = 0; x < W; x++)
			{
				const uint xb = x * 4;
				const size_t bi = ((size_t)(z / bd) * gh + y / (8 * bh)) * gw + xb / 64;
				const size_t o = bi * blk + ((size_t)(z % bd) * bh + (y % (8 * bh)) / 8) * 512 + ptex_gob_orin (xb, y);
				memcpy (out + (((size_t)y * W * D) + (size_t)z * W + x) * 4, body + o, 4);
			}
	*dest = out;
	*width = W * D;
	*height = H;
	return ERR_OK;
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

	if (pi.depth > 1)
		return ptex_decode_3d (dest, width, height, &pi, f, body, body_size);

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

//-----------------------------------------------------------------------------
// Sygnus ".soundstream" (SOL-AVES sound engine): "SYGN" .. "STRM" header, payload at 0x2c.
// Layout and ADPCM decoder after Nenkai's SolAvesSygnus_SoundStream.bt (010GameTemplates).

static const u8 sygnus_key_raw[24] = { 0xf7, 0xbe, 0x9c, 0x6a, 0x28, 0xa6, 0x69, 0x46, 0xb5, 0x3e, 0xc3, 0x05,
	0x45, 0xf8, 0xaf, 0x3d, 0x86, 0x9d, 0x1d, 0x56, 0xd6, 0x59, 0xb6, 0x1f };
static const char sygnus_key_mask[25] = "Ua#oK3P94vdxX,ft*k-mnjoO";

static bool sygnus_header_ok (const u8 *d)
{
	return !memcmp (d, "SYGN", 4) && !memcmp (d + 8, "STRM", 4);
}

static sygnus_kind_t sygnus_payload_kind (const u8 *p, uint size)
{
	if (size < 16)
		return SYGNUS_OTHER;
	const u32 v = le32 (p);
	if (!memcmp (p, "RIFF", 4))
		return SYGNUS_RIFF;
	if (v == 0x80000001)
		return SYGNUS_OPUS;
	if (v >= 1 && v <= 8 && le32 (p + 4) >= 4000 && le32 (p + 4) <= 192000 && le32 (p + 12) >= 1)
		return SYGNUS_ADPCM;
	return SYGNUS_OTHER;
}

sygnus_kind_t PrepareSygnusStream (u8 *data, uint size, bool *decrypted)
{
	if (decrypted)
		*decrypted = false;
	if (!data || size < SYGNUS_PAYLOAD + 4)
		return SYGNUS_NONE;
	if (!sygnus_header_ok (data))
	{
		// Kirby Air Riders stores most streams AES-192-ECB encrypted as a whole.
		u8 key[24], probe[16];
		for (uint i = 0; i < 24; i++)
			key[i] = sygnus_key_raw[i] ^ (u8)sygnus_key_mask[i];
		memcpy (probe, data, 16);
		AES192_ECB_Decrypt (key, probe, 16);
		if (!sygnus_header_ok (probe))
			return SYGNUS_NONE;
		AES192_ECB_Decrypt (key, data, size & ~15u);
		if (decrypted)
			*decrypted = true;
	}
	return sygnus_payload_kind (data + SYGNUS_PAYLOAD, size - SYGNUS_PAYLOAD);
}

// Step table: row r (0..31) scales 8 magnitudes by 2^(r/2); odd rows use sqrt(2) rounded to 1/256.
static float sygnus_step (uint row, uint code, uint *next)
{
	static const float m0[8] = { 1, 3.25f, 5.5f, 8, 11, 15, 21, 31 };
	static const float m1[8] = { 362 / 256.0f, 1177 / 256.0f, 1991 / 256.0f, 2896 / 256.0f,
		3982 / 256.0f, 5431 / 256.0f, 7603 / 256.0f, 11223 / 256.0f };
	static const int adj[8] = { -1, 0, 0, 0, 0, 1, 1, 4 };
	const uint mag = code >= 8 ? code - 8 : 7 - code;
	const float v = ldexpf ((row & 1 ? m1 : m0)[mag], (int)(row / 2) - 18);
	const int n = (int)row + adj[mag];
	*next = n < 0 ? 0 : n > 31 ? 31 : (uint)n;
	return code >= 8 ? v : -v;
}

sygnus_kind_t FindSygnusSource (const u8 *data, uint size, uint *payload_off)
{
	// phx "PSND" records embed a Sygnus "ASRC" bank. Banks holding one embedded source end with
	// {u32 size, payload}; streamed sources (format 3) only point at a ".soundstream".
	*payload_off = 0;
	for (uint s = 0; s + 0x24 <= size && s < 0x1000; s += 4)
	{
		if (memcmp (data + s, "SYGN", 4) || memcmp (data + s + 8, "ASRC", 4))
			continue;
		if (le32 (data + s + 0x20) != 1)
			return SYGNUS_NONE;
		for (uint o = s + 0x24; o + 4 + 16 <= size; o += 4)
			if ((u64)o + 4 + le32 (data + o) == size)
			{
				const sygnus_kind_t k = sygnus_payload_kind (data + o + 4, size - o - 4);
				if (k == SYGNUS_OTHER)
					continue;
				*payload_off = o + 4;
				return k;
			}
		return SYGNUS_NONE;
	}
	return SYGNUS_NONE;
}

enumError DecodeSygnusADPCM (s16 **pcm, uint *channels, uint *rate, uint *n_samples, const u8 *data, uint size)
{
	*pcm = 0;
	if (size < 16)
		return ERR_INVALID_DATA;
	const u8 *h = data;
	const uint ch = le32 (h), sr = le32 (h + 4), ns = le32 (h + 8), spb = le32 (h + 12);
	if (ch < 1 || ch > 8 || !ns || ns > 0x10000000 || spb < 1 || spb > 0x100000)
		return ERR_INVALID_DATA;
	s16 *out = CALLOC ((size_t)ns * ch, sizeof (s16));
	if (!out)
		return ERR_OUT_OF_MEMORY;
	const u8 *in = h + 16, *end = data + size;
	for (uint done = 0; done < ns;)
	{
		const uint n = ns - done < spb ? ns - done : spb;
		const uint bs = n >= 3 ? ((n - 1) >> 1) + 9 : 4 * n;
		if ((size_t)(end - in) < (size_t)bs * ch)
		{
			FREE (out);
			return ERR_INVALID_DATA;
		}
		for (uint c = 0; c < ch; c++, in += bs)
		{
			s16 *o = out + (size_t)done * ch + c;
			float p2 = lef4 (in), p1 = n >= 2 ? lef4 (in + 4) : 0;
			uint row = in[8] < 31 ? in[8] : 31;
			for (uint t = 0; t < n; t++)
			{
				float v = t == 0 ? p2 : p1;
				if (t >= 2)
				{
					const u8 b = in[9 + (t - 2) / 2];
					v = sygnus_step (row, (t - 2) & 1 ? b >> 4 : b & 15, &row) + 2 * p1 - p2;
					p2 = p1;
					p1 = v;
				}
				const float x = v * 32767.0f;
				o[(size_t)t * ch] = x >= 32767 ? 32767 : x <= -32768 ? -32768 : (s16)lrintf (x);
			}
		}
		done += n;
	}
	*pcm = out;
	*channels = ch;
	*rate = sr;
	*n_samples = ns;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// PMSH mesh: "Main" flatbuffer + "GBuf" vertex/index data (+ "CBuf", not needed for geometry).
// root.f3 geometry { f1 [stream {f1 attribute, f2 size, f3 GBuf offset, f4 format}],
//                    f2 [submesh {f1 material, f3 vertex count, f4 first index, f5 index count}],
//                    f4 [{f0 u64 material slot hash}], f5 bounds, f11 GBuf size }
// Attributes: 0 position f32x3, 1 normal / 2 tangent unorm 10:10:10:2, 8..12 UV half2,
// 24..26 colour RGBA8, 40 indices (format 1: u16, 2: u32), 41 CBuf data.

static uint fb_ref (const u8 *d, uint n, uint t, uint field)
{
	const uint a = fb_field (d, n, t, field, 4);
	if (!a)
		return 0;
	const u64 r = (u64)a + le32 (d + a);
	return r + 4 <= n ? (uint)r : 0;
}

static u32 fb_u32 (const u8 *d, uint n, uint t, uint field, u32 def)
{
	const uint a = fb_field (d, n, t, field, 4);
	return a ? le32 (d + a) : def;
}

static uint fb_vec (const u8 *d, uint n, uint t, uint field, uint *count)
{
	*count = 0;
	const uint v = fb_ref (d, n, t, field);
	if (!v)
		return 0;
	const u32 c = le32 (d + v);
	if ((u64)v + 4 + (u64)c * 4 > n)
		return 0;
	*count = c;
	return v + 4;
}

static uint fb_vec_table (const u8 *d, uint n, uint base, uint i)
{
	const uint a = base + 4 * i;
	const u64 r = (u64)a + le32 (d + a);
	return r + 4 <= n ? (uint)r : 0;
}

static float half_f (u16 h)
{
	const int e = h >> 10 & 31, m = h & 1023;
	const float v = e == 0 ? ldexpf ((float)m, -24) : e == 31 ? (m ? NAN : INFINITY) : ldexpf ((float)(m | 1024), e - 25);
	return h & 0x8000 ? -v : v;
}

bool IsPMSH (const u8 *data, uint size)
{
	return data && size >= 0x20 && !memcmp (data + 4, "PMSH", 4) && le32 (data) < size;
}

typedef struct pmsh_stream_t
{
	const u8 *p;
	uint size, format;
} pmsh_stream_t;

// Skinning. Attribute 4 holds four {u16 palette index, u16 weight / 65535} pairs per vertex;
// geometry.f3 is the joint palette: {f1 {u64 joint-name hash}, f2 inverse bind matrix (4x4 f32,
// column-major)}. The palette alone gives a flat skeleton; PMSHApplySkeleton adds the PSKS
// hierarchy. Influences are indexed by source vertex (position_node = vertex index).

static void pmsh_joint_name (char *buf, size_t sz, u64 hash)
{
	snprintf (buf, sz, "joint_%016llx", (unsigned long long)hash);
}

// 4x4 column-major -> joint_t's 3x4 row-major
static void pmsh_m44_to_43 (float out[12], const float m[16])
{
	for (uint r = 0; r < 3; r++)
		for (uint c = 0; c < 4; c++)
			out[r * 4 + c] = m[c * 4 + r];
}

static bool pmsh_invert43 (float out[12], const float m[12])
{
	const double det = (double)m[0] * (m[5] * m[10] - m[6] * m[9]) - (double)m[1] * (m[4] * m[10] - m[6] * m[8])
		+ (double)m[2] * (m[4] * m[9] - m[5] * m[8]);
	if (fabs (det) < 1e-20)
		return false;
	const float d = (float)(1.0 / det);
	out[0] = (m[5] * m[10] - m[6] * m[9]) * d;
	out[1] = (m[2] * m[9] - m[1] * m[10]) * d;
	out[2] = (m[1] * m[6] - m[2] * m[5]) * d;
	out[4] = (m[6] * m[8] - m[4] * m[10]) * d;
	out[5] = (m[0] * m[10] - m[2] * m[8]) * d;
	out[6] = (m[2] * m[4] - m[0] * m[6]) * d;
	out[8] = (m[4] * m[9] - m[5] * m[8]) * d;
	out[9] = (m[1] * m[8] - m[0] * m[9]) * d;
	out[10] = (m[0] * m[5] - m[1] * m[4]) * d;
	out[3] = -(out[0] * m[3] + out[1] * m[7] + out[2] * m[11]);
	out[7] = -(out[4] * m[3] + out[5] * m[7] + out[6] * m[11]);
	out[11] = -(out[8] * m[3] + out[9] * m[7] + out[10] * m[11]);
	return true;
}

static void pmsh_mul43 (float out[12], const float a[12], const float b[12])
{
	float t[12];
	for (uint r = 0; r < 3; r++)
		for (uint c = 0; c < 4; c++)
			t[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c] + (c == 3 ? a[r * 4 + 3] : 0);
	memcpy (out, t, sizeof (t));
}

static bool pmsh_skin (model_t *model, const u8 *d, uint n, uint geo, const u8 *sk, uint nv)
{
	uint np = 0;
	const uint pv = fb_vec (d, n, geo, 3, &np);
	if (!pv || !np || np > 1024)
		return false;
	model->joints = CALLOC (np, sizeof (*model->joints));
	model->node_influences = CALLOC (nv, sizeof (*model->node_influences));
	if (!model->joints || !model->node_influences)
		goto fail;
	model->num_joints = np;
	for (uint i = 0; i < np; i++)
	{
		joint_t *j = model->joints + i;
		const uint t = fb_vec_table (d, n, pv, i), nt = t ? fb_ref (d, n, t, 1) : 0;
		const uint hp = nt ? fb_field (d, n, nt, 0, 8) : 0, mp = t ? fb_field (d, n, t, 2, 64) : 0;
		pmsh_joint_name (j->name, sizeof (j->name), hp ? le64 (d + hp) : i);
		j->parent_idx = -1;
		j->scale = (vec3_t){ 1, 1, 1 };
		if (mp)
		{
			float m[16];
			for (uint k = 0; k < 16; k++)
				m[k] = lef4 (d + mp + 4 * k);
			pmsh_m44_to_43 (j->inverse_bind, m);
			j->has_inverse_bind = pmsh_invert43 (j->bind, j->inverse_bind);
		}
		if (!j->has_inverse_bind)
		{
			memset (j->bind, 0, sizeof (j->bind));
			j->bind[0] = j->bind[5] = j->bind[10] = 1;
			memcpy (j->inverse_bind, j->bind, sizeof (j->bind));
			j->has_inverse_bind = 1;
		}
	}
	for (uint v = 0; v < nv; v++)
	{
		node_influence_t *ni = model->node_influences + v;
		ni->weights = MALLOC (4 * sizeof (*ni->weights));
		if (!ni->weights)
			goto fail;
		for (uint k = 0; k < 4; k++)
		{
			const u16 ji = le16 (sk + (size_t)v * 16 + k * 4), w = le16 (sk + (size_t)v * 16 + k * 4 + 2);
			if (w && ji < np)
				ni->weights[ni->num_weights++] = (influence_t){ ji, w / 65535.0f };
		}
		if (!ni->num_weights)
			ni->weights[ni->num_weights++] = (influence_t){ 0, 1.0f };
	}
	model->num_node_influences = nv;
	return true;
fail:
	for (uint v = 0; model->node_influences && v < nv; v++)
		FREE (model->node_influences[v].weights);
	FREE (model->joints);
	FREE (model->node_influences);
	model->joints = 0;
	model->node_influences = 0;
	model->num_joints = 0;
	return false;
}

bool IsPSKS (const u8 *data, uint size)
{
	return data && size >= 0x20 && !memcmp (data + 4, "PSKS", 4) && le32 (data) < size;
}

// PSKS skeleton: f3.f1 = joints {f2 {u64 name hash}, f4 u16 parent index (0xffff = root),
// f5 translation {x,y,z}, f6 rotation quaternion {x,y,z,w} (all zero = identity), f7 scale}.
// PSKS joints -> joint_t: name "joint_<hash>", parent, local TRS (rotate = XYZ Euler degrees, R = Rz Ry Rx)
// and the absolute bind matrix with its inverse.
static joint_t *psks_joints (const u8 *d, uint n, uint *count)
{
	*count = 0;
	if (!IsPSKS (d, n))
		return 0;
	const uint sk = fb_ref (d, n, le32 (d), 3);
	uint nj = 0;
	const uint jv = sk ? fb_vec (d, n, sk, 1, &nj) : 0;
	if (!jv || !nj || nj > 4096)
		return 0;
	joint_t *js = CALLOC (nj, sizeof (*js));
	if (!js)
		return 0;
	for (uint i = 0; i < nj; i++)
	{
		joint_t *j = js + i;
		const uint t = fb_vec_table (d, n, jv, i), nt = t ? fb_ref (d, n, t, 2) : 0;
		const uint hp = nt ? fb_field (d, n, nt, 0, 8) : 0, pp = t ? fb_field (d, n, t, 4, 2) : 0;
		pmsh_joint_name (j->name, sizeof (j->name), hp ? le64 (d + hp) : i);
		const uint par = pp ? le16 (d + pp) : 0xffff;
		j->parent_idx = par < i ? (int)par : -1; // parents precede children
		float tr[3] = { 0, 0, 0 }, q[4] = { 0, 0, 0, 0 }, s[3] = { 1, 1, 1 };
		const uint tt = t ? fb_ref (d, n, t, 5) : 0, rt = t ? fb_ref (d, n, t, 6) : 0, st = t ? fb_ref (d, n, t, 7) : 0;
		for (uint k = 0; k < 4; k++)
		{
			uint a;
			if (k < 3 && tt && (a = fb_field (d, n, tt, k, 4)))
				tr[k] = lef4 (d + a);
			if (rt && (a = fb_field (d, n, rt, k, 4)))
				q[k] = lef4 (d + a);
			if (k < 3 && st && (a = fb_field (d, n, st, k, 4)))
				s[k] = lef4 (d + a);
		}
		const double qn = sqrt ((double)q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
		double x = 0, y = 0, z = 0, w = 1;
		if (qn > 1e-6)
			x = q[0] / qn, y = q[1] / qn, z = q[2] / qn, w = q[3] / qn;
		const float local[12] = { (float)(1 - 2 * (y * y + z * z)) * s[0], (float)(2 * (x * y - z * w)) * s[1],
			(float)(2 * (x * z + y * w)) * s[2], tr[0], (float)(2 * (x * y + z * w)) * s[0],
			(float)(1 - 2 * (x * x + z * z)) * s[1], (float)(2 * (y * z - x * w)) * s[2], tr[1],
			(float)(2 * (x * z - y * w)) * s[0], (float)(2 * (y * z + x * w)) * s[1],
			(float)(1 - 2 * (x * x + y * y)) * s[2], tr[2] };
		j->translate = (vec3_t){ tr[0], tr[1], tr[2] };
		j->scale = (vec3_t){ s[0], s[1], s[2] };
		const double r20 = 2 * (x * z - y * w), r21 = 2 * (y * z + x * w), r22 = 1 - 2 * (x * x + y * y),
					 r10 = 2 * (x * y + z * w), r00 = 1 - 2 * (y * y + z * z);
		if (fabs (r20) < 0.999999)
			j->rotate = (vec3_t){ (float)(atan2 (r21, r22) * 180 / M_PI), (float)(asin (-r20) * 180 / M_PI),
				(float)(atan2 (r10, r00) * 180 / M_PI) };
		else
		{
			// gimbal lock (pitch +-90): fold the yaw into roll
			const double r01 = 2 * (x * y - z * w), r02 = 2 * (x * z + y * w);
			j->rotate = r20 < 0 ? (vec3_t){ (float)(atan2 (r01, r02) * 180 / M_PI), 90, 0 }
								: (vec3_t){ (float)(atan2 (-r01, -r02) * 180 / M_PI), -90, 0 };
		}
		if (j->parent_idx >= 0)
			pmsh_mul43 (j->bind, js[j->parent_idx].bind, local);
		else
			memcpy (j->bind, local, sizeof (local));
		j->has_inverse_bind = pmsh_invert43 (j->inverse_bind, j->bind);
	}
	*count = nj;
	return js;
}

uint PMSHApplySkeleton (model_t *model, const u8 *d, uint n)
{
	if (!model || !model->num_joints)
		return 0;
	uint nj = 0, matched = 0;
	joint_t *js = psks_joints (d, n, &nj);
	int *remap = js ? MALLOC (model->num_joints * sizeof (int)) : 0;
	if (!js || !remap)
		goto out;
	for (uint p = 0; p < model->num_joints; p++)
	{
		remap[p] = -1;
		for (uint i = 0; i < nj && remap[p] < 0; i++)
			if (!strcmp (model->joints[p].name, js[i].name))
				remap[p] = (int)i;
		matched += remap[p] >= 0;
	}
	if (matched < model->num_joints)
	{
		matched = 0; // a different skeleton: keep the flat palette
		goto out;
	}
	for (size_t v = 0; v < model->num_node_influences; v++)
	{
		node_influence_t *ni = model->node_influences + v;
		for (size_t k = 0; k < ni->num_weights; k++)
			ni->weights[k].bone_idx = remap[ni->weights[k].bone_idx];
	}
	FREE (model->joints);
	model->joints = js;
	model->num_joints = nj;
	js = 0;
out:
	FREE (js);
	FREE (remap);
	return matched;
}

model_t *DecodePMSH (const u8 *main_d, uint main_n, const u8 *gbuf, uint gbuf_n)
{
	if (!IsPMSH (main_d, main_n) || !gbuf)
		return 0;
	const uint geo = fb_ref (main_d, main_n, le32 (main_d), 3);
	if (!geo)
		return 0;
	pmsh_stream_t st[64] = { { 0 } };
	uint ns, nsub;
	const uint sv = fb_vec (main_d, main_n, geo, 1, &ns), mv = fb_vec (main_d, main_n, geo, 2, &nsub);
	if (!sv || !mv || !nsub)
		return 0;
	for (uint i = 0; i < ns; i++)
	{
		const uint t = fb_vec_table (main_d, main_n, sv, i);
		if (!t)
			return 0;
		const u32 attr = fb_u32 (main_d, main_n, t, 1, 0), size = fb_u32 (main_d, main_n, t, 2, 0),
				  off = fb_u32 (main_d, main_n, t, 3, 0);
		const uint fa = fb_field (main_d, main_n, t, 4, 1);
		if (attr >= 64 || attr == 41 || (u64)off + size > gbuf_n)
			continue;
		st[attr].p = gbuf + off;
		st[attr].size = size;
		st[attr].format = fa ? main_d[fa] : 0;
	}
	const uint nv = st[0].size / 12;
	if (!st[0].p || !nv || !st[40].p)
		return 0;
	const uint isz = st[40].format == 2 ? 4 : 2;
	const uint n_idx = st[40].size / isz;
	const bool has_nrm = st[1].p && st[1].size >= nv * 4, has_uv = st[8].p && st[8].format == 2 && st[8].size >= nv * 4,
			   has_col = st[24].p && st[24].size == nv * 4;

	model_t *model = CALLOC (1, sizeof (*model));
	if (!model)
		return 0;
	const bool has_skin = st[4].p && st[4].size >= nv * 16 && pmsh_skin (model, main_d, main_n, geo, st[4].p, nv);
	int *map = MALLOC (nv * sizeof (int));
	if (!map)
	{
		FREE (model);
		return 0;
	}
	uint max_mat = 0;
	for (uint s = 0; s < nsub; s++)
	{
		const uint t = fb_vec_table (main_d, main_n, mv, s);
		const u32 mat = t ? fb_u32 (main_d, main_n, t, 1, 0) : 0, first = t ? fb_u32 (main_d, main_n, t, 4, 0) : 0,
				  cnt = t ? fb_u32 (main_d, main_n, t, 5, 0) : 0;
		if (!t || mat > 255 || !cnt || cnt % 3 || (u64)first + cnt > n_idx)
			continue;
		if (mat + 1 > max_mat)
			max_mat = mat + 1;
		mesh_t *nm = REALLOC (model->meshes, (model->num_meshes + 1) * sizeof (*nm));
		if (!nm)
			break;
		model->meshes = nm;
		mesh_t *mesh = nm + model->num_meshes;
		memset (mesh, 0, sizeof (*mesh));
		model->num_meshes++;
		snprintf (mesh->name, sizeof (mesh->name), "submesh_%u", s);
		mesh->material_idx = (int)mat;
		mesh->vertices = CALLOC (cnt, sizeof (*mesh->vertices));
		mesh->positions = CALLOC (cnt, sizeof (*mesh->positions));
		if (has_nrm)
			mesh->normals = CALLOC (cnt, sizeof (*mesh->normals));
		if (has_uv)
			mesh->texcoords = CALLOC (cnt, sizeof (*mesh->texcoords));
		if (has_col)
			mesh->colors[0] = CALLOC (cnt, sizeof (*mesh->colors[0]));
		if (has_skin)
			mesh->position_node = CALLOC (cnt, sizeof (int));
		if (!mesh->vertices || !mesh->positions || (has_nrm && !mesh->normals) || (has_uv && !mesh->texcoords)
			|| (has_col && !mesh->colors[0]) || (has_skin && !mesh->position_node))
			break;
		memset (map, -1, nv * sizeof (int));
		uint nu = 0;
		for (uint k = 0; k < cnt; k++)
		{
			const u8 *ip = st[40].p + (size_t)(first + k) * isz;
			const u32 vi = isz == 4 ? le32 (ip) : le16 (ip);
			vertex_t *v = mesh->vertices + k;
			v->tangent_idx = v->matrix_idx = v->color_idx[1] = -1;
			for (int e = 0; e < 7; e++)
				v->extra_texcoord_idx[e] = -1;
			if (vi >= nv)
			{
				// out-of-range index: point at vertex 0 of this submesh
				v->position_idx = v->normal_idx = v->texcoord_idx = v->color_idx[0] = 0;
				continue;
			}
			if (map[vi] < 0)
			{
				map[vi] = (int)nu;
				if (has_skin)
					mesh->position_node[nu] = (int)vi;
				const u8 *pp = st[0].p + (size_t)vi * 12;
				mesh->positions[nu] = (vec3_t){ lef4 (pp), lef4 (pp + 4), lef4 (pp + 8) };
				if (has_nrm)
				{
					const u32 w = le32 (st[1].p + (size_t)vi * 4);
					mesh->normals[nu] = (vec3_t){ (w & 1023) / 1023.0f * 2 - 1, (w >> 10 & 1023) / 1023.0f * 2 - 1,
						(w >> 20 & 1023) / 1023.0f * 2 - 1 };
				}
				if (has_uv)
				{
					const u8 *q = st[8].p + (size_t)vi * 4;
					mesh->texcoords[nu] = (vec2_t){ half_f (le16 (q)), half_f (le16 (q + 2)) };
				}
				if (has_col)
				{
					const u8 *q = st[24].p + (size_t)vi * 4;
					mesh->colors[0][nu] = (color4_t){ q[0] / 255.0f, q[1] / 255.0f, q[2] / 255.0f, q[3] / 255.0f };
				}
				nu++;
			}
			v->position_idx = map[vi];
			v->normal_idx = has_nrm ? map[vi] : -1;
			v->texcoord_idx = has_uv ? map[vi] : -1;
			v->color_idx[0] = has_col ? map[vi] : -1;
		}
		mesh->num_vertices = cnt;
		mesh->num_positions = nu;
		mesh->num_normals = has_nrm ? nu : 0;
		mesh->num_texcoords = has_uv ? nu : 0;
		mesh->num_colors[0] = has_col ? nu : 0;
	}
	FREE (map);
	if (max_mat)
	{
		model->materials = CALLOC (max_mat, sizeof (*model->materials));
		if (model->materials)
		{
			model->num_materials = max_mat;
			for (uint i = 0; i < max_mat; i++)
			{
				material_t *mt = model->materials + i;
				// geometry.f4: one 64-bit slot-name hash per material index
				uint nslot = 0;
				const uint slots = fb_vec (main_d, main_n, geo, 4, &nslot);
				const uint st_t = slots && i < nslot ? fb_vec_table (main_d, main_n, slots, i) : 0;
				const uint hp = st_t ? fb_field (main_d, main_n, st_t, 0, 8) : 0;
				if (hp)
					snprintf (mt->name, sizeof (mt->name), "material_%016llx", (unsigned long long)le64 (main_d + hp));
				else
					snprintf (mt->name, sizeof (mt->name), "material_%u", i);
				mt->diffuse[0] = mt->diffuse[1] = mt->diffuse[2] = mt->diffuse[3] = 1.0f;
			}
		}
	}
	if (!model->num_meshes || !model->meshes[model->num_meshes - 1].vertices)
	{
		FreeModel (model);
		return 0;
	}
	return model;
}

//-----------------------------------------------------------------------------
// PANM animation (Main flatbuffer). root.f3 = {f1 u32 last frame, f2 f32 fps, f5 groups {f2 targets
// {f1 {u64 target name hash}, f2 channels {f1 {u64 property hash}, f2 u8 value type, f3 u8 kind,
// f4 u32 keys, f6 u32 offset, f7 u32 size}}}, f6 u32 blob file offset, f7 u32 blob size}.
// Value type 1 is a transform: 10 floats {scale xyz, rotation xyzw, translation xyz}. Kind 2 stores
// them as a constant; kind 3 is a quantized key stream:
//   +0 u16 version, +2 u16 flags (1 scale, 2 uniform scale, 4 rotation, 8 translation, 16 flag bit),
//   +4 u16 offset of the reference transform, +6 u16 bits per key, +8 u32 key data offset, +12 u32 keys,
//   +16 9 x {f32 min, f32 max, u16 bits, pad} for scale xyz, rotation xyz, translation xyz.
// Each key packs, LSB first, the present components (min + (max - min) * v / (2^bits - 1); a zero-width
// component keeps the reference value), then for rotation a sign bit for w = +-sqrt(1 - x^2-y^2-z^2).

bool IsPANM (const u8 *data, uint size)
{
	return data && size >= 0x20 && !memcmp (data + 4, "PANM", 4) && le32 (data) < size;
}

static u32 panm_bits (const u8 *b, uint bn, uint pos, uint bits)
{
	u64 v = 0;
	for (uint k = 0; k < 5; k++)
		if ((pos >> 3) + k < bn)
			v |= (u64)b[(pos >> 3) + k] << (8 * k);
	return (u32)(v >> (pos & 7)) & (u32)((1ull << bits) - 1);
}

static bool panm_sample (const u8 *b, uint bn, uint key, float out[10])
{
	if (bn < 16 + 9 * 16)
		return false;
	const uint flags = le16 (b + 2), ref = le16 (b + 4), bpk = le16 (b + 6);
	if ((u64)ref + 40 > bn)
		return false;
	for (uint i = 0; i < 10; i++)
		out[i] = lef4 (b + ref + 4 * i);
	uint pos = bpk * key + le32 (b + 8) * 8;
	float v;
#define PANM_READ(rec, dst)                                                                                         \
	do                                                                                                              \
	{                                                                                                               \
		const u8 *r_ = b + 16 + 16 * (rec);                                                                         \
		const uint bits_ = le16 (r_ + 8);                                                                           \
		if (bits_ && bits_ <= 32)                                                                                   \
		{                                                                                                           \
			v = (float)(lef4 (r_) + ((double)lef4 (r_ + 4) - lef4 (r_)) * panm_bits (b, bn, pos, bits_)          \
					/ (double)((1ull << bits_) - 1));                                                               \
			pos += bits_;                                                                                           \
			dst = v;                                                                                                \
		}                                                                                                           \
	} while (0)
	if (flags & 1)
	{
		if (flags & 2)
		{
			float u = out[0];
			PANM_READ (0, u);
			out[0] = out[1] = out[2] = u;
		}
		else
			for (uint i = 0; i < 3; i++)
				PANM_READ (i, out[i]);
	}
	if (flags & 4)
		for (uint i = 0; i < 3; i++)
			PANM_READ (3 + i, out[3 + i]);
	if (flags & 8)
		for (uint i = 0; i < 3; i++)
			PANM_READ (6 + i, out[7 + i]);
#undef PANM_READ
	if (flags & 4)
	{
		const double x = out[3], y = out[4], z = out[5], w2 = 1 - (x * x + y * y + z * z);
		double w = w2 > 0 ? sqrt (w2) : 0;
		if (panm_bits (b, bn, pos, 1))
			w = -w;
		const double qn = sqrt (x * x + y * y + z * z + w * w);
		if (qn > 1e-9)
		{
			out[3] = (float)(x / qn);
			out[4] = (float)(y / qn);
			out[5] = (float)(z / qn);
			out[6] = (float)(w / qn);
		}
	}
	return true;
}

typedef struct panm_chan_t
{
	u64 target;
	uint kind, keys;
	const u8 *b;
	uint bn;
} panm_chan_t;

// Transform channels of a PANM; returns the count (array in *out), 0 when none.
static uint panm_channels (const u8 *d, uint n, panm_chan_t **out, float *fps)
{
	*out = 0;
	if (!IsPANM (d, n))
		return 0;
	const uint a = fb_ref (d, n, le32 (d), 3);
	if (!a)
		return 0;
	const uint fp = fb_field (d, n, a, 2, 4);
	*fps = fp && lef4 (d + fp) > 0 && lef4 (d + fp) < 1000 ? lef4 (d + fp) : 30.0f;
	const u32 blob = fb_u32 (d, n, a, 6, 0), blob_n = fb_u32 (d, n, a, 7, 0);
	if ((u64)blob + blob_n > n)
		return 0;
	uint ng = 0, cnt = 0, cap = 0;
	const uint gv = fb_vec (d, n, a, 5, &ng);
	for (uint g = 0; gv && g < ng; g++)
	{
		const uint gt = fb_vec_table (d, n, gv, g);
		uint nt = 0;
		const uint tv = gt ? fb_vec (d, n, gt, 2, &nt) : 0;
		for (uint t = 0; tv && t < nt; t++)
		{
			const uint tt = fb_vec_table (d, n, tv, t), nm = tt ? fb_ref (d, n, tt, 1) : 0;
			const uint hp = nm ? fb_field (d, n, nm, 0, 8) : 0;
			uint nc = 0;
			const uint cv = tt ? fb_vec (d, n, tt, 2, &nc) : 0;
			for (uint c = 0; hp && cv && c < nc; c++)
			{
				const uint ct = fb_vec_table (d, n, cv, c), tp = ct ? fb_field (d, n, ct, 2, 1) : 0;
				const uint kp = ct ? fb_field (d, n, ct, 3, 1) : 0;
				if (!tp || d[tp] != 1 || !kp)
					continue;
				const u32 off = fb_u32 (d, n, ct, 6, 0), sz = fb_u32 (d, n, ct, 7, 0);
				if ((u64)off + sz > blob_n || sz < 40)
					continue;
				if (cnt == cap)
				{
					cap = cap ? cap * 2 : 64;
					panm_chan_t *nn = REALLOC (*out, cap * sizeof (**out));
					if (!nn)
						return cnt;
					*out = nn;
				}
				panm_chan_t *pc = *out + cnt++;
				pc->target = le64 (d + hp);
				pc->kind = d[kp];
				pc->keys = d[kp] == 3 && sz >= 16 ? le32 (d + blob + off + 12) : 1;
				pc->b = d + blob + off;
				pc->bn = sz;
				if (pc->kind == 3 && (sz < 16 + 9 * 16 || !pc->keys || pc->keys > 1u << 20))
					cnt--;
			}
		}
	}
	if (!cnt)
	{
		FREE (*out);
		*out = 0;
	}
	return cnt;
}

uint PANMTargets (const u8 *d, uint n, u64 **targets)
{
	panm_chan_t *ch = 0;
	float fps;
	const uint nc = panm_channels (d, n, &ch, &fps);
	*targets = nc ? MALLOC (nc * sizeof (u64)) : 0;
	uint k = 0;
	for (uint i = 0; *targets && i < nc; i++)
		(*targets)[k++] = ch[i].target;
	FREE (ch);
	return k;
}

model_t *DecodePANM (const u8 *d, uint n, const u8 *psks, uint psks_n, ccp name, uint *matched)
{
	*matched = 0;
	panm_chan_t *ch = 0;
	float fps = 30;
	const uint nc = panm_channels (d, n, &ch, &fps);
	if (!nc)
		return 0;
	model_t *model = CALLOC (1, sizeof (*model));
	uint nj = 0;
	joint_t *js = psks ? psks_joints (psks, psks_n, &nj) : 0;
	if (!js)
	{
		// no skeleton: one flat joint per animated target
		js = CALLOC (nc, sizeof (*js));
		for (uint i = 0; js && i < nc; i++)
		{
			uint k = 0;
			while (k < nj && strtoull (js[k].name + 6, 0, 16) != ch[i].target)
				k++;
			if (k < nj)
				continue;
			joint_t *j = js + nj++;
			pmsh_joint_name (j->name, sizeof (j->name), ch[i].target);
			j->parent_idx = -1;
			j->scale = (vec3_t){ 1, 1, 1 };
		}
	}
	model_anim_channel_t *ac = CALLOC ((size_t)nc * 3, sizeof (*ac));
	if (!model || !js || !ac)
		goto fail;
	// a skin without a mesh makes importers build an armature; the binds stay so the rest pose is right
	model->skeleton_skin = 1;
	model->joints = js;
	model->num_joints = nj;
	js = 0;
	model->animations = CALLOC (1, sizeof (*model->animations));
	if (!model->animations)
		goto fail;
	model->num_animations = 1;
	model_animation_t *an = model->animations;
	snprintf (an->name, sizeof (an->name), "%s", name ? name : "panm");
	an->channels = ac;
	uint na = 0;
	for (uint i = 0; i < nc; i++)
	{
		char jn[64];
		pmsh_joint_name (jn, sizeof (jn), ch[i].target);
		int node = -1;
		for (uint k = 0; k < model->num_joints && node < 0; k++)
			if (!strcmp (model->joints[k].name, jn))
				node = (int)k;
		if (node < 0)
			continue;
		(*matched)++;
		const uint keys = ch[i].keys;
		float *tm = MALLOC (keys * sizeof (float)), *tv = MALLOC (keys * 3 * sizeof (float)),
			  *rv = MALLOC (keys * 4 * sizeof (float)), *sv = MALLOC (keys * 3 * sizeof (float));
		float *t2 = MALLOC (keys * sizeof (float)), *t3 = MALLOC (keys * sizeof (float));
		if (!tm || !tv || !rv || !sv || !t2 || !t3)
		{
			FREE (tm), FREE (tv), FREE (rv), FREE (sv), FREE (t2), FREE (t3);
			continue;
		}
		for (uint k = 0; k < keys; k++)
		{
			float o[10];
			if (ch[i].kind == 3)
				panm_sample (ch[i].b, ch[i].bn, k, o);
			else
				for (uint c = 0; c < 10; c++)
					o[c] = lef4 (ch[i].b + 4 * c);
			tm[k] = t2[k] = t3[k] = k / fps;
			memcpy (sv + 3 * k, o, 3 * sizeof (float));
			memcpy (rv + 4 * k, o + 3, 4 * sizeof (float));
			memcpy (tv + 3 * k, o + 7, 3 * sizeof (float));
		}
		ac[na++] = (model_anim_channel_t){ node, MODEL_ANIM_TRANSLATION, tm, tv, keys, 3 };
		ac[na++] = (model_anim_channel_t){ node, MODEL_ANIM_ROTATION, t2, rv, keys, 4 };
		ac[na++] = (model_anim_channel_t){ node, MODEL_ANIM_SCALE, t3, sv, keys, 3 };
	}
	an->num_channels = na;
	FREE (ch);
	if (!na)
	{
		FreeModel (model);
		return 0;
	}
	return model;
fail:
	FREE (ch);
	FREE (js);
	if (model && !model->animations)
		FREE (ac);
	FreeModel (model);
	return 0;
}

//-----------------------------------------------------------------------------
// Kirby Air Riders "engine_bin" obfuscation (startup.dat, Bin/config.bin): out[i] = InvSbox[in[i] ^ in[i+1]],
// one byte shorter than the input. startup.dat then holds MessagePack values (mount table: directory ->
// 128-bit digest, labels -> "engine_bin:/..." targets); config.bin holds "Prism" data for Bin/schema.bin.

uint PhxDeobfuscate (u8 *out, const u8 *in, uint n)
{
	const u8 *t = AES_InvSbox ();
	for (uint i = 0; i + 1 < n; i++)
		out[i] = t[in[i] ^ in[i + 1]];
	return n ? n - 1 : 0;
}

static bool mp_json (FILE *f, const u8 *d, uint n, uint *pos, uint depth)
{
#define MP_NEED(k) \
	if (*pos + (k) > n) \
		return false
	if (depth > 64)
		return false;
	MP_NEED (1);
	const u8 b = d[(*pos)++];
	u64 len = 0;
	uint kind; // 0 str, 1 bin, 2 array, 3 map, 4 ext
	if (b <= 0x7f)
	{
		if (f)
			fprintf (f, "%u", b);
		return true;
	}
	if (b >= 0xe0)
	{
		if (f)
			fprintf (f, "%d", (s8)b);
		return true;
	}
	if ((b & 0xe0) == 0xa0)
		kind = 0, len = b & 31;
	else if ((b & 0xf0) == 0x90)
		kind = 2, len = b & 15;
	else if ((b & 0xf0) == 0x80)
		kind = 3, len = b & 15;
	else
		switch (b)
		{
		case 0xc0:
			if (f)
				fputs ("null", f);
			return true;
		case 0xc2:
		case 0xc3:
			if (f)
				fputs (b == 0xc3 ? "true" : "false", f);
			return true;
		case 0xc4: case 0xc5: case 0xc6: case 0xd9: case 0xda: case 0xdb:
		{
			const uint w = (b == 0xc4 || b == 0xd9) ? 1 : (b == 0xc5 || b == 0xda) ? 2 : 4;
			MP_NEED (w);
			len = w == 1 ? d[*pos] : w == 2 ? be16 (d + *pos) : be32 (d + *pos);
			*pos += w;
			kind = b >= 0xd9 ? 0 : 1;
			break;
		}
		case 0xdc: case 0xdd: case 0xde: case 0xdf:
		{
			const uint w = (b == 0xdc || b == 0xde) ? 2 : 4;
			MP_NEED (w);
			len = w == 2 ? be16 (d + *pos) : be32 (d + *pos);
			*pos += w;
			kind = b <= 0xdd ? 2 : 3;
			break;
		}
		case 0xca:
			MP_NEED (4);
			if (f)
			{
				const u32 u = be32 (d + *pos);
				float v;
				memcpy (&v, &u, 4);
				fprintf (f, "%.9g", v);
			}
			*pos += 4;
			return true;
		case 0xcb:
			MP_NEED (8);
			if (f)
			{
				const u64 u = be64 (d + *pos);
				double v;
				memcpy (&v, &u, 8);
				fprintf (f, "%.17g", v);
			}
			*pos += 8;
			return true;
		case 0xcc: case 0xcd: case 0xce: case 0xcf: case 0xd0: case 0xd1: case 0xd2: case 0xd3:
		{
			const uint w = 1u << (b & 3);
			MP_NEED (w);
			u64 u = 0;
			for (uint k = 0; k < w; k++)
				u = u << 8 | d[*pos + k];
			*pos += w;
			if (f)
			{
				if (b >= 0xd0)
				{
					const s64 sv = w == 8 ? (s64)u : (s64)(u ^ (1ull << (8 * w - 1))) - (s64)(1ull << (8 * w - 1));
					fprintf (f, "%lld", (long long)sv);
				}
				else
					fprintf (f, "%llu", (unsigned long long)u);
			}
			return true;
		}
		case 0xd4: case 0xd5: case 0xd6: case 0xd7: case 0xd8:
			kind = 4, len = 1u << (b - 0xd4);
			MP_NEED (1);
			(*pos)++; // ext type
			break;
		case 0xc7: case 0xc8: case 0xc9:
		{
			const uint w = 1u << (b - 0xc7);
			MP_NEED (w + 1);
			len = w == 1 ? d[*pos] : w == 2 ? be16 (d + *pos) : be32 (d + *pos);
			*pos += w + 1;
			kind = 4;
			break;
		}
		default:
			return false;
		}
	if (kind <= 1 || kind == 4)
	{
		MP_NEED (len);
		if (f)
		{
			fputc ('"', f);
			for (u64 k = 0; k < len; k++)
			{
				const u8 c = d[*pos + k];
				if (kind)
					fprintf (f, "%02x", c);
				else if (c == '"' || c == '\\')
					fprintf (f, "\\%c", c);
				else if (c < 0x20)
					fprintf (f, "\\u%04x", c);
				else
					fputc (c, f);
			}
			fputc ('"', f);
		}
		*pos += (uint)len;
		return true;
	}
	if (len > n)
		return false;
	if (f)
		fputc (kind == 2 ? '[' : '{', f);
	for (u64 k = 0; k < len; k++)
	{
		if (f && k)
			fputs (", ", f);
		if (!mp_json (f, d, n, pos, depth + 1))
			return false;
		if (kind == 3)
		{
			if (f)
				fputs (": ", f);
			if (!mp_json (f, d, n, pos, depth + 1))
				return false;
		}
	}
	if (f)
		fputc (kind == 2 ? ']' : '}', f);
	return true;
#undef MP_NEED
}

bool PhxMsgpackToJSON (FILE *f, const u8 *d, uint n)
{
	// a sequence of top-level values -> JSON array; validate first, then write
	uint pos = 0, cnt = 0;
	while (pos < n)
	{
		if (!mp_json (0, d, n, &pos, 0))
			return false;
		cnt++;
	}
	if (!cnt || !f)
		return cnt > 0;
	fputs ("[\n  ", f);
	pos = 0;
	for (uint i = 0; pos < n; i++)
	{
		if (i)
			fputs (",\n  ", f);
		mp_json (f, d, n, &pos, 0);
	}
	fputs ("\n]\n", f);
	return true;
}

//-----------------------------------------------------------------------------
// PARC material binding -> "<dest>/materials.tsv": slot hash, PMTR material, base-colour PTEX.
// - PMSH Main: geometry.f4 = material-slot hashes.
// - Pfab (model prefab, serialized object graph): a material assignment is {u64 ptr, u64 material
//   id} where ptr is the absolute offset of the slot hash it applies to.
// - PMTR Main: f3.f2 = texture refs {f1 asset id}. Sampler names are hashed, so the table lists
//   base-colour candidates: sRGB textures wider than 8 px (slot 0 is a shared 4x4 default) in
//   reference order, then linear ones; the PMSH extractor picks the first that decodes to a
//   plausible colour map.

static bool push_u64 (u64 **a, uint *n, uint *cap, const u64 *v, uint k)
{
	if (*n + k > *cap)
	{
		uint c = *cap ? *cap * 2 : 1024;
		while (c < *n + k)
			c *= 2;
		u64 *na = REALLOC (*a, (size_t)c * sizeof (u64));
		if (!na)
			return false;
		*a = na;
		*cap = c;
	}
	memcpy (*a + *n, v, k * sizeof (u64));
	*n += k;
	return true;
}

static int cmp_u64 (const void *a, const void *b)
{
	const u64 x = *(const u64 *)a, y = *(const u64 *)b;
	return x < y ? -1 : x > y;
}

static const u64 *find_u64 (const u64 *a, uint n, uint stride, u64 key)
{
	uint lo = 0, hi = n / stride;
	while (lo < hi)
	{
		const uint mid = (lo + hi) / 2;
		const u64 v = a[(size_t)mid * stride];
		if (v == key)
			return a + (size_t)mid * stride;
		if (v < key)
			lo = mid + 1;
		else
			hi = mid;
	}
	return 0;
}

static void phx_bind_collect (phx_bind_t *b, ccp type, u64 id, uint asset, const u8 *d, uint n)
{
	if (!strcmp (type, "pmsh"))
	{
		const uint geo = IsPMSH (d, n) ? fb_ref (d, n, le32 (d), 3) : 0;
		uint cnt = 0;
		const uint v = geo ? fb_vec (d, n, geo, 4, &cnt) : 0;
		for (uint i = 0; v && i < cnt && i < 256; i++)
		{
			const uint t = fb_vec_table (d, n, v, i), hp = t ? fb_field (d, n, t, 0, 8) : 0;
			if (hp)
			{
				const u64 h = le64 (d + hp);
				push_u64 (&b->slot, &b->n_slot, &b->cap_slot, &h, 1);
			}
		}
	}
	else if (!strcmp (type, "pmtr"))
	{
		const uint m = n >= 8 ? fb_ref (d, n, le32 (d), 3) : 0;
		uint cnt = 0;
		const uint v = m ? fb_vec (d, n, m, 2, &cnt) : 0;
		const u64 first = b->n_ref;
		uint used = 0;
		for (uint i = 0; v && i < cnt && i < 256; i++)
		{
			const uint t = fb_vec_table (d, n, v, i), ip = t ? fb_field (d, n, t, 1, 8) : 0;
			const u64 r = ip ? le64 (d + ip) : ~(u64)0;
			if (push_u64 (&b->ref, &b->n_ref, &b->cap_ref, &r, 1))
				used++;
		}
		const u64 trip[3] = { id, first, used };
		push_u64 (&b->mat, &b->n_mat, &b->cap_mat, trip, 3);
	}
	else if (!strcmp (type, "ptex"))
	{
		ptex_info_t pi;
		if (ptex_info (d, n, &pi))
		{
			const ptex_fmt_t *f = ptex_format (pi.format);
			const bool srgb = f && strstr (f->name, "SRGB");
			const u64 pair[2] = { id, (u64)pi.width << 1 | srgb };
			push_u64 (&b->tex, &b->n_tex, &b->cap_tex, pair, 2);
		}
	}
	else if (!strcmp (type, "psks"))
	{
		const uint sk = IsPSKS (d, n) ? fb_ref (d, n, le32 (d), 3) : 0;
		uint cnt = 0;
		const uint v = sk ? fb_vec (d, n, sk, 1, &cnt) : 0;
		for (uint i = 0; v && i < cnt && i < 4096; i++)
		{
			const uint t = fb_vec_table (d, n, v, i), nt = t ? fb_ref (d, n, t, 2) : 0;
			const uint hp = nt ? fb_field (d, n, nt, 0, 8) : 0;
			if (hp)
			{
				const u64 pr[2] = { le64 (d + hp), id };
				push_u64 (&b->skel, &b->n_skel, &b->cap_skel, pr, 2);
			}
		}
	}
	else if (!strcmp (type, "pfab"))
	{
		if (b->n_pfab == b->cap_pfab)
		{
			const uint c = b->cap_pfab ? b->cap_pfab * 2 : 256;
			uint *np = REALLOC (b->pfab, c * sizeof (uint));
			if (!np)
				return;
			b->pfab = np;
			b->cap_pfab = c;
		}
		b->pfab[b->n_pfab++] = asset;
	}
}

static int cmp_slot_seq (const void *a, const void *b)
{
	const u64 *x = a, *y = b;
	return x[0] != y[0] ? (x[0] < y[0] ? -1 : 1) : x[1] != y[1] ? (x[1] < y[1] ? -1 : 1) : 0;
}

static int cmp_u64x3 (const void *a, const void *b)
{
	return cmp_u64 (a, b);
}

static int cmp_u64x2 (const void *a, const void *b)
{
	return cmp_u64 (a, b);
}

static void phx_bind_finish (phx_bind_t *b, parc_ctx_t *c, ccp dest)
{
	u64 *pairs = 0; // {slot, material}
	uint n_pairs = 0, cap_pairs = 0;
	if (b->n_skel)
	{
		// "<dest>/skeletons.tsv": joint name hash -> every PSKS defining it, for skinned PMSH export
		qsort (b->skel, b->n_skel / 2, 2 * sizeof (u64), cmp_slot_seq);
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/skeletons.tsv", dest);
		FILE *f = fopen (path, "w");
		if (f)
		{
			fprintf (f, "# joint\tskeleton\n");
			for (uint i = 0; i < b->n_skel; i += 2)
				if (!i || b->skel[i] != b->skel[i - 2] || b->skel[i + 1] != b->skel[i - 1])
					fprintf (f, "%016llx\t%016llx\n", (unsigned long long)b->skel[i], (unsigned long long)b->skel[i + 1]);
			fclose (f);
		}
	}
	if (!b->n_slot || !b->n_mat || !b->n_pfab)
		goto out;
	qsort (b->slot, b->n_slot, sizeof (u64), cmp_u64);
	qsort (b->mat, b->n_mat / 3, 3 * sizeof (u64), cmp_u64x3);
	qsort (b->tex, b->n_tex / 2, 2 * sizeof (u64), cmp_u64x2);

	for (uint k = 0; k < b->n_pfab; k++)
	{
		uint nb = 0;
		const u8 *rec = parc_record (&c->pc, b->pfab[k], &nb);
		s64 n = -1;
		for (uint bi = 0; rec && bi < nb; bi++)
			if (!strncmp ((ccp)rec + 24 + bi * PARC_BLOCK_SIZE + 16, "Main", 4)
				|| !strncmp ((ccp)rec + 24 + bi * PARC_BLOCK_SIZE + 16, "MAIN", 4))
			{
				n = parc_load (c, le64 (rec + 24 + bi * PARC_BLOCK_SIZE + 24));
				break;
			}
		if (n < 16)
			continue;
		const u8 *d = c->buf;
		for (size_t o = 8; o + 8 <= (size_t)n; o += 4)
		{
			const u64 mat = le64 (d + o);
			if (!find_u64 (b->mat, b->n_mat, 3, mat))
				continue;
			const u64 p = le64 (d + o - 8);
			if (p & 7 || p + 8 > (u64)n)
				continue;
			const u64 slot = le64 (d + p);
			if (!find_u64 (b->slot, b->n_slot, 1, slot))
				continue;
			const u64 pr[2] = { slot, mat };
			push_u64 (&pairs, &n_pairs, &cap_pairs, pr, 2);
		}
	}
	if (!n_pairs)
		goto out;
	{
		// by slot, first assignment wins (later ones are per-instance variants): sort {slot, seq, mat}
		const uint np = n_pairs / 2;
		u64 *tri = MALLOC ((size_t)np * 3 * sizeof (u64));
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/materials.tsv", dest);
		FILE *f = tri ? fopen (path, "w") : 0;
		if (!f)
		{
			FREE (tri);
			goto out;
		}
		for (uint i = 0; i < np; i++)
		{
			tri[3 * i] = pairs[2 * i];
			tri[3 * i + 1] = i;
			tri[3 * i + 2] = pairs[2 * i + 1];
		}
		qsort (tri, np, 3 * sizeof (u64), cmp_slot_seq);
		fprintf (f, "# slot\tmaterial\tbase_colour_candidates\n");
		for (uint i = 0; i < np; i++)
		{
			if (i && tri[3 * i] == tri[3 * (i - 1)])
				continue;
			const u64 slot = tri[3 * i], mat = tri[3 * i + 2];
			const u64 *m = find_u64 (b->mat, b->n_mat, 3, mat);
			// candidates: sRGB textures wider than 8 px in reference order, then linear ones
			fprintf (f, "%016llx\t%016llx\t", (unsigned long long)slot, (unsigned long long)mat);
			uint nc = 0;
			for (uint pass = 0; pass < 2; pass++)
				for (u64 r = 0; m && r < m[2]; r++)
				{
					const u64 tid = b->ref[m[1] + r];
					const u64 *t = find_u64 (b->tex, b->n_tex, 2, tid);
					if (!t || (t[1] >> 1) <= 8 || (t[1] & 1) != (pass == 0))
						continue;
					fprintf (f, "%s%016llx", nc++ ? "," : "", (unsigned long long)tid);
				}
			fprintf (f, "%s\n", nc ? "" : "-");
		}
		fclose (f);
		FREE (tri);
	}
out:
	FREE (pairs);
	FREE (b->slot);
	FREE (b->mat);
	FREE (b->ref);
	FREE (b->tex);
	FREE (b->pfab);
	FREE (b->skel);
	memset (b, 0, sizeof (*b));
}

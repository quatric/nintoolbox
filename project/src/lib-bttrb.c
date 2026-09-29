// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Blue Tongue "TRB\0" package reader; see lib-bttrb.h.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-bttrb.h"
#include "lib-excite.h"
#include <math.h>

#define BT_MAX_SECTIONS 1024
#define BT_MAX_SYMBOLS 200000
#define BT_MAX_TEXTURES 20000
#define BT_FILL 0x0df00bb0u

static u32 bt_u32 (const u8 *d, size_t size, size_t o)
{
	if (o + 4 > size)
		return 0;
	const u8 *p = d + o;
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static uint bt_u16 (const u8 *d, size_t size, size_t o)
{
	return o + 2 > size ? 0 : d[o] << 8 | d[o + 1];
}

bool IsBlueTongueTrb (const u8 *d, size_t size)
{
	if (size < 0x100 || size > 0xffffffffu || memcmp (d, "TRB\0", 4)
		|| bt_u32 (d, size, 4) != 0x7d1)
		return false;
	const u32 n = bt_u32 (d, size, 0x0c);
	return n && n <= BT_MAX_SECTIONS && 0x80 + 0x30ull * n <= size;
}

typedef struct bt_sec_t
{
	u32 name, size, off;
} bt_sec_t;

static bt_sec_t bt_section (const u8 *d, size_t size, uint i)
{
	const size_t o = 0x80 + 0x30 * (size_t)i;
	bt_sec_t s
		= { bt_u32 (d, size, o + 4), bt_u32 (d, size, o + 0x10), bt_u32 (d, size, o + 0x18) };
	if ((u64)s.off + s.size > size)
		s.size = 0;
	return s;
}

// NUL-terminated string of the string table (section 0), or "".
static ccp bt_name (const u8 *d, size_t size, u32 name_off)
{
	const bt_sec_t t = bt_section (d, size, 0);
	const u64 o = (u64)t.off + name_off;
	if (!t.size || name_off >= t.size)
		return "";
	return memchr (d + o, 0, size - o) ? (ccp)d + o : "";
}

const u8 *FindBlueTongueSection (const u8 *d, size_t size, ccp name, u32 *len)
{
	if (!IsBlueTongueTrb (d, size))
		return 0;
	const uint n = bt_u32 (d, size, 0x0c);
	for (uint i = 0; i < n; i++)
	{
		const bt_sec_t s = bt_section (d, size, i);
		if (s.size && !strcmp (bt_name (d, size, s.name), name))
		{
			*len = s.size;
			return d + s.off;
		}
	}
	return 0;
}

static bool bt_dim (uint v)
{
	return v && v <= 4096;
}

static void bt_clean_name (char *out, size_t out_size, ccp src, uint fallback)
{
	char tmp[256];
	snprintf (tmp, sizeof (tmp), "%s", src);
	ccp base = tmp;
	for (ccp p = tmp; *p; p++)
		if (*p == '\\' || *p == '/')
			base = p + 1;
	char tmp2[256];
	snprintf (tmp2, sizeof (tmp2), "%s", base);
	char *dot = strrchr (tmp2, '.');
	if (dot && dot != tmp2 && strlen (dot) <= 5)
		*dot = 0;
	size_t o = 0;
	for (ccp p = tmp2; *p && o + 1 < out_size; p++)
	{
		const u8 ch = *p;
		out[o++] = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
				|| ch == '_' || ch == '-' || ch == '.' || ch == ' ' || ch == '+' || ch == '('
				|| ch == ')'
			? ch
			: '_';
	}
	out[o] = 0;
	if (!*out || !strcmp (out, ".") || !strcmp (out, ".."))
		snprintf (out, out_size, "unnamed_%04u", fallback);
}

static void bt_unique (bttrb_tex_t *list, uint n)
{
	bttrb_tex_t *a = list + n;
	char base[sizeof (a->name)];
	snprintf (base, sizeof (base), "%s", a->name);
	const size_t tlen = strnlen (base, 80);
	bool clash = false;
	uint max_k = 1;
	for (uint i = 0; i < n; i++)
	{
		const bttrb_tex_t *o = list + i;
		if (!strcasecmp (o->name, base))
			clash = true;
		else if (!strncasecmp (o->name, base, tlen) && o->name[tlen] == '_'
			&& isdigit ((uchar)o->name[tlen + 1]))
		{
			char *end;
			const unsigned long k = strtoul (o->name + tlen + 1, &end, 10);
			if (!*end && k >= max_k && k < UINT_MAX)
				max_k = (uint)k;
		}
	}
	if (clash)
		snprintf (a->name, sizeof (a->name), "%.80s_%u", base, max_k + 1);
}

bttrb_tex_t *ListBlueTongueTextures (const u8 *d, size_t size, uint *count)
{
	*count = 0;
	if (!IsBlueTongueTrb (d, size))
		return 0;
	const uint nsec = bt_u32 (d, size, 0x0c), nsym = bt_u32 (d, size, 0x14);
	const size_t symtab = 0x80 + 0x30 * (size_t)nsec;
	if (!nsym || nsym > BT_MAX_SYMBOLS || symtab + 16ull * nsym > size)
		return 0;

	bt_sec_t pool = { 0, 0, 0 };
	for (uint i = 0; i < nsec; i++)
	{
		const bt_sec_t s = bt_section (d, size, i);
		if (s.size && !strcmp (bt_name (d, size, s.name), "__s00000"))
			pool = s;
	}
	if (!pool.size)
		return 0;

	bttrb_tex_t *list = CALLOC (BT_MAX_TEXTURES, sizeof (*list));
	uint n = 0;
	for (uint i = 0; i < nsym && n < BT_MAX_TEXTURES; i++)
	{
		const size_t so = symtab + 16 * (size_t)i;
		if (strcmp (bt_name (d, size, bt_u32 (d, size, so + 12)), "ttex"))
			continue;
		const uint si = bt_u32 (d, size, so + 8) >> 16;
		if (si >= nsec)
			continue;
		const bt_sec_t s = bt_section (d, size, si);
		const u64 obj = (u64)s.off + bt_u32 (d, size, so + 4);
		if (!s.size || obj + 0x60 > size)
			continue;

		size_t f = obj + 0xc;
		for (uint k = 0; k < 16 && bt_u32 (d, size, f) == BT_FILL; k++)
			f += 4;
		if (f + 0x60 > size)
			continue;

		bttrb_tex_t *t = list + n;
		t->format = bt_u32 (d, size, f);
		const u32 pix = bt_u32 (d, size, f + 0xc), bytes = bt_u32 (d, size, f + 0x1c);
		uint hw = 0x40;
		if (!bt_dim (bt_u16 (d, size, f + hw)) || !bt_dim (bt_u16 (d, size, f + hw + 2)))
			hw = 0x44;
		t->width = bt_u16 (d, size, f + hw);
		t->height = bt_u16 (d, size, f + hw + 2);
		if (!bt_dim (t->width) || !bt_dim (t->height) || t->format > 14 || !bytes
			|| (u64)pix + bytes > pool.size)
			continue;
		t->off = pool.off + pix;
		t->size = bytes;
		const u32 pal = bt_u32 (d, size, f + 0x14);
		t->pal_off = pal ? pool.off + pal : 0;
		t->pal_format = bt_u32 (d, size, f + 0x18);

		size_t nm = f + hw + 8;
		while (nm < f + hw + 0x30 && d[nm] < 0x20)
			nm++;
		char raw[128] = "";
		for (uint k = 0; k + 1 < sizeof (raw) && nm + k < size && d[nm + k]; k++)
			raw[k] = d[nm + k], raw[k + 1] = 0;
		bt_clean_name (t->name, sizeof (t->name), raw, n);
		bt_unique (list, n);
		n++;
	}
	if (!n)
	{
		FREE (list);
		return 0;
	}
	*count = n;
	return list;
}

enumError DecodeBlueTongueTexture (u8 **rgba, const u8 *d, size_t size, const bttrb_tex_t *t)
{
	if ((u64)t->off + t->size > size)
		return ERR_NOTHING_TO_DO;
	const u8 *pal = 0;
	uint pal_count = 0;
	if (t->format == 8 || t->format == 9)
	{
		pal_count = t->format == 8 ? 16 : 256;
		if (!t->pal_off || (u64)t->pal_off + 2 * pal_count > size)
			return ERR_NOTHING_TO_DO;
		pal = d + t->pal_off;
	}
	return DecodeGXTexture_RGBA (
		rgba, t->width, t->height, t->format, d + t->off, t->size, pal, pal_count, t->pal_format);
}

//-----------------------------------------------------------------------------
// Models
//-----------------------------------------------------------------------------

#define BT_TCMD 0x646d6374u
#define BT_MAX_BATCHES 4096
#define BT_MAX_ATTR 8
#define BT_MAX_VERTS 200000

static bool bt_section_named (const u8 *d, size_t size, ccp name, bt_sec_t *out)
{
	const uint n = bt_u32 (d, size, 0x0c);
	for (uint i = 0; i < n; i++)
	{
		const bt_sec_t s = bt_section (d, size, i);
		if (s.size && !strcmp (bt_name (d, size, s.name), name))
		{
			*out = s;
			return true;
		}
	}
	return false;
}

// Absolute file offset of the OBJECT-th "tcmd" symbol's data, or 0.
static u64 bt_tcmd (const u8 *d, size_t size, uint index, char *name, size_t name_size)
{
	const uint nsec = bt_u32 (d, size, 0x0c), nsym = bt_u32 (d, size, 0x14);
	const size_t symtab = 0x80 + 0x30 * (size_t)nsec;
	if (!nsym || nsym > BT_MAX_SYMBOLS || symtab + 16ull * nsym > size)
		return 0;
	uint seen = 0;
	for (uint i = 0; i < nsym; i++)
	{
		const size_t so = symtab + 16 * (size_t)i;
		if (bt_u32 (d, size, so) != BT_TCMD)
			continue;
		if (seen++ != index)
			continue;
		const uint si = bt_u32 (d, size, so + 8) >> 16;
		if (si >= nsec)
			return 0;
		const bt_sec_t s = bt_section (d, size, si);
		const u32 off = bt_u32 (d, size, so + 4);
		if (!s.size || off + 0x60ull > s.size)
			return 0;
		if (name)
			snprintf (name, name_size, "%s", bt_name (d, size, bt_u32 (d, size, so + 12)));
		return (u64)s.off + off;
	}
	return 0;
}

uint CountBlueTongueModels (const u8 *d, size_t size)
{
	if (!IsBlueTongueTrb (d, size))
		return 0;
	uint n = 0;
	while (bt_tcmd (d, size, n, 0, 0))
		n++;
	return n;
}

typedef struct bt_batch_t
{
	uint n; // attributes stored per vertex (BT_MAX_ATTR at most)
	u32 dl_size, dl_off;
	uint attr[BT_MAX_ATTR]; // GX attribute ids, in vertex order (< 9: inline byte, no array)
	u32 arr[BT_MAX_ATTR], cnt[BT_MAX_ATTR], elem[BT_MAX_ATTR], frac[BT_MAX_ATTR];
	bool shared[BT_MAX_ATTR]; // array not stored here (bit 15 of its count)
	bool absent[BT_MAX_ATTR]; // no array descriptor at all (index width unknown, see bt_dl_auto)
	uint wd_guess[BT_MAX_ATTR]; // index width (1 or 2) assumed for an absent array
	const u8 *src[BT_MAX_ATTR]; // start of the gpu_data holding the array
} bt_batch_t;

static void bt_vert (const u8 *q, const uint wd[BT_MAX_ATTR], uint out[BT_MAX_ATTR])
{
	for (uint k = 0; k < BT_MAX_ATTR; k++)
	{
		out[k] = wd[k] == 0 ? 0 : wd[k] == 1 ? q[0] : q[0] << 8 | q[1];
		q += wd[k];
	}
}

// Triangulates the display list; fills IDX (when given) and returns the number
// of vertices, or 0 for a malformed list.
static size_t bt_dl_verts (const u8 *d, size_t size, const bt_sec_t *gpu, const bt_batch_t *b,
	uint (*idx)[BT_MAX_ATTR], size_t max)
{
	uint wd[BT_MAX_ATTR] = { 0 };
	size_t stride = 0;
	for (uint a = 0; a < b->n; a++)
		stride += wd[a] = b->attr[a] < 9 ? 1
			: b->absent[a]               ? (b->wd_guess[a] ? b->wd_guess[a] : 1)
			: b->cnt[a] <= 256           ? 1
										 : 2;
	if (b->dl_off + (u64)b->dl_size > gpu->size || (u64)gpu->off + gpu->size > size)
		return 0;
	const u8 *dl = d + gpu->off + b->dl_off;
	size_t p = 0, out = 0;
	while (p < b->dl_size)
	{
		const u8 c = dl[p];
		if (!c)
		{
			p++;
			continue;
		}
		if (p + 3 > b->dl_size)
			return 0;
		const uint n = dl[p + 1] << 8 | dl[p + 2];
		p += 3;
		if ((c != 0x80 && c != 0x90 && c != 0x98 && c != 0xa0) || p + n * stride > b->dl_size)
			return 0;
		uint tri[6][3];
		uint nt = 0;
		for (uint i = 0; i < n; i++)
		{
			nt = 0;
			if (c == 0x90 && i % 3 == 2)
				tri[nt][0] = i - 2, tri[nt][1] = i - 1, tri[nt++][2] = i;
			else if (c == 0x98 && i >= 2)
			{
				if (i & 1)
					tri[nt][0] = i - 1, tri[nt][1] = i - 2, tri[nt++][2] = i;
				else
					tri[nt][0] = i - 2, tri[nt][1] = i - 1, tri[nt++][2] = i;
			}
			else if (c == 0xa0 && i >= 2)
				tri[nt][0] = 0, tri[nt][1] = i, tri[nt++][2] = i - 1;
			else if (c == 0x80 && i % 4 == 3)
			{
				tri[nt][0] = i - 3, tri[nt][1] = i - 2, tri[nt++][2] = i - 1;
				tri[nt][0] = i - 3, tri[nt][1] = i - 1, tri[nt++][2] = i;
			}
			for (uint t = 0; t < nt; t++)
			{
				if (idx && out + 3 <= max)
					for (uint k = 0; k < 3; k++)
						bt_vert (dl + p + (size_t)tri[t][k] * stride, wd, idx[out + k]);
				out += 3;
			}
		}
		p += n * stride;
	}
	return out;
}

// bt_dl_verts() for a batch that may have arrays without a descriptor: their index
// width (u8 or u16) is not stored, so try the combinations until the display list
// parses exactly.
static size_t bt_dl_auto (const u8 *d, size_t size, const bt_sec_t *gpu, bt_batch_t *b,
	uint (*idx)[BT_MAX_ATTR], size_t max)
{
	uint free_attr[BT_MAX_ATTR], nf = 0;
	for (uint a = 0; a < b->n; a++)
		if (b->absent[a] && b->attr[a] >= 9)
			free_attr[nf++] = a;
	for (uint combo = 0; combo < (1u << nf); combo++)
	{
		for (uint f = 0; f < nf; f++)
			b->wd_guess[free_attr[f]] = 1 + (combo >> f & 1);
		const size_t nv = bt_dl_verts (d, size, gpu, b, idx, max);
		if (nv)
			return nv;
	}
	return 0;
}

// Parses batch record R of the package into B; false when it cannot be read.
static bool bt_parse_batch (
	const u8 *d, size_t size, const bt_sec_t *data, const bt_sec_t *gpu, size_t r, bt_batch_t *b)
{
	memset (b, 0, sizeof (*b));
	const u32 fmt = bt_u32 (d, size, r + 4), pairs = bt_u32 (d, size, r + 0x14);
	if (fmt + 0x14ull > data->size || pairs < 8 || pairs + 40ull > data->size)
		return false;
	b->n = bt_u32 (d, size, (size_t)data->off + fmt);
	if (!b->n || b->n > BT_MAX_ATTR || fmt + 4 + 4ull * b->n > data->size)
		return false;
	b->dl_size = bt_u32 (d, size, (size_t)data->off + pairs - 8);
	b->dl_off = bt_u32 (d, size, (size_t)data->off + pairs - 4);
	bool ok = b->dl_size != 0;
	bool has[16] = { 0 };
	uint np = 0;
	for (uint a = 0; a < b->n && ok; a++)
	{
		const u32 fd = bt_u32 (d, size, (size_t)data->off + fmt + 4 + 4 * a);
		b->attr[a] = fd & 0xff;
		b->frac[a] = fd >> 8 & 0xff;
		b->src[a] = d + gpu->off;
		if (b->attr[a] < 9)
			continue; // matrix index bytes: nothing stored
		// descriptors are stored only for the arrays a batch owns or shares; an
		// attribute without one (the lightmap coordinates of some terrain
		// batches) has no array here
		size_t po = (size_t)data->off + pairs + 8 * np;
		if (pairs + 8ull * (np + 1) > data->size || (bt_u32 (d, size, po + 4) >> 24) != b->attr[a])
		{
			b->absent[a] = true;
			continue;
		}
		np++;
		b->arr[a] = bt_u32 (d, size, po);
		const u32 desc = bt_u32 (d, size, po + 4);
		b->elem[a] = desc >> 16 & 0xff;
		b->cnt[a] = desc & 0xffff;
		b->shared[a] = (desc & 0x8000) != 0;
		if (b->shared[a])
		{
			b->cnt[a] = 0;
			ok = (desc >> 24) == b->attr[a] && b->attr[a] < 16 && !has[b->attr[a]];
			has[b->attr[a]] = true;
			continue;
		}
		ok = (desc >> 24) == b->attr[a] && b->cnt[a] && b->attr[a] < 16
			&& ((b->attr[a] == 9 && b->elem[a] == 6) || (b->attr[a] == 10 && b->elem[a] == 3)
				|| (b->attr[a] >= 13 && b->elem[a] == 4) || b->attr[a] == 11 || b->attr[a] == 12)
			&& !has[b->attr[a]] && (u64)b->arr[a] + (u64)b->cnt[a] * b->elem[a] <= gpu->size
			&& pairs + 8ull * np <= data->size;
		has[b->attr[a]] = true;
	}
	return ok;
}

// Packages searched for the arrays of shared batches (see SetBlueTongueLibraries).
#define BT_MAX_LIBS 512
static const u8 *bt_lib_data[BT_MAX_LIBS];
static size_t bt_lib_size[BT_MAX_LIBS];
static uint bt_n_libs;

void SetBlueTongueLibraries (const u8 **data, const size_t *size, uint count)
{
	bt_n_libs = count > BT_MAX_LIBS ? BT_MAX_LIBS : count;
	for (uint i = 0; i < bt_n_libs; i++)
		bt_lib_data[i] = data[i], bt_lib_size[i] = size[i];
}

// Does LB own an array for every attribute WANT shares?
static bool bt_lib_covers (const bt_batch_t *want, const bt_batch_t *lb)
{
	for (uint a = 0; a < want->n; a++)
	{
		if (!want->shared[a])
			continue;
		uint j = 0;
		while (j < lb->n && lb->attr[j] != want->attr[a])
			j++;
		if (j >= lb->n || lb->shared[j] || lb->absent[j])
			return false;
	}
	return true;
}

// Looks in package D for the (*SKIP + 1)-th batch of a model NAME with NB batches that
// owns the arrays WANT shares. Batch K is tried first (it is usually the counterpart),
// then the others: an instance in a cell may list its batches in another order than
// the model that owns the arrays.
static bool bt_lib_batch (const u8 *d, size_t size, ccp name, uint nb, uint k,
	const bt_batch_t *want, const u8 *skip_rec_of, uint *skip, bt_batch_t *out)
{
	bt_sec_t data, gpu;
	if (!IsBlueTongueTrb (d, size) || !bt_section_named (d, size, ".data", &data)
		|| !bt_section_named (d, size, "gpu_data", &gpu))
		return false;
	char nm[64];
	for (uint i = 0; bt_tcmd (d, size, i, nm, sizeof (nm)); i++)
	{
		if (strcmp (nm, name))
			continue;
		const u64 obj = bt_tcmd (d, size, i, 0, 0);
		if (bt_u32 (d, size, obj + 0x10) != nb)
			continue;
		const u32 arr = bt_u32 (d, size, obj + 0x14);
		if (arr + 4ull * nb > data.size)
			continue;
		for (uint step = 0; step < nb; step++)
		{
			const uint j = step == 0 ? k : step - 1 < k ? step - 1 : step;
			const u32 rec = bt_u32 (d, size, (size_t)data.off + arr + 4 * j);
			if (rec + 0x68ull > data.size || (skip_rec_of && d + data.off + rec == skip_rec_of))
				continue;
			bt_batch_t lb;
			if (!bt_parse_batch (d, size, &data, &gpu, (size_t)data.off + rec, &lb)
				|| !bt_lib_covers (want, &lb))
				continue;
			if (*skip)
			{
				(*skip)--;
				continue;
			}
			*out = lb;
			return true;
		}
	}
	return false;
}

// Same over the library package LIB, the registered packages and the package itself.
static bool bt_find_lib_batch (const u8 *self, size_t self_size, const u8 *lib, size_t lib_size,
	ccp name, uint nb, uint k, const bt_batch_t *want, const u8 *skip_rec, uint skip,
	bt_batch_t *out)
{
	if (lib && bt_lib_batch (lib, lib_size, name, nb, k, want, 0, &skip, out))
		return true;
	for (uint i = 0; i < bt_n_libs; i++)
		if (bt_lib_data[i] != lib && bt_lib_data[i] != self
			&& bt_lib_batch (bt_lib_data[i], bt_lib_size[i], name, nb, k, want, 0, &skip, out))
			return true;
	return bt_lib_batch (self, self_size, name, nb, k, want, skip_rec, &skip, out);
}

//-----------------------------------------------------------------------------
// Material binding. A model name selects an entry of "CoreMaterialSets_Mem"
// {u32 name (".data" offset of the string), u32 count, u32 offset of the
// pointer list, count * u32 offset of a record in "CoreMaterials_Mem"}; a
// record has 28 bytes {u32 name, ..., u32 word 2 (low 12 bits: index into
// "GXSET_TexObj_Mem")}; a TexObj entry has 52 bytes {u32 0x06nnkk00 (nn =
// texture count), then {u32 0, u32 texture << 16} pairs}; a texture is an
// index into the "ttlt" texture name table {u32 count, u32 offset of count
// name offsets}. The first entry of a lit material is the cell's lightmap;
// the others are the diffuse texture and its spec / env / bump helpers.
//-----------------------------------------------------------------------------

// NUL-terminated string at an absolute file offset, or "".
static ccp bt_name_at (const u8 *d, size_t size, size_t o)
{
	return o < size && memchr (d + o, 0, size - o) ? (ccp)d + o : "";
}

static bool bt_sym_named (const u8 *d, size_t size, ccp name, bt_sec_t *sec, u32 *off)
{
	const uint nsec = bt_u32 (d, size, 0x0c), nsym = bt_u32 (d, size, 0x14);
	const size_t symtab = 0x80 + 0x30 * (size_t)nsec;
	if (!nsym || nsym > BT_MAX_SYMBOLS || symtab + 16ull * nsym > size)
		return false;
	for (uint i = 0; i < nsym; i++)
	{
		const size_t so = symtab + 16 * (size_t)i;
		if (strcmp (bt_name (d, size, bt_u32 (d, size, so + 12)), name))
			continue;
		const uint si = bt_u32 (d, size, so + 8) >> 16;
		if (si >= nsec)
			continue;
		*sec = bt_section (d, size, si);
		*off = bt_u32 (d, size, so + 4);
		return sec->size != 0;
	}
	return false;
}

// Helper textures never used as a diffuse map.
static bool bt_helper_texture (ccp n)
{
	static const ccp skip[] = { "lightmap", "spec", "envmap", "_bump", "_mask", "reflect",
		"indirect", "invalid" };
	for (uint i = 0; i < sizeof (skip) / sizeof (*skip); i++)
		if (strcasestr (n, skip[i]))
			return true;
	return false;
}

// Replaces the placeholder material of M by the materials of MODEL_NAME found in the
// material tables of package LIB and points each mesh at the material of its batch.
static void bt_bind_materials (
	model_t *m, ccp model_name, const u8 *lib, size_t lib_size, const uint *mesh_batch)
{
	bt_sec_t data, sets, mats, texobj, ttlt_sec;
	u32 ttlt_off;
	if (!bt_section_named (lib, lib_size, ".data", &data)
		|| !bt_section_named (lib, lib_size, "CoreMaterialSets_Mem", &sets)
		|| !bt_section_named (lib, lib_size, "CoreMaterials_Mem", &mats)
		|| !bt_section_named (lib, lib_size, "GXSET_TexObj_Mem", &texobj)
		|| !bt_sym_named (lib, lib_size, "ttlt", &ttlt_sec, &ttlt_off) || ttlt_sec.off != data.off)
		return;
	const u32 n_names = bt_u32 (lib, lib_size, (size_t)data.off + ttlt_off);
	const u32 names_o = bt_u32 (lib, lib_size, (size_t)data.off + ttlt_off + 4);
	if (!n_names || n_names > 65536 || (u64)names_o + 4ull * n_names > data.size)
		return;

	// the set of the model
	const size_t mlen = strlen (model_name);
	u32 count = 0, ptrs = 0;
	for (u32 o = 0; (u64)o + 16 <= sets.size;)
	{
		const u32 nm = bt_u32 (lib, lib_size, (size_t)sets.off + o);
		const u32 c = bt_u32 (lib, lib_size, (size_t)sets.off + o + 4);
		if (bt_u32 (lib, lib_size, (size_t)sets.off + o + 8) != o + 12 || c > 64
			|| (u64)o + 12 + 4ull * c > sets.size)
			break;
		if (nm < data.size && (u64)data.off + nm + mlen < lib_size
			&& !memcmp (lib + data.off + nm, model_name, mlen) && !lib[data.off + nm + mlen])
		{
			count = c;
			ptrs = o + 12;
			break;
		}
		o += 12 + 4 * c;
	}
	if (!count)
		return;

	material_t *mm = CALLOC (count, sizeof (*mm));
	if (!mm)
		return;
	uint n_mat = 0;
	for (uint k = 0; k < count; k++)
	{
		const u32 rec = bt_u32 (lib, lib_size, (size_t)sets.off + ptrs + 4 * k);
		if ((u64)rec + 28 > mats.size)
			continue;
		const size_t r = (size_t)mats.off + rec;
		material_t *mat = mm + n_mat++;
		const u32 nm = bt_u32 (lib, lib_size, r);
		snprintf (mat->name, sizeof (mat->name), "%s",
			nm < data.size ? bt_name_at (lib, lib_size, (size_t)data.off + nm) : "material");
		mat->diffuse[0] = mat->diffuse[1] = mat->diffuse[2] = mat->diffuse[3] = 1.0f;
		const u32 ti = bt_u32 (lib, lib_size, r + 8) & 0xfff;
		if ((u64)(ti + 1) * 52 > texobj.size)
			continue;
		const size_t e = (size_t)texobj.off + 52 * (size_t)ti;
		const uint nt = bt_u32 (lib, lib_size, e) >> 16 & 0xff;
		for (uint t = 0; t < nt && t < 6; t++)
		{
			const u32 id = bt_u32 (lib, lib_size, e + 8 + 8 * t) >> 16;
			if (id >= n_names)
				continue;
			const u32 np = bt_u32 (lib, lib_size, (size_t)data.off + names_o + 4 * id);
			if (np >= data.size)
				continue;
			char clean[64];
			bt_clean_name (clean, sizeof (clean), bt_name_at (lib, lib_size, (size_t)data.off + np), id);
			if (bt_helper_texture (clean))
				continue;
			snprintf (mat->textures[0], sizeof (mat->textures[0]), "%s", clean);
			mat->num_textures = 1;
			break;
		}
	}
	if (!n_mat)
	{
		FREE (mm);
		return;
	}
	FREE (m->materials);
	m->materials = mm;
	m->num_materials = n_mat;
	for (size_t i = 0; i < m->num_meshes; i++)
	{
		const uint k = mesh_batch[i];
		m->meshes[i].material_idx = (int)(k < n_mat ? k : n_mat - 1);
	}
}

// Do all indices of the display list of B stay inside the arrays it shares (WANT
// marks which ones are shared)?
static bool bt_indices_fit (const u8 *d, size_t size, const bt_sec_t *gpu, bt_batch_t *b,
	const bt_batch_t *want)
{
	const size_t nv = bt_dl_auto (d, size, gpu, b, 0, 0);
	if (!nv || nv > BT_MAX_VERTS)
		return false;
	uint (*idx)[BT_MAX_ATTR] = MALLOC (nv * sizeof (*idx));
	if (!idx)
		return false;
	bt_dl_auto (d, size, gpu, b, idx, nv);
	bool fit = true;
	for (size_t i = 0; i < nv && fit; i++)
		for (uint a = 0; a < b->n && fit; a++)
			if (want->shared[a] && idx[i][a] >= b->cnt[a])
				fit = false;
	FREE (idx);
	return fit;
}

typedef struct
{
	bool valid;
	u32 arr, cnt, elem, frac;
	const u8 *src;
} bt_last_t;

static bool bt_absent_pos (const bt_batch_t *b)
{
	for (uint a = 0; a < b->n; a++)
		if (b->attr[a] == 9)
			return b->absent[a];
	return true;
}

model_t *BuildBlueTongueModel (const u8 *d, size_t size, uint index, char *name, size_t name_size,
	const u8 *lib, size_t lib_size)
{
	char name_of_model[64];
	const u64 obj = bt_tcmd (d, size, index, name_of_model, sizeof (name_of_model));
	if (name)
		snprintf (name, name_size, "%s", name_of_model);
	bt_sec_t data, gpu;
	if (!obj || !bt_section_named (d, size, ".data", &data)
		|| !bt_section_named (d, size, "gpu_data", &gpu))
		return 0;
	const u32 nb = bt_u32 (d, size, obj + 0x10), arr = bt_u32 (d, size, obj + 0x14);
	if (!nb || nb > BT_MAX_BATCHES || arr + 4ull * nb > data.size)
		return 0;

	model_t *m = CALLOC (1, sizeof (*m));
	uint *mesh_batch = CALLOC (nb, sizeof (*mesh_batch));
	if (!m || !mesh_batch)
	{
		FREE (m);
		FREE (mesh_batch);
		return 0;
	}
	bt_last_t last[16];
	memset (last, 0, sizeof (last));
	for (uint k = 0; k < nb; k++)
	{
		const u32 rec = bt_u32 (d, size, (size_t)data.off + arr + 4 * k);
		if (rec + 0x68ull > data.size)
			continue;
		const size_t r = (size_t)data.off + rec;
		bt_batch_t b;
		if (!bt_parse_batch (d, size, &data, &gpu, r, &b))
			continue;
		bool any_shared = false;
		for (uint a = 0; a < b.n; a++)
			any_shared = any_shared || b.shared[a];
		if (any_shared)
		{
			// the arrays come from a same-named model of another package of the level;
			// the candidate is right when every index of the display list fits its arrays
			const bt_batch_t own = b;
			bool found = false;
			for (uint cand = 0; !found; cand++)
			{
				bt_batch_t lb;
				if (!bt_find_lib_batch (
						d, size, lib, lib_size, name_of_model, nb, k, &own, d + r, cand, &lb))
					break;
				b = own;
				for (uint a = 0; a < b.n; a++)
				{
					if (!b.shared[a])
						continue;
					uint j = 0;
					while (j < lb.n && lb.attr[j] != b.attr[a])
						j++;
					b.arr[a] = lb.arr[j];
					b.cnt[a] = lb.cnt[j];
					b.elem[a] = lb.elem[j];
					b.frac[a] = lb.frac[j];
					b.src[a] = lb.src[0];
				}
				found = bt_indices_fit (d, size, &gpu, &b, &own);
			}
			if (!found)
				continue;
		}

		bool has_pos = false;
		for (uint a = 0; a < b.n; a++)
			has_pos = has_pos || b.attr[a] == 9;
		if (!has_pos)
			continue;
		// a batch without a descriptor for an attribute continues with the array of the
		// closest earlier batch of the model that had one (terrain pieces share their
		// position pool this way); if that does not give a clean display list the
		// attribute stays absent
		bt_batch_t plain = b;
		bool inherited = false;
		for (uint a = 0; a < b.n; a++)
		{
			if (b.attr[a] >= 9 && b.absent[a] && last[b.attr[a] & 15].valid)
			{
				const bt_last_t *l = last + (b.attr[a] & 15);
				b.arr[a] = l->arr, b.cnt[a] = l->cnt, b.elem[a] = l->elem, b.frac[a] = l->frac;
				b.src[a] = l->src;
				b.absent[a] = false;
				inherited = true;
			}
		}
		if (inherited && !bt_dl_verts (d, size, &gpu, &b, 0, 0))
			b = plain;
		for (uint a = 0; a < b.n; a++)
			if (b.attr[a] >= 9 && !b.absent[a])
				last[b.attr[a] & 15]
					= (bt_last_t) { true, b.arr[a], b.cnt[a], b.elem[a], b.frac[a], b.src[a] };
		if (bt_absent_pos (&b))
			continue;
		const size_t nv = bt_dl_auto (d, size, &gpu, &b, 0, 0);
		if (!nv || nv > BT_MAX_VERTS)
			continue;
		uint (*idx)[BT_MAX_ATTR] = MALLOC (nv * sizeof (*idx));
		if (!idx)
			continue;
		bt_dl_auto (d, size, &gpu, &b, idx, nv);

		mesh_t *nm = REALLOC (m->meshes, (m->num_meshes + 1) * sizeof (*nm));
		if (!nm)
		{
			FREE (idx);
			break;
		}
		m->meshes = nm;
		mesh_batch[m->num_meshes] = k;
		mesh_t *mesh = m->meshes + m->num_meshes++;
		memset (mesh, 0, sizeof (*mesh));
		snprintf (mesh->name, sizeof (mesh->name), "batch%u", k);
		mesh->positions = CALLOC (nv, sizeof (vec3_t));
		mesh->normals = CALLOC (nv, sizeof (vec3_t));
		mesh->texcoords = CALLOC (nv, sizeof (vec2_t));
		mesh->vertices = CALLOC (nv, sizeof (vertex_t));
		if (!mesh->positions || !mesh->normals || !mesh->texcoords || !mesh->vertices)
		{
			FREE (idx);
			FreeModel (m);
			FREE (mesh_batch);
			return 0;
		}
		bool has_tex1 = false;
		for (uint a = 0; a < b.n; a++)
			has_tex1 = has_tex1 || b.attr[a] == 14;
		for (size_t i = 0; i < nv; i++)
		{
			vertex_t *v = mesh->vertices + i;
			v->position_idx = v->normal_idx = v->texcoord_idx = (int)i;
			v->tangent_idx = v->matrix_idx = -1;
			v->color_idx[0] = v->color_idx[1] = -1;
			for (int e = 0; e < 7; e++)
				v->extra_texcoord_idx[e] = -1;
			mesh->normals[i] = (vec3_t) { 0, 0, 1 };
			for (uint a = 0; a < b.n; a++)
			{
				if (b.attr[a] < 9 || b.absent[a])
					continue;
				const uint ix = idx[i][a] < b.cnt[a] ? idx[i][a] : 0;
				const u8 *q = b.src[a] + b.arr[a] + (size_t)b.elem[a] * ix;
				const float sc = 1.0f / (float)(1u << (b.frac[a] & 15));
				if (b.attr[a] == 9)
					mesh->positions[i] = (vec3_t) { (int16_t)(q[0] << 8 | q[1]) * sc,
						(int16_t)(q[2] << 8 | q[3]) * sc, (int16_t)(q[4] << 8 | q[5]) * sc };
				else if (b.attr[a] == 10)
				{
					vec3_t n = { (int8_t)q[0] * sc, (int8_t)q[1] * sc, (int8_t)q[2] * sc };
					const float len = sqrtf (n.x * n.x + n.y * n.y + n.z * n.z);
					mesh->normals[i] = len > 0 ? (vec3_t) { n.x / len, n.y / len, n.z / len } : n;
				}
				else if (b.attr[a] == 13 || b.attr[a] == 14)
				{
					// TEX0 carries the lightmap coordinates of a lit material, TEX1
					// (when present) the diffuse ones
					const vec2_t uv = { (int16_t)(q[0] << 8 | q[1]) * sc,
						(int16_t)(q[2] << 8 | q[3]) * sc };
					if (b.attr[a] == 14 || !has_tex1)
						mesh->texcoords[i] = uv;
				}
			}
		}
		mesh->num_positions = mesh->num_normals = mesh->num_texcoords = mesh->num_vertices = nv;
		FREE (idx);
	}
	if (!m->num_meshes)
	{
		FreeModel (m);
		FREE (mesh_batch);
		return 0;
	}
	m->materials = CALLOC (1, sizeof (*m->materials));
	if (m->materials)
	{
		m->num_materials = 1;
		snprintf (m->materials[0].name, sizeof (m->materials[0].name), "material");
		m->materials[0].diffuse[0] = m->materials[0].diffuse[1] = m->materials[0].diffuse[2]
			= m->materials[0].diffuse[3] = 1.0f;
	}
	// a cell's props take their materials from the level's LevelAssets package
	bt_bind_materials (m, name_of_model, lib ? lib : d, lib ? lib_size : size, mesh_batch);
	FREE (mesh_batch);
	return m;
}

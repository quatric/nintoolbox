// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Goliath engine "GS" package (*.pkz) scanner; see lib-goliath.h for the
// chunk-tree layout, the texture/audio record formats and the retail
// verification numbers behind every rule applied below.
//-----------------------------------------------------------------------------
#include "lib-goliath.h"
#include "lib-image.h"
#include "lib-std.h"
#include <string.h>
#include <stdlib.h>
#include <zlib.h>

//-----------------------------------------------------------------------------
// chunk tree
//-----------------------------------------------------------------------------

// Two dialects exist. The newer one (Skylanders, engine "GS version 9.x") uses
// a 16-byte chunk header and ids with the high bit set. The older one (The
// Amazing Spider-Man, "GS version 6.64") drops the size_hi word (12-byte
// header) and the high id bit, and shifts every record field down by 4.
#define GS_CHUNK_HEAD 16
#define GS_CHUNK_HEAD_V6 12
#define GS_ROOT_ID 0x80000001

// Resource wrapper and the name record that is always its first child.
#define GS_ID_RESOURCE 0x8000138d
#define GS_ID_NAME 0x8000138e
#define GS_NAME_OFFSET 0x1c
#define GS_NAME_OFFSET_V6 0x18

// Texture description (header lives one level below) and the pooled pixels.
#define GS_ID_TEXTURE 0x80000191
#define GS_ID_TEX_HEAD 0x80000197
#define GS_ID_TEX_DATA 0x80000195
#define GS_TEX_HEAD_SIZE 0x30
#define GS_TEX_HEAD_SIZE_V6 0x2c

// Audio description and the pooled RIFX stream.
#define GS_ID_AUDIO 0x80001133
#define GS_ID_AUDIO_DATA 0x80001134

// A chunk tree deeper than this is not something the engine produces; the
// limit only exists so a malformed file cannot blow the C stack.
#define GS_MAX_DEPTH 32
// Sanity caps so a corrupt size field cannot make us allocate wildly.
#define GS_MAX_RECORDS 0x40000

static u32 gs_rd32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
static u16 gs_rd16 (const u8 *p)
{
	return (u16)((u16)p[0] << 8 | p[1]);
}

// Walk one level of the tree; returns false unless the children consume
// [off,end) exactly. Every chunk must carry the high id bit, a zero size
// high word and a payload that fits inside its parent.
static bool gs_walk_check (const u8 *data, size_t off, size_t end, uint depth, bool v6)
{
	const uint head = v6 ? GS_CHUNK_HEAD_V6 : GS_CHUNK_HEAD;
	if (depth > GS_MAX_DEPTH)
		return false;
	while (off < end)
	{
		if (off + head > end)
			return false;
		const u8 *h = data + off;
		const u32 id = gs_rd32 (h);
		const u16 children = gs_rd16 (h + 6);
		const u32 size_hi = v6 ? 0 : gs_rd32 (h + 8);
		const u32 size = gs_rd32 (h + head - 4);
		if (v6 ? (id & 0x80000000) : !(id & 0x80000000))
			return false;
		if (size_hi || children > 1)
			return false;
		const size_t body = off + head;
		if (size > end - body)
			return false;
		if (children && !gs_walk_check (data, body, body + size, depth + 1, v6))
			return false;
		off = body + size;
	}
	return off == end;
}

// 0 = not a package, 6 = old 12-byte dialect, 9 = new 16-byte dialect.
static int gs_dialect (const u8 *data, size_t size)
{
	if (!data || size < GS_CHUNK_HEAD_V6)
		return 0;
	if (size >= GS_CHUNK_HEAD && gs_rd32 (data) == GS_ROOT_ID && !gs_rd32 (data + 8)
		&& gs_rd16 (data + 6) == 1 && (u64)gs_rd32 (data + 12) + GS_CHUNK_HEAD == size
		&& gs_walk_check (data, 0, size, 0, false))
		return 9;
	if (gs_rd32 (data) == 1 && gs_rd16 (data + 6) == 1
		&& (u64)gs_rd32 (data + 8) + GS_CHUNK_HEAD_V6 == size
		&& gs_walk_check (data, 0, size, 0, true))
		return 6;
	return 0;
}

bool IsGoliathPKZ (const u8 *data, size_t size)
{
	return gs_dialect (data, size) != 0;
}

//-----------------------------------------------------------------------------
// "BABEB1B0" block-zlib wrapper (The Amazing Spider-Man .pkz on Wii)
//
//   0x00 u32 magic 0xBABEB1B0 (big-endian on Wii)
//   0x04 u32 block size          0x8000
//   0x08 u32 data offset         0x8000 (first block)
//   0x0c u32 unknown
//   0x10 u32 block count
//   0x14 u32 file size           compressed total, the file length
//   0x18 u32 uncompressed total
//   0x1c u32 ends[count]         cumulative uncompressed end of each block
//   ...      u32 index table     per 0x8000 output slice; not needed
//   at data_offset + i * block_size: one raw zlib stream per block
//
// Each block is an independent zlib stream (0x78 0x01) that carries no final
// block bit, so a plain inflate to the expected length is used and the
// padding after the data is ignored. The concatenated result is a GS
// chunk-tree package ("GS version 6.64" in its first payload chunk).
//-----------------------------------------------------------------------------

#define GS_BLOCK_MAGIC 0xBABEB1B0
#define GS_BLOCK_MAX_OUT 0x40000000u

bool IsGoliathBlockPKZ (const u8 *data, size_t size)
{
	if (!data || size < 0x20 || gs_rd32 (data) != GS_BLOCK_MAGIC)
		return false;
	const u32 bsize = gs_rd32 (data + 4), doff = gs_rd32 (data + 8);
	const u32 count = gs_rd32 (data + 16), total = gs_rd32 (data + 0x18);
	return bsize && doff && count && total <= GS_BLOCK_MAX_OUT && (u64)count * 4 + 0x1c <= size
		&& (u64)doff + (u64)count * bsize <= size + bsize;
}

enumError DecodeGoliathBlockPKZ (const u8 *data, size_t size, u8 **dest, size_t *dest_size)
{
	if (dest)
		*dest = 0;
	if (dest_size)
		*dest_size = 0;
	if (!dest || !dest_size || !IsGoliathBlockPKZ (data, size))
		return ERR_INVALID_DATA;

	const u32 bsize = gs_rd32 (data + 4), doff = gs_rd32 (data + 8);
	const u32 count = gs_rd32 (data + 16), total = gs_rd32 (data + 0x18);
	u8 *out = MALLOC (total ? total : 1);
	if (!out)
		return ERR_OUT_OF_MEMORY;

	u32 prev = 0;
	for (u32 i = 0; i < count; i++)
	{
		const u32 end = gs_rd32 (data + 0x1c + 4 * i);
		const u64 src = (u64)doff + (u64)i * bsize;
		if (end < prev || end > total || src >= size)
		{
			FREE (out);
			return ERR_INVALID_DATA;
		}
		const u32 want = end - prev;
		const size_t avail = size - src < bsize ? size - src : bsize;

		z_stream z;
		memset (&z, 0, sizeof (z));
		if (inflateInit (&z) != Z_OK)
		{
			FREE (out);
			return ERR_INVALID_DATA;
		}
		z.next_in = (Bytef *)(data + src);
		z.avail_in = (uInt)avail;
		z.next_out = out + prev;
		z.avail_out = want;
		const int zerr = inflate (&z, Z_SYNC_FLUSH);
		const bool full = z.total_out == want;
		inflateEnd (&z);
		if (!full && zerr != Z_STREAM_END)
		{
			FREE (out);
			return ERR_INVALID_DATA;
		}
		prev = end;
	}
	if (prev != total)
	{
		FREE (out);
		return ERR_INVALID_DATA;
	}
	*dest = out;
	*dest_size = total;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// collected records
//-----------------------------------------------------------------------------

typedef struct gs_ref_t
{
	size_t off; // payload offset
	u32 size; // payload size
	ccp name; // borrowed pointer into a name buffer, may be NULL
	u32 key; // hash from the enclosing name record (0x138e), 0 if none
} gs_ref_t;

typedef struct gs_list_t
{
	gs_ref_t *v;
	uint used, alloc;
} gs_list_t;

static bool gs_push (gs_list_t *l, size_t off, u32 size, ccp name, u32 key)
{
	if (l->used >= GS_MAX_RECORDS)
		return false;
	if (l->used == l->alloc)
	{
		const uint want = l->alloc ? l->alloc * 2 : 64;
		gs_ref_t *nv = REALLOC (l->v, want * sizeof (*nv));
		if (!nv)
			return false;
		l->v = nv;
		l->alloc = want;
	}
	l->v[l->used].off = off;
	l->v[l->used].size = size;
	l->v[l->used].name = name;
	l->v[l->used].key = key;
	l->used++;
	return true;
}

typedef struct gs_scan_t
{
	const u8 *data;
	bool v6; // old 12-byte dialect, see the top of this file
	uint head; // chunk header length
	uint name_off; // name field offset inside a 0x138e record
	uint tex_head_min; // shortest acceptable texture header
	u32 cur_key;
	gs_list_t tex_head, tex_data, audio_desc, audio_data, resources;
	ccp cur_name; // name of the resource wrapper we are currently inside
	bool overflow;
} gs_scan_t;

// Pull the NUL-terminated name out of a 0x8000138e record, or NULL.
static ccp gs_name_of (const gs_scan_t *s, const u8 *body, u32 size)
{
	if (size <= s->name_off)
		return 0;
	const char *p = (const char *)body + s->name_off;
	const u32 avail = size - s->name_off;
	u32 len = 0;
	while (len < avail && p[len])
		len++;
	return len && len < avail ? p : 0;
}

static void gs_collect (gs_scan_t *s, size_t off, size_t end, uint depth)
{
	while (off + s->head <= end && !s->overflow)
	{
		const u8 *h = s->data + off;
		const u32 id = gs_rd32 (h) | 0x80000000; // old dialect ids lack the high bit
		const u16 children = gs_rd16 (h + 6);
		const u32 size = gs_rd32 (h + s->head - 4);
		const size_t body = off + s->head;

		switch (id)
		{
			case GS_ID_NAME:
				s->cur_name = gs_name_of (s, s->data + body, size);
				s->cur_key = size >= 4 ? gs_rd32 (s->data + body) : 0;
				break;
			case GS_ID_TEX_HEAD:
				if (size >= s->tex_head_min
					&& !gs_push (&s->tex_head, body, size, s->cur_name, s->cur_key))
					s->overflow = true;
				break;
			case GS_ID_TEX_DATA:
				if (!gs_push (&s->tex_data, body, size, 0, s->cur_key))
					s->overflow = true;
				break;
			case GS_ID_AUDIO:
				if (!gs_push (&s->audio_desc, body, size, s->cur_name, s->cur_key))
					s->overflow = true;
				break;
			case GS_ID_AUDIO_DATA:
				if (!gs_push (&s->audio_data, body, size, 0, s->cur_key))
					s->overflow = true;
				break;
			case GS_ID_RESOURCE:
				s->cur_name = 0;
				break;
		}

		if (children && depth < GS_MAX_DEPTH)
			gs_collect (s, body, body + size, depth + 1);

		// A resource wrapper's typed children are done; record what it was
		// so the manifest can list even the types not decoded here.
		if (id == GS_ID_RESOURCE && s->cur_name
			&& !gs_push (&s->resources, body, size, s->cur_name, s->cur_key))
			s->overflow = true;

		off = body + size;
	}
}

//-----------------------------------------------------------------------------
// GameCube texture geometry
//-----------------------------------------------------------------------------

// Bits per pixel and tile size of the three formats this scanner accepts.
typedef struct gs_gx_t
{
	uint bpp, bw, bh;
} gs_gx_t;

static const gs_gx_t gs_gx_cmpr = { 4, 8, 8 };
static const gs_gx_t gs_gx_i8 = { 8, 8, 4 };
static const gs_gx_t gs_gx_rgb5a3 = { 16, 4, 4 };

static u64 gs_level_size (const gs_gx_t *g, u32 w, u32 h)
{
	const u64 pw = ((u64)w + g->bw - 1) / g->bw * g->bw;
	const u64 ph = ((u64)h + g->bh - 1) / g->bh * g->bh;
	return pw * ph * g->bpp / 8;
}

static u64 gs_chain_size (const gs_gx_t *g, u32 w, u32 h, uint levels)
{
	u64 total = 0;
	for (uint i = 0; i < levels; i++)
	{
		const u32 lw = w >> i ? w >> i : 1;
		const u32 lh = h >> i ? h >> i : 1;
		total += gs_level_size (g, lw, lh);
	}
	return total;
}

// Wrap one raw GX mip level in a single-image TPL, which the repo's existing
// GameCube texture decoder reads directly (same construction as lib-ptlg.c).
static u8 *gs_make_tpl (u32 w, u32 h, u32 iform, const u8 *pixels, u32 pixel_size, uint *out_size)
{
	const u32 tpl_hdr = sizeof (tpl_header_t);
	const u32 tpl_tab = tpl_hdr + sizeof (tpl_imgtab_t);
	const u32 tpl_data = tpl_tab + sizeof (tpl_img_header_t);
	u8 *tpl = CALLOC (tpl_data + pixel_size, 1);
	if (!tpl)
		return 0;

	write_be32 (tpl, TPL_MAGIC_NUM);
	write_be32 (tpl + 4, 1);
	write_be32 (tpl + 8, tpl_hdr);
	write_be32 (tpl + tpl_hdr, tpl_tab);
	write_be32 (tpl + tpl_hdr + 4, 0);
	write_be16 (tpl + tpl_tab, (u16)h);
	write_be16 (tpl + tpl_tab + 2, (u16)w);
	write_be32 (tpl + tpl_tab + 4, iform);
	write_be32 (tpl + tpl_tab + 8, tpl_data);
	write_be32 (tpl + tpl_tab + 20, 1);
	write_be32 (tpl + tpl_tab + 24, 1);
	memcpy (tpl + tpl_data, pixels, pixel_size);

	*out_size = tpl_data + pixel_size;
	return tpl;
}

//-----------------------------------------------------------------------------
// entry helpers
//-----------------------------------------------------------------------------

typedef struct gs_out_t
{
	nintendo_sarc_entry_t *v;
	uint used, alloc;
} gs_out_t;

static bool gs_emit (gs_out_t *o, char *name, u8 *payload, uint size)
{
	if (!name || !payload)
	{
		FREE (name);
		FREE (payload);
		return false;
	}
	if (o->used == o->alloc)
	{
		const uint want = o->alloc ? o->alloc * 2 : 64;
		nintendo_sarc_entry_t *nv = REALLOC (o->v, want * sizeof (*nv));
		if (!nv)
		{
			FREE (name);
			FREE (payload);
			return false;
		}
		o->v = nv;
		o->alloc = want;
	}
	o->v[o->used].name = name;
	o->v[o->used].data = payload;
	o->v[o->used].size = size;
	o->used++;
	return true;
}

// Build "<dir>/<index>_<name><ext>". The index prefix is what keeps member
// names unique: resource names repeat inside a package and the host file
// system may be case-insensitive, so a bare name is not safe to use.
static char *gs_member_name (ccp dir, uint index, ccp name, ccp ext)
{
	char clean[80];
	uint n = 0;
	for (ccp p = name; p && *p && n + 1 < sizeof (clean); p++)
	{
		const u8 c = (u8)*p;
		clean[n++] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
				|| c == '.' || c == '-' || c == '_'
			? (char)c
			: '_';
	}
	clean[n] = 0;
	if (!n)
		snprintf (clean, sizeof (clean), "unnamed");

	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/%04u_%s%s", dir, index, clean, ext);
	return STRDUP (path);
}

//-----------------------------------------------------------------------------
// textures
//-----------------------------------------------------------------------------

static void gs_extract_textures (gs_scan_t *s, gs_out_t *out)
{
	// The two pools are matched positionally, so a count mismatch means the
	// assumption does not hold for this file and no texture is emitted.
	if (!s->v6 && s->tex_head.used != s->tex_data.used)
		return;

	bool *taken = s->v6 ? CALLOC (s->tex_data.used ? s->tex_data.used : 1, 1) : 0;
	for (uint i = 0; i < s->tex_head.used; i++)
	{
		// Old dialect: the pixel pool is not in document order; each payload
		// sits in a 0x26 wrapper with its own name record, and the hash in
		// that record equals the one in the resource that owns the header.
		uint di = i;
		if (s->v6)
		{
			for (di = 0; di < s->tex_data.used; di++)
				if (!taken[di] && s->tex_data.v[di].key == s->tex_head.v[i].key)
					break;
			if (di == s->tex_data.used || !s->tex_head.v[i].key)
				continue;
			taken[di] = true;
		}
		const u8 *h = s->data + s->tex_head.v[i].off;
		// The first two words are height then width, not the other way
		// round: with them swapped every non-square texture's mip chain
		// comes out the wrong length, and the 140 retail textures that
		// exposed it decode to garbled stripes instead of an image.
		const u32 height = gs_rd32 (h);
		const u32 w = gs_rd32 (h + 4);
		const uint levels = gs_rd32 (h + 8) & 0xff;
		const u32 format = gs_rd32 (h + 12);
		const u8 *pix = s->data + s->tex_data.v[di].off;
		const u32 pix_size = s->tex_data.v[di].size;

		if (!w || !height || w > 0x2000 || height > 0x2000 || !levels || levels > 16)
			continue;

		const gs_gx_t *colour = format == 2 ? &gs_gx_rgb5a3 : &gs_gx_cmpr;
		const u32 iform = format == 2 ? IMG_RGB5A3 : IMG_CMPR;
		u64 want = gs_chain_size (colour, w, height, levels);
		if (format == 4)
			want += gs_chain_size (&gs_gx_i8, w, height, levels);
		// Formats 5 and the handful of oversized format-2 textures land here
		// and are skipped: the payload does not match any layout verified
		// against the retail corpus, and guessing would emit a wrong image.
		if (format > 4 || want != pix_size)
			continue;

		const u32 base = (u32)gs_level_size (colour, w, height);
		uint tpl_size = 0;
		u8 *tpl = gs_make_tpl (w, height, iform, pix, base, &tpl_size);
		gs_emit (out, gs_member_name ("textures", i, s->tex_head.v[i].name, ".tpl"), tpl, tpl_size);

		if (format == 4)
		{
			const u32 alpha_off = (u32)gs_chain_size (colour, w, height, levels);
			const u32 alpha_base = (u32)gs_level_size (&gs_gx_i8, w, height);
			u8 *atpl = gs_make_tpl (w, height, IMG_I8, pix + alpha_off, alpha_base, &tpl_size);
			gs_emit (out, gs_member_name ("textures", i, s->tex_head.v[i].name, ".alpha.tpl"), atpl,
				tpl_size);
		}
	}
	FREE (taken);
}

//-----------------------------------------------------------------------------
// audio
//-----------------------------------------------------------------------------

#define GS_DSP_HEAD 0x60
#define GS_DSP_FRAME 8
#define GS_DSP_SAMPLES 14

// Locate a chunk inside a big-endian RIFF ("RIFX") WAVE container.
static const u8 *gs_rifx_chunk (const u8 *d, u32 size, ccp id, u32 *out_size)
{
	if (size < 12 || memcmp (d, "RIFX", 4) || memcmp (d + 8, "WAVE", 4))
		return 0;
	u32 off = 12;
	while (off + 8 <= size)
	{
		const u32 csize = gs_rd32 (d + off + 4);
		if (csize > size - off - 8)
			return 0;
		if (!memcmp (d + off, id, 4))
		{
			*out_size = csize;
			return d + off + 8;
		}
		off += 8 + csize + (csize & 1);
	}
	return 0;
}

// Rewrite one channel of a RIFX DSP-ADPCM stream as a standalone .dsp.
static u8 *gs_make_dsp (const u8 *fmt, const u8 *adpcm, u32 adpcm_size, u32 srate, u32 samples,
	uint channel, uint *out_size)
{
	const u64 frames = adpcm_size / GS_DSP_FRAME;
	if (!frames)
		return 0;
	if (samples > frames * GS_DSP_SAMPLES)
		samples = (u32)(frames * GS_DSP_SAMPLES);
	if (!samples)
		return 0;

	const u32 total = GS_DSP_HEAD + adpcm_size;
	u8 *dsp = CALLOC (total, 1);
	if (!dsp)
		return 0;

	write_be32 (dsp, samples);
	write_be32 (dsp + 4, samples / GS_DSP_SAMPLES * 16 + samples % GS_DSP_SAMPLES * 2);
	write_be32 (dsp + 8, srate);
	write_be16 (dsp + 12, 0); // loop flag
	write_be16 (dsp + 14, 0); // format: ADPCM
	write_be32 (dsp + 24, 2); // current address, as Nintendo's own tool writes it

	// Per-channel state block: 16 s16 coefficients then gain/scale/history.
	const u8 *cs = fmt + 18 + 10 + (size_t)channel * 44;
	memcpy (dsp + 28, cs, 32);
	write_be16 (dsp + 60, gs_rd16 (cs + 32)); // gain
	write_be16 (dsp + 62, adpcm[0]); // initial predictor/scale
	memcpy (dsp + GS_DSP_HEAD, adpcm, adpcm_size);

	*out_size = total;
	return dsp;
}

static void gs_extract_audio (gs_scan_t *s, gs_out_t *out)
{
	if (s->audio_desc.used != s->audio_data.used)
		return;

	for (uint i = 0; i < s->audio_desc.used; i++)
	{
		const u8 *riff = s->data + s->audio_data.v[i].off;
		const u32 riff_size = s->audio_data.v[i].size;
		u32 fmt_size = 0, data_size = 0;
		const u8 *fmt = gs_rifx_chunk (riff, riff_size, "fmt ", &fmt_size);
		const u8 *data = gs_rifx_chunk (riff, riff_size, "data", &data_size);
		if (!fmt || !data || fmt_size < 18)
			continue;

		const u16 tag = gs_rd16 (fmt);
		const uint channels = gs_rd16 (fmt + 2);
		const u32 srate = gs_rd32 (fmt + 4);
		const uint block = gs_rd16 (fmt + 12);
		const u16 bits = gs_rd16 (fmt + 14);
		const u16 cbsize = gs_rd16 (fmt + 16);
		// Only the DSP-ADPCM dialect documented in lib-goliath.h is decoded.
		if (tag != 2 || bits != 4 || !channels || channels > 2)
			continue;
		if (cbsize < 10 + 44 * channels || (u32)18 + cbsize > fmt_size)
			continue;
		const u32 samples = gs_rd32 (fmt + 18 + 6);
		if (!samples || srate < 2000 || srate > 192000 || !data_size)
			continue;

		if (channels == 1)
		{
			uint size = 0;
			u8 *dsp = gs_make_dsp (fmt, data, data_size, srate, samples, 0, &size);
			if (dsp)
				gs_emit (
					out, gs_member_name ("audio", i, s->audio_desc.v[i].name, ".dsp"), dsp, size);
			continue;
		}

		// Stereo: the channels alternate in blockAlign/channels byte blocks,
		// and the final block may be short.
		const uint istep = block / channels;
		if (!istep || istep % GS_DSP_FRAME)
			continue;
		for (uint c = 0; c < channels; c++)
		{
			u8 *chan = MALLOC (data_size / channels + istep);
			if (!chan)
				break;
			u32 used = 0;
			for (u32 off = (u32)c * istep; off < data_size; off += istep * channels)
			{
				const u32 take = data_size - off < istep ? data_size - off : istep;
				memcpy (chan + used, data + off, take);
				used += take;
			}
			uint size = 0;
			u8 *dsp = gs_make_dsp (fmt, chan, used, srate, samples, c, &size);
			FREE (chan);
			if (!dsp)
				continue;
			char ext[16];
			snprintf (ext, sizeof (ext), ".ch%u.dsp", c);
			gs_emit (out, gs_member_name ("audio", i, s->audio_desc.v[i].name, ext), dsp, size);
		}
	}
}

//-----------------------------------------------------------------------------
// manifest
//-----------------------------------------------------------------------------

// A plain listing of every named resource and the chunk type describing it,
// so the resources this scanner does not decode are still visible.
static void gs_emit_manifest (gs_scan_t *s, gs_out_t *out)
{
	if (!s->resources.used)
		return;

	FastBuf_t fb;
	InitializeFastBufAlloc (&fb, 0x10000);
	AppendFastBuf (&fb, "# Goliath GS package resources: name, describing chunk type\n",
		strlen ("# Goliath GS package resources: name, describing chunk type\n"));

	for (uint i = 0; i < s->resources.used; i++)
	{
		const size_t body = s->resources.v[i].off;
		const u32 size = s->resources.v[i].size;
		// First child after the name record is the typed description.
		u32 type = 0;
		size_t off = body;
		while (off + s->head <= body + size)
		{
			const u32 id = gs_rd32 (s->data + off) | 0x80000000;
			if (id != GS_ID_NAME)
			{
				type = id;
				break;
			}
			off += s->head + gs_rd32 (s->data + off + s->head - 4);
		}
		char line[256];
		const int len
			= snprintf (line, sizeof (line), "%-56s %08x\n", s->resources.v[i].name, type);
		if (len > 0)
			AppendFastBuf (&fb, line, len);
	}

	const uint len = GetFastBufLen (&fb);
	u8 *text = MALLOC (len);
	if (text)
	{
		memcpy (text, GetFastBufString (&fb), len);
		gs_emit (out, STRDUP ("resources.txt"), text, len);
	}
	ResetFastBuf (&fb);
}

//-----------------------------------------------------------------------------
// entry point
//-----------------------------------------------------------------------------

enumError ScanGoliathPKZ (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size)
{
	const int dialect = gs_dialect (data, size);
	if (!entries || !n_entries || !dialect)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	gs_scan_t s;
	memset (&s, 0, sizeof (s));
	s.data = data;
	s.v6 = dialect == 6;
	s.head = s.v6 ? GS_CHUNK_HEAD_V6 : GS_CHUNK_HEAD;
	s.name_off = s.v6 ? GS_NAME_OFFSET_V6 : GS_NAME_OFFSET;
	s.tex_head_min = s.v6 ? GS_TEX_HEAD_SIZE_V6 : GS_TEX_HEAD_SIZE;
	gs_collect (&s, 0, size, 0);

	gs_out_t out;
	memset (&out, 0, sizeof (out));
	if (!s.overflow)
	{
		gs_extract_textures (&s, &out);
		gs_extract_audio (&s, &out);
		gs_emit_manifest (&s, &out);
	}

	FREE (s.tex_head.v);
	FREE (s.tex_data.v);
	FREE (s.audio_desc.v);
	FREE (s.audio_data.v);
	FREE (s.resources.v);

	if (!out.used)
	{
		FREE (out.v);
		return ERR_NOTHING_TO_DO;
	}
	*entries = out.v;
	*n_entries = out.used;
	return ERR_OK;
}

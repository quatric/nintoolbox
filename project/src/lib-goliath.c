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

//-----------------------------------------------------------------------------
// chunk tree
//-----------------------------------------------------------------------------

#define GS_CHUNK_HEAD 16
#define GS_ROOT_ID 0x80000001

// Resource wrapper and the name record that is always its first child.
#define GS_ID_RESOURCE 0x8000138d
#define GS_ID_NAME 0x8000138e
#define GS_NAME_OFFSET 0x1c

// Texture description (header lives one level below) and the pooled pixels.
#define GS_ID_TEXTURE 0x80000191
#define GS_ID_TEX_HEAD 0x80000197
#define GS_ID_TEX_DATA 0x80000195
#define GS_TEX_HEAD_SIZE 0x30

// Audio description and the pooled RIFX stream.
#define GS_ID_AUDIO 0x80001133
#define GS_ID_AUDIO_DATA 0x80001134

// A chunk tree deeper than this is not something the engine produces; the
// limit only exists so a malformed file cannot blow the C stack.
#define GS_MAX_DEPTH 32
// Sanity caps so a corrupt size field cannot make us allocate wildly.
#define GS_MAX_RECORDS 0x40000

static u32 gs_rd32 (const u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static u16 gs_rd16 (const u8 *p) { return (u16)((u16)p[0] << 8 | p[1]); }

// Walk one level of the tree; returns false unless the children consume
// [off,end) exactly. Every chunk must carry the high id bit, a zero size
// high word and a payload that fits inside its parent.
static bool gs_walk_check (const u8 *data, size_t off, size_t end, uint depth)
{
	if (depth > GS_MAX_DEPTH)
		return false;
	while (off < end)
	{
		if (off + GS_CHUNK_HEAD > end)
			return false;
		const u8 *h = data + off;
		const u32 id = gs_rd32 (h);
		const u16 children = gs_rd16 (h + 6);
		const u32 size_hi = gs_rd32 (h + 8);
		const u32 size = gs_rd32 (h + 12);
		if (!(id & 0x80000000) || size_hi || children > 1)
			return false;
		const size_t body = off + GS_CHUNK_HEAD;
		if (size > end - body)
			return false;
		if (children && !gs_walk_check (data, body, body + size, depth + 1))
			return false;
		off = body + size;
	}
	return off == end;
}

bool IsGoliathPKZ (const u8 *data, size_t size)
{
	if (!data || size < GS_CHUNK_HEAD || gs_rd32 (data) != GS_ROOT_ID)
		return false;
	if (gs_rd32 (data + 8) || gs_rd16 (data + 6) != 1)
		return false;
	if ((u64)gs_rd32 (data + 12) + GS_CHUNK_HEAD != size)
		return false;
	return gs_walk_check (data, 0, size, 0);
}

//-----------------------------------------------------------------------------
// collected records
//-----------------------------------------------------------------------------

typedef struct gs_ref_t
{
	size_t off; // payload offset
	u32 size; // payload size
	ccp name; // borrowed pointer into a name buffer, may be NULL
} gs_ref_t;

typedef struct gs_list_t
{
	gs_ref_t *v;
	uint used, alloc;
} gs_list_t;

static bool gs_push (gs_list_t *l, size_t off, u32 size, ccp name)
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
	l->used++;
	return true;
}

typedef struct gs_scan_t
{
	const u8 *data;
	gs_list_t tex_head, tex_data, audio_desc, audio_data, resources;
	ccp cur_name; // name of the resource wrapper we are currently inside
	bool overflow;
} gs_scan_t;

// Pull the NUL-terminated name out of a 0x8000138e record, or NULL.
static ccp gs_name_of (const u8 *body, u32 size)
{
	if (size <= GS_NAME_OFFSET)
		return 0;
	const char *p = (const char *)body + GS_NAME_OFFSET;
	const u32 avail = size - GS_NAME_OFFSET;
	u32 len = 0;
	while (len < avail && p[len])
		len++;
	return len && len < avail ? p : 0;
}

static void gs_collect (gs_scan_t *s, size_t off, size_t end, uint depth)
{
	while (off + GS_CHUNK_HEAD <= end && !s->overflow)
	{
		const u8 *h = s->data + off;
		const u32 id = gs_rd32 (h);
		const u16 children = gs_rd16 (h + 6);
		const u32 size = gs_rd32 (h + 12);
		const size_t body = off + GS_CHUNK_HEAD;

		switch (id)
		{
			case GS_ID_NAME:
				s->cur_name = gs_name_of (s->data + body, size);
				break;
			case GS_ID_TEX_HEAD:
				if (size >= GS_TEX_HEAD_SIZE && !gs_push (&s->tex_head, body, size, s->cur_name))
					s->overflow = true;
				break;
			case GS_ID_TEX_DATA:
				if (!gs_push (&s->tex_data, body, size, 0))
					s->overflow = true;
				break;
			case GS_ID_AUDIO:
				if (!gs_push (&s->audio_desc, body, size, s->cur_name))
					s->overflow = true;
				break;
			case GS_ID_AUDIO_DATA:
				if (!gs_push (&s->audio_data, body, size, 0))
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
		if (id == GS_ID_RESOURCE && s->cur_name && !gs_push (&s->resources, body, size, s->cur_name))
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
	if (s->tex_head.used != s->tex_data.used)
		return;

	for (uint i = 0; i < s->tex_head.used; i++)
	{
		const u8 *h = s->data + s->tex_head.v[i].off;
		// The first two words are height then width, not the other way
		// round: with them swapped every non-square texture's mip chain
		// comes out the wrong length, and the 140 retail textures that
		// exposed it decode to garbled stripes instead of an image.
		const u32 height = gs_rd32 (h);
		const u32 w = gs_rd32 (h + 4);
		const uint levels = gs_rd32 (h + 8) & 0xff;
		const u32 format = gs_rd32 (h + 12);
		const u8 *pix = s->data + s->tex_data.v[i].off;
		const u32 pix_size = s->tex_data.v[i].size;

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
				gs_emit (out, gs_member_name ("audio", i, s->audio_desc.v[i].name, ".dsp"), dsp,
					size);
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
		while (off + GS_CHUNK_HEAD <= body + size)
		{
			const u32 id = gs_rd32 (s->data + off);
			if (id != GS_ID_NAME)
			{
				type = id;
				break;
			}
			off += GS_CHUNK_HEAD + gs_rd32 (s->data + off + 12);
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
	if (!entries || !n_entries || !IsGoliathPKZ (data, size))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	gs_scan_t s;
	memset (&s, 0, sizeof (s));
	s.data = data;
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

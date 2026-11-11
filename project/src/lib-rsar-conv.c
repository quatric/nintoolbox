// Native BRSAR -> MIDI + SF2 conversion. See lib-rsar-conv.h for provenance.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib-std.h"
#include "lib-sequence.h"
#include "lib-rsar-conv.h"

///////////////////////////////////////////////////////////////////////////////
// big-endian readers with bounds checks (out of range reads yield 0)

typedef struct rc_file_t
{
	const u8 *d;
	size_t n;
} rc_file_t;

static u32 rc_u8 (const rc_file_t *f, size_t o) { return o < f->n ? f->d[o] : 0; }
static u32 rc_u16 (const rc_file_t *f, size_t o) { return rc_u8 (f, o) << 8 | rc_u8 (f, o + 1); }
static u32 rc_u32 (const rc_file_t *f, size_t o) { return rc_u16 (f, o) << 16 | rc_u16 (f, o + 2); }
static bool rc_match (const rc_file_t *f, size_t o, const void *m, size_t len)
{
	return o + len <= f->n && !memcmp (f->d + o, m, len);
}

// returns {content offset, content size} of a "XXXX" block at o (size field includes its own 8 byte header)
static bool rc_block (const rc_file_t *f, size_t o, const char *magic, size_t *content, size_t *csize)
{
	if (!rc_match (f, o, magic, 4))
		return false;
	const u32 size = rc_u32 (f, o + 4);
	*content = o + 8;
	*csize = size >= 8 ? size - 8 : 0;
	return true;
}

///////////////////////////////////////////////////////////////////////////////
// growable byte buffer

typedef struct rc_buf_t
{
	u8 *d;
	size_t n, cap;
} rc_buf_t;

static bool rc_grow (rc_buf_t *b, size_t add)
{
	if (b->n + add <= b->cap)
		return true;
	size_t c = b->cap ? b->cap : 256;
	while (c < b->n + add)
		c *= 2;
	u8 *p = REALLOC (b->d, c);
	if (!p)
		return false;
	b->d = p;
	b->cap = c;
	return true;
}

static void rc_put (rc_buf_t *b, const void *s, size_t len)
{
	if (rc_grow (b, len))
	{
		if (len)
			memcpy (b->d + b->n, s, len);
		b->n += len;
	}
}

static void rc_put16 (rc_buf_t *b, u32 v) { u8 t[2] = { v, v >> 8 }; rc_put (b, t, 2); }
static void rc_put32 (rc_buf_t *b, u32 v) { u8 t[4] = { v, v >> 8, v >> 16, v >> 24 }; rc_put (b, t, 4); }
static void rc_putname (rc_buf_t *b, ccp s, uint len)
{
	char t[64] = { 0 };
	snprintf (t, sizeof (t), "%s", s ? s : "");
	rc_put (b, t, len);
}

///////////////////////////////////////////////////////////////////////////////
// RSAR tables (RSARScanner.cpp)

typedef struct rs_range_t { u32 offset, size; } rs_range_t;

typedef struct rs_sound_t
{
	char name[96];
	u32 fileID, dataOffset, bankID, allocTrack;
} rs_sound_t;

typedef struct rs_bank_t
{
	char name[96];
	u32 fileID;
} rs_bank_t;

typedef struct rs_file_t { u32 groupID, index; bool valid; } rs_file_t;

typedef struct rs_item_t
{
	u32 fileID;
	rs_range_t data, waveData;
} rs_item_t;

typedef struct rs_group_t
{
	rs_range_t data, waveData;
	rs_item_t *items;
	uint n_items;
} rs_group_t;

typedef struct rs_t
{
	rc_file_t f;
	char **strings;
	uint n_strings;
	rs_sound_t *sounds;
	uint n_sounds;
	rs_bank_t *banks;
	uint n_banks;
	rs_file_t *files;
	uint n_files;
	rs_group_t *groups;
	uint n_groups;
} rs_t;

static void rs_free (rs_t *r)
{
	for (uint i = 0; i < r->n_strings; i++)
		FREE (r->strings[i]);
	FREE (r->strings);
	FREE (r->sounds);
	FREE (r->banks);
	FREE (r->files);
	for (uint i = 0; i < r->n_groups; i++)
		FREE (r->groups[i].items);
	FREE (r->groups);
}

static const char *rs_str (const rs_t *r, u32 id)
{
	return id != 0xffffffff && id < r->n_strings && r->strings[id] ? r->strings[id] : "";
}

static bool rs_parse (rs_t *r)
{
	const rc_file_t *f = &r->f;
	static const u8 magic[7] = { 'R', 'S', 'A', 'R', 0xfe, 0xff, 0x01 };
	if (!rc_match (f, 0, magic, 7))
		return false;
	size_t symb, symb_n, info, info_n;
	if (!rc_block (f, rc_u32 (f, 0x10), "SYMB", &symb, &symb_n) || !rc_block (f, rc_u32 (f, 0x18), "INFO", &info, &info_n))
		return false;

	// SYMB: string table
	const size_t st = symb + rc_u32 (f, symb);
	r->n_strings = rc_u32 (f, st);
	if (r->n_strings > 100000)
		return false;
	r->strings = CALLOC (r->n_strings ? r->n_strings : 1, sizeof (char *));
	if (!r->strings)
		return false;
	for (uint i = 0; i < r->n_strings; i++)
	{
		const size_t so = symb + rc_u32 (f, st + 4 + 4 * (size_t)i);
		if (so < f->n)
			r->strings[i] = STRDUP ((const char *)f->d + so);
	}

	// sounds (only sequences are kept)
	{
		const u32 to = rc_u32 (f, info + 4), cnt = rc_u32 (f, info + to);
		if (cnt > 100000)
			return false;
		r->sounds = CALLOC (cnt ? cnt : 1, sizeof (*r->sounds));
		if (!r->sounds)
			return false;
		for (uint i = 0; i < cnt; i++)
		{
			const size_t sb = info + rc_u32 (f, info + to + 8 + 8 * (size_t)i);
			if (rc_u8 (f, sb + 0x16) != 1) // SEQ
				continue;
			rs_sound_t *s = r->sounds + r->n_sounds++;
			snprintf (s->name, sizeof (s->name), "%s", rs_str (r, rc_u32 (f, sb)));
			s->fileID = rc_u32 (f, sb + 4);
			const size_t si = info + rc_u32 (f, sb + 0x1c);
			s->dataOffset = rc_u32 (f, si);
			s->bankID = rc_u32 (f, si + 4);
			s->allocTrack = rc_u32 (f, si + 8);
		}
	}
	// banks
	{
		const u32 to = rc_u32 (f, info + 0x0c), cnt = rc_u32 (f, info + to);
		if (cnt > 100000)
			return false;
		r->banks = CALLOC (cnt ? cnt : 1, sizeof (*r->banks));
		if (!r->banks)
			return false;
		r->n_banks = cnt;
		for (uint i = 0; i < cnt; i++)
		{
			const size_t bb = info + rc_u32 (f, info + to + 8 + 8 * (size_t)i);
			snprintf (r->banks[i].name, sizeof (r->banks[i].name), "%s", rs_str (r, rc_u32 (f, bb)));
			r->banks[i].fileID = rc_u32 (f, bb + 4);
		}
	}
	// files
	{
		const u32 to = rc_u32 (f, info + 0x1c), cnt = rc_u32 (f, info + to);
		if (cnt > 1000000)
			return false;
		r->files = CALLOC (cnt ? cnt : 1, sizeof (*r->files));
		if (!r->files)
			return false;
		r->n_files = cnt;
		for (uint i = 0; i < cnt; i++)
		{
			const size_t fb = info + rc_u32 (f, info + to + 8 + 8 * (size_t)i);
			const size_t pt = info + rc_u32 (f, fb + 0x18);
			if (!rc_u32 (f, pt))
				continue; // dummy file
			const size_t pb = info + rc_u32 (f, pt + 8);
			r->files[i].groupID = rc_u32 (f, pb);
			r->files[i].index = rc_u32 (f, pb + 4);
			r->files[i].valid = true;
		}
	}
	// groups
	{
		const u32 to = rc_u32 (f, info + 0x24), cnt = rc_u32 (f, info + to);
		if (cnt > 100000)
			return false;
		r->groups = CALLOC (cnt ? cnt : 1, sizeof (*r->groups));
		if (!r->groups)
			return false;
		r->n_groups = cnt;
		for (uint i = 0; i < cnt; i++)
		{
			const size_t gb = info + rc_u32 (f, info + to + 8 + 8 * (size_t)i);
			rs_group_t *g = r->groups + i;
			g->data = (rs_range_t){ rc_u32 (f, gb + 0x10), rc_u32 (f, gb + 0x14) };
			g->waveData = (rs_range_t){ rc_u32 (f, gb + 0x18), rc_u32 (f, gb + 0x1c) };
			const size_t it = info + rc_u32 (f, gb + 0x24);
			const u32 ic = rc_u32 (f, it);
			if (ic > 1000000)
				return false;
			g->items = CALLOC (ic ? ic : 1, sizeof (rs_item_t));
			if (!g->items)
				return false;
			g->n_items = ic;
			for (uint j = 0; j < ic; j++)
			{
				const size_t ib = info + rc_u32 (f, it + 8 + 8 * (size_t)j);
				g->items[j].fileID = rc_u32 (f, ib);
				g->items[j].data = (rs_range_t){ rc_u32 (f, ib + 4), rc_u32 (f, ib + 8) };
				g->items[j].waveData = (rs_range_t){ rc_u32 (f, ib + 0x0c), rc_u32 (f, ib + 0x10) };
			}
		}
	}
	return true;
}

// where the data of a file ID lives: absolute offset of the file, absolute offset of its wave data, size
static bool rs_resolve (const rs_t *r, u32 fileID, u32 *data_off, u32 *wave_off, u32 *data_size)
{
	if (fileID >= r->n_files || !r->files[fileID].valid)
		return false;
	const rs_file_t *fi = r->files + fileID;
	if (fi->groupID >= r->n_groups)
		return false;
	const rs_group_t *g = r->groups + fi->groupID;
	if (fi->index >= g->n_items)
		return false;
	const rs_item_t *it = g->items + fi->index;
	*data_off = it->data.offset + g->data.offset;
	*wave_off = it->waveData.offset + g->waveData.offset;
	*data_size = it->data.size;
	return true;
}

///////////////////////////////////////////////////////////////////////////////
// samples (RBNKSamp / RSARSampColl*)

enum { RS_PCM8 = 0, RS_PCM16 = 1, RS_ADPCM = 2 };

typedef struct rb_samp_t
{
	bool valid;
	int fmt, channels;
	bool loop;
	u32 rate, loopStart, loopEnd; // addresses as stored
	u32 dataOff; // absolute offset of channel data base
	u32 chData[2];
	s16 coef[2][16];
	s16 yn1[2], yn2[2];
	bool has_adpcm[2];
	u32 nsamples;
	// SF2 view: 16 bit signed little endian samples (interleaved when stereo, like the original)
	s16 *pcm;
	size_t pcm_count; // number of 16 bit values
	u32 loop_start_s, loop_len_s; // in 16 bit values
	int unity_from_region; // set while building regions
} rb_samp_t;

static u32 rb_dsp_samples (int fmt, u32 a)
{
	if (fmt == RS_ADPCM)
		return (a / 16) * 14 + (a % 16) - 2;
	return a;
}

static s16 rb_clamp16 (s32 v) { return v > 0x7fff ? 0x7fff : v < -0x8000 ? -0x8000 : (s16)v; }

static void rb_decode_adpcm (const rc_file_t *f, const rb_samp_t *s, int ch, s16 *dst, int spacing, u32 nsamp)
{
	s16 h1 = s->yn1[ch], h2 = s->yn2[ch];
	u32 done = 0;
	size_t src = (size_t)s->dataOff + s->chData[ch];
	size_t di = 0;
	while (done < nsamp)
	{
		const u8 hdr = rc_u8 (f, src++);
		const s32 scale = 1 << (hdr & 15);
		const uint ci = hdr >> 4;
		const s16 c1 = s->coef[ch][(ci * 2) & 15], c2 = s->coef[ch][(ci * 2 + 1) & 15];
		for (int i = 0; i < 14; i++)
		{
			const u8 byte = rc_u8 (f, src + (i >> 1));
			const u8 un = (i & 1) ? (byte & 15) : (byte >> 4);
			const int8_t sn = (int8_t)(un << 4) >> 4;
			const s16 sm = rb_clamp16 ((((sn * scale) << 11) + 1024 + (c1 * h1 + c2 * h2)) >> 11);
			dst[di] = sm;
			di += spacing;
			h2 = h1;
			h1 = sm;
			if (++done >= nsamp)
				break;
		}
		src += 7;
	}
}

static void rb_samp_load (const rc_file_t *f, rb_samp_t *s, size_t info, size_t data_base, size_t data_extra_is_unused)
{
	(void)data_extra_is_unused;
	memset (s, 0, sizeof (*s));
	s->fmt = rc_u8 (f, info);
	if (s->fmt > RS_ADPCM)
		return;
	s->loop = !!rc_u8 (f, info + 1);
	s->channels = rc_u8 (f, info + 2);
	if (s->channels < 1 || s->channels > 2)
		return;
	s->rate = rc_u8 (f, info + 3) << 16 | rc_u16 (f, info + 4);
	if (rc_u8 (f, info + 6) != 0)
		return; // only OFFSET based data locations
	s->loopStart = rc_u32 (f, info + 8);
	s->loopEnd = rc_u32 (f, info + 0x0c);
	const size_t cit = info + rc_u32 (f, info + 0x10);
	for (int i = 0; i < s->channels; i++)
	{
		const size_t ci = info + rc_u32 (f, cit + 4 * (size_t)i);
		s->chData[i] = rc_u32 (f, ci);
		const u32 ao = rc_u32 (f, ci + 4);
		if (ao)
		{
			const size_t ab = info + ao;
			for (int k = 0; k < 16; k++)
				s->coef[i][k] = (s16)rc_u16 (f, ab + 2 * (size_t)k);
			s->yn1[i] = (s16)rc_u16 (f, ab + 0x24);
			s->yn2[i] = (s16)rc_u16 (f, ab + 0x26);
			s->has_adpcm[i] = true;
		}
	}
	s->dataOff = (u32)data_base + rc_u32 (f, info + 0x14);
	s->nsamples = rb_dsp_samples (s->fmt, s->loopEnd);
	if (s->nsamples > 0x4000000)
		return;

	// uncompressed buffer exactly as the original lays it out
	const int bps = s->fmt == RS_PCM8 ? 8 : 16;
	const size_t bytes = (size_t)s->nsamples * (bps / 8) * s->channels;
	u8 *buf = CALLOC (1, bytes ? bytes + 2 : 2);
	if (!buf)
		return;
	for (int c = 0; c < s->channels; c++)
	{
		const int bytes_per = bps / 8;
		u8 *bc = buf + (size_t)c * bytes_per;
		if (s->fmt == RS_ADPCM)
			rb_decode_adpcm (f, s, c, (s16 *)bc, s->channels, s->nsamples);
		else if (s->fmt == RS_PCM8)
		{
			const size_t src = (size_t)s->dataOff + s->chData[c];
			for (u32 i = 0; i < s->nsamples && (size_t)(bc - buf) + i < bytes; i++)
				bc[i] = rc_u8 (f, src + i) ^ 0x80;
		}
		else
		{
			size_t src = (size_t)s->dataOff + s->chData[c];
			for (u32 i = 0; i < s->nsamples; i++, src += 2)
				((s16 *)bc)[(size_t)i * s->channels] = (s16)rc_u16 (f, src);
		}
	}
	// to 16 bit signed (SF2 view)
	s->pcm_count = bytes / 2 + (bps == 8 ? bytes - bytes / 2 : 0);
	if (bps == 8)
	{
		s->pcm_count = bytes;
		s->pcm = CALLOC (bytes ? bytes : 1, 2);
		if (s->pcm)
			for (size_t i = 0; i < bytes; i++)
				s->pcm[i] = (s16)(((int)buf[i] - 128) << 8);
		FREE (buf);
	}
	else
	{
		s->pcm_count = bytes / 2;
		s->pcm = (s16 *)buf; // host order = little endian on all supported targets (see rc_put16 users)
	}
	s->valid = s->pcm != 0;

	// loop info (samples)
	if (s->loop)
	{
		s->loop_start_s = rb_dsp_samples (s->fmt, s->loopStart) - 2;
		s->loop_len_s = rb_dsp_samples (s->fmt, s->loopEnd) - rb_dsp_samples (s->fmt, s->loopStart) + 2;
	}
}

static void rb_samp_free (rb_samp_t *s)
{
	FREE (s->pcm);
}

// a sample collection: list of samples
typedef struct rb_coll_t
{
	rb_samp_t *s;
	uint n;
} rb_coll_t;

static void rb_coll_free (rb_coll_t *c)
{
	for (uint i = 0; i < c->n; i++)
		rb_samp_free (c->s + i);
	FREE (c->s);
	c->n = 0;
}

// RBNK with an embedded WAVE block: wave table at "wave_base" (block content), sample data at data_base
static void rb_coll_wave (const rc_file_t *f, rb_coll_t *c, size_t wave_base, size_t data_base)
{
	const u32 cnt = rc_u32 (f, wave_base);
	if (cnt > 100000)
		return;
	c->s = CALLOC (cnt ? cnt : 1, sizeof (rb_samp_t));
	if (!c->s)
		return;
	c->n = cnt;
	for (uint i = 0; i < cnt; i++)
	{
		const size_t wo = wave_base + rc_u32 (f, wave_base + 8 + 8 * (size_t)i);
		rb_samp_load (f, c->s + i, wo, data_base, 0);
	}
}

// RWAR archive at "base"
static void rb_coll_rwar (const rc_file_t *f, rb_coll_t *c, size_t base)
{
	static const u8 magic[7] = { 'R', 'W', 'A', 'R', 0xfe, 0xff, 0x01 };
	if (!rc_match (f, base, magic, 7))
		return;
	const size_t tabl_o = base + rc_u32 (f, base + 0x10), data_o = base + rc_u32 (f, base + 0x18);
	size_t tc, ts;
	if (!rc_block (f, tabl_o, "TABL", &tc, &ts))
		return;
	const u32 cnt = rc_u32 (f, tc);
	if (cnt > 100000)
		return;
	c->s = CALLOC (cnt ? cnt : 1, sizeof (rb_samp_t));
	if (!c->s)
		return;
	c->n = cnt;
	for (uint i = 0; i < cnt; i++)
	{
		const size_t ro = data_o + rc_u32 (f, tc + 8 + 12 * (size_t)i);
		static const u8 rwav[7] = { 'R', 'W', 'A', 'V', 0xfe, 0xff, 0x01 };
		if (!rc_match (f, ro, rwav, 7))
			continue;
		size_t ic, isz, dc, dsz;
		if (!rc_block (f, ro + rc_u32 (f, ro + 0x10), "INFO", &ic, &isz) || !rc_block (f, ro + rc_u32 (f, ro + 0x18), "DATA", &dc, &dsz))
			continue;
		// the data location stored in the wave info is relative to the DATA block header
		rb_samp_load (f, c->s + i, ic, ic + 8, 0);
	}
}

///////////////////////////////////////////////////////////////////////////////
// instruments (RBNKInstr / RSARInstrSet)

typedef struct rb_rgn_t
{
	u8 klo, khi, vlo, vhi;
	u32 samp;
	double attack, decay, sustain_level, release; // seconds / 0..1 (-1 = none)
	double pan, volume;
	int8_t unity;
} rb_rgn_t;

typedef struct rb_instr_t
{
	u32 num;
	rb_rgn_t *r;
	uint n;
} rb_instr_t;

static float rb_falling_rate (u8 t)
{
	if (t == 0x7f)
		return 65535.0f;
	else if (t == 0x7e)
		return 120 / 5.0f;
	else if (t < 0x32)
		return ((t << 1) + 1) / 128.0f / 5.0f;
	return ((60.0f) / (126 - t)) / 5.0f;
}

static const float rb_attack_table[128] = {
	0.9992175f, 0.9984326f, 0.9976452f, 0.9968553f, 0.9960629f, 0.9952679f, 0.9944704f, 0.9936704f, 0.9928677f, 0.9920625f,
	0.9912546f, 0.9904441f, 0.9896309f, 0.9888151f, 0.9879965f, 0.9871752f, 0.9863512f, 0.9855244f, 0.9846949f, 0.9838625f,
	0.9830273f, 0.9821893f, 0.9813483f, 0.9805045f, 0.9796578f, 0.9788081f, 0.9779555f, 0.9770999f, 0.9762413f, 0.9753797f,
	0.9745150f, 0.9736472f, 0.9727763f, 0.9719023f, 0.9710251f, 0.9701448f, 0.9692612f, 0.9683744f, 0.9674844f, 0.9665910f,
	0.9656944f, 0.9647944f, 0.9638910f, 0.9629842f, 0.9620740f, 0.9611604f, 0.9602433f, 0.9593226f, 0.9583984f, 0.9574706f,
	0.9565392f, 0.9556042f, 0.9546655f, 0.9537231f, 0.9527769f, 0.9518270f, 0.9508732f, 0.9499157f, 0.9489542f, 0.9479888f,
	0.9470195f, 0.9460462f, 0.9450689f, 0.9440875f, 0.9431020f, 0.9421124f, 0.9411186f, 0.9401206f, 0.9391184f, 0.9381118f,
	0.9371009f, 0.9360856f, 0.9350659f, 0.9340417f, 0.9330131f, 0.9319798f, 0.9309420f, 0.9298995f, 0.9288523f, 0.9278004f,
	0.9267436f, 0.9256821f, 0.9246156f, 0.9235442f, 0.9224678f, 0.9213864f, 0.9202998f, 0.9192081f, 0.9181112f, 0.9170091f,
	0.9159016f, 0.9147887f, 0.9136703f, 0.9125465f, 0.9114171f, 0.9102821f, 0.9091414f, 0.9079949f, 0.9068427f, 0.9056845f,
	0.9045204f, 0.9033502f, 0.9021740f, 0.9009916f, 0.8998029f, 0.8986080f, 0.8974066f, 0.8961988f, 0.8949844f, 0.8900599f,
	0.8824622f, 0.8759247f, 0.8691861f, 0.8636406f, 0.8535788f, 0.8430189f, 0.8286135f, 0.8149099f, 0.8002172f, 0.7780663f,
	0.7554750f, 0.7242125f, 0.6828239f, 0.6329169f, 0.5592135f, 0.4551411f, 0.3298770f, 0.0000000f
};

static const s16 rb_sustain_table[128] = {
	-723, -722, -721, -651, -601, -562, -530, -503, -480, -460, -442, -425, -410, -396, -383, -371, -360, -349, -339, -330, -321,
	-313, -305, -297, -289, -282, -276, -269, -263, -257, -251, -245, -239, -234, -229, -224, -219, -214, -210, -205, -201, -196,
	-192, -188, -184, -180, -176, -173, -169, -165, -162, -158, -155, -152, -149, -145, -142, -139, -136, -133, -130, -127, -125,
	-122, -119, -116, -114, -111, -109, -106, -103, -101, -99, -96, -94, -91, -89, -87, -85, -82, -80, -78, -76, -74, -72, -70,
	-68, -66, -64, -62, -60, -58, -56, -54, -52, -50, -49, -47, -45, -43, -42, -40, -38, -36, -35, -33, -31, -30, -28, -27, -25,
	-23, -22, -20, -19, -17, -16, -14, -13, -11, -10, -8, -7, -6, -4, -3, -1, 0
};

static void rb_envelope (rb_rgn_t *r, u8 a, u8 d, u8 s, u8 rel)
{
	const float db_min = -90.4f;
	const float real_attack = rb_attack_table[a & 127];
	int msecs = 0;
	float vol = db_min * 10.0f;
	while (vol <= -1.0f / 32.0f)
	{
		vol *= real_attack;
		msecs++;
	}
	r->attack = msecs / 1000.0;
	const float decay_rate = rb_falling_rate (d);
	const s16 sus = rb_sustain_table[s & 127];
	r->decay = ((0.0f - sus) / decay_rate) / 1000.0;
	r->sustain_level = 1.0 - (sus / db_min);
	const float release_rate = rb_falling_rate (rel);
	r->release = ((sus - db_min) / release_rate) / 1000.0;
}

typedef struct rb_reg_t { u8 high; size_t sub; } rb_reg_t;

// enumerates one level of the region trees; "set" is the instrument-set content offset (RBNK DATA block body)
static uint rb_regions (const rc_file_t *f, size_t set, size_t ref, rb_reg_t *out, uint max)
{
	const uint kind = rc_u8 (f, ref + 1);
	uint n = 0;
	if (kind == 1)
	{
		out[n++] = (rb_reg_t){ 0x7f, ref };
	}
	else if (kind == 2)
	{
		const size_t o = set + rc_u32 (f, ref + 4);
		const uint ts = rc_u8 (f, o);
		const size_t te = (o + 1 + ts + 3) & ~(size_t)3;
		for (uint i = 0; i < ts && n < max; i++)
			out[n++] = (rb_reg_t){ rc_u8 (f, o + 1 + i), te + i * 8 };
	}
	else if (kind == 3)
	{
		const size_t o = set + rc_u32 (f, ref + 4);
		const uint mn = rc_u8 (f, o), mx = rc_u8 (f, o + 1);
		const size_t te = o + 4;
		for (uint i = mn; i < mx && n < max; i++)
			out[n++] = (rb_reg_t){ i, te + (size_t)(i - mn) * 8 };
	}
	return n;
}

static bool rb_load_instrs (const rc_file_t *f, size_t set, rb_instr_t **out, uint *n_out)
{
	const u32 cnt = rc_u32 (f, set);
	if (cnt > 100000)
		return false;
	rb_instr_t *ins = CALLOC (cnt ? cnt : 1, sizeof (*ins));
	if (!ins)
		return false;
	uint ni = 0;
	for (uint i = 0; i < cnt; i++)
	{
		const size_t ref = set + 4 + 8 * (size_t)i;
		if (!rc_u8 (f, ref + 1))
			continue;
		rb_reg_t kr[256], vr[256];
		const uint nk = rb_regions (f, set, ref, kr, 256);
		rb_instr_t *in = ins + ni++;
		in->num = i;
		uint cap = 0;
		for (uint k = 0; k < nk; k++)
		{
			const u8 klo = k ? kr[k - 1].high + 1 : 0, khi = kr[k].high;
			const uint nv = rb_regions (f, set, kr[k].sub, vr, 256);
			for (uint v = 0; v < nv; v++)
			{
				const u8 vlo = v ? vr[v - 1].high + 1 : 0, vhi = vr[v].high;
				const size_t rb = set + rc_u32 (f, vr[v].sub + 4);
				if (in->n == cap)
				{
					cap = cap ? cap * 2 : 8;
					in->r = REALLOC (in->r, cap * sizeof (rb_rgn_t));
					if (!in->r)
					{
						in->n = 0;
						break;
					}
				}
				rb_rgn_t *r = in->r + in->n++;
				memset (r, 0, sizeof (*r));
				r->klo = klo; r->khi = khi; r->vlo = vlo; r->vhi = vhi;
				r->samp = rc_u32 (f, rb);
				rb_envelope (r, rc_u8 (f, rb + 4), rc_u8 (f, rb + 5), rc_u8 (f, rb + 6), rc_u8 (f, rb + 7));
				r->unity = (int8_t)rc_u8 (f, rb + 0x0c);
				r->volume = rc_u8 (f, rb + 0x0d) / 127.0;
				const u8 p = rc_u8 (f, rb + 0x0e);
				r->pan = p == 127 ? 1.0 : p == 0 ? 0.0 : p == 64 ? 0.5 : (double)p / 127.0;
			}
		}
	}
	*out = ins;
	*n_out = ni;
	return true;
}

static void rb_free_instrs (rb_instr_t *ins, uint n)
{
	for (uint i = 0; i < n; i++)
		FREE (ins[i].r);
	FREE (ins);
}

///////////////////////////////////////////////////////////////////////////////
// SoundFont writer (SF2File.cpp + VGMColl::CreateSynthFile + ScaleConversion)

static int rb_roundi (double x) { return isnan (x) || fabs (x) > 2e9 ? 0 : x > 0 ? (int)(x + 0.5) : (int)(x - 0.5); }
// std::min (a, 100.0) semantics of the original: a NaN stays NaN
static double rb_min100 (double a) { return 100.0 < a ? 100.0 : a; }
static double rb_log_to_atten (double p) { return p == 0 ? 100.0 : rb_min100 (-(20 * log10 (p))); }
static int16_t rb_i16 (double v) { return isnan (v) || v > 32767.0 || v < -32768.0 ? 0 : (int16_t)v; }
static double rb_secs_to_tc (double s) { return log (s) / log (2.0) * 1200; }

static void sf_chunk_begin (rc_buf_t *b, const char *id, size_t *mark)
{
	rc_put (b, id, 4);
	*mark = b->n;
	rc_put32 (b, 0);
}

static void sf_chunk_end (rc_buf_t *b, size_t mark)
{
	const u32 sz = (u32)(b->n - mark - 4);
	if (b->n & 1)
		rc_put (b, "\0", 1);
	b->d[mark] = sz; b->d[mark + 1] = sz >> 8; b->d[mark + 2] = sz >> 16; b->d[mark + 3] = sz >> 24;
}

static void sf_string (rc_buf_t *b, const char *id, ccp s)
{
	size_t m;
	sf_chunk_begin (b, id, &m);
	rc_put (b, s, strlen (s) + 1);
	sf_chunk_end (b, m);
}

static void sf_gen (rc_buf_t *b, u16 oper, int amount)
{
	rc_put16 (b, oper);
	rc_put16 (b, (u16)amount);
}

static void sf_gen_range (rc_buf_t *b, u16 oper, u8 lo, u8 hi)
{
	rc_put16 (b, oper);
	rc_put16 (b, lo | hi << 8);
}

static enumError sf_build (const rb_coll_t *coll, const rb_instr_t *ins, uint n_ins, ccp name, u8 **out, size_t *out_size)
{
	rc_buf_t b = { 0 };
	size_t riff, list, ck;
	sf_chunk_begin (&b, "RIFF", &riff);
	rc_put (&b, "sfbk", 4);

	// INFO
	sf_chunk_begin (&b, "LIST", &list);
	rc_put (&b, "INFO", 4);
	sf_chunk_begin (&b, "ifil", &ck);
	rc_put16 (&b, 2);
	rc_put16 (&b, 1);
	sf_chunk_end (&b, ck);
	sf_string (&b, "isng", "EMU8000");
	sf_string (&b, "INAM", name);
	sf_string (&b, "ISFT", "nintoolbox wbrsar");
	sf_chunk_end (&b, list);

	// sdta / smpl
	sf_chunk_begin (&b, "LIST", &list);
	rc_put (&b, "sdta", 4);
	sf_chunk_begin (&b, "smpl", &ck);
	for (uint i = 0; i < coll->n; i++)
	{
		const rb_samp_t *s = coll->s + i;
		for (size_t k = 0; k < s->pcm_count; k++)
			rc_put16 (&b, (u16)s->pcm[k]);
		for (int k = 0; k < 46; k++)
			rc_put16 (&b, 0);
	}
	sf_chunk_end (&b, ck);
	sf_chunk_end (&b, list);

	// pdta
	sf_chunk_begin (&b, "LIST", &list);
	rc_put (&b, "pdta", 4);

	// instruments that own at least one region are written
	uint *map = CALLOC (n_ins ? n_ins : 1, sizeof (uint));
	uint nw = 0;
	size_t n_rgn = 0;
	for (uint i = 0; map && i < n_ins; i++)
		if (ins[i].n)
		{
			map[nw++] = i;
			n_rgn += ins[i].n;
		}
	if (!map)
	{
		FREE (b.d);
		return ERR_OUT_OF_MEMORY;
	}

	char nm[64];
	// phdr
	sf_chunk_begin (&b, "phdr", &ck);
	for (uint k = 0; k < nw; k++)
	{
		snprintf (nm, sizeof (nm), "Instr bnk0 num%u", ins[map[k]].num);
		rc_putname (&b, nm, 20);
		rc_put16 (&b, ins[map[k]].num);
		rc_put16 (&b, 0); // bank
		rc_put16 (&b, k); // preset bag index
		rc_put32 (&b, 0); rc_put32 (&b, 0); rc_put32 (&b, 0);
	}
	rc_putname (&b, "EOP", 20);
	rc_put16 (&b, 0); rc_put16 (&b, 0); rc_put16 (&b, nw);
	rc_put32 (&b, 0); rc_put32 (&b, 0); rc_put32 (&b, 0);
	sf_chunk_end (&b, ck);
	// pbag
	sf_chunk_begin (&b, "pbag", &ck);
	for (uint k = 0; k <= nw; k++)
	{
		rc_put16 (&b, k * 2);
		rc_put16 (&b, 0);
	}
	sf_chunk_end (&b, ck);
	// pmod
	sf_chunk_begin (&b, "pmod", &ck);
	for (int k = 0; k < 10; k++)
		rc_put (&b, "\0", 1);
	sf_chunk_end (&b, ck);
	// pgen
	sf_chunk_begin (&b, "pgen", &ck);
	for (uint k = 0; k < nw; k++)
	{
		sf_gen (&b, 16, 250); // reverbEffectsSend
		sf_gen (&b, 41, (int)k); // instrument
	}
	rc_put32 (&b, 0);
	sf_chunk_end (&b, ck);
	// inst
	sf_chunk_begin (&b, "inst", &ck);
	{
		uint bag = 0;
		for (uint k = 0; k < nw; k++)
		{
			snprintf (nm, sizeof (nm), "Instr bnk0 num%u", ins[map[k]].num);
			rc_putname (&b, nm, 20);
			rc_put16 (&b, bag);
			bag += ins[map[k]].n;
		}
		rc_putname (&b, "EOI", 20);
		rc_put16 (&b, bag);
	}
	sf_chunk_end (&b, ck);
	// ibag
	sf_chunk_begin (&b, "ibag", &ck);
	{
		uint gens = 0;
		for (uint k = 0; k < nw; k++)
			for (uint j = 0; j < ins[map[k]].n; j++)
			{
				rc_put16 (&b, gens);
				rc_put16 (&b, 0);
				gens += ins[map[k]].r[j].vhi ? 11 : 10;
			}
		rc_put16 (&b, gens);
		rc_put16 (&b, 0);
	}
	sf_chunk_end (&b, ck);
	// imod
	sf_chunk_begin (&b, "imod", &ck);
	for (int k = 0; k < 10; k++)
		rc_put (&b, "\0", 1);
	sf_chunk_end (&b, ck);
	// igen
	sf_chunk_begin (&b, "igen", &ck);
	bool *sample_seen = CALLOC (coll->n ? coll->n : 1, sizeof (bool));
	int8_t *sample_unity = CALLOC (coll->n ? coll->n : 1, 1);
	for (uint k = 0; k < nw; k++)
		for (uint j = 0; j < ins[map[k]].n; j++)
		{
			const rb_rgn_t *r = ins[map[k]].r + j;
			uint si = r->samp < coll->n ? r->samp : (coll->n ? coll->n - 1 : 0);
			const rb_samp_t *sp = coll->n ? coll->s + si : 0;
			int8_t unity = r->unity;
			if (unity == -1)
				unity = 0x3c;
			if (sample_seen && !sample_seen[si])
			{
				sample_seen[si] = true;
				sample_unity[si] = unity;
			}
			sf_gen_range (&b, 43, r->klo, r->khi);
			if (r->vhi)
				sf_gen_range (&b, 44, r->vlo, r->vhi);
			sf_gen (&b, 48, r->volume < 0 ? 0 : rb_i16 (rb_log_to_atten (r->volume) * 10)); // initialAttenuation
			sf_gen (&b, 17, (int16_t)(rb_roundi (r->pan * 1000) - 500)); // pan
			sf_gen (&b, 54, sp && sp->loop ? 1 : 0); // sampleModes
			sf_gen (&b, 58, (u16)(int16_t)unity); // overridingRootKey
			sf_gen (&b, 34, r->attack == 0 ? -32768 : rb_roundi (rb_secs_to_tc (r->attack)));
			sf_gen (&b, 36, r->decay == 0 ? -32768 : rb_roundi (rb_secs_to_tc (r->decay)));
			double sus = r->sustain_level == -1 ? 0.0 : (r->sustain_level == 0 ? 100.0 : rb_min100 (-(20 * log10 (r->sustain_level))));
			if (sus > 100.0)
				sus = 100.0;
			sf_gen (&b, 37, rb_i16 (sus * 10));
			sf_gen (&b, 38, r->release == 0 ? -32768 : rb_roundi (rb_secs_to_tc (r->release)));
			sf_gen (&b, 53, (int)si); // sampleID (terminal generator)
		}
	rc_put32 (&b, 0);
	sf_chunk_end (&b, ck);
	// shdr
	sf_chunk_begin (&b, "shdr", &ck);
	{
		u32 off = 0;
		for (uint i = 0; i < coll->n; i++)
		{
			const rb_samp_t *s = coll->s + i;
			snprintf (nm, sizeof (nm), "Sample_%04u", i);
			rc_putname (&b, nm, 20);
			const u32 start = off, end = start + (u32)s->pcm_count;
			off = end + 46;
			rc_put32 (&b, start);
			rc_put32 (&b, end);
			rc_put32 (&b, start + s->loop_start_s);
			rc_put32 (&b, start + s->loop_start_s + s->loop_len_s);
			rc_put32 (&b, s->rate);
			rc_put (&b, sample_seen && sample_seen[i] ? (u8[]){ (u8)sample_unity[i] } : (u8[]){ 0x3c }, 1);
			rc_put (&b, "\0", 1); // correction
			rc_put16 (&b, 0); // link
			rc_put16 (&b, 1); // mono
		}
		rc_putname (&b, "EOS", 20);
		for (int k = 0; k < 26; k++)
			rc_put (&b, "\0", 1);
	}
	sf_chunk_end (&b, ck);
	FREE (sample_seen);
	FREE (sample_unity);
	FREE (map);
	(void)n_rgn;
	sf_chunk_end (&b, list);
	sf_chunk_end (&b, riff);
	*out = b.d;
	*out_size = b.n;
	return b.d ? ERR_OK : ERR_OUT_OF_MEMORY;
}

///////////////////////////////////////////////////////////////////////////////
// banks of an archive

typedef struct rb_bank_t
{
	bool have;
	size_t instr_off;
	size_t wave_off; // WAVE block content (0 for RWAR)
	size_t wave_data; // wave data base (absolute)
	bool is_wave;
} rb_bank_t;

static bool rb_bank_locate (const rs_t *r, const rs_bank_t *bank, rb_bank_t *out)
{
	memset (out, 0, sizeof (*out));
	u32 d, w, sz;
	if (!rs_resolve (r, bank->fileID, &d, &w, &sz))
		return false;
	static const u8 magic[7] = { 'R', 'B', 'N', 'K', 0xfe, 0xff, 0x01 };
	if (!rc_match (&r->f, d, magic, 7))
		return false;
	size_t dc, dn, wc, wn;
	if (!rc_block (&r->f, d + rc_u32 (&r->f, d + 0x10), "DATA", &dc, &dn))
		return false;
	out->instr_off = dc;
	out->wave_data = w;
	if (rc_block (&r->f, d + rc_u32 (&r->f, d + 0x18), "WAVE", &wc, &wn) && wn)
	{
		out->is_wave = true;
		out->wave_off = wc;
	}
	out->have = true;
	return true;
}

static void rb_name_unique (char *dst, size_t n, ccp base, char (*used)[128], uint *n_used)
{
	char name[128];
	snprintf (name, sizeof (name), "%s", *base ? base : "unnamed");
	for (char *c = name; *c; c++)
		if (strchr ("/\\:*?\"<>|", *c))
			*c = '_';
	char cand[160];
	snprintf (cand, sizeof (cand), "%s", name);
	for (uint k = 2;; k++)
	{
		bool dup = false;
		for (uint i = 0; i < *n_used; i++)
			if (!strcmp (used[i], cand))
				dup = true;
		if (!dup)
			break;
		snprintf (cand, sizeof (cand), "%.120s_%u", name, k);
	}
	snprintf (dst, n, "%s", cand);
	if (*n_used < 4096)
		snprintf (used[(*n_used)++], 128, "%.127s", cand);
}

static void rb_write_file (ccp path, const u8 *d, size_t n)
{
	FILE *fp = fopen (path, "wb");
	if (fp)
	{
		fwrite (d, 1, n, fp);
		fclose (fp);
	}
}

enumError ConvertRSARNative (const u8 *data, size_t size, ccp out_dir)
{
	rs_t r = { .f = { data, size } };
	if (size < 0x40 || memcmp (data, "RSAR", 4) || !rs_parse (&r))
	{
		rs_free (&r);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	char midi_dir[PATH_MAX], sf_dir[PATH_MAX], path[PATH_MAX];
	snprintf (midi_dir, sizeof (midi_dir), "%s/midi", out_dir);
	snprintf (sf_dir, sizeof (sf_dir), "%s/soundfonts", out_dir);
	CreatePath (midi_dir, true);
	CreatePath (sf_dir, true);

	// bank -> collection / instruments, built lazily
	rb_coll_t *colls = CALLOC (r.n_banks ? r.n_banks : 1, sizeof (*colls));
	rb_instr_t **inss = CALLOC (r.n_banks ? r.n_banks : 1, sizeof (*inss));
	uint *n_inss = CALLOC (r.n_banks ? r.n_banks : 1, sizeof (uint));
	rb_bank_t *bl = CALLOC (r.n_banks ? r.n_banks : 1, sizeof (*bl));
	bool *used_bank = CALLOC (r.n_banks ? r.n_banks : 1, sizeof (bool));
	char (*used_names)[128] = CALLOC (4096, 128);
	char (*used_sf)[128] = CALLOC (4096, 128);
	uint n_un = 0, n_us = 0;
	rc_buf_t manifest = { 0 };
	rc_put (&manifest, "midi\tsoundfont\n", 15);
	char *bank_sf = CALLOC (r.n_banks ? r.n_banks : 1, 160);
	if (!colls || !inss || !n_inss || !bl || !used_bank || !used_names || !used_sf || !bank_sf)
		goto done;

	for (uint i = 0; i < r.n_banks; i++)
		rb_bank_locate (&r, r.banks + i, bl + i);

	uint n_mid = 0;
	for (uint s = 0; s < r.n_sounds; s++)
	{
		const rs_sound_t *snd = r.sounds + s;
		u32 d, w, sz;
		if (!rs_resolve (&r, snd->fileID, &d, &w, &sz))
			continue;
		static const u8 magic[7] = { 'R', 'S', 'E', 'Q', 0xfe, 0xff, 0x01 };
		if (!rc_match (&r.f, d, magic, 7))
			continue;
		const size_t avail = size - d;
		u8 *mid = 0;
		size_t mid_n = 0;
		if (SequenceToMIDIFrom (&mid, &mid_n, data + d, sz && sz < avail ? sz : avail, snd->dataOffset) != ERR_OK || !mid)
		{
			FREE (mid);
			continue;
		}
		char uniq[160];
		rb_name_unique (uniq, sizeof (uniq), snd->name, used_names, &n_un);
		snprintf (path, sizeof (path), "%s/%s.mid", midi_dir, uniq);
		rb_write_file (path, mid, mid_n);
		FREE (mid);
		n_mid++;
		char line[400];
		const char *bn = snd->bankID < r.n_banks ? r.banks[snd->bankID].name : "";
		if (snd->bankID < r.n_banks && bl[snd->bankID].have)
		{
			used_bank[snd->bankID] = true;
			if (!bank_sf[snd->bankID * 160])
				rb_name_unique (bank_sf + snd->bankID * 160, 160, bn, used_sf, &n_us);
			snprintf (line, sizeof (line), "%s.mid\t%s.sf2\n", uniq, bank_sf + snd->bankID * 160);
		}
		else
			snprintf (line, sizeof (line), "%s.mid\t\n", uniq);
		rc_put (&manifest, line, strlen (line));
	}

	// one SoundFont per bank used by at least one sequence
	uint n_sf = 0;
	for (uint i = 0; i < r.n_banks; i++)
	{
		if (!used_bank[i])
			continue;
		const rb_bank_t *b = bl + i;
		if (b->is_wave)
			rb_coll_wave (&r.f, colls + i, b->wave_off, b->wave_data);
		else
			rb_coll_rwar (&r.f, colls + i, b->wave_data);
		if (!rb_load_instrs (&r.f, b->instr_off, inss + i, n_inss + i))
			continue;
		u8 *sf = 0;
		size_t sf_n = 0;
		if (colls[i].n && sf_build (colls + i, inss[i], n_inss[i], bank_sf + i * 160, &sf, &sf_n) == ERR_OK)
		{
			snprintf (path, sizeof (path), "%s/%s.sf2", sf_dir, bank_sf + i * 160);
			rb_write_file (path, sf, sf_n);
			n_sf++;
		}
		FREE (sf);
	}
	snprintf (path, sizeof (path), "%s/sequences.tsv", midi_dir);
	rb_write_file (path, manifest.d, manifest.n);
	err = n_mid || n_sf ? ERR_OK : ERR_NOTHING_TO_DO;

done:
	FREE (manifest.d);
	for (uint i = 0; colls && i < r.n_banks; i++)
	{
		rb_coll_free (colls + i);
		if (inss && inss[i])
			rb_free_instrs (inss[i], n_inss[i]);
	}
	FREE (colls);
	FREE (inss);
	FREE (n_inss);
	FREE (bl);
	FREE (used_bank);
	FREE (used_names);
	FREE (used_sf);
	FREE (bank_sf);
	rs_free (&r);
	return err;
}

// standalone bank: the RBNK and its wave data are laid out back to back in one image
enumError ConvertRBNKToSF2 (const u8 *rbnk, size_t rbnk_size, const u8 *wave, size_t wave_size, ccp bank_name, u8 **out_sf2, size_t *out_size)
{
	if (!rbnk || rbnk_size < 0x20 || memcmp (rbnk, "RBNK", 4))
		return ERR_NOTHING_TO_DO;
	size_t base = (rbnk_size + 31) & ~(size_t)31;
	u8 *img = CALLOC (1, base + wave_size + 32);
	if (!img)
		return ERR_OUT_OF_MEMORY;
	memcpy (img, rbnk, rbnk_size);
	if (wave)
		memcpy (img + base, wave, wave_size);
	rc_file_t f = { img, base + wave_size };
	enumError err = ERR_NOTHING_TO_DO;
	size_t dc, dn, wc, wn;
	if (rc_block (&f, rc_u32 (&f, 0x10), "DATA", &dc, &dn))
	{
		rb_coll_t coll = { 0 };
		if (rc_block (&f, rc_u32 (&f, 0x18), "WAVE", &wc, &wn) && wn)
			rb_coll_wave (&f, &coll, wc, base);
		else if (wave)
			rb_coll_rwar (&f, &coll, base);
		rb_instr_t *ins = 0;
		uint n_ins = 0;
		if (coll.n && rb_load_instrs (&f, dc, &ins, &n_ins))
			err = sf_build (&coll, ins, n_ins, bank_name && *bank_name ? bank_name : "bank", out_sf2, out_size);
		rb_free_instrs (ins, n_ins);
		rb_coll_free (&coll);
	}
	FREE (img);
	return err;
}

///////////////////////////////////////////////////////////////////////////////
// Nintendo DS SDAT (NDSScanner.cpp / NDSInstrSet.cpp): SYMB/INFO/FAT tables, SBNK
// instrument banks, SWAR wave archives (PCM8 / PCM16 / IMA-ADPCM) and the eight
// PSG duty-cycle waves vgmtrans synthesizes for the DS square channels.

static u32 nl16 (const rc_file_t *f, size_t o) { return rc_u8 (f, o) | rc_u8 (f, o + 1) << 8; }
static u32 nl32 (const rc_file_t *f, size_t o) { return nl16 (f, o) | nl16 (f, o + 2) << 16; }

static const unsigned nds_adpcm_table[89] = {
	0x0007, 0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x0010, 0x0011, 0x0013, 0x0015, 0x0017, 0x0019, 0x001C, 0x001F,
	0x0022, 0x0025, 0x0029, 0x002D, 0x0032, 0x0037, 0x003C, 0x0042, 0x0049, 0x0050, 0x0058, 0x0061, 0x006B, 0x0076, 0x0082, 0x008F,
	0x009D, 0x00AD, 0x00BE, 0x00D1, 0x00E6, 0x00FD, 0x0117, 0x0133, 0x0151, 0x0173, 0x0198, 0x01C1, 0x01EE, 0x0220, 0x0256, 0x0292,
	0x02D4, 0x031C, 0x036C, 0x03C3, 0x0424, 0x048E, 0x0502, 0x0583, 0x0610, 0x06AB, 0x0756, 0x0812, 0x08E0, 0x09C3, 0x0ABD, 0x0BD0,
	0x0CFF, 0x0E4C, 0x0FBA, 0x114C, 0x1307, 0x14EE, 0x1706, 0x1954, 0x1BDC, 0x1EA5, 0x21B6, 0x2515, 0x28CA, 0x2CDF, 0x315B, 0x364B,
	0x3BB9, 0x41B2, 0x4844, 0x4F7E, 0x5771, 0x602F, 0x69CE, 0x7462, 0x7FFF
};
static const int nds_ima_index[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

static void nds_ima_nibble (uint nib, int *index, int *pcm)
{
	int diff = nds_adpcm_table[*index] / 8;
	if (nib & 1) diff += nds_adpcm_table[*index] / 4;
	if (nib & 2) diff += nds_adpcm_table[*index] / 2;
	if (nib & 4) diff += nds_adpcm_table[*index];
	if (!(nib & 8))
		*pcm = *pcm + diff > 0x7fff ? 0x7fff : *pcm + diff;
	else
		*pcm = *pcm - diff < -0x7fff ? -0x7fff : *pcm - diff;
	const int ni = *index + nds_ima_index[nib & 7];
	*index = ni > 88 ? 88 : ni < 0 ? 0 : ni;
}

// samples of one SWAR (rb_coll_t from the wave archive at "base")
static void nds_load_swar (const rc_file_t *f, rb_coll_t *c, size_t base)
{
	const u32 cnt = nl32 (f, base + 0x38);
	if (cnt > 100000)
		return;
	c->s = CALLOC (cnt ? cnt : 1, sizeof (rb_samp_t));
	if (!c->s)
		return;
	c->n = cnt;
	for (uint i = 0; i < cnt; i++)
	{
		rb_samp_t *s = c->s + i;
		const size_t ps = base + nl32 (f, base + 0x3c + 4 * (size_t)i);
		const uint type = rc_u8 (f, ps);
		if (type > 2)
			continue;
		const bool loops = rc_u8 (f, ps + 1) != 0;
		s->rate = nl16 (f, ps + 2);
		u32 loop_off = nl16 (f, ps + 6) * 4, non_loop = nl16 (f, ps + 8) * 4;
		size_t data;
		u32 len;
		if (type == 2) // IMA-ADPCM
		{
			data = ps + 0x10;
			len = loop_off + non_loop - 4;
		}
		else
		{
			data = ps + 0xc;
			len = loop_off + non_loop;
		}
		if (len > 0x2000000 || data + len > f->n)
			continue;
		s->loop = loops;
		if (type == 2)
		{
			const u32 hdr = nl32 (f, data - 4);
			int pcm = hdr & 0xffff, idx = (hdr >> 16) & 0x7f;
			if (idx > 88)
				idx = 88;
			s->pcm_count = (size_t)len * 2 + 1;
			s->pcm = CALLOC (s->pcm_count, 2);
			if (!s->pcm)
				continue;
			size_t d = 0;
			s->pcm[d++] = (s16)pcm;
			for (u32 k = 0; k < len; k++)
			{
				const u8 b = rc_u8 (f, data + k);
				nds_ima_nibble (b & 15, &idx, &pcm);
				s->pcm[d++] = (s16)pcm;
				nds_ima_nibble (b >> 4, &idx, &pcm);
				s->pcm[d++] = (s16)pcm;
			}
			u32 lo = loop_off * 2;
			lo = lo - 8 + 1;
			non_loop = (len * 2 + 1) - lo;
			s->loop_start_s = lo;
			s->loop_len_s = non_loop;
		}
		else if (type == 1) // PCM16
		{
			s->pcm_count = len / 2;
			s->pcm = CALLOC (s->pcm_count ? s->pcm_count : 1, 2);
			if (!s->pcm)
				continue;
			for (size_t k = 0; k < s->pcm_count; k++)
				s->pcm[k] = (s16)nl16 (f, data + 2 * k);
			u32 ll = non_loop;
			if (loops && !ll)
				ll = len - loop_off;
			s->loop_start_s = loop_off / 2;
			s->loop_len_s = ll / 2;
		}
		else // PCM8
		{
			s->pcm_count = len;
			s->pcm = CALLOC (len ? len : 1, 2);
			if (!s->pcm)
				continue;
			for (u32 k = 0; k < len; k++)
				s->pcm[k] = (s16)((int8_t)rc_u8 (f, data + k) << 8);
			u32 ll = non_loop;
			if (loops && !ll)
				ll = len - loop_off;
			s->loop_start_s = loop_off;
			s->loop_len_s = ll;
		}
		if (!loops)
			s->loop_start_s = s->loop_len_s = 0;
		s->valid = true;
	}
}

// the eight synthesized duty cycle waves ("PSG Samples"), one second at 44.1 kHz each
static void nds_psg_coll (rb_coll_t *c)
{
	c->s = CALLOC (8, sizeof (rb_samp_t));
	if (!c->s)
		return;
	c->n = 8;
	for (uint duty = 0; duty < 8; duty++)
	{
		rb_samp_t *s = c->s + duty;
		const int rate = 44100;
		s->rate = rate;
		s->loop = true;
		s->pcm_count = rate;
		s->loop_start_s = 0;
		s->loop_len_s = rate;
		s->pcm = CALLOC (rate, 2);
		if (!s->pcm)
			continue;
		const uint num_high = (duty + 1) % 8, num_low = 8 - num_high;
		(void)num_high;
		const double dt = 1.0 / rate, width = 1.0 / 440.0;
		uint pos = 0;
		s16 sample = 0;
		double t = 0;
		bool gen = true;
		for (int i = 0; i < rate; i++)
		{
			t += dt;
			if (t >= width)
			{
				gen = true;
				t -= width;
			}
			if (gen)
			{
				sample = pos < num_low ? -0x7fff : 0x7fff;
				pos = (pos + 1) % 8;
				gen = false;
			}
			s->pcm[i] = sample;
		}
		s->valid = true;
	}
}

static u16 nds_falling_rate (u8 t)
{
	u32 r;
	if (t == 0x7f)
		r = 0xffff;
	else if (t == 0x7e)
		r = 0x3c00;
	else if (t < 0x32)
		r = (t * 2 + 1) & 0xffff;
	else
		r = (0x1e00 / (u8)(0x7e - t)) & 0xffff;
	return (u16)r;
}

static void nds_artic (const rc_file_t *f, size_t o, rb_rgn_t *r)
{
	static const u8 attack_table[19] = { 0x00, 0x01, 0x05, 0x0E, 0x1A, 0x26, 0x33, 0x3F, 0x49, 0x54, 0x5C, 0x64, 0x6D, 0x74, 0x7B, 0x7F, 0x84, 0x89, 0x8F };
	static const u16 sustain_table[128] = {
		0xFD2D, 0xFD2E, 0xFD2F, 0xFD75, 0xFDA7, 0xFDCE, 0xFDEE, 0xFE09, 0xFE20, 0xFE34, 0xFE46, 0xFE57, 0xFE66, 0xFE74, 0xFE81, 0xFE8D,
		0xFE98, 0xFEA3, 0xFEAD, 0xFEB6, 0xFEBF, 0xFEC7, 0xFECF, 0xFED7, 0xFEDF, 0xFEE6, 0xFEEC, 0xFEF3, 0xFEF9, 0xFEFF, 0xFF05, 0xFF0B,
		0xFF11, 0xFF16, 0xFF1B, 0xFF20, 0xFF25, 0xFF2A, 0xFF2E, 0xFF33, 0xFF37, 0xFF3C, 0xFF40, 0xFF44, 0xFF48, 0xFF4C, 0xFF50, 0xFF53,
		0xFF57, 0xFF5B, 0xFF5E, 0xFF62, 0xFF65, 0xFF68, 0xFF6B, 0xFF6F, 0xFF72, 0xFF75, 0xFF78, 0xFF7B, 0xFF7E, 0xFF81, 0xFF83, 0xFF86,
		0xFF89, 0xFF8C, 0xFF8E, 0xFF91, 0xFF93, 0xFF96, 0xFF99, 0xFF9B, 0xFF9D, 0xFFA0, 0xFFA2, 0xFFA5, 0xFFA7, 0xFFA9, 0xFFAB, 0xFFAE,
		0xFFB0, 0xFFB2, 0xFFB4, 0xFFB6, 0xFFB8, 0xFFBA, 0xFFBC, 0xFFBE, 0xFFC0, 0xFFC2, 0xFFC4, 0xFFC6, 0xFFC8, 0xFFCA, 0xFFCC, 0xFFCE,
		0xFFCF, 0xFFD1, 0xFFD3, 0xFFD5, 0xFFD6, 0xFFD8, 0xFFDA, 0xFFDC, 0xFFDD, 0xFFDF, 0xFFE1, 0xFFE2, 0xFFE4, 0xFFE5, 0xFFE7, 0xFFE9,
		0xFFEA, 0xFFEC, 0xFFED, 0xFFEF, 0xFFF0, 0xFFF2, 0xFFF3, 0xFFF5, 0xFFF6, 0xFFF8, 0xFFF9, 0xFFFA, 0xFFFC, 0xFFFD, 0xFFFF, 0x0000
	};
	const double intr = 1.0 / 192.0;
	r->unity = (int8_t)rc_u8 (f, o);
	const u8 at = rc_u8 (f, o + 1), dt = rc_u8 (f, o + 2), st = rc_u8 (f, o + 3) & 127, rt = rc_u8 (f, o + 4), pan = rc_u8 (f, o + 5);
	const u8 real_attack = at >= 0x6d ? attack_table[(0x7f - at) % 19] : (u8)(0xff - at);
	const s16 real_decay = (s16)nds_falling_rate (dt), real_release = (s16)nds_falling_rate (rt);
	int count = 0;
	for (long i = 0x16980; i != 0; i = (i * real_attack) >> 8)
		count++;
	r->attack = count * intr;
	long sus = st == 0x7f ? 0 : (long)(0x10000 - sustain_table[st]) << 7;
	if (dt == 0x7f)
		r->decay = 0.001;
	else
		r->decay = (real_decay ? 0x16980 / real_decay : 0) * intr;
	r->sustain_level = sus == 0 ? 1.0 : (double)(0x16980 - sus) / (double)0x16980;
	r->release = (real_release ? 0x16980 / real_release : 0) * intr;
	r->pan = pan == 0 ? 0 : pan == 127 ? 1.0 : pan == 64 ? 0.5 : (double)pan / 127.0;
	r->volume = -1;
}

typedef struct nds_wa_t { bool have; rb_coll_t c; } nds_wa_t;

// instruments of one SBNK; "wa_slot" maps the four wave archive slots to absolute sample offsets
static bool nds_load_bank (const rc_file_t *f, size_t base, const int *slot_off, const int *slot_cnt, rb_instr_t **out, uint *n_out)
{
	const u32 cnt = nl32 (f, base + 0x38);
	if (cnt > 100000)
		return false;
	rb_instr_t *ins = CALLOC (cnt ? cnt : 1, sizeof (*ins));
	if (!ins)
		return false;
	uint ni = 0;
	for (uint i = 0; i < cnt; i++)
	{
		const u32 temp = nl32 (f, base + 0x3c + 4 * (size_t)i);
		if (!temp)
			continue;
		const uint type = temp & 0xff;
		const size_t o = (temp >> 8) + base;
		rb_instr_t *in = ins + ni;
		in->num = i;
		uint cap = 0;
#define ADDRGN(rg) do { if (in->n == cap) { cap = cap ? cap * 2 : 4; in->r = REALLOC (in->r, cap * sizeof (rb_rgn_t)); if (!in->r) { in->n = 0; goto next; } } \
		rb_rgn_t *rg = in->r + in->n++; memset (rg, 0, sizeof (*rg)); rg->vhi = 0x7f; rg->khi = 0x7f;
#define ENDRGN } while (0)
		// sample index for a (sampNum, waSlot) pair; slot < 0 = PSG
		#define SAMPIDX(sn, wa) ((wa) >= 0 && (wa) < 4 && slot_off[(wa)] >= 0 ? (u32)(slot_off[(wa)] + (int)(sn)) : (u32)(sn))
		if (type == 1)
		{
			ADDRGN (r);
			const uint sn = nl16 (f, o);
			const int wa = (int)nl16 (f, o + 2);
			r->samp = SAMPIDX (sn, wa);
			nds_artic (f, o + 4, r);
			ENDRGN;
		}
		else if (type == 2)
		{
			ADDRGN (r);
			const uint duty = rc_u8 (f, o) & 7;
			r->samp = duty; // PSG collection comes first
			nds_artic (f, o + 4, r);
			r->unity = 45;
			ENDRGN;
		}
		else if (type == 0x10)
		{
			const uint lo = rc_u8 (f, o), hi = rc_u8 (f, o + 1);
			const uint nr = hi - lo + 1;
			for (uint k = 0; k < nr && k < 128; k++)
			{
				ADDRGN (r);
				const size_t ro = o + 2 + (size_t)k * 12;
				r->klo = r->khi = (u8)(lo + k);
				const uint sn = nl16 (f, ro + 2);
				const int wa = (int)nl16 (f, ro + 4);
				r->samp = SAMPIDX (sn, wa);
				nds_artic (f, ro + 6, r);
				ENDRGN;
			}
		}
		else if (type == 0x11)
		{
			u8 kr[8];
			uint nr = 0;
			for (int k = 0; k < 8; k++)
			{
				kr[k] = rc_u8 (f, o + k);
				if (!kr[k])
					break;
				nr++;
			}
			for (uint k = 0; k < nr; k++)
			{
				ADDRGN (r);
				const size_t ro = o + 8 + (size_t)k * 12;
				r->klo = k ? kr[k - 1] + 1 : 0;
				r->khi = kr[k];
				const uint sn = nl16 (f, ro + 2);
				const int wa = (int)nl16 (f, ro + 4);
				r->samp = SAMPIDX (sn, wa);
				nds_artic (f, ro + 6, r);
				ENDRGN;
			}
		}
	next:
		ni++;
	}
	(void)slot_cnt;
	*out = ins;
	*n_out = ni;
	return true;
}

enumError ConvertSDATNative (const u8 *data, size_t size, ccp out_dir)
{
	static const u8 magic[8] = { 'S', 'D', 'A', 'T', 0xff, 0xfe, 0x00, 0x01 };
	rc_file_t f = { data, size };
	if (size < 0x40 || !rc_match (&f, 0, magic, 8))
		return ERR_NOTHING_TO_DO;
	const size_t symb = nl32 (&f, 0x10), info = nl32 (&f, 0x18), fat = nl32 (&f, 0x20);
	const bool has_symb = symb != 0;
	const u32 n_seq = nl32 (&f, nl32 (&f, info + 8) + info), n_bnk = nl32 (&f, nl32 (&f, info + 0x10) + info), n_wa = nl32 (&f, nl32 (&f, info + 0x14) + info);
	if (n_seq > 65536 || n_bnk > 65536 || n_wa > 65536)
		return ERR_NOTHING_TO_DO;

	char (*seq_names)[96] = CALLOC (n_seq ? n_seq : 1, 96);
	char (*bnk_names)[96] = CALLOC (n_bnk ? n_bnk : 1, 96);
	for (int kind = 0; kind < 2 && seq_names && bnk_names; kind++)
	{
		const u32 n = kind ? n_bnk : n_seq;
		char (*names)[96] = kind ? bnk_names : seq_names;
		const size_t list = has_symb ? symb + nl32 (&f, symb + (kind ? 0x10 : 0x08)) : 0;
		for (uint i = 0; i < n; i++)
		{
			if (has_symb)
			{
				const size_t so = symb + nl32 (&f, list + 4 + 4 * (size_t)i);
				if (so < size)
					snprintf (names[i], 96, "%.31s", (const char *)data + so);
			}
			else
				snprintf (names[i], 96, "%s_%04u", kind ? "SBNK" : "SSEQ", i);
		}
	}

	// INFO tables
	const size_t p_seq = nl32 (&f, info + 8) + info, p_bnk = nl32 (&f, info + 0x10) + info, p_wa = nl32 (&f, info + 0x14) + info;
	const u32 ns = nl32 (&f, p_seq), nb = nl32 (&f, p_bnk), nw = nl32 (&f, p_wa);
	u16 *seq_fid = CALLOC (ns ? ns : 1, 2), *seq_bnk = CALLOC (ns ? ns : 1, 2), *bnk_fid = CALLOC (nb ? nb : 1, 2), *wa_fid = CALLOC (nw ? nw : 1, 2);
	u16 (*bnk_wa)[4] = CALLOC (nb ? nb : 1, 8);
	if (!seq_names || !bnk_names || !seq_fid || !seq_bnk || !bnk_fid || !wa_fid || !bnk_wa || ns > 65536 || nb > 65536 || nw > 65536)
	{
		FREE (seq_names); FREE (bnk_names); FREE (seq_fid); FREE (seq_bnk); FREE (bnk_fid); FREE (wa_fid); FREE (bnk_wa);
		return ERR_NOTHING_TO_DO;
	}
	for (uint i = 0; i < ns; i++)
	{
		const u32 u = nl32 (&f, p_seq + 4 + 4 * (size_t)i);
		seq_fid[i] = u ? nl16 (&f, info + u) : 0xffff;
		seq_bnk[i] = nl16 (&f, info + u + 4);
	}
	for (uint i = 0; i < nb; i++)
	{
		const u32 u = nl32 (&f, p_bnk + 4 + 4 * (size_t)i);
		bnk_fid[i] = u ? nl16 (&f, info + u) : 0xffff;
		for (int j = 0; j < 4; j++)
		{
			const u32 w = nl16 (&f, info + u + 4 + 2 * (size_t)j);
			bnk_wa[i][j] = w >= n_wa ? 0xffff : (u16)w;
		}
	}
	for (uint i = 0; i < nw; i++)
	{
		const u32 u = nl32 (&f, p_wa + 4 + 4 * (size_t)i);
		wa_fid[i] = u ? nl16 (&f, info + u) : 0xffff;
	}

	enumError err = ERR_NOTHING_TO_DO;
	char midi_dir[PATH_MAX], sf_dir[PATH_MAX], path[PATH_MAX];
	snprintf (midi_dir, sizeof (midi_dir), "%s/midi", out_dir);
	snprintf (sf_dir, sizeof (sf_dir), "%s/soundfonts", out_dir);
	CreatePath (midi_dir, true);
	CreatePath (sf_dir, true);
	rc_buf_t manifest = { 0 };
	rc_put (&manifest, "midi\tsoundfont\n", 15);
	char (*used_names)[128] = CALLOC (4096, 128);
	char (*used_sf)[128] = CALLOC (4096, 128);
	bool *bank_used = CALLOC (nb ? nb : 1, sizeof (bool));
	char *bank_sf = CALLOC (nb ? nb : 1, 160);
	uint n_un = 0, n_us = 0, n_mid = 0, n_sf = 0;
	if (!used_names || !used_sf || !bank_used || !bank_sf)
		goto done;

	for (uint i = 0; i < ns; i++)
	{
		if (seq_fid[i] == 0xffff)
			continue;
		const size_t fo = fat + 12 + (size_t)seq_fid[i] * 0x10;
		const size_t off = nl32 (&f, fo), len = nl32 (&f, fo + 4);
		if (off >= size || !len)
			continue;
		const u32 bidx = seq_bnk[i];
		u8 *mid = 0;
		size_t mid_n = 0;
		if (SequenceToMIDI (&mid, &mid_n, data + off, len < size - off ? len : size - off) != ERR_OK || !mid)
		{
			FREE (mid);
			continue;
		}
		char uniq[160];
		rb_name_unique (uniq, sizeof (uniq), seq_names[i], used_names, &n_un);
		snprintf (path, sizeof (path), "%s/%s.mid", midi_dir, uniq);
		rb_write_file (path, mid, mid_n);
		FREE (mid);
		n_mid++;
		char line[400];
		if (bidx < nb && bnk_fid[bidx] != 0xffff)
		{
			bank_used[bidx] = true;
			if (!bank_sf[bidx * 160])
				rb_name_unique (bank_sf + bidx * 160, 160, bnk_names[bidx], used_sf, &n_us);
			snprintf (line, sizeof (line), "%s.mid\t%s.sf2\n", uniq, bank_sf + bidx * 160);
		}
		else
			snprintf (line, sizeof (line), "%s.mid\t\n", uniq);
		rc_put (&manifest, line, strlen (line));
	}

	for (uint b = 0; b < nb; b++)
	{
		if (!bank_used[b])
			continue;
		// sample collections: PSG first, then the bank's wave archives in slot order
		rb_coll_t all = { 0 }, psg = { 0 };
		rb_coll_t wa[4] = { { 0 } };
		int slot_off[4] = { -1, -1, -1, -1 }, slot_cnt[4] = { 0 };
		nds_psg_coll (&psg);
		uint total = psg.n;
		for (int j = 0; j < 4; j++)
		{
			const u16 w = bnk_wa[b][j];
			if (w == 0xffff || wa_fid[w] == 0xffff)
				continue;
			const size_t fo = fat + 12 + (size_t)wa_fid[w] * 0x10;
			const size_t off = nl32 (&f, fo);
			if (off >= size)
				continue;
			nds_load_swar (&f, wa + j, off);
			slot_off[j] = (int)total;
			slot_cnt[j] = (int)wa[j].n;
			total += wa[j].n;
		}
		all.n = total;
		all.s = CALLOC (total ? total : 1, sizeof (rb_samp_t));
		if (all.s)
		{
			uint k = 0;
			for (uint x = 0; x < psg.n; x++)
				all.s[k++] = psg.s[x];
			for (int j = 0; j < 4; j++)
				for (uint x = 0; x < wa[j].n; x++)
					all.s[k++] = wa[j].s[x];
			const size_t fo = fat + 12 + (size_t)bnk_fid[b] * 0x10;
			const size_t off = nl32 (&f, fo);
			rb_instr_t *ins = 0;
			uint n_ins = 0;
			if (off < size && nds_load_bank (&f, off, slot_off, slot_cnt, &ins, &n_ins))
			{
				u8 *sf = 0;
				size_t sf_n = 0;
				if (sf_build (&all, ins, n_ins, bank_sf + b * 160, &sf, &sf_n) == ERR_OK)
				{
					snprintf (path, sizeof (path), "%s/%s.sf2", sf_dir, bank_sf + b * 160);
					rb_write_file (path, sf, sf_n);
					n_sf++;
				}
				FREE (sf);
				rb_free_instrs (ins, n_ins);
			}
			FREE (all.s); // sample buffers are owned by psg / wa[]
		}
		rb_coll_free (&psg);
		for (int j = 0; j < 4; j++)
			rb_coll_free (wa + j);
	}
	snprintf (path, sizeof (path), "%s/sequences.tsv", midi_dir);
	rb_write_file (path, manifest.d, manifest.n);
	err = n_mid || n_sf ? ERR_OK : ERR_NOTHING_TO_DO;
done:
	FREE (manifest.d);
	FREE (used_names);
	FREE (used_sf);
	FREE (bank_used);
	FREE (bank_sf);
	FREE (seq_names); FREE (bnk_names); FREE (seq_fid); FREE (seq_bnk); FREE (bnk_fid); FREE (wa_fid); FREE (bnk_wa);
	return err;
}

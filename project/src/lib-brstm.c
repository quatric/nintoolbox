// Ported from mobipeg's FFmpeg fork (libavformat/brstm.c, brstmenc.c),
// Copyright (c) 2012 Paul B Mahol, LGPL v2.1+. See lib-brstm.h.

#include "lib-brstm.h"
#include <math.h>

// -----------------------------------------------------------------------------
// tiny growable byte buffer (same shape as lib-brsar.c's, kept local/static
// on purpose -- this file has no other dependency on lib-brsar.c)

typedef struct membuf_t
{
	u8 *data;
	size_t size, capacity;
} membuf_t;

static void mb_init (membuf_t *mb)
{
	mb->data = 0;
	mb->size = mb->capacity = 0;
}

static void mb_reserve (membuf_t *mb, size_t need)
{
	if (mb->size + need <= mb->capacity)
		return;
	size_t cap = mb->capacity ? mb->capacity * 2 : 0x1000;
	while (cap < mb->size + need)
		cap *= 2;
	mb->data = REALLOC (mb->data, cap);
	mb->capacity = cap;
}

static void mb_append (membuf_t *mb, const void *src, size_t len)
{
	mb_reserve (mb, len);
	if (len)
		memcpy (mb->data + mb->size, src, len);
	mb->size += len;
}

static void mb_u8 (membuf_t *mb, u8 v)
{
	mb_append (mb, &v, 1);
}
static void mb_u16 (membuf_t *mb, u16 v)
{
	u8 b[2] = { (u8)(v >> 8), (u8)v };
	mb_append (mb, b, 2);
}
static void mb_u32 (membuf_t *mb, u32 v)
{
	u8 b[4] = { (u8)(v >> 24), (u8)(v >> 16), (u8)(v >> 8), (u8)v };
	mb_append (mb, b, 4);
}
static void mb_tag (membuf_t *mb, const char *tag)
{
	mb_append (mb, tag, 4);
}

// FSTM/CSTM variants share this layout but CSTM defaults to little-endian
// fields (brstm_write_fstm()'s wr16/wr32, driven by c->little_endian).
static void mb_u16e (membuf_t *mb, u16 v, bool le)
{
	u8 b[2];
	if (le)
	{
		b[0] = (u8)v;
		b[1] = (u8)(v >> 8);
	}
	else
	{
		b[0] = (u8)(v >> 8);
		b[1] = (u8)v;
	}
	mb_append (mb, b, 2);
}
static void mb_u32e (membuf_t *mb, u32 v, bool le)
{
	u8 b[4];
	if (le)
	{
		b[0] = (u8)v;
		b[1] = (u8)(v >> 8);
		b[2] = (u8)(v >> 16);
		b[3] = (u8)(v >> 24);
	}
	else
	{
		b[0] = (u8)(v >> 24);
		b[1] = (u8)(v >> 16);
		b[2] = (u8)(v >> 8);
		b[3] = (u8)v;
	}
	mb_append (mb, b, 4);
}

static void mb_pad_to (membuf_t *mb, size_t target)
{
	if (target > mb->size)
	{
		mb_reserve (mb, target - mb->size);
		memset (mb->data + mb->size, 0, target - mb->size);
		mb->size = target;
	}
}

static void mb_free (membuf_t *mb)
{
	if (mb->data)
		FREE (mb->data);
	mb->data = 0;
	mb->size = mb->capacity = 0;
}

static u32 rd_u32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}
static u16 rd_u16 (const u8 *p)
{
	return (u16)((u16)p[0] << 8 | p[1]);
}
static u32 rd_u32e (const u8 *p, bool le)
{
	return le ? ((u32)p[3] << 24 | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0]) : rd_u32 (p);
}
static u16 rd_u16e (const u8 *p, bool le)
{
	return le ? (u16)((u16)p[1] << 8 | p[0]) : rd_u16 (p);
}

// -----------------------------------------------------------------------------

void FreeBRSTMAudio (brstm_audio_t *audio)
{
	for (int ch = 0; ch < DSP_ADPCM_MAX_CHANNELS; ch++)
	{
		if (audio->pcm[ch])
			FREE (audio->pcm[ch]);
		audio->pcm[ch] = 0;
	}
}

// Shared ADPCM setup: derive coefficients and encode the whole (single-block)
// stream. Returns bytes-per-channel in *block_size.
static void EncodeAdpcmChannels (
	const brstm_audio_t *audio, s16 coefs[][16], u8 *adpcm_data[], s64 *block_size)
{
	for (int ch = 0; ch < audio->channels; ch++)
	{
		DspAdpcmCorrelateCoefs (audio->pcm[ch], audio->n_samples, coefs[ch]);

		s64 nframes = DspAdpcmFrameCount (audio->n_samples);
		*block_size = nframes * DSP_ADPCM_BYTES_PER_FRAME;
		adpcm_data[ch] = MALLOC (*block_size);
		memset (adpcm_data[ch], 0, *block_size);

		int h1 = 0, h2 = 0;
		for (s64 f = 0; f < nframes; f++)
		{
			s64 off = f * DSP_ADPCM_SAMPLES_PER_FRAME;
			int count = (int)(audio->n_samples - off < DSP_ADPCM_SAMPLES_PER_FRAME
					? audio->n_samples - off
					: DSP_ADPCM_SAMPLES_PER_FRAME);
			DspAdpcmEncodeBlock (audio->pcm[ch] + off, count,
				adpcm_data[ch] + f * DSP_ADPCM_BYTES_PER_FRAME, coefs[ch], &h1, &h2);
		}
	}
}

// -----------------------------------------------------------------------------
// EncodeRSTM(): mirrors brstm_write_rstm() with block_count fixed at 1 (see
// lib-brstm.h for why). Field offsets (h1rel/h2rel/h3rel/ci0, chunk-table
// layout, HEAD1/HEAD2/HEAD3 sub-blocks) are taken verbatim from that function.

static enumError EncodeRSTM (
	u8 **out_data, size_t *out_size, const brstm_audio_t *audio, bool use_adpcm)
{
	int channels = audio->channels;
	s16 coefs[DSP_ADPCM_MAX_CHANNELS][16];
	u8 *adpcm_data[DSP_ADPCM_MAX_CHANNELS] = { 0 };
	s64 block_size = 0; // bytes per channel

	if (use_adpcm)
		EncodeAdpcmChannels (audio, coefs, adpcm_data, &block_size);
	else
		block_size = audio->n_samples * 2; // bytes per channel, PCM16

	s64 samples_per_block = use_adpcm
		? block_size / DSP_ADPCM_BYTES_PER_FRAME * DSP_ADPCM_SAMPLES_PER_FRAME
		: block_size / 2;

	int h1rel = 0x18;
	int h2rel = h1rel + 0x34;
	int h3rel = h2rel + 0x20;
	int ci0 = h3rel + 4 + 8 * channels;
	int head_body = ci0 + 0x38 * channels;
	int head_size = ((8 + head_body + 0x1F) / 0x20) * 0x20;
	s64 adpc_data = use_adpcm ? 1 /*block_count*/ * channels * 4 : 0;
	s64 adpc_size = use_adpcm ? ((8 + adpc_data + 0x1F) / 0x20) * 0x20 : 0;
	s64 audio_bytes = block_size * channels;
	s64 data_off = 0x40 + head_size + adpc_size;
	s64 data_size = ((0x20 + audio_bytes + 0x1F) / 0x20) * 0x20;
	s64 head_body_base = 0x40 + 8;

	membuf_t out;
	mb_init (&out);

	mb_tag (&out, "RSTM");
	mb_u16 (&out, 0xFEFF);
	mb_u8 (&out, 1);
	mb_u8 (&out, 0); // version 1.0
	mb_u32 (&out, (u32)(data_off + data_size)); // file size
	mb_u16 (&out, 0x40); // header size
	mb_u16 (&out, use_adpcm ? 3 : 2); // chunk count
	mb_u32 (&out, 0x40);
	mb_u32 (&out, (u32)head_size);
	mb_u32 (&out, use_adpcm ? (u32)(0x40 + head_size) : 0);
	mb_u32 (&out, (u32)adpc_size);
	mb_u32 (&out, (u32)data_off);
	mb_u32 (&out, (u32)data_size);
	mb_pad_to (&out, 0x40);

	// HEAD
	mb_tag (&out, "HEAD");
	mb_u32 (&out, (u32)head_size);
	mb_u32 (&out, 0x01000000);
	mb_u32 (&out, h1rel);
	mb_u32 (&out, 0x01000000);
	mb_u32 (&out, h2rel);
	mb_u32 (&out, 0x01000000);
	mb_u32 (&out, h3rel);

	// HEAD1
	mb_pad_to (&out, head_body_base + h1rel);
	mb_u8 (&out, use_adpcm ? 2 : 1);
	mb_u8 (&out, audio->loop ? 1 : 0);
	mb_u8 (&out, channels);
	mb_u8 (&out, 0);
	mb_u16 (&out, audio->sample_rate);
	mb_u16 (&out, 0);
	mb_u32 (&out, audio->loop ? (u32)audio->loop_start : 0);
	mb_u32 (&out, (u32)audio->n_samples);
	mb_u32 (&out, (u32)(data_off + 0x20));
	mb_u32 (&out, 1); // block_count
	mb_u32 (&out, (u32)block_size);
	mb_u32 (&out, (u32)samples_per_block);
	mb_u32 (&out, (u32)block_size); // last_block_used_bytes: the only block, fully used
	mb_u32 (&out, (u32)audio->n_samples); // last_block_samples
	mb_u32 (&out, (u32)block_size); // last_block_size
	mb_u32 (&out, (u32)samples_per_block);
	mb_u32 (&out, 4);

	// HEAD2 (unread by any decoder incl. ours; present for player compat)
	mb_pad_to (&out, head_body_base + h2rel);
	mb_u8 (&out, 1);
	mb_u8 (&out, 1);
	mb_u16 (&out, 0);
	mb_u32 (&out, 0x01010000);
	mb_u32 (&out, h2rel + 12);
	mb_u8 (&out, 0x7F);
	mb_u8 (&out, 0x40);
	mb_u16 (&out, 0);
	mb_u32 (&out, 0);
	mb_u8 (&out, channels);
	mb_u8 (&out, 0);
	mb_u8 (&out, channels > 1 ? 1 : 0);
	mb_u8 (&out, 0);

	// HEAD3
	mb_pad_to (&out, head_body_base + h3rel);
	mb_u8 (&out, channels);
	mb_u8 (&out, 0);
	mb_u16 (&out, 0);
	for (int ch = 0; ch < channels; ch++)
	{
		mb_u32 (&out, 0x01000000);
		mb_u32 (&out, ci0 + 0x38 * ch);
	}
	for (int ch = 0; ch < channels; ch++)
	{
		u16 ps = use_adpcm && block_size ? adpcm_data[ch][0] : 0;
		mb_pad_to (&out, head_body_base + ci0 + 0x38 * ch);
		mb_u32 (&out, 0x01000000);
		mb_u32 (&out, ci0 + 0x38 * ch + 8);
		for (int i = 0; i < 16; i++)
			mb_u16 (&out, use_adpcm ? (u16)coefs[ch][i] : 0);
		mb_u16 (&out, 0); // gain
		mb_u16 (&out, ps); // initial predictor/scale
		mb_u16 (&out, 0);
		mb_u16 (&out, 0); // initial hist1/hist2
		mb_u16 (&out, 0);
		mb_u16 (&out, 0);
		mb_u16 (&out, 0); // loop ps/hist1/hist2
		mb_u16 (&out, 0);
	}
	mb_pad_to (&out, 0x40 + head_size);

	// ADPC: block_count == 1, so every channel's only seek entry is the
	// zero history block 0 always starts with.
	if (use_adpcm)
	{
		mb_tag (&out, "ADPC");
		mb_u32 (&out, (u32)adpc_size);
		for (int i = 0; i < channels; i++)
		{
			mb_u16 (&out, 0);
			mb_u16 (&out, 0);
		}
		mb_pad_to (&out, data_off);
	}

	mb_tag (&out, "DATA");
	mb_u32 (&out, (u32)data_size);
	mb_u32 (&out, 0x18);
	mb_pad_to (&out, data_off + 0x20);
	for (int ch = 0; ch < channels; ch++)
	{
		if (use_adpcm)
			mb_append (&out, adpcm_data[ch], block_size);
		else
			for (s64 i = 0; i < audio->n_samples; i++)
				mb_u16 (&out, (u16)audio->pcm[ch][i]);
	}
	mb_pad_to (&out, data_off + data_size);

	for (int ch = 0; ch < channels; ch++)
		if (adpcm_data[ch])
			FREE (adpcm_data[ch]);

	*out_data = out.data;
	*out_size = out.size;
	return ERR_OK;
}

// -----------------------------------------------------------------------------
// EncodeFSTM(): mirrors brstm_write_fstm() (Wii U "FSTM" / 3DS "CSTM", same
// INFO/SEEK/DATA section-table layout, differing only in the magic, the
// version word, and CSTM's little-endian-by-default fields). Field offsets
// (h1rel/h3rel/ci0/ai0, the 46-byte per-channel record, the 0x4000/0x4001/
// 0x4002 section flags) are taken verbatim from that function.

static enumError EncodeFSTM (
	u8 **out_data, size_t *out_size, const brstm_audio_t *audio, bool use_adpcm)
{
	bool cstm = audio->variant == BRSTM_VARIANT_CSTM;
	bool le = cstm; // brstm_common_init(): "c->little_endian = c->variant == BRSTM_CSTM"
	int channels = audio->channels;
	s16 coefs[DSP_ADPCM_MAX_CHANNELS][16];
	u8 *adpcm_data[DSP_ADPCM_MAX_CHANNELS] = { 0 };
	s64 block_size = 0;

	if (use_adpcm)
		EncodeAdpcmChannels (audio, coefs, adpcm_data, &block_size);
	else
		block_size = audio->n_samples * 2;

	s64 samples_per_block = use_adpcm
		? block_size / DSP_ADPCM_BYTES_PER_FRAME * DSP_ADPCM_SAMPLES_PER_FRAME
		: block_size / 2;

	int h1rel = 0x18;
	int h3rel = h1rel + 0x40;
	int ci0 = 4 + 8 * channels;
	int ai0 = 4 + 16 * channels;
	int info_body = h3rel + ai0 + 46 * channels;
	int info_size = ((8 + info_body + 0x1F) / 0x20) * 0x20;
	s64 seek_data = use_adpcm ? 1 /*block_count*/ * channels * 4 : 0;
	s64 seek_size = use_adpcm ? ((8 + seek_data + 0x1F) / 0x20) * 0x20 : 0;
	s64 audio_bytes = block_size * channels;
	s64 info_off = 0x40;
	s64 seek_off = info_off + info_size;
	s64 data_off = seek_off + seek_size;
	s64 data_size = ((0x20 + audio_bytes + 0x1F) / 0x20) * 0x20;
	int sections = use_adpcm ? 3 : 2;
	s64 info_body_base = info_off + 8;
	s64 ci_base = info_body_base + h3rel;

	membuf_t out;
	mb_init (&out);

	mb_tag (&out, cstm ? "CSTM" : "FSTM");
	mb_u16e (&out, 0xFEFF, le);
	mb_u16e (&out, 0x40, le); // header size
	mb_u32e (&out, cstm ? 0x00000200 : 0x00030000, le); // version
	mb_u32e (&out, (u32)(data_off + data_size), le); // file size
	mb_u16e (&out, sections, le);
	mb_u16e (&out, 0, le);
	mb_u16e (&out, 0x4000, le);
	mb_u16e (&out, 0, le);
	mb_u32e (&out, (u32)info_off, le);
	mb_u32e (&out, (u32)info_size, le);
	if (use_adpcm)
	{
		mb_u16e (&out, 0x4001, le);
		mb_u16e (&out, 0, le);
		mb_u32e (&out, (u32)seek_off, le);
		mb_u32e (&out, (u32)seek_size, le);
	}
	mb_u16e (&out, 0x4002, le);
	mb_u16e (&out, 0, le);
	mb_u32e (&out, (u32)data_off, le);
	mb_u32e (&out, (u32)data_size, le);
	mb_pad_to (&out, info_off);

	// INFO
	mb_tag (&out, "INFO");
	mb_u32e (&out, (u32)info_size, le);
	mb_u32e (&out, 0x41000000, le);
	mb_u32e (&out, h1rel, le);
	mb_u32e (&out, 0x01010000, le);
	mb_u32e (&out, 0xFFFFFFFF, le); // no track table
	mb_u32e (&out, 0x01010000, le);
	mb_u32e (&out, h3rel, le);

	mb_pad_to (&out, info_body_base + h1rel);
	mb_u8 (&out, use_adpcm ? 2 : 1);
	mb_u8 (&out, audio->loop ? 1 : 0);
	mb_u8 (&out, channels);
	mb_u8 (&out, 0); // region count
	mb_u32e (&out, (u32)audio->sample_rate, le);
	mb_u32e (&out, audio->loop ? (u32)audio->loop_start : 0, le);
	mb_u32e (&out, (u32)audio->n_samples, le);
	mb_u32e (&out, 1, le); // block_count
	mb_u32e (&out, (u32)block_size, le);
	mb_u32e (&out, (u32)samples_per_block, le);
	mb_u32e (&out, (u32)block_size, le); // last_block_used_bytes
	mb_u32e (&out, (u32)audio->n_samples, le); // last_block_samples
	mb_u32e (&out, (u32)block_size, le); // last_block_size
	mb_u32e (&out, (u32)samples_per_block, le);
	mb_u32e (&out, 4, le);
	mb_u32e (&out, 0x1F000000, le);
	mb_u32e (&out, 0x18, le);

	mb_pad_to (&out, ci_base);
	mb_u32e (&out, channels, le);
	for (int ch = 0; ch < channels; ch++)
	{
		mb_u32e (&out, 0x41020000, le);
		mb_u32e (&out, ci0 + 8 * ch, le);
	}
	for (int ch = 0; ch < channels; ch++)
	{
		mb_u32e (&out, use_adpcm ? 0x03000000 : 0xFFFFFFFF, le);
		mb_u32e (&out, use_adpcm ? (u32)(ai0 + 46 * ch) : 0xFFFFFFFF, le);
	}
	for (int ch = 0; ch < channels; ch++)
	{
		u16 ps = use_adpcm && block_size ? adpcm_data[ch][0] : 0;
		mb_pad_to (&out, ci_base + ai0 + 46 * ch);
		for (int i = 0; i < 16; i++)
			mb_u16e (&out, use_adpcm ? (u16)coefs[ch][i] : 0, le);
		mb_u16e (&out, ps, le);
		mb_u16e (&out, 0, le);
		mb_u16e (&out, 0, le);
		mb_u16e (&out, 0, le);
		mb_u16e (&out, 0, le);
		mb_u16e (&out, 0, le);
		mb_u16e (&out, 0, le);
	}
	mb_pad_to (&out, info_off + info_size);

	// SEEK: entries are little-endian even in a big-endian (FSTM) file --
	// an inconsistency in the format itself.
	if (use_adpcm)
	{
		mb_tag (&out, "SEEK");
		mb_u32e (&out, (u32)seek_size, le);
		for (int i = 0; i < channels; i++)
		{
			mb_u16e (&out, 0, true);
			mb_u16e (&out, 0, true);
		}
		mb_pad_to (&out, data_off);
	}

	mb_tag (&out, "DATA");
	mb_u32e (&out, (u32)data_size, le);
	mb_pad_to (&out, data_off + 0x20);
	for (int ch = 0; ch < channels; ch++)
	{
		if (use_adpcm)
			mb_append (&out, adpcm_data[ch], block_size);
		else
			for (s64 i = 0; i < audio->n_samples; i++)
				mb_u16e (&out, (u16)audio->pcm[ch][i], le);
	}
	mb_pad_to (&out, data_off + data_size);

	for (int ch = 0; ch < channels; ch++)
		if (adpcm_data[ch])
			FREE (adpcm_data[ch]);

	*out_data = out.data;
	*out_size = out.size;
	return ERR_OK;
}

enumError EncodeBRSTM (u8 **out_data, size_t *out_size, const brstm_audio_t *audio, bool use_adpcm)
{
	if (!audio->channels || audio->channels > DSP_ADPCM_MAX_CHANNELS)
		return ERROR0 (ERR_INVALID_DATA, "EncodeBRSTM: bad channel count %d\n", audio->channels);

	return audio->variant == BRSTM_VARIANT_RSTM ? EncodeRSTM (out_data, out_size, audio, use_adpcm)
												: EncodeFSTM (out_data, out_size, audio, use_adpcm);
}

// -----------------------------------------------------------------------------
// DecodeRSTM(): field offsets/semantics taken from brstm.c's read_header()
// (non-bfstm/RSTM path) and read_packet(). Coefficient-table location is
// derived structurally from the HEAD3 ref table (offsets relative to
// head_body_base) rather than replicating ffmpeg's more roundabout
// double-indirection seek sequence -- both resolve to the same bytes for
// any file using the documented ref-table convention, which is the same
// convention EncodeRSTM() above follows.

static enumError DecodeRSTM (brstm_audio_t *audio, const u8 *data, size_t size)
{
	u16 bom = rd_u16 (data + 4);
	if (bom != 0xFEFF)
		return ERROR0 (ERR_INVALID_DATA, "DecodeBRSTM: only big-endian RSTM is supported\n");

	u32 head_offs = rd_u32 (data + 0x10);
	u32 data_offs = rd_u32 (data + 0x20);
	if ((size_t)head_offs + 8 > size || memcmp (data + head_offs, "HEAD", 4))
		return ERROR0 (ERR_INVALID_DATA, "DecodeBRSTM: missing HEAD chunk\n");
	if ((size_t)data_offs + 8 > size || memcmp (data + data_offs, "DATA", 4))
		return ERROR0 (ERR_INVALID_DATA, "DecodeBRSTM: missing DATA chunk\n");

	u32 head_body_base = head_offs + 8;
	u32 h1rel = rd_u32 (data + head_body_base + 4);
	u32 h3rel = rd_u32 (data + head_body_base + 20);
	const u8 *h1 = data + head_body_base + h1rel;

	u8 codec = h1[0];
	u8 loop = h1[1];
	u8 channels = h1[2];
	if (!channels || channels > DSP_ADPCM_MAX_CHANNELS)
		return ERROR0 (ERR_INVALID_DATA, "DecodeBRSTM: bad channel count %d\n", channels);

	u16 sample_rate = rd_u16 (h1 + 4);
	u32 loop_start = rd_u32 (h1 + 8);
	u32 n_samples = rd_u32 (h1 + 0xC);
	u32 block_count = rd_u32 (h1 + 0x14);
	u32 block_size = rd_u32 (h1 + 0x18);
	u32 last_block_used = rd_u32 (h1 + 0x20);
	u32 last_block_size = rd_u32 (h1 + 0x28);

	bool is_adpcm = (codec == 2);
	if (codec != 0 && codec != 1 && codec != 2)
		return ERROR0 (ERR_INVALID_DATA, "DecodeBRSTM: unsupported codec %d\n", codec);

	s16 coefs[DSP_ADPCM_MAX_CHANNELS][16];
	if (is_adpcm)
	{
		const u8 *h3 = data + head_body_base + h3rel;
		for (int ch = 0; ch < channels; ch++)
		{
			u32 coef_ref = rd_u32 (h3 + 4 + 8 * ch + 4); // relative to head_body_base
			// +8: coef_ref points at this channel's own {type,offset} ref
			// pair, not the coefficients themselves.
			const u8 *cp = data + head_body_base + coef_ref + 8;
			for (int i = 0; i < 16; i++)
				coefs[ch][i] = (s16)rd_u16 (cp + i * 2);
		}
	}

	u8 *ch_bytes[DSP_ADPCM_MAX_CHANNELS] = { 0 };
	const u8 *audio_start = data + data_offs
		+ 0x20; // DATA content base (data_offs+8) + the 0x18 offset field written there

	for (int b = 0; b < (int)block_count; b++)
	{
		u32 span = (b == (int)block_count - 1) ? last_block_size : block_size;
		for (int ch = 0; ch < channels; ch++)
		{
			const u8 *src = audio_start + (s64)b * span * channels + (s64)ch * span;
			if (b == 0)
			{
				ch_bytes[ch] = MALLOC ((s64)block_count * block_size);
				memset (ch_bytes[ch], 0, (s64)block_count * block_size);
			}
			u32 use = (b == (int)block_count - 1) ? last_block_used : block_size;
			memcpy (ch_bytes[ch] + (s64)b * block_size, src, use);
		}
	}

	audio->channels = channels;
	audio->sample_rate = sample_rate;
	audio->n_samples = n_samples;
	audio->is_adpcm = is_adpcm;
	audio->loop = loop != 0;
	audio->loop_start = loop_start;

	for (int ch = 0; ch < channels; ch++)
	{
		audio->pcm[ch] = MALLOC (n_samples * sizeof (s16));

		if (is_adpcm)
		{
			int h1s = 0, h2s = 0;
			s64 nframes = DspAdpcmFrameCount (n_samples);
			for (s64 f = 0; f < nframes; f++)
			{
				s64 off = f * DSP_ADPCM_SAMPLES_PER_FRAME;
				int count = (int)(n_samples - off < DSP_ADPCM_SAMPLES_PER_FRAME
						? n_samples - off
						: DSP_ADPCM_SAMPLES_PER_FRAME);
				DspAdpcmDecodeBlock (ch_bytes[ch] + f * DSP_ADPCM_BYTES_PER_FRAME, count,
					audio->pcm[ch] + off, coefs[ch], &h1s, &h2s);
			}
		}
		else
		{
			for (s64 i = 0; i < n_samples; i++)
				audio->pcm[ch][i] = (s16)rd_u16 (ch_bytes[ch] + i * 2);
		}
	}

	for (int ch = 0; ch < channels; ch++)
		if (ch_bytes[ch])
			FREE (ch_bytes[ch]);

	return ERR_OK;
}

// -----------------------------------------------------------------------------
// DecodeFSTM(): the FSTM/CSTM structural counterpart of DecodeRSTM(), same
// caveat about not replicating ffmpeg's roundabout seek sequence.

static enumError DecodeFSTM (brstm_audio_t *audio, const u8 *data, size_t size, bool le)
{
	u32 info_offs = 0, data_offs = 0;
	u16 sections = rd_u16e (data + 16, le);
	u32 pos = 20;
	for (int i = 0; i < sections && pos + 12 <= size; i++, pos += 12)
	{
		u16 flag = rd_u16e (data + pos, le);
		if (flag == 0x4000)
			info_offs = rd_u32e (data + pos + 4, le);
		else if (flag == 0x4002)
			data_offs = rd_u32e (data + pos + 4, le);
	}
	if (!info_offs || (size_t)info_offs + 8 > size || memcmp (data + info_offs, "INFO", 4))
		return ERROR0 (ERR_INVALID_DATA, "DecodeBRSTM: missing INFO section\n");
	if (!data_offs || (size_t)data_offs + 8 > size || memcmp (data + data_offs, "DATA", 4))
		return ERROR0 (ERR_INVALID_DATA, "DecodeBRSTM: missing DATA section\n");

	u32 info_body_base = info_offs + 8;
	u32 h1rel = rd_u32e (data + info_body_base + 4, le);
	u32 h3rel = rd_u32e (data + info_body_base + 20, le);
	const u8 *h1 = data + info_body_base + h1rel;

	u8 codec = h1[0];
	u8 loop = h1[1];
	u8 channels = h1[2];
	if (!channels || channels > DSP_ADPCM_MAX_CHANNELS)
		return ERROR0 (ERR_INVALID_DATA, "DecodeBRSTM: bad channel count %d\n", channels);

	u32 sample_rate = rd_u32e (h1 + 4, le);
	u32 loop_start = rd_u32e (h1 + 8, le);
	u32 n_samples = rd_u32e (h1 + 0xC, le);
	u32 block_count = rd_u32e (h1 + 0x10, le);
	u32 block_size = rd_u32e (h1 + 0x14, le);
	u32 last_block_used = rd_u32e (h1 + 0x1C, le);
	u32 last_block_size = rd_u32e (h1 + 0x24, le);

	bool is_adpcm = (codec == 2);
	if (codec != 0 && codec != 1 && codec != 2)
		return ERROR0 (ERR_INVALID_DATA, "DecodeBRSTM: unsupported codec %d\n", codec);

	int ci0 = 4 + 8 * channels;
	int ai0 = 4 + 16 * channels;
	s64 ci_base = info_body_base + h3rel;

	s16 coefs[DSP_ADPCM_MAX_CHANNELS][16];
	if (is_adpcm)
	{
		for (int ch = 0; ch < channels; ch++)
		{
			const u8 *cp = data + ci_base + ai0 + 46 * ch;
			for (int i = 0; i < 16; i++)
				coefs[ch][i] = (s16)rd_u16e (cp + i * 2, le);
		}
	}
	(void)ci0;

	u8 *ch_bytes[DSP_ADPCM_MAX_CHANNELS] = { 0 };
	const u8 *audio_start = data + data_offs + 0x20;

	for (int b = 0; b < (int)block_count; b++)
	{
		u32 span = (b == (int)block_count - 1) ? last_block_size : block_size;
		for (int ch = 0; ch < channels; ch++)
		{
			const u8 *src = audio_start + (s64)b * span * channels + (s64)ch * span;
			if (b == 0)
			{
				ch_bytes[ch] = MALLOC ((s64)block_count * block_size);
				memset (ch_bytes[ch], 0, (s64)block_count * block_size);
			}
			u32 use = (b == (int)block_count - 1) ? last_block_used : block_size;
			memcpy (ch_bytes[ch] + (s64)b * block_size, src, use);
		}
	}

	audio->channels = channels;
	audio->sample_rate = sample_rate;
	audio->n_samples = n_samples;
	audio->is_adpcm = is_adpcm;
	audio->loop = loop != 0;
	audio->loop_start = loop_start;

	for (int ch = 0; ch < channels; ch++)
	{
		audio->pcm[ch] = MALLOC (n_samples * sizeof (s16));

		if (is_adpcm)
		{
			int h1s = 0, h2s = 0;
			s64 nframes = DspAdpcmFrameCount (n_samples);
			for (s64 f = 0; f < nframes; f++)
			{
				s64 off = f * DSP_ADPCM_SAMPLES_PER_FRAME;
				int count = (int)(n_samples - off < DSP_ADPCM_SAMPLES_PER_FRAME
						? n_samples - off
						: DSP_ADPCM_SAMPLES_PER_FRAME);
				DspAdpcmDecodeBlock (ch_bytes[ch] + f * DSP_ADPCM_BYTES_PER_FRAME, count,
					audio->pcm[ch] + off, coefs[ch], &h1s, &h2s);
			}
		}
		else
		{
			for (s64 i = 0; i < n_samples; i++)
				audio->pcm[ch][i] = (s16)rd_u16e (ch_bytes[ch] + i * 2, le);
		}
	}

	for (int ch = 0; ch < channels; ch++)
		if (ch_bytes[ch])
			FREE (ch_bytes[ch]);

	return ERR_OK;
}

enumError DecodeBRSTM (brstm_audio_t *audio, const u8 *data, size_t size)
{
	memset (audio, 0, sizeof (*audio));

	if (size < 0x40)
		return ERROR0 (ERR_INVALID_DATA, "DecodeBRSTM: file too small\n");

	if (!memcmp (data, "RSTM", 4))
	{
		audio->variant = BRSTM_VARIANT_RSTM;
		return DecodeRSTM (audio, data, size);
	}
	if (!memcmp (data, "FSTM", 4) || !memcmp (data, "CSTM", 4))
	{
		bool cstm = data[0] == 'C';
		audio->variant = cstm ? BRSTM_VARIANT_CSTM : BRSTM_VARIANT_FSTM;
		u16 bom = rd_u16 (data + 4);
		bool le = (bom != 0xFEFF); // 0xFFFE (bytes swapped) means little-endian fields
		return DecodeFSTM (audio, data, size, le);
	}

	return ERROR0 (ERR_INVALID_DATA, "DecodeBRSTM: not an RSTM/FSTM/CSTM file\n");
}

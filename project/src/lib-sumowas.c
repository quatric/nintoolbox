// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Sumo Digital .was standalone audio decoder; see lib-sumowas.h.
//-----------------------------------------------------------------------------
#include "lib-sumowas.h"
#include "lib-dspadpcm.h"
#include "lib-std.h"
#include "lib-archive-util.h"

#include <string.h>

#define WAS_GLOBAL_HDR_SIZE  0x20
#define WAS_CHAN_HDR_SIZE    0x60
#define WAS_MAX_CHANNELS     DSP_ADPCM_MAX_CHANNELS

typedef struct was_chan_t
{
	u32 nsamples;
	u32 srate;
	u16 loop_flag;
	u16 fmt;
	s16 coefs[16];
	int hist1, hist2;
}
was_chan_t;

static bool was_parse (const u8 *d, size_t size, u32 *nchan_out, u32 *blocksize_out,
	u32 *chan_bytes_out, size_t *header_end_out, was_chan_t chans[WAS_MAX_CHANNELS])
{
	if (!d || size < WAS_GLOBAL_HDR_SIZE)
		return false;
	if (memcmp (d, "iSWS", 4))
		return false;

	const u32 nchan = rd_be32 (d + 8);
	const u32 blocksize = rd_be32 (d + 0x10);
	const u32 chan_bytes = rd_be32 (d + 0x14);

	if (!nchan || nchan > WAS_MAX_CHANNELS)
		return false;
	if (!blocksize || blocksize > 0x1000000)
		return false;

	const size_t header_end = WAS_GLOBAL_HDR_SIZE + (size_t)nchan * WAS_CHAN_HDR_SIZE;
	if (header_end > size)
		return false;

	for (u32 c = 0; c < nchan; c++)
	{
		const u8 *h = d + WAS_GLOBAL_HDR_SIZE + c * WAS_CHAN_HDR_SIZE;
		const u32 nsamples = rd_be32 (h);
		const u32 srate = rd_be32 (h + 8);
		const u16 loop_flag = rd_be16 (h + 12);
		const u16 fmt = rd_be16 (h + 14);

		if (!nsamples || nsamples > 0x10000000)
			return false;
		if (srate < 2000 || srate > 192000)
			return false;
		if (loop_flag > 1)
			return false;
		if (fmt != 0 && fmt != 2)
			return false;

		if (chans)
		{
			was_chan_t *wc = chans + c;
			wc->nsamples = nsamples;
			wc->srate = srate;
			wc->loop_flag = loop_flag;
			wc->fmt = fmt;
			for (int i = 0; i < 16; i++)
				wc->coefs[i] = (s16)rd_be16 (h + 0x1c + i * 2);
			wc->hist1 = (s16)rd_be16 (h + 0x40);
			wc->hist2 = (s16)rd_be16 (h + 0x42);
		}
	}

	// Per-channel decodable byte span: prefer the header field, but fall
	// back to a value derived from chan_bytes[0]'s own nsamples if it is
	// missing or absurd, so a slightly nonstandard sample still decodes.
	u32 span = chan_bytes;
	if (!span || (u64)span > size)
	{
		const u32 nsamples0 = rd_be32 (d + WAS_GLOBAL_HDR_SIZE);
		const u64 full_frames = nsamples0 / 14;
		const u64 rem = nsamples0 % 14;
		span = (u32)(full_frames * 8 + (rem ? 1 + (rem + 1) / 2 : 0));
	}

	const u64 nblocks = (span + (u64)blocksize - 1) / blocksize;
	const u64 total = header_end + nblocks * blocksize * nchan;
	if (!nblocks || total > size)
		return false;

	if (nchan_out) *nchan_out = nchan;
	if (blocksize_out) *blocksize_out = blocksize;
	if (chan_bytes_out) *chan_bytes_out = span;
	if (header_end_out) *header_end_out = header_end;
	return true;
}

bool IsSumoWAS (const u8 *data, size_t size)
{
	return was_parse (data, size, 0, 0, 0, 0, 0);
}

enumError DecodeSumoWASToWAV (const u8 *data, size_t size, u8 **wav_out, size_t *wav_size_out)
{
	u32 nchan = 0, blocksize = 0, chan_bytes = 0;
	size_t header_end = 0;
	was_chan_t chans[WAS_MAX_CHANNELS];
	if (!was_parse (data, size, &nchan, &blocksize, &chan_bytes, &header_end, chans))
		return ERR_INVALID_DATA;

	const u32 nsamples = chans[0].nsamples;
	const u32 srate = chans[0].srate;
	const u32 nblocks = (chan_bytes + blocksize - 1) / blocksize;

	const size_t wav_len = 44 + (size_t)nsamples * 2 * nchan;
	u8 *w = MALLOC (wav_len);
	if (!w)
		return ERR_OUT_OF_MEMORY;

	memcpy (w, "RIFF", 4);
	const u32 rsz = (u32)(wav_len - 8);
	w[4] = (u8)rsz; w[5] = (u8)(rsz >> 8); w[6] = (u8)(rsz >> 16); w[7] = (u8)(rsz >> 24);

	memcpy (w + 8, "WAVEfmt ", 8);
	const u32 block_align = nchan * 2;
	const u32 fmt_fields[4] = { 16, 1 | (nchan << 16), srate, srate * block_align };
	for (uint i = 0; i < 4; i++)
		for (uint k = 0; k < 4; k++)
			w[16 + 4 * i + k] = (u8)(fmt_fields[i] >> (8 * k));

	w[32] = (u8)block_align;
	w[33] = (u8)(block_align >> 8);
	w[34] = 16; // bits per sample
	w[35] = 0;

	memcpy (w + 36, "data", 4);
	const u32 dsz = nsamples * 2 * nchan;
	w[40] = (u8)dsz; w[41] = (u8)(dsz >> 8); w[42] = (u8)(dsz >> 16); w[43] = (u8)(dsz >> 24);

	for (u32 c = 0; c < nchan; c++)
	{
		int h1 = chans[c].hist1, h2 = chans[c].hist2;
		u32 done = 0;
		for (u64 f = 0; done < nsamples; f++)
		{
			s16 out[14];
			const uint cnt = (nsamples - done < 14) ? (nsamples - done) : 14;

			// Map this frame's byte offset within the channel's logical,
			// contiguous ADPCM stream to its real file offset inside the
			// block-interleaved layout.
			const u64 streampos = f * 8;
			const u64 block_idx = streampos / blocksize;
			const u64 file_off = header_end
				+ (block_idx * nchan + c) * (u64)blocksize
				+ streampos % blocksize;

			u8 block[8] = { 0 };
			if (block_idx < nblocks && file_off < size)
			{
				const size_t avail = size - file_off;
				const size_t take = avail < 8 ? avail : 8;
				memcpy (block, data + file_off, take);
			}

			DspAdpcmDecodeBlock (block, cnt, out, chans[c].coefs, &h1, &h2);
			for (uint k = 0; k < cnt; k++)
			{
				const size_t idx = 44 + 2 * (size_t)nchan * (done + k) + 2 * c;
				w[idx] = (u8)out[k];
				w[idx + 1] = (u8)(out[k] >> 8);
			}
			done += cnt;
		}
	}

	*wav_out = w;
	*wav_size_out = wav_len;
	return ERR_OK;
}

enumError ExtractSumoWASAudio (ccp arg, ccp basedir, uint depth, const u8 *data, size_t size)
{
	(void)depth;
	char dest[PATH_MAX];
	ccp base = FindFilename (arg, 0);
	size_t blen = strlen (base);
	if (blen > 4 && !strcasecmp (base + blen - 4, ".was"))
		blen -= 4;

	if (basedir && *basedir)
	{
		CreatePath (basedir, true);
		snprintf (dest, sizeof (dest), "%s/%.*s.wav", basedir, (int)blen, base);
	}
	else
	{
		ccp slash = strrchr (arg, '/');
		if (slash)
			snprintf (dest, sizeof (dest), "%.*s/%.*s.wav", (int)(slash - arg), arg, (int)blen, base);
		else
			snprintf (dest, sizeof (dest), "%.*s.wav", (int)blen, base);
	}

	u8 *wav = 0;
	size_t wav_size = 0;
	enumError err = DecodeSumoWASToWAV (data, size, &wav, &wav_size);
	if (err)
		return err;

	err = SaveFile (dest, 0, 0, wav, (uint)wav_size, 0);
	FREE (wav);
	return err;
}

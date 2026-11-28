// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Nintendo GameCube/Wii DSP-ADPCM standalone audio stream (.dsp).
//-----------------------------------------------------------------------------
#include "lib-dsp.h"
#include "lib-dspadpcm.h"
#include "lib-std.h"
#include "lib-archive-util.h"

#include <string.h>

bool IsDSP (const u8 *data, size_t size)
{
	if (!data || size < 0x60)
		return false;

	const u32 nsamples = rd_be32 (data);
	const u32 srate = rd_be32 (data + 8);
	const u16 loop_flag = rd_be16 (data + 12);
	const u16 fmt = rd_be16 (data + 14);

	if (!nsamples || nsamples > 0x10000000)
		return false;
	if (srate < 2000 || srate > 192000)
		return false;
	if (loop_flag > 1)
		return false;
	if (fmt != 0 && fmt != 2)
		return false;

	const u64 full_frames = nsamples / 14;
	const u64 rem = nsamples % 14;
	const u64 min_bytes = 0x60 + full_frames * 8 + (rem ? 1 + (rem + 1) / 2 : 0);
	if (min_bytes > size)
		return false;

	return true;
}

enumError DecodeDSPToWAV (const u8 *data, size_t size, u8 **wav_out, size_t *wav_size_out)
{
	if (!wav_out || !wav_size_out || !IsDSP (data, size))
		return ERR_INVALID_DATA;

	const u32 nsamples = rd_be32 (data);
	const u32 srate = rd_be32 (data + 8);

	s16 coefs[16];
	for (int i = 0; i < 16; i++)
		coefs[i] = (s16)rd_be16 (data + 28 + i * 2);

	int h1 = (s16)rd_be16 (data + 64);
	int h2 = (s16)rd_be16 (data + 66);

	const size_t wav_len = 44 + (size_t)nsamples * 2;
	u8 *w = MALLOC (wav_len);
	if (!w)
		return ERR_OUT_OF_MEMORY;

	memcpy (w, "RIFF", 4);
	const u32 rsz = (u32)(wav_len - 8);
	w[4] = (u8)rsz;
	w[5] = (u8)(rsz >> 8);
	w[6] = (u8)(rsz >> 16);
	w[7] = (u8)(rsz >> 24);

	memcpy (w + 8, "WAVEfmt ", 8);
	const u32 fmt_fields[4] = { 16, 1 | (1 << 16), srate, srate * 2 };
	for (uint i = 0; i < 4; i++)
		for (uint k = 0; k < 4; k++)
			w[16 + 4 * i + k] = (u8)(fmt_fields[i] >> (8 * k));

	w[32] = 2; // block align
	w[33] = 0;
	w[34] = 16; // bits per sample
	w[35] = 0;

	memcpy (w + 36, "data", 4);
	const u32 dsz = nsamples * 2;
	w[40] = (u8)dsz;
	w[41] = (u8)(dsz >> 8);
	w[42] = (u8)(dsz >> 16);
	w[43] = (u8)(dsz >> 24);

	u32 done = 0;
	for (u64 f = 0; done < nsamples; f++)
	{
		s16 out[14];
		const uint cnt = (nsamples - done < 14) ? (nsamples - done) : 14;
		u8 block[8] = { 0 };
		const size_t off = 0x60 + f * 8;
		const size_t avail = (off < size) ? (size - off) : 0;
		const size_t take = avail < 8 ? avail : 8;
		memcpy (block, data + off, take);

		// hardware-exact decode: the 11-bit shift is applied to the whole sum with a
		// +1024 rounding term (DspAdpcmDecodeBlock follows ffmpeg's THP variant, which
		// differs by a few LSBs)
		{
			const int info = block[0];
			const s64 f1 = coefs[((info >> 4) & 7) * 2], f2 = coefs[((info >> 4) & 7) * 2 + 1];
			for (uint k = 0; k < cnt; k++)
			{
				const int nb = block[1 + k / 2];
				int nib = (k & 1) ? nb & 15 : nb >> 4;
				nib = nib > 7 ? nib - 16 : nib;
				s64 smp = (((s64)nib * (1 << (info & 15))) * 2048 + 1024 + h1 * f1 + h2 * f2) >> 11;
				smp = smp < -32768 ? -32768 : smp > 32767 ? 32767 : smp;
				out[k] = (s16)smp;
				h2 = h1;
				h1 = (int)smp;
			}
		}
		for (uint k = 0; k < cnt; k++)
		{
			w[44 + 2 * (done + k)] = (u8)out[k];
			w[45 + 2 * (done + k)] = (u8)(out[k] >> 8);
		}
		done += cnt;
	}

	*wav_out = w;
	*wav_size_out = wav_len;
	return ERR_OK;
}

enumError ExtractDSPAudio (ccp arg, ccp basedir, uint depth, const u8 *data, size_t size)
{
	(void)depth;
	char dest[PATH_MAX];
	ccp base = FindFilename (arg, 0);
	size_t blen = strlen (base);
	if (blen > 4 && !strcasecmp (base + blen - 4, ".dsp"))
		blen -= 4;
	else if (blen > 6 && !strcasecmp (base + blen - 6, ".adpcm"))
		blen -= 6;

	if (basedir && *basedir)
	{
		CreatePath (basedir, true);
		snprintf (dest, sizeof (dest), "%s/%.*s.wav", basedir, (int)blen, base);
	}
	else
	{
		ccp slash = strrchr (arg, '/');
		if (slash)
			snprintf (
				dest, sizeof (dest), "%.*s/%.*s.wav", (int)(slash - arg), arg, (int)blen, base);
		else
			snprintf (dest, sizeof (dest), "%.*s.wav", (int)blen, base);
	}

	u8 *wav = 0;
	size_t wav_size = 0;
	enumError err = DecodeDSPToWAV (data, size, &wav, &wav_size);
	if (err)
		return err;

	err = SaveFile (dest, 0, 0, wav, (uint)wav_size, 0);
	FREE (wav);
	return err;
}

// Mono 16-bit PCM WAV -> standalone .dsp (0x60-byte header + ADPCM frames, no loop).
// shortcut: mono only, no loop points, gain 0; stereo needs the two-header layout.
enumError EncodeDSPFromWAV (u8 **dest, uint *dest_size, const u8 *wav, uint wav_size)
{
	if (!dest || !dest_size || !wav || wav_size < 44 || memcmp (wav, "RIFF", 4) || memcmp (wav + 8, "WAVE", 4))
		return ERR_INVALID_DATA;
	u32 rate = 0, nbytes = 0;
	const u8 *pcm = 0;
	bool fmt_ok = false;
	for (u64 p = 12; p + 8 <= wav_size;)
	{
		const u32 len = rd_le32 (wav + p + 4);
		const u8 *body = wav + p + 8;
		if (!memcmp (wav + p, "fmt ", 4) && len >= 16 && p + 8 + len <= wav_size)
		{
			fmt_ok = rd_le16 (body) == 1 && rd_le16 (body + 2) == 1 && rd_le16 (body + 14) == 16;
			rate = rd_le32 (body + 4);
		}
		else if (!memcmp (wav + p, "data", 4))
		{
			pcm = body;
			nbytes = (u64)len > wav_size - p - 8 ? wav_size - p - 8 : len;
			break;
		}
		p += 8 + (u64)len + (len & 1);
	}
	const uint n = nbytes / 2;
	if (!fmt_ok || !pcm || !n || !rate)
		return ERR_INVALID_DATA; // only mono PCM16 is supported
	s16 *samples = MALLOC (sizeof (s16) * n);
	if (!samples)
		return ERR_OUT_OF_MEMORY;
	for (uint i = 0; i < n; i++)
		samples[i] = (s16)rd_le16 (pcm + 2 * i);

	const uint frames = (uint)DspAdpcmFrameCount (n), nibbles = (uint)DspAdpcmNibbleCount (n);
	u8 *out = CALLOC (1, 0x60 + (size_t)frames * 8);
	if (!out)
	{
		FREE (samples);
		return ERR_OUT_OF_MEMORY;
	}
	s16 coefs[16];
	DspAdpcmCorrelateCoefs (samples, n, coefs);
	wr_be32 (out, n);
	wr_be32 (out + 4, nibbles);
	wr_be32 (out + 8, rate);
	wr_be32 (out + 20, nibbles ? nibbles - 1 : 0); // loop end (unused: loop flag 0)
	wr_be32 (out + 24, 2);						  // first nibble address
	for (int i = 0; i < 16; i++)
		wr_be16 (out + 28 + 2 * i, (u16)coefs[i]);
	int h1 = 0, h2 = 0;
	for (uint f = 0; f < frames; f++)
	{
		const uint cnt = n - f * DSP_ADPCM_SAMPLES_PER_FRAME < DSP_ADPCM_SAMPLES_PER_FRAME
			? n - f * DSP_ADPCM_SAMPLES_PER_FRAME
			: DSP_ADPCM_SAMPLES_PER_FRAME;
		DspAdpcmEncodeBlock (samples + f * DSP_ADPCM_SAMPLES_PER_FRAME, cnt, out + 0x60 + 8 * f, coefs, &h1, &h2);
	}
	wr_be16 (out + 62, out[0x60]); // initial predictor/scale
	FREE (samples);
	*dest = out;
	*dest_size = 0x60 + frames * 8;
	return ERR_OK;
}

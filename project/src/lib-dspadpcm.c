// Ported from mobipeg's FFmpeg fork:
//   libavformat/dsp_adpcm.c   (sizing helpers, DspAdpcmAdvance)
//   libavcodec/adpcm.c        (ADPCM_THP decode recurrence -> DspAdpcmDecodeBlock)
//   libavcodec/adpcmenc.c     (thp_correlate_coefs -> DspAdpcmCorrelateCoefs,
//                               adpcm_thp_encode_block -> DspAdpcmEncodeBlock)
// Copyright (c) 2001-2003 The FFmpeg project; BRSTM support Copyright (c)
// 2012 Paul B Mahol. LGPL v2.1+. See lib-dspadpcm.h.

#include "lib-dspadpcm.h"
#include <math.h>
#include <float.h>

static inline s16 clip_s16 (int v)
{
	return v < -32768 ? -32768 : v > 32767 ? 32767 : (s16)v;
}

static inline int sign_extend4 (int nibble)
{
	return nibble > 7 ? nibble - 16 : nibble;
}

s64 DspAdpcmFrameCount (s64 samples)
{
	return (samples + DSP_ADPCM_SAMPLES_PER_FRAME - 1) / DSP_ADPCM_SAMPLES_PER_FRAME;
}

s64 DspAdpcmByteCount (s64 samples)
{
	return DspAdpcmFrameCount (samples) * DSP_ADPCM_BYTES_PER_FRAME;
}

s64 DspAdpcmNibbleCount (s64 samples)
{
	s64 whole = samples / DSP_ADPCM_SAMPLES_PER_FRAME;
	s64 rest = samples % DSP_ADPCM_SAMPLES_PER_FRAME;
	return whole * 16 + (rest ? rest + 2 : 0);
}

s64 DspAdpcmNibbleAddress (s64 sample)
{
	return sample / DSP_ADPCM_SAMPLES_PER_FRAME * 16 + sample % DSP_ADPCM_SAMPLES_PER_FRAME + 2;
}

s64 DspAdpcmNibblesToSamples (s64 nibbles)
{
	s64 frames = nibbles / 16;
	s64 rest = nibbles % 16;
	s64 r = rest - 2;
	if (r < 0)
		r = 0;
	return frames * DSP_ADPCM_SAMPLES_PER_FRAME + r;
}

void DspAdpcmAdvance (const u8 *src, s64 nb_frames, const s16 *coefs, s16 *hist1, s16 *hist2)
{
	int s1 = *hist1, s2 = *hist2;

	for (s64 f = 0; f < nb_frames; f++)
	{
		int header = *src++;
		int index = (header >> 4) & 7;
		unsigned e = header & 0x0F;
		int factor1 = coefs[index * 2];
		int factor2 = coefs[index * 2 + 1];

		for (int n = 0; n < DSP_ADPCM_SAMPLES_PER_FRAME; n++)
		{
			int byte = src[n >> 1];
			int nib = sign_extend4 ((n & 1) ? byte & 0x0F : byte >> 4);
			int sample = ((s1 * factor1 + s2 * factor2) >> 11) + nib * (1 << e);

			sample = clip_s16 (sample);
			s2 = s1;
			s1 = sample;
		}
		src += DSP_ADPCM_BYTES_PER_FRAME - 1;
	}

	*hist1 = (s16)s1;
	*hist2 = (s16)s2;
}

void DspAdpcmDecodeBlock (
	const u8 *src, int count, s16 *samples, const s16 *coefs, int *hist1, int *hist2)
{
	int byte = src[0];
	int index = (byte >> 4) & 7;
	unsigned exp = byte & 0x0F;
	s64 factor1 = coefs[index * 2];
	s64 factor2 = coefs[index * 2 + 1];

	for (int n = 0; n < count; n++)
	{
		int nibble_byte = src[1 + n / 2];
		int sampledat = sign_extend4 ((n & 1) ? nibble_byte & 0x0F : nibble_byte >> 4);

		s32 sample = (s32)((*hist1 * factor1 + *hist2 * factor2) >> 11) + sampledat * (1 << exp);
		sample = clip_s16 (sample);
		samples[n] = (s16)sample;
		*hist2 = *hist1;
		*hist1 = sample;
	}
}

// --- coefficient generation (thp_correlate_coefs) --------------------------
// Autocorrelation + Levinson-style refinement over the whole channel, exactly
// as ported from adpcmenc.c. The math (tvec = double[3] "order-2 LPC
// coefficient" records) is transcribed as closely as possible to keep the
// derived coefficients numerically identical to ffmpeg's THP encoder.

typedef double tvec[3];

static inline void thp_inner_product_merge (tvec vecOut, s16 pcmBuf[14])
{
	for (int i = 0; i <= 2; i++)
	{
		vecOut[i] = 0.0;
		for (int x = 0; x < 14; x++)
			vecOut[i] -= pcmBuf[x - i] * pcmBuf[x];
	}
}

static inline void thp_outer_product_merge (tvec mtxOut[3], s16 pcmBuf[14])
{
	for (int x = 1; x <= 2; x++)
		for (int y = 1; y <= 2; y++)
		{
			mtxOut[x][y] = 0.0;
			for (int z = 0; z < 14; z++)
				mtxOut[x][y] += pcmBuf[z - x] * pcmBuf[z - y];
		}
}

static int thp_analyze_ranges (tvec mtx[3], int *vecIdxsOut)
{
	double recips[3];
	double val, tmp, min, max;

	for (int x = 1; x <= 2; x++)
	{
		val = fabs (mtx[x][1]) > fabs (mtx[x][2]) ? fabs (mtx[x][1]) : fabs (mtx[x][2]);
		if (val < DBL_EPSILON)
			return 1;
		recips[x] = 1.0 / val;
	}

	int maxIndex = 0;
	for (int i = 1; i <= 2; i++)
	{
		for (int x = 1; x < i; x++)
		{
			tmp = mtx[x][i];
			for (int y = 1; y < x; y++)
				tmp -= mtx[x][y] * mtx[y][i];
			mtx[x][i] = tmp;
		}

		val = 0.0;
		for (int x = i; x <= 2; x++)
		{
			tmp = mtx[x][i];
			for (int y = 1; y < i; y++)
				tmp -= mtx[x][y] * mtx[y][i];

			mtx[x][i] = tmp;
			tmp = fabs (tmp) * recips[x];
			if (tmp >= val)
			{
				val = tmp;
				maxIndex = x;
			}
		}

		if (maxIndex != i)
		{
			for (int y = 1; y <= 2; y++)
			{
				tmp = mtx[maxIndex][y];
				mtx[maxIndex][y] = mtx[i][y];
				mtx[i][y] = tmp;
			}
			recips[maxIndex] = recips[i];
		}

		vecIdxsOut[i] = maxIndex;

		if (mtx[i][i] == 0.0)
			return 1;

		if (i != 2)
		{
			tmp = 1.0 / mtx[i][i];
			for (int x = i + 1; x <= 2; x++)
				mtx[x][i] *= tmp;
		}
	}

	min = 1.0e10;
	max = 0.0;
	for (int i = 1; i <= 2; i++)
	{
		tmp = fabs (mtx[i][i]);
		if (tmp < min)
			min = tmp;
		if (tmp > max)
			max = tmp;
	}

	return (min / max < 1.0e-10) ? 1 : 0;
}

static void thp_bidirectional_filter (tvec mtx[3], int *vecIdxs, tvec vecOut)
{
	double tmp;

	for (int i = 1, x = 0; i <= 2; i++)
	{
		int index = vecIdxs[i];
		tmp = vecOut[index];
		vecOut[index] = vecOut[i];
		if (x != 0)
			for (int y = x; y <= i - 1; y++)
				tmp -= vecOut[y] * mtx[i][y];
		else if (tmp != 0.0)
			x = i;
		vecOut[i] = tmp;
	}

	for (int i = 2; i > 0; i--)
	{
		tmp = vecOut[i];
		for (int y = i + 1; y <= 2; y++)
			tmp -= vecOut[y] * mtx[i][y];
		vecOut[i] = tmp / mtx[i][i];
	}

	vecOut[0] = 1.0;
}

static int thp_quadratic_merge (tvec inOutVec)
{
	double v0, v1, v2 = inOutVec[2];
	double tmp = 1.0 - (v2 * v2);

	if (tmp == 0.0)
		return 1;

	v0 = (inOutVec[0] - (v2 * v2)) / tmp;
	v1 = (inOutVec[1] - (inOutVec[1] * v2)) / tmp;

	inOutVec[0] = v0;
	inOutVec[1] = v1;

	return fabs (v1) > 1.0;
}

static void thp_finish_record (tvec in, tvec out)
{
	for (int z = 1; z <= 2; z++)
	{
		if (in[z] >= 1.0)
			in[z] = 0.9999999999;
		else if (in[z] <= -1.0)
			in[z] = -0.9999999999;
	}
	out[0] = 1.0;
	out[1] = (in[2] * in[1]) + in[1];
	out[2] = in[2];
}

static void thp_matrix_filter (tvec src, tvec dst)
{
	tvec mtx[3];

	mtx[2][0] = 1.0;
	for (int i = 1; i <= 2; i++)
		mtx[2][i] = -src[i];

	for (int i = 2; i > 0; i--)
	{
		double val = 1.0 - (mtx[i][i] * mtx[i][i]);
		for (int y = 1; y <= i; y++)
			mtx[i - 1][y] = ((mtx[i][i] * mtx[i][y]) + mtx[i][y]) / val;
	}

	dst[0] = 1.0;
	for (int i = 1; i <= 2; i++)
	{
		dst[i] = 0.0;
		for (int y = 1; y <= i; y++)
			dst[i] += mtx[i][y] * dst[i - y];
	}
}

static void thp_merge_finish_record (tvec src, tvec dst)
{
	tvec tmp;
	double val = src[0];

	dst[0] = 1.0;
	for (int i = 1; i <= 2; i++)
	{
		double v2 = 0.0;
		for (int y = 1; y < i; y++)
			v2 += dst[y] * src[i - y];

		if (val > 0.0)
			dst[i] = -(v2 + src[i]) / val;
		else
			dst[i] = 0.0;

		tmp[i] = dst[i];

		for (int y = 1; y < i; y++)
			dst[y] += dst[i] * dst[i - y];

		val *= 1.0 - (dst[i] * dst[i]);
	}

	thp_finish_record (tmp, dst);
}

static double thp_contrast_vectors (tvec source1, tvec source2)
{
	double val = (source2[2] * source2[1] + -source2[1]) / (1.0 - source2[2] * source2[2]);
	double val1 = (source1[0] * source1[0]) + (source1[1] * source1[1]) + (source1[2] * source1[2]);
	double val2 = (source1[0] * source1[1]) + (source1[1] * source1[2]);
	double val3 = source1[0] * source1[2];
	return val1 + (2.0 * val * val2) + (2.0 * (-source2[1] * val + -source2[2]) * val3);
}

static void thp_filter_records (tvec vecBest[8], int exp, tvec records[], int recordCount)
{
	tvec bufferList[8];
	int buffer1[8];
	tvec buffer2;
	int index;
	double value, tempVal = 0;

	for (int x = 0; x < 2; x++)
	{
		for (int y = 0; y < exp; y++)
		{
			buffer1[y] = 0;
			for (int i = 0; i <= 2; i++)
				bufferList[y][i] = 0.0;
		}
		for (int z = 0; z < recordCount; z++)
		{
			index = 0;
			value = 1.0e30;
			for (int i = 0; i < exp; i++)
			{
				tempVal = thp_contrast_vectors (vecBest[i], records[z]);
				if (tempVal < value)
				{
					value = tempVal;
					index = i;
				}
			}
			buffer1[index]++;
			thp_matrix_filter (records[z], buffer2);
			for (int i = 0; i <= 2; i++)
				bufferList[index][i] += buffer2[i];
		}

		for (int i = 0; i < exp; i++)
			if (buffer1[i] > 0)
				for (int y = 0; y <= 2; y++)
					bufferList[i][y] /= buffer1[i];

		for (int i = 0; i < exp; i++)
			thp_merge_finish_record (bufferList[i], vecBest[i]);
	}
}

void DspAdpcmCorrelateCoefs (const s16 *source, s64 samples, s16 *coefsOut)
{
	int numFrames = (int)((samples + 13) / 14);
	int frameSamples;
	s16 pcmHistBuffer[2][14] = { { 0 } };
	tvec vec1, vec2, mtx[3];
	int vecIdxs[3];
	tvec vecBest[8];
	int recordCount = 0;
	const s16 *src = source;

	s16 *blockBuffer = MALLOC (0x3800 * sizeof (s16));
	tvec *records = MALLOC ((numFrames * 2 > 1 ? numFrames * 2 : 1) * sizeof (tvec));

	for (s64 x = samples; x > 0;)
	{
		if (x > 0x3800)
		{
			frameSamples = 0x3800;
			x -= 0x3800;
		}
		else
		{
			frameSamples = (int)x;
			memset (blockBuffer, 0, 0x3800 * sizeof (s16));
			x = 0;
		}

		memcpy (blockBuffer, src, frameSamples * sizeof (s16));
		src += frameSamples;

		for (int i = 0; i < frameSamples;)
		{
			for (int z = 0; z < 14; z++)
				pcmHistBuffer[0][z] = pcmHistBuffer[1][z];
			for (int z = 0; z < 14; z++)
				pcmHistBuffer[1][z] = (i < frameSamples) ? blockBuffer[i++] : 0;

			thp_inner_product_merge (vec1, pcmHistBuffer[1]);
			if (fabs (vec1[0]) > 10.0)
			{
				thp_outer_product_merge (mtx, pcmHistBuffer[1]);
				if (!thp_analyze_ranges (mtx, vecIdxs))
				{
					thp_bidirectional_filter (mtx, vecIdxs, vec1);
					if (!thp_quadratic_merge (vec1))
					{
						thp_finish_record (vec1, records[recordCount]);
						recordCount++;
					}
				}
			}
		}
	}

	vec1[0] = 1.0;
	vec1[1] = 0.0;
	vec1[2] = 0.0;

	for (int z = 0; z < recordCount; z++)
	{
		thp_matrix_filter (records[z], vecBest[0]);
		for (int y = 1; y <= 2; y++)
			vec1[y] += vecBest[0][y];
	}
	if (recordCount)
		for (int y = 1; y <= 2; y++)
			vec1[y] /= recordCount;

	thp_merge_finish_record (vec1, vecBest[0]);

	{
		int exp = 1;
		for (int w = 0; w < 3;)
		{
			vec2[0] = 0.0;
			vec2[1] = -1.0;
			vec2[2] = 0.0;
			for (int i = 0; i < exp; i++)
				for (int y = 0; y <= 2; y++)
					vecBest[exp + i][y] = (0.01 * vec2[y]) + vecBest[i][y];
			++w;
			exp = 1 << w;
			thp_filter_records (vecBest, exp, records, recordCount);
		}
	}

	for (int z = 0; z < 8; z++)
	{
		double d;
		d = -vecBest[z][1] * 2048.0;
		coefsOut[z * 2] = d > 0.0 ? (d > 32767.0 ? 32767 : (s16)lround (d))
								  : (d < -32768.0 ? -32768 : (s16)lround (d));
		d = -vecBest[z][2] * 2048.0;
		coefsOut[z * 2 + 1] = d > 0.0 ? (d > 32767.0 ? 32767 : (s16)lround (d))
									  : (d < -32768.0 ? -32768 : (s16)lround (d));
	}

	FREE (records);
	FREE (blockBuffer);
}

void DspAdpcmEncodeBlock (
	const s16 *samples, int count, u8 *dst, const s16 *coefs, int *hist1, int *hist2)
{
	int best_index = 0, best_exp = 0;
	s64 best_err = 0x7FFFFFFFFFFFFFFFLL;
	u8 best_nib[14] = { 0 };
	int best_h1 = *hist1, best_h2 = *hist2;

	for (int index = 0; index < 8; index++)
	{
		s64 f1 = coefs[index * 2], f2 = coefs[index * 2 + 1];
		int h1 = *hist1, h2 = *hist2, maxres = 0;

		for (int n = 0; n < count; n++)
		{
			int pred = (int)((h1 * f1 + h2 * f2) >> 11);
			int res = samples[n] - pred;
			if (res < 0)
				res = -res;
			if (res > maxres)
				maxres = res;
			h2 = h1;
			h1 = samples[n];
		}

		int exp0 = 0;
		while (exp0 < 15 && (maxres >> exp0) > 7)
			exp0++;

		for (int e = exp0; e <= (exp0 + 1 < 15 ? exp0 + 1 : 15); e++)
		{
			int lh1 = *hist1, lh2 = *hist2;
			s64 err = 0;
			u8 nib[14] = { 0 };

			for (int n = 0; n < count; n++)
			{
				int pred = (int)((lh1 * f1 + lh2 * f2) >> 11);
				int diff = samples[n] - pred;
				int q = diff >= 0 ? ((diff + (1 << e) / 2) >> e) : -(((-diff) + (1 << e) / 2) >> e);
				int recon, d;
				q = q < -8 ? -8 : q > 7 ? 7 : q;
				recon = clip_s16 (pred + q * (1 << e));
				d = samples[n] - recon;
				err += (s64)d * d;
				nib[n] = q & 0x0F;
				lh2 = lh1;
				lh1 = recon;
			}

			if (err < best_err)
			{
				best_err = err;
				best_index = index;
				best_exp = e;
				best_h1 = lh1;
				best_h2 = lh2;
				for (int n = 0; n < count; n++)
					best_nib[n] = nib[n];
			}
		}
	}

	dst[0] = (u8)((best_index << 4) | best_exp);
	for (int n = 0; n < 14; n++)
	{
		if (n & 1)
			dst[1 + n / 2] |= best_nib[n];
		else
			dst[1 + n / 2] = (u8)(best_nib[n] << 4);
	}
	*hist1 = best_h1;
	*hist2 = best_h2;
}

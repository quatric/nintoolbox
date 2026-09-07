// Level-5 SADL audio stream -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

//-----------------------------------------------------------------------------
///////////////		Level-5 SADL Audio Stream -> WAV		///////////////
//-----------------------------------------------------------------------------

enumError DecodeSADL_WAV (u8 **dest_wav, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest_wav || !dest_size || !src || src_size < 0x100 || memcmp (src, "SADL", 4))
		return EINVAL;

	const uint channels = src[0x32] ? src[0x32] : 1;
	const u8 coding = src[0x33];
	const uint sample_rate = (coding & 6) == 4 ? 32728 : 16364;
	const uint file_sz = (uint)src[0x40] | ((uint)src[0x41] << 8) | ((uint)src[0x42] << 16)
		| ((uint)src[0x43] << 24);
	const uint data_sz
		= file_sz > 0x100 && file_sz <= src_size ? file_sz - 0x100 : src_size - 0x100;
	const uint num_samples = (data_sz / channels) * 2;

	if (!num_samples || num_samples > (64u << 20))
		return EINVAL;

	const uint wav_hdr_sz = 44;
	const uint pcm_bytes = num_samples * channels * 2;
	u8 *wav = CALLOC (1, wav_hdr_sz + pcm_bytes);
	if (!wav)
		return ERR_CANT_CREATE;

	// Write WAV Header
	memcpy (wav, "RIFF", 4);
	const u32 riff_sz = 36 + pcm_bytes;
	wav[4] = (u8)(riff_sz & 0xFF);
	wav[5] = (u8)((riff_sz >> 8) & 0xFF);
	wav[6] = (u8)((riff_sz >> 16) & 0xFF);
	wav[7] = (u8)((riff_sz >> 24) & 0xFF);
	memcpy (wav + 8, "WAVEfmt ", 8);
	wav[16] = 16;
	wav[20] = 1; // PCM
	wav[22] = (u8)channels;
	wav[24] = (u8)(sample_rate & 0xFF);
	wav[25] = (u8)((sample_rate >> 8) & 0xFF);
	wav[26] = (u8)((sample_rate >> 16) & 0xFF);
	wav[27] = (u8)((sample_rate >> 24) & 0xFF);
	const u32 byte_rate = sample_rate * channels * 2;
	wav[28] = (u8)(byte_rate & 0xFF);
	wav[29] = (u8)((byte_rate >> 8) & 0xFF);
	wav[30] = (u8)((byte_rate >> 16) & 0xFF);
	wav[31] = (u8)((byte_rate >> 24) & 0xFF);
	wav[32] = (u8)(channels * 2);
	wav[34] = 16;
	memcpy (wav + 36, "data", 4);
	wav[40] = (u8)(pcm_bytes & 0xFF);
	wav[41] = (u8)((pcm_bytes >> 8) & 0xFF);
	wav[42] = (u8)((pcm_bytes >> 16) & 0xFF);
	wav[43] = (u8)((pcm_bytes >> 24) & 0xFF);

	short *pcm = (short *)(wav + wav_hdr_sz);
	const u8 *in_data = src + 0x100;
	static const short index_table[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
	static const short stepsize_table[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25,
		28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
		209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
		1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
		4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
		18500, 20350, 22385, 24623, 27086, 29794, 32767 };

	int sample = 0;
	int index = 0;
	uint s_idx = 0;
	for (uint i = 0; i < data_sz && s_idx < num_samples; i++)
	{
		const u8 byte = in_data[i];
		for (int nib = 0; nib < 2 && s_idx < num_samples; nib++)
		{
			const u8 delta = nib == 0 ? (byte & 0x0F) : (byte >> 4);
			int step = stepsize_table[index];
			int diff = step >> 3;
			if (delta & 1)
				diff += step >> 2;
			if (delta & 2)
				diff += step >> 1;
			if (delta & 4)
				diff += step;
			if (delta & 8)
				sample -= diff;
			else
				sample += diff;
			if (sample > 32767)
				sample = 32767;
			if (sample < -32768)
				sample = -32768;
			index += index_table[delta];
			if (index < 0)
				index = 0;
			if (index > 88)
				index = 88;
			pcm[s_idx++] = (short)sample;
		}
	}

	*dest_wav = wav;
	*dest_size = wav_hdr_sz + pcm_bytes;
	return ERR_OK;
}

// Procyon Studio SADL streams: IMA and Procyon ADPCM, 16-byte channel blocks.
// Layout and codec behavior cross-checked against vgmstream's sadl.c,
// ima_decoder.c and nds_procyon_decoder.c; see docs/FORMAT_AUDIT.md.
// Copyright and permission: docs/licenses/vgmstream.txt.
#include "lib-std.h"
#include "lib-nintendo.h"

static int sadl_clamp16 (s64 sample)
{
	return sample < -32768 ? -32768 : sample > 32767 ? 32767 : (int)sample;
}

enumError DecodeSADL_WAV (u8 **dest_wav, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest_wav || !dest_size)
		return ERR_INVALID_DATA;
	*dest_wav = 0;
	*dest_size = 0;
	if (!src || src_size < 0xc0 || (memcmp (src, "sadl", 4) && memcmp (src, "SADL", 4)))
		return ERR_INVALID_DATA;
	const uint channels = src[0x32], flags = src[0x33];
	const uint codec = flags & 0xf0, rate = flags & 6;
	const uint start = rd_le32 (src + 0x48), end = rd_le32 (src + 0x40);
	if (!channels || channels > 2 || rate == 6 || (codec != 0 && codec != 0x70 && codec != 0xb0)
		|| start < 0xc0 || start >= end || end > src_size)
		return ERR_INVALID_DATA;
	const uint bytes = end - start, block_size = channels * 16;
	if (bytes % block_size)
		return ERR_INVALID_DATA;
	const uint block_samples = codec == 0xb0 ? 30 : 32;
	const u64 frames = (u64)(bytes / block_size) * block_samples;
	const uint sample_rate = rate == 4 ? 32728 : 16364;
	const bool loop = src[0x31] != 0;
	uint loop_frame = 0;
	if (loop)
	{
		const uint loop_offset = rd_le32 (src + 0x54);
		if (loop_offset < start || loop_offset >= end || (loop_offset - start) % block_size)
			return ERR_INVALID_DATA;
		loop_frame = (loop_offset - start) / block_size * block_samples;
	}
	const u64 pcm_bytes = (u64)frames * channels * 2;
	const u64 wav_size = 44 + pcm_bytes + (loop ? 68 : 0);
	if (!frames || wav_size > (256u << 20))
		return ERR_FILE_TOO_BIG;
	int predictor[2] = { 0 }, index[2] = { 0 };
	s64 hist1[2] = { 0 }, hist2[2] = { 0 };
	if (codec != 0xb0)
		for (uint ch = 0; ch < channels; ch++)
		{
			predictor[ch] = (s16)rd_le16 (src + 0x80 + ch * 4);
			index[ch] = (s16)rd_le16 (src + 0x82 + ch * 4);
			if (index[ch] < 0 || index[ch] > 88)
				return ERR_INVALID_DATA;
		}
	u8 *wav = CALLOC (1, (size_t)wav_size);
	if (!wav)
		return ERR_OUT_OF_MEMORY;
	memcpy (wav, "RIFF", 4);
	wr_le32 (wav + 4, (uint)wav_size - 8);
	memcpy (wav + 8, "WAVEfmt ", 8);
	wr_le32 (wav + 16, 16);
	wr_le16 (wav + 20, 1);
	wr_le16 (wav + 22, channels);
	wr_le32 (wav + 24, sample_rate);
	wr_le32 (wav + 28, sample_rate * channels * 2);
	wr_le16 (wav + 32, channels * 2);
	wr_le16 (wav + 34, 16);
	memcpy (wav + 36, "data", 4);
	wr_le32 (wav + 40, (uint)pcm_bytes);
	static const short index_table[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
	static const short stepsize_table[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25,
		28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
		209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
		1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
		4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
		18500, 20350, 22385, 24623, 27086, 29794, 32767 };
	static const int coefs[16][2]
		= { { 0, 0 }, { 60, 0 }, { 115, -52 }, { 98, -55 }, { 122, -60 } };
	for (uint block = 0; block < bytes / block_size; block++)
		for (uint ch = 0; ch < channels; ch++)
		{
			const u8 *in = src + start + block * block_size + ch * 16;
			const uint header = in[15] ^ 0x80;
			for (uint n = 0; n < block_samples; n++)
			{
				int sample;
				if (codec == 0xb0)
				{
					const uint nibble = ((in[n / 2] ^ 0x80) >> (4 * (n & 1))) & 15;
					const int signed_nibble = nibble < 8 ? (int)nibble : (int)nibble - 16;
					const s64 residual = (s64)signed_nibble * (1u << (header & 15)) * 64;
					const s64 next = (hist1[ch] * coefs[header >> 4][0]
										 + hist2[ch] * coefs[header >> 4][1] + 32)
							/ 64
						+ residual;
					// Keep the un-clipped predictor, but bound malformed streams.
					if (next < INT32_MIN || next > INT32_MAX)
					{
						FREE (wav);
						return ERR_INVALID_DATA;
					}
					hist2[ch] = hist1[ch];
					hist1[ch] = next;
					sample = sadl_clamp16 ((next + 32) / 64) / 64 * 64;
				}
				else
				{
					const uint code = (in[n / 2] >> (4 * (n & 1))) & 15;
					const int step = stepsize_table[index[ch]];
					int diff = step >> 3;
					if (code & 1)
						diff += step >> 2;
					if (code & 2)
						diff += step >> 1;
					if (code & 4)
						diff += step;
					predictor[ch] = sadl_clamp16 (predictor[ch] + ((code & 8) ? -diff : diff));
					index[ch] += index_table[code];
					if (index[ch] < 0)
						index[ch] = 0;
					if (index[ch] > 88)
						index[ch] = 88;
					sample = predictor[ch];
				}
				wr_le16 (wav + 44 + ((size_t)(block * block_samples + n) * channels + ch) * 2,
					(u16)sample);
			}
		}
	if (loop)
	{
		u8 *smpl = wav + 44 + pcm_bytes;
		memcpy (smpl, "smpl", 4);
		wr_le32 (smpl + 4, 60);
		wr_le32 (smpl + 16, 1000000000u / sample_rate);
		wr_le32 (smpl + 20, 60);
		wr_le32 (smpl + 36, 1);
		wr_le32 (smpl + 52, loop_frame);
		wr_le32 (smpl + 56, frames - 1);
	}
	*dest_wav = wav;
	*dest_size = (uint)wav_size;
	return ERR_OK;
}

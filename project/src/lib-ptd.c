// SPDX-License-Identifier: GPL-2.0+
#include "lib-ptd.h"
#include "lib-archive-util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline u32 ptd_nibbles_to_samples (u32 nibbles)
{
	u32 whole_frames = nibbles / 16;
	u32 remainder = nibbles % 16;
	return (remainder > 0) ? whole_frames * 14 + remainder - 2 : whole_frames * 14;
}

bool IsPTD (const u8 *data, uint size)
{
	if (!data || size < 32)
		return false;

	const u16 version = rd_be16 (data);
	const u16 num_files = rd_be16 (data + 2);
	const u32 sample_rate = rd_be32 (data + 8);
	const u32 channel_count = rd_be32 (data + 12);
	const u32 entry_offsets = rd_be32 (data + 16);
	const u32 coef_offset = rd_be32 (data + 20);
	const u32 header_offset = rd_be32 (data + 24);
	const u32 stream_offset = rd_be32 (data + 28);

	if (version != 1 && version != 2)
		return false;
	if (num_files < 1 || num_files > 2000)
		return false;
	if (sample_rate < 4000 || sample_rate > 96000)
		return false;
	if (channel_count < 1 || channel_count > 8)
		return false;
	if (entry_offsets < 32 || entry_offsets + (u64)num_files * 4 > size)
		return false;
	if (coef_offset >= size || header_offset >= size || stream_offset > size)
		return false;

	return true;
}

static u8 *ptd_build_dsp (uint *dsp_size, const u8 *data, uint size, u32 flags, u32 srate,
	u32 nibble_cnt, u32 loop_start, u32 stream_off, u16 coef_idx, u32 coef_offset)
{
	const uint byte_count = (nibble_cnt + 1) / 2;
	if (stream_off + byte_count > size)
		return 0;

	const u8 *coef_data = 0;
	if (coef_offset + (u64)coef_idx * 32 + 32 <= size)
		coef_data = data + coef_offset + (u64)coef_idx * 32;

	const uint total = 0x60 + byte_count;
	u8 *dsp = CALLOC (1, total);
	if (!dsp)
		return 0;

	const u32 sample_count = ptd_nibbles_to_samples (nibble_cnt);
	wr_be32 (dsp, sample_count);
	wr_be32 (dsp + 4, nibble_cnt);
	wr_be32 (dsp + 8, srate);
	wr_be16 (dsp + 12, (flags & 0x02000000) ? 1 : 0);
	wr_be16 (dsp + 14, 0); // format = 0 (ADPCM)
	wr_be32 (dsp + 16, loop_start);
	wr_be32 (dsp + 20, nibble_cnt > 0 ? nibble_cnt - 1 : 0);
	wr_be32 (dsp + 24, 0);

	if (coef_data)
		memcpy (dsp + 28, coef_data, 32);

	const u8 *adpcm_src = data + stream_off;
	if (byte_count > 0)
	{
		wr_be16 (dsp + 62, (u16)adpcm_src[0]); // initial_ps
		memcpy (dsp + 0x60, adpcm_src, byte_count);
	}

	*dsp_size = total;
	return dsp;
}

enumError ScanPTD (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || !IsPTD (data, size))
		return ERR_INVALID_DATA;

	const u16 num_files = rd_be16 (data + 2);
	const u32 entry_offsets = rd_be32 (data + 16);
	const u32 coef_offset = rd_be32 (data + 20);

	*entries = 0;
	*n_entries = 0;

	nintendo_sarc_entry_t *out = CALLOC (num_files * 2, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint out_cnt = 0;
	for (u32 i = 0; i < num_files; i++)
	{
		const u32 file_off = rd_be32 (data + entry_offsets + i * 4);
		if (!file_off || file_off + 24 > size)
			continue;

		const u32 flags = rd_be32 (data + file_off);
		const u32 srate = rd_be32 (data + file_off + 4);
		const u32 nibble_cnt = rd_be32 (data + file_off + 8);
		const u32 loop_start = rd_be32 (data + file_off + 12);
		const u32 ch1_stream_off = rd_be32 (data + file_off + 16);
		const u16 ch1_coef_idx = rd_be16 (data + file_off + 20);

		const bool stereo = (flags & 0x01000000) != 0;

		uint dsp1_sz = 0;
		u8 *dsp1 = ptd_build_dsp (&dsp1_sz, data, size, flags, srate, nibble_cnt,
			loop_start, ch1_stream_off, ch1_coef_idx, coef_offset);

		if (dsp1)
		{
			char name[64];
			if (stereo)
				snprintf (name, sizeof (name), "stream%03u_L.dsp", i);
			else
				snprintf (name, sizeof (name), "stream%03u.dsp", i);
			OwnedEntryAdd (out, out_cnt++, name, dsp1, dsp1_sz);
			FREE (dsp1);
		}

		if (stereo && file_off + 32 <= size)
		{
			const u32 ch2_stream_off = rd_be32 (data + file_off + 24);
			const u16 ch2_coef_idx = rd_be16 (data + file_off + 28);

			uint dsp2_sz = 0;
			u8 *dsp2 = ptd_build_dsp (&dsp2_sz, data, size, flags, srate, nibble_cnt,
				loop_start, ch2_stream_off, ch2_coef_idx, coef_offset);

			if (dsp2)
			{
				char name[64];
				snprintf (name, sizeof (name), "stream%03u_R.dsp", i);
				OwnedEntryAdd (out, out_cnt++, name, dsp2, dsp2_sz);
				FREE (dsp2);
			}
		}
	}

	*entries = out;
	*n_entries = out_cnt;
	return ERR_OK;
}

// SPDX-License-Identifier: GPL-2.0+
#include "lib-bnvib.h"
#include <string.h>

static const float bnvib_pow_table[32] = {
	1.000000f, 1.021897f, 1.044274f, 1.067140f, 1.090508f, 1.114387f, 1.138789f, 1.163725f,
	1.189207f, 1.215247f, 1.241858f, 1.269051f, 1.296840f, 1.325237f, 1.354256f, 1.383910f,
	1.414214f, 1.445181f, 1.476826f, 1.509164f, 1.542211f, 1.575981f, 1.610490f, 1.645755f,
	1.681793f, 1.718619f, 1.756252f, 1.794709f, 1.834008f, 1.874168f, 1.915207f, 1.957144f,
};

static inline float bnvib_get_freq (u8 b)
{
	const int freq_min = 10;
	return (float)(freq_min << (b / 32)) * bnvib_pow_table[b % 32];
}

static inline float bnvib_get_amp (u8 b)
{
	return (float)b / 255.0f;
}

void InitializeBNVIB (bnvib_t *vib)
{
	if (vib)
		memset (vib, 0, sizeof (*vib));
}

void ResetBNVIB (bnvib_t *vib)
{
	if (vib)
	{
		FREE (vib->samples);
		memset (vib, 0, sizeof (*vib));
	}
}

bool IsBNVIB (const u8 *data, uint size)
{
	if (!data || size < 8)
		return false;

	const u32 meta_data_size = rd_le32 (data);
	if (meta_data_size < 4 || meta_data_size > 1024)
		return false;

	if (size < 8 + meta_data_size)
		return false;

	const u16 format_id = rd_le16 (data + 4);
	const u16 sampling_rate = rd_le16 (data + 6);

	if (format_id != 3 || sampling_rate != 200)
		return false;

	const u32 data_size_offset = 4 + meta_data_size;
	if (data_size_offset + 4 > size)
		return false;

	const u32 data_size = rd_le32 (data + data_size_offset);
	if (data_size % 4 != 0)
		return false;

	const u64 total_expected = (u64)data_size_offset + 4 + data_size;
	if (total_expected > size)
		return false;

	if (meta_data_size >= 12)
	{
		const u32 loop_start = rd_le32 (data + 8);
		const u32 loop_end = rd_le32 (data + 12);
		const u32 sample_count = data_size / 4;
		if (loop_start > loop_end || loop_end > sample_count)
			return false;
	}

	return true;
}

enumError ScanBNVIB (bnvib_t *vib, const u8 *data, uint size)
{
	if (!vib || !IsBNVIB (data, size))
		return ERR_INVALID_DATA;

	InitializeBNVIB (vib);

	vib->meta_data_size = rd_le32 (data);
	vib->format_id = rd_le16 (data + 4);
	vib->sampling_rate = rd_le16 (data + 6);

	if (vib->meta_data_size >= 12)
	{
		vib->is_loop = true;
		vib->loop_start = rd_le32 (data + 8);
		vib->loop_end = rd_le32 (data + 12);
		if (vib->meta_data_size >= 16)
			vib->loop_interval = rd_le32 (data + 16);
	}

	const u32 data_size_offset = 4 + vib->meta_data_size;
	const u32 data_size = rd_le32 (data + data_size_offset);
	vib->sample_count = data_size / 4;

	if (!vib->is_loop)
	{
		vib->loop_start = 0;
		vib->loop_end = vib->sample_count;
		vib->loop_interval = 0;
	}

	vib->samples = MALLOC (vib->sample_count * sizeof (*vib->samples));
	if (!vib->samples && vib->sample_count > 0)
		return ERR_OUT_OF_MEMORY;

	const u8 *payload = data + data_size_offset + 4;
	for (uint i = 0; i < vib->sample_count; i++)
	{
		const u8 *s = payload + i * 4;
		vib->samples[i].amp_low = bnvib_get_amp (s[0]);
		vib->samples[i].freq_low = bnvib_get_freq (s[1]);
		vib->samples[i].amp_high = bnvib_get_amp (s[2]);
		vib->samples[i].freq_high = bnvib_get_freq (s[3]);
	}

	return ERR_OK;
}

enumError SaveTextBNVIB (const bnvib_t *vib, ccp dest_path)
{
	if (!vib || !dest_path)
		return ERR_INVALID_DATA;

	FILE *fp = fopen (dest_path, "w");
	if (!fp)
		return ERR_CANT_CREATE;

	const uint duration_ms = vib->sample_count * 5; // 200 Hz = 5ms per sample
	fprintf (fp, "# BNVIB - Nintendo Switch Joy-Con Vibration Data\n");
	fprintf (fp, "FormatId:     %u\n", vib->format_id);
	fprintf (fp, "SamplingRate: %u Hz\n", vib->sampling_rate);
	fprintf (fp, "Duration:     %u ms (%u samples)\n", duration_ms, vib->sample_count);
	fprintf (fp, "IsLoop:       %s\n", vib->is_loop ? "true" : "false");
	if (vib->is_loop)
	{
		fprintf (fp, "LoopStart:    %u (%u ms)\n", vib->loop_start, vib->loop_start * 5);
		fprintf (fp, "LoopEnd:      %u (%u ms)\n", vib->loop_end, vib->loop_end * 5);
		fprintf (fp, "LoopInterval: %u\n", vib->loop_interval);
	}
	fprintf (fp, "\n");
	fprintf (fp, "# Index  Time(ms)  AmpLow     FreqLow(Hz)  AmpHigh    FreqHigh(Hz)\n");

	for (uint i = 0; i < vib->sample_count; i++)
	{
		const bnvib_sample_t *s = &vib->samples[i];
		fprintf (fp, "%7u  %8u  %8.4f   %10.2f   %8.4f   %10.2f\n",
			i, i * 5, s->amp_low, s->freq_low, s->amp_high, s->freq_high);
	}

	fclose (fp);
	return ERR_OK;
}

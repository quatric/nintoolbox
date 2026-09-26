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
		u8 *dsp1 = ptd_build_dsp (&dsp1_sz, data, size, flags, srate, nibble_cnt, loop_start,
			ch1_stream_off, ch1_coef_idx, coef_offset);

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
			u8 *dsp2 = ptd_build_dsp (&dsp2_sz, data, size, flags, srate, nibble_cnt, loop_start,
				ch2_stream_off, ch2_coef_idx, coef_offset);

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

// ----------------------------------------------------------------------------
// Full-container parse + rebuild.
// Reference: MPLibrary/GCWii/Audio/PTD.cs PtdFile::Read/Write.
//
// One deviation from the reference writer, deliberately: its per-file block
// writes the second channel's fields whenever Channels.Count > 0 (i.e. even
// for mono files, where Channels[1] does not exist). The reader only ever
// creates a second channel when flags & 0x01000000, so the writer can only
// round-trip what the reader produces if the second channel is gated on
// num_channels > 1. That is what CreatePTD does.

#define PTD_UNKNOWN_MAGIC 23871488u
#define PTD_UNKNOWN_SIZE 144u

enumError ScanPTDFile (ptd_file_t *file, const u8 *data, uint size)
{
	if (!file || !data || !IsPTD (data, size))
		return ERR_INVALID_DATA;
	memset (file, 0, sizeof (*file));

	const u16 num_files = rd_be16 (data + 2);
	const u32 entry_offsets = rd_be32 (data + 16);
	const u32 coef_offset = rd_be32 (data + 20);

	file->version = rd_be16 (data);
	file->unknown2 = rd_be32 (data + 4);
	file->sample_rate = rd_be32 (data + 8);
	file->channel_count = rd_be32 (data + 12);

	ptd_stream_t *streams = CALLOC (num_files ? num_files : 1, sizeof (*streams));
	if (!streams)
		return ERR_OUT_OF_MEMORY;

	uint n = 0;
	for (uint i = 0; i < num_files; i++)
	{
		const u32 file_off = rd_be32 (data + entry_offsets + i * 4);
		ptd_stream_t *s = streams + n;
		if (!file_off)
			continue; // empty slot: kept as zeroed stream, no channels
		if (file_off + 24 > size)
		{
			// Truncated entry: keep the slot empty rather than failing the
			// whole container.
			continue;
		}
		s->flags = rd_be32 (data + file_off);
		s->sample_rate = rd_be32 (data + file_off + 4);
		s->nibble_count = rd_be32 (data + file_off + 8);
		s->loop_start = rd_be32 (data + file_off + 12);
		const u32 ch1_off = rd_be32 (data + file_off + 16);
		const u16 ch1_coef = rd_be16 (data + file_off + 20);
		s->channels[0].unknown = rd_be16 (data + file_off + 22);

		const bool stereo = (s->flags & 0x01000000) != 0;
		const uint payload = (s->nibble_count + 1) / 2;

		u32 ch2_off = 0;
		u16 ch2_coef = 0;
		size_t hdr_end = (size_t)file_off + 24;
		if (stereo)
		{
			if (file_off + 32 > size)
				continue;
			ch2_off = rd_be32 (data + file_off + 24);
			ch2_coef = rd_be16 (data + file_off + 28);
			s->channels[1].unknown = rd_be16 (data + file_off + 30);
			hdr_end = (size_t)file_off + 32;
		}
		if (s->flags == PTD_UNKNOWN_MAGIC)
		{
			if (hdr_end + PTD_UNKNOWN_SIZE > size)
				continue;
			s->unknown_data = data + hdr_end;
			s->unknown_size = PTD_UNKNOWN_SIZE;
		}

		// Coefficient tables (16 u16 each, BE).
		if ((u64)coef_offset + (u64)ch1_coef * 32 + 32 <= size)
			for (int c = 0; c < 16; c++)
				s->channels[0].coef[c] = rd_be16 (data + coef_offset + (u64)ch1_coef * 32 + c * 2);
		if (ch1_off && ch1_off < size)
		{
			uint avail = (uint)(size - ch1_off);
			// For stereo the first channel's bytes run up to the second
			// channel's offset; otherwise they are nibble_count/2 bytes.
			uint want = payload;
			if (stereo && ch2_off > ch1_off && ch2_off - ch1_off < want)
				want = ch2_off - ch1_off;
			if (want > avail)
				want = avail;
			s->channels[0].data = data + ch1_off;
			s->channels[0].data_size = want;
		}
		s->num_channels = 1;
		if (stereo)
		{
			if ((u64)coef_offset + (u64)ch2_coef * 32 + 32 <= size)
				for (int c = 0; c < 16; c++)
					s->channels[1].coef[c]
						= rd_be16 (data + coef_offset + (u64)ch2_coef * 32 + c * 2);
			if (ch2_off && ch2_off < size)
			{
				uint avail = (uint)(size - ch2_off);
				uint want = payload < avail ? payload : avail;
				s->channels[1].data = data + ch2_off;
				s->channels[1].data_size = want;
			}
			s->num_channels = 2;
		}
		n++;
	}

	file->streams = streams;
	file->num_streams = num_files;
	return ERR_OK;
}

void ResetPTDFile (ptd_file_t *file)
{
	if (file)
	{
		FREE (file->streams);
		memset (file, 0, sizeof (*file));
	}
}

static inline void ptd_wr_be16 (u8 *p, u16 v)
{
	p[0] = v >> 8;
	p[1] = v;
}

static inline void ptd_wr_be32 (u8 *p, u32 v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}

enumError CreatePTD (u8 **dest, uint *dest_size, const ptd_file_t *file)
{
	if (!dest || !dest_size || !file || !file->num_streams || file->num_streams > 2000
		|| !file->streams)
		return ERR_INVALID_DATA;

	const uint n = file->num_streams;

	// Header sizes per stream.
	u64 hdr_total = 0;
	uint n_coefs = 0;
	for (uint i = 0; i < n; i++)
	{
		const ptd_stream_t *s = file->streams + i;
		if (!s->num_channels)
			continue;
		if (s->num_channels > 2)
			return ERR_INVALID_DATA;
		hdr_total += 24;
		if (s->num_channels > 1)
			hdr_total += 8;
		if (s->flags == PTD_UNKNOWN_MAGIC)
			hdr_total += PTD_UNKNOWN_SIZE;
		n_coefs += s->num_channels;
	}

	// Layout mirrors PtdFile::Write: 32-byte header, offset table, coef
	// table (16-aligned), file headers, align16, 16 zero bytes, then stream
	// data with per-file align8.
	u64 pos = 32 + (u64)n * 4;
	u64 coef_off = pos;
	u64 hdr_off = coef_off + (u64)n_coefs * 32;
	hdr_off = (hdr_off + 15) & ~(u64)15;
	u64 stream_base = hdr_off + hdr_total;
	stream_base = (stream_base + 7) & ~(u64)7;
	stream_base += 16;
	if (stream_base > NFMT_MAX_OUTPUT)
		return EFBIG;

	// Stream payload sizes.
	u64 payload_total = 0;
	for (uint i = 0; i < n; i++)
	{
		const ptd_stream_t *s = file->streams + i;
		if (!s->num_channels)
			continue;
		const uint pl = (s->nibble_count + 1) / 2;
		payload_total += pl; // ch1
		if (s->num_channels > 1)
		{
			payload_total = (payload_total + 3) & ~(u64)3;
			payload_total += pl; // ch2
		}
		payload_total = (payload_total + 7) & ~(u64)7;
	}
	const u64 total = stream_base + payload_total;
	if (total > NFMT_MAX_OUTPUT || total > UINT_MAX)
		return EFBIG;

	u8 *out = CALLOC (1, (size_t)total);
	if (!out)
		return ERR_OUT_OF_MEMORY;

	ptd_wr_be16 (out, file->version);
	ptd_wr_be16 (out + 2, (u16)n);
	ptd_wr_be32 (out + 4, file->unknown2);
	ptd_wr_be32 (out + 8, file->sample_rate);
	ptd_wr_be32 (out + 12, file->channel_count);
	ptd_wr_be32 (out + 16, 32);
	ptd_wr_be32 (out + 20, (u32)coef_off);
	ptd_wr_be32 (out + 24, (u32)hdr_off);
	ptd_wr_be32 (out + 28, (u32)stream_base);

	// Offset table + coef table.
	u64 cur_hdr = hdr_off;
	uint coef_idx = 0;
	for (uint i = 0; i < n; i++)
	{
		const ptd_stream_t *s = file->streams + i;
		if (!s->num_channels)
		{
			ptd_wr_be32 (out + 32 + (u64)i * 4, 0);
			continue;
		}
		ptd_wr_be32 (out + 32 + (u64)i * 4, (u32)cur_hdr);
		u64 hsize = 24 + (s->num_channels > 1 ? 8 : 0)
			+ (s->flags == PTD_UNKNOWN_MAGIC ? PTD_UNKNOWN_SIZE : 0);
		cur_hdr += hsize;
		for (uint c = 0; c < s->num_channels; c++)
		{
			for (int k = 0; k < 16; k++)
				ptd_wr_be16 (out + coef_off + (u64)coef_idx * 32 + k * 2, s->channels[c].coef[k]);
			coef_idx++;
		}
	}

	// File headers + stream addresses.
	u64 cur_stream = stream_base;
	coef_idx = 0;
	cur_hdr = hdr_off;
	for (uint i = 0; i < n; i++)
	{
		const ptd_stream_t *s = file->streams + i;
		if (!s->num_channels)
			continue;
		const uint pl = (s->nibble_count + 1) / 2;
		ptd_wr_be32 (out + cur_hdr, s->flags);
		ptd_wr_be32 (out + cur_hdr + 4, s->sample_rate);
		ptd_wr_be32 (out + cur_hdr + 8, s->nibble_count);
		ptd_wr_be32 (out + cur_hdr + 12, s->loop_start);
		ptd_wr_be32 (out + cur_hdr + 16, (u32)cur_stream);
		ptd_wr_be16 (out + cur_hdr + 20, (u16)coef_idx++);
		ptd_wr_be16 (out + cur_hdr + 22, s->channels[0].unknown);
		u64 hpos = cur_hdr + 24;
		u64 ch1_addr = cur_stream;
		u64 ch2_addr = 0;
		cur_stream += pl;
		if (s->num_channels > 1)
		{
			cur_stream = (cur_stream + 3) & ~(u64)3;
			ch2_addr = cur_stream;
			ptd_wr_be32 (out + hpos, (u32)ch2_addr);
			ptd_wr_be16 (out + hpos + 4, (u16)coef_idx++);
			ptd_wr_be16 (out + hpos + 6, s->channels[1].unknown);
			hpos += 8;
			cur_stream += pl;
		}
		if (s->flags == PTD_UNKNOWN_MAGIC && s->unknown_data && s->unknown_size)
		{
			const uint cp = s->unknown_size < PTD_UNKNOWN_SIZE ? s->unknown_size : PTD_UNKNOWN_SIZE;
			memcpy (out + hpos, s->unknown_data, cp);
			hpos += PTD_UNKNOWN_SIZE;
		}
		else if (s->flags == PTD_UNKNOWN_MAGIC)
			hpos += PTD_UNKNOWN_SIZE;
		cur_hdr = hpos;
		cur_stream = (cur_stream + 7) & ~(u64)7;

		// Payload bytes.
		if (s->channels[0].data && s->channels[0].data_size)
		{
			const uint cp = s->channels[0].data_size < pl ? s->channels[0].data_size : pl;
			memcpy (out + ch1_addr, s->channels[0].data, cp);
		}
		if (s->num_channels > 1 && s->channels[1].data && s->channels[1].data_size)
		{
			const uint cp = s->channels[1].data_size < pl ? s->channels[1].data_size : pl;
			memcpy (out + ch2_addr, s->channels[1].data, cp);
		}
	}

	*dest = out;
	*dest_size = (uint)total;
	return ERR_OK;
}

// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_ADX_H
#define LIB_ADX_H 1

#include "lib-std.h"
#include <stdio.h>

// CRI ADX compressed audio stream (.adx). Header-only support: this reads
// the fixed audio-parameter header and, when present, the loop-point block
// inside the "copyright offset" padding region. It does not decode the
// ADPCM sample data itself. See lib-adx.c for the exact byte layout,
// confirmed against real Arc Rise Fantasia (Wii) sample files.
typedef struct
{
	u16 copyright_offset;	// offset of the "(c)CRI" footer tag = header size - 4
	u8  encoding_type;	// 2,3,4 = ADX variants, 0x10/0x11 = AHX
	u8  block_size;		// bytes per channel per block, usually 18
	u8  sample_bitdepth;	// usually 4
	u8  channel_count;
	u32 sample_rate;
	u32 total_samples;
	u16 highpass_freq;
	u8  version;		// 3, 4 or 5
	u8  flags;

	// Loop info (version 4 header extension). valid_loop is false when the
	// header is too short to hold it or the loop-enable field isn't 1.
	bool valid_loop;
	u16 loop_align_samples;
	u16 loop_enabled;
	u32 loop_start_sample;
	u32 loop_start_byte;
	u32 loop_end_sample;
	u32 loop_end_byte;
} adx_header_t;

bool IsADX (const u8 *data, size_t size);
enumError ScanADXHeader (adx_header_t *hd, const u8 *data, size_t size);
enumError DecodeADX_Text (FILE *f, const u8 *data, size_t size);

#endif // LIB_ADX_H

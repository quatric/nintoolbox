// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// CRI ADX compressed audio (.adx). Confirmed against ~7000 real sample files
// from Arc Rise Fantasia (Wii, imageEpoch): all observed samples are
// encoding_type 3 ("standard ADX"), version 4, block_size 18, 1 or 2
// channels, 48000 Hz.
//
// Fixed 20-byte big-endian header (bytes 0..19), matches the widely
// published CRI ADX layout (Wikipedia/rewiki/multimedia.cx) byte-for-byte:
//   0x00  u16 signature (0x8000)
//   0x02  u16 copyright_offset -- the "(c)CRI" footer tag starts at this
//         file offset; sample data itself starts at copyright_offset + 4
//         (confirmed: byte-inspected at that exact offset in 3 samples)
//   0x04  u8  encoding_type (2/3/4 = ADX variants, 0x10/0x11 = AHX)
//   0x05  u8  block_size (bytes/channel/block, 18 in every sample seen)
//   0x06  u8  sample_bitdepth (4 in every sample seen)
//   0x07  u8  channel_count
//   0x08  u32 sample_rate
//   0x0c  u32 total_samples
//   0x10  u16 highpass_freq
//   0x12  u8  version
//   0x13  u8  flags
//
// Bytes 0x14..0x1f (12 bytes) are zero padding in every sample checked here
// -- NOT loop fields, despite some third-party docs placing the loop block
// immediately after the fixed header. In this game's files the loop block
// instead starts at a fixed offset of 0x20 (32), same shape as documented
// elsewhere for version-4 ADX ("ainf"-less loop extension):
//   0x20  u16 loop_alignment_samples
//   0x22  u16 loop_enabled (0 or 1)
//   0x24  u32 loop_start_sample
//   0x28  u32 loop_start_byte
//   0x2c  u32 loop_end_sample
//   0x30  u32 loop_end_byte
// BEST-EFFORT field pairing: cross-checked on 3 full BGM files, where
// loop_end_sample always equals the main header's total_samples exactly
// (byte-for-byte identical field value, i.e. these tracks loop to the very
// end of the file) and loop_end_byte always lands within ~1.2KB of EOF --
// both consistent with a full-track loop. loop_start_sample was 1 in every
// checked sample (loops back almost to the very start); loop_start_byte did
// not resolve to a clean multiple of the 18-byte/36-byte (mono/stereo)
// block size, so exact block alignment of the loop start is NOT confirmed.
// A 4-byte field at 0x34 (close to, but short of, EOF in every sample) is
// left undecoded -- likely a trailing chunk/footer pointer, not part of the
// loop block.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-adx.h"
#include <string.h>

bool IsADX (const u8 *data, size_t size)
{
	if (!data || size < 20)
		return false;
	if (data[0] != 0x80 || data[1] != 0x00)
		return false;
	const u16 copyright_offset = rd_be16 (data + 2);
	const u8 enc = data[4];
	if (!copyright_offset || copyright_offset >= size)
		return false;
	if (enc != 2 && enc != 3 && enc != 4 && enc != 0x10 && enc != 0x11)
		return false;
	const u8 version = data[18];
	if (!version || version > 5)
		return false;
	return true;
}

enumError ScanADXHeader (adx_header_t *hd, const u8 *data, size_t size)
{
	if (!hd || !IsADX (data, size))
		return ERR_INVALID_DATA;
	memset (hd, 0, sizeof (*hd));

	hd->copyright_offset	= rd_be16 (data + 2);
	hd->encoding_type	= data[4];
	hd->block_size		= data[5];
	hd->sample_bitdepth	= data[6];
	hd->channel_count	= data[7];
	hd->sample_rate		= rd_be32 (data + 8);
	hd->total_samples	= rd_be32 (data + 12);
	hd->highpass_freq	= rd_be16 (data + 16);
	hd->version		= data[18];
	hd->flags		= data[19];

	// Loop block: only decoded when the fixed offset (0x20) and the whole
	// 20-byte block fit before the copyright tag / EOF, and loop_enabled
	// reads exactly 1 (never seen any other value in real samples; a
	// non-1 value here is treated as "no loop" rather than guessed at).
	if (size >= 0x34 && (size_t) hd->copyright_offset >= 0x34)
	{
		const u16 enabled = rd_be16 (data + 0x22);
		if (enabled == 1)
		{
			hd->valid_loop		= true;
			hd->loop_align_samples	= rd_be16 (data + 0x20);
			hd->loop_enabled	= enabled;
			hd->loop_start_sample	= rd_be32 (data + 0x24);
			hd->loop_start_byte	= rd_be32 (data + 0x28);
			hd->loop_end_sample	= rd_be32 (data + 0x2c);
			hd->loop_end_byte	= rd_be32 (data + 0x30);
		}
	}

	return ERR_OK;
}

static ccp adx_encoding_name (u8 enc)
{
	switch (enc)
	{
		case 2:    return "ADX (fixed coefficients)";
		case 3:    return "ADX (standard)";
		case 4:    return "ADX (exponential scale)";
		case 0x10: return "AHX (level 2)";
		case 0x11: return "AHX (level 3)";
		default:   return "unknown";
	}
}

enumError DecodeADX_Text (FILE *f, const u8 *data, size_t size)
{
	if (!f)
		return ERR_INVALID_DATA;

	adx_header_t hd;
	enumError err = ScanADXHeader (&hd, data, size);
	if (err)
		return err;

	fprintf (f, "#ADX\n");
	fprintf (f, "encoding-type    = %u (%s)\n", hd.encoding_type, adx_encoding_name (hd.encoding_type));
	fprintf (f, "version          = %u\n", hd.version);
	fprintf (f, "flags            = 0x%02x\n", hd.flags);
	fprintf (f, "block-size       = %u\n", hd.block_size);
	fprintf (f, "sample-bitdepth  = %u\n", hd.sample_bitdepth);
	fprintf (f, "channels         = %u\n", hd.channel_count);
	fprintf (f, "sample-rate      = %u\n", hd.sample_rate);
	fprintf (f, "total-samples    = %u\n", hd.total_samples);
	fprintf (f, "highpass-freq    = %u\n", hd.highpass_freq);
	fprintf (f, "copyright-offset = %u\n", hd.copyright_offset);
	fprintf (f, "data-offset      = %u\n", hd.copyright_offset + 4u);
	if (hd.valid_loop)
	{
		fprintf (f, "loop-align-samples = %u\n", hd.loop_align_samples);
		fprintf (f, "loop-start-sample  = %u\n", hd.loop_start_sample);
		fprintf (f, "loop-start-byte    = %u\n", hd.loop_start_byte);
		fprintf (f, "loop-end-sample    = %u\n", hd.loop_end_sample);
		fprintf (f, "loop-end-byte      = %u\n", hd.loop_end_byte);
	}
	else
		fprintf (f, "loop               = none\n");

	return ERR_OK;
}

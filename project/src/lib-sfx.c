// Monster Games .sfx audio (Excite Truck / ExciteBots, Wii).
//
// A 0x80 header over a plain Nintendo DSP-ADPCM stream. The header mixes
// endianness: the sizes and the sample rate are little-endian, while the
// nibble count and the coefficient table are the ordinary big-endian DSP
// fields, evidently kept in the layout the SDK produced them in.
//
//   0x00  u32 LE  payload size; + 0x80 equals the file size exactly
//   0x04  u32 LE  header size, always 0x80
//   0x10  u32 LE  sample rate
//   0x14  u32 LE  byte rate, always rate * 2 (decoded audio is 16-bit mono)
//   0x34  u32 BE  nibble count, about twice the payload size
//   0x3c  16 x s16 BE  DSP-ADPCM coefficients
//   0x80  DSP-ADPCM frames: one predictor/scale byte then 14 nibbles
//
// Decoding is deliberately handed to mobipeg rather than done here. Its
// adpcm_thp is the same decoder the rest of this project's audio work already
// relies on, and writing a second one only creates a second thing to get
// subtly wrong -- a first attempt at exactly that, carrying a rounding term
// DSP-ADPCM does not use, was within 66/32768 of correct and still wrong on
// 99.75% of its samples.

#include "lib-sfx.h"
#include "lib-std.h"
#include <string.h>
#include <stdio.h>

#define SFX_HEADER_SIZE 0x80

static u32 sfx_rd_le32 (const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static u32 sfx_rd_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

bool GetSFXInfo (const u8 *data, uint size, sfx_info_t *info)
{
	if (!data || !info || size <= SFX_HEADER_SIZE)
		return false;

	const u32 data_size = sfx_rd_le32 (data);
	const u32 hdr_size = sfx_rd_le32 (data + 4);
	const u32 rate = sfx_rd_le32 (data + 0x10);
	const u32 byte_rate = sfx_rd_le32 (data + 0x14);

	// There is no magic, so the header has to identify itself by agreeing with
	// the file: the payload must account for every byte after it, the decoded
	// form must be 16-bit mono, and the rate must be one a console mixer uses.
	if (hdr_size != SFX_HEADER_SIZE || (u64)data_size + hdr_size != size)
		return false;
	if (!rate || rate > 48000 || byte_rate != rate * 2)
		return false;

	const u32 nibbles = sfx_rd_be32 (data + 0x34);
	if (nibbles < data_size || nibbles > (u64)data_size * 2 + 16)
		return false;

	info->data_size = data_size;
	info->sample_rate = rate;
	info->byte_rate = byte_rate;
	info->num_nibbles = nibbles;
	for (int i = 0; i < 16; i++)
		info->coef[i] = (s16)((u16)data[0x3c + i * 2] << 8 | data[0x3d + i * 2]);
	return true;
}

// GENH keeps its own fields little-endian but carries the DSP coefficients in
// the big-endian order the codec expects, which is how they already sit in a
// .sfx -- so they are copied across untouched.
#define GENH_HEADER_SIZE 0x100
#define GENH_COEF_OFFSET 0x50
#define GENH_CODEC_NGC_DSP 12

enumError BuildGENHFromSFX (const u8 *data, uint size, u8 **dest, uint *dest_size)
{
	sfx_info_t info;
	if (!dest || !dest_size || !GetSFXInfo (data, size, &info))
		return ERR_INVALID_DATA;

	// Eight bytes hold one frame of fourteen samples.
	const u32 samples = info.data_size / 8 * 14;

	u8 *out = CALLOC (1, GENH_HEADER_SIZE + info.data_size);
	if (!out)
		return ERR_OUT_OF_MEMORY;

	memcpy (out, "GENH", 4);
	write_le32 (out + 0x04, 1); // channels
	write_le32 (out + 0x08, info.data_size); // interleave
	write_le32 (out + 0x0c, info.sample_rate);
	write_le32 (out + 0x10, (u32)-1); // loop start: not looping
	write_le32 (out + 0x14, samples);
	write_le32 (out + 0x18, GENH_CODEC_NGC_DSP);
	write_le32 (out + 0x1c, GENH_HEADER_SIZE); // data start
	write_le32 (out + 0x20, GENH_HEADER_SIZE); // header size
	write_le32 (out + 0x24, GENH_COEF_OFFSET);
	write_le32 (out + 0x28, GENH_COEF_OFFSET);
	memcpy (out + GENH_COEF_OFFSET, data + 0x3c, 32);
	memcpy (out + GENH_HEADER_SIZE, data + SFX_HEADER_SIZE, info.data_size);

	*dest = out;
	*dest_size = GENH_HEADER_SIZE + info.data_size;
	return ERR_OK;
}

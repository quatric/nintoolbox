// SPDX-License-Identifier: GPL-2.0+
#include "lib-donkeykonga.h"
#include "lib-archive-util.h"
#include "lib-dsp.h"
#include "lib-dspadpcm.h"
#include <string.h>
#include <zlib.h>

//-----------------------------------------------------------------------------
// (1) ".tpl.dkz" "DKZF" zlib texture wrapper

#define DKZF_HEADER_SIZE 8
#define DKZF_MAX_DECOMPRESSED 0x10000000u // 256 MiB sanity cap

bool IsDKZF (const u8 *data, size_t size)
{
	if (!data || size < DKZF_HEADER_SIZE + 2)
		return false;
	if (memcmp (data, "DKZF", 4))
		return false;

	const u32 dsize = rd_be32 (data + 4);
	if (!dsize || dsize > DKZF_MAX_DECOMPRESSED)
		return false;

	// Standard raw zlib stream: first byte is always 0x78 for the
	// deflate/32K-window headers every real sample uses.
	return data[DKZF_HEADER_SIZE] == 0x78;
}

enumError DecodeDKZF (const u8 *data, size_t size, u8 **dest, uint *dest_size)
{
	if (dest)
		*dest = 0;
	if (dest_size)
		*dest_size = 0;
	if (!dest || !dest_size || !IsDKZF (data, size))
		return ERR_INVALID_DATA;

	const u32 dsize = rd_be32 (data + 4);
	u8 *out = MALLOC (dsize ? dsize : 1);
	if (!out)
		return ERR_OUT_OF_MEMORY;

	uLongf destLen = dsize;
	const int zerr
		= uncompress (out, &destLen, data + DKZF_HEADER_SIZE, (uLong)(size - DKZF_HEADER_SIZE));
	if (zerr != Z_OK || destLen != dsize)
	{
		FREE (out);
		return ERR_INVALID_DATA;
	}

	*dest = out;
	*dest_size = dsize;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (2) ".chd" / ".c3d" "CHDp" / "C3Dp" + ".cbd" hit-sound bank

#define DKSB_BASE_HEADER_SIZE 16
#define DKSB_ENTRY_SIZE 176
#define DKSB_MAX_ENTRIES 4096

// Offsets within one 176-byte entry.
#define DKSB_E_OFFSET 0x00
#define DKSB_E_NIBBLES 0x54
#define DKSB_E_SRATE 0x58
#define DKSB_E_COEF 0x68

static bool dksb_check_magic (const u8 *data, size_t size)
{
	return data && size >= DKSB_BASE_HEADER_SIZE
		&& (!memcmp (data, "CHDp", 4) || !memcmp (data, "C3Dp", 4));
}

bool IsDKSoundBank (const u8 *chd_data, size_t chd_size)
{
	if (!dksb_check_magic (chd_data, chd_size))
		return false;

	const u16 count = rd_be16 (chd_data + 8);
	if (!count || count > DKSB_MAX_ENTRIES)
		return false;

	const u32 cbd_size = rd_be32 (chd_data + 12);
	if (!cbd_size || cbd_size > 0x10000000u)
		return false;

	const u64 table_end = (u64)DKSB_BASE_HEADER_SIZE + (u64)count * DKSB_ENTRY_SIZE;
	// C3Dp appends one extra 20-byte record per entry after the table;
	// CHDp ends exactly at the table. Either way the table itself must
	// fit inside the file.
	return table_end <= chd_size;
}

enumError ScanDKSoundBank (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *chd_data,
	size_t chd_size, const u8 *cbd_data, size_t cbd_size)
{
	if (entries)
		*entries = 0;
	if (n_entries)
		*n_entries = 0;
	if (!entries || !n_entries || !cbd_data || !IsDKSoundBank (chd_data, chd_size))
		return ERR_INVALID_DATA;

	const u32 declared_cbd_size = rd_be32 (chd_data + 12);
	if (declared_cbd_size != cbd_size)
		return ERR_INVALID_DATA;

	const u16 count = rd_be16 (chd_data + 8);
	nintendo_sarc_entry_t *out = CALLOC (count, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint out_cnt = 0;
	for (u16 i = 0; i < count; i++)
	{
		const u8 *e = chd_data + DKSB_BASE_HEADER_SIZE + (uint)i * DKSB_ENTRY_SIZE;

		const u32 cbd_off = rd_be32 (e + DKSB_E_OFFSET);
		const u32 nibble_count = rd_be32 (e + DKSB_E_NIBBLES);
		const u32 srate = rd_be32 (e + DKSB_E_SRATE);

		if (!nibble_count || srate < 4000 || srate > 96000)
			continue; // empty/invalid slot: skip, don't abort the whole bank

		const u32 byte_count = (nibble_count + 1) / 2;
		const u32 aligned = (byte_count + 31) & ~31u;
		if ((u64)cbd_off + aligned > cbd_size)
			continue; // declared range overruns the paired .cbd: skip

		const s64 sample_count = DspAdpcmNibblesToSamples (nibble_count);
		if (sample_count <= 0)
			continue;

		// Build a standard in-memory .dsp buffer (0x60-byte header + raw
		// ADPCM payload) so decoding can reuse DecodeDSPToWAV() verbatim
		// instead of reimplementing ADPCM decode here.
		const uint dsp_size = 0x60 + byte_count;
		u8 *dsp = CALLOC (1, dsp_size);
		if (!dsp)
			continue;

		wr_be32 (dsp, (u32)sample_count);
		wr_be32 (dsp + 4, nibble_count);
		wr_be32 (dsp + 8, srate);
		wr_be16 (dsp + 12, 0); // loop_flag: none of these clips loop
		wr_be16 (dsp + 14, 0); // format: 0 = ADPCM
		wr_be32 (dsp + 16, 0); // loop_start
		wr_be32 (dsp + 20, nibble_count > 0 ? nibble_count - 1 : 0); // loop_end
		wr_be32 (dsp + 24, 0);
		memcpy (dsp + 28, e + DKSB_E_COEF, 32); // coefficient table, already BE
		if (byte_count > 0)
			memcpy (dsp + 0x60, cbd_data + cbd_off, byte_count);

		u8 *wav = 0;
		size_t wav_size = 0;
		const enumError derr = DecodeDSPToWAV (dsp, dsp_size, &wav, &wav_size);
		FREE (dsp);
		if (derr || !wav)
		{
			FREE (wav);
			continue;
		}

		char name[32];
		snprintf (name, sizeof (name), "clip%03u.wav", i);
		OwnedEntryAdd (out, out_cnt++, name, wav, (uint)wav_size);
		FREE (wav);
	}

	*entries = out;
	*n_entries = out_cnt;
	return ERR_OK;
}

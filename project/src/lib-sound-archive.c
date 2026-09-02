#include "lib-std.h"
#include "lib-sound-archive.h"
#include <string.h>
#include <errno.h>

//-----------------------------------------------------------------------------
// Sound Archive Family Implementation (BCSAR, BFSAR, BCWAR, BFWAR, BCGRP, BFGRP)
//-----------------------------------------------------------------------------

void ResetSoundArchive (sound_archive_t *sar)
{
	if (!sar)
		return;
	if (sar->entries)
	{
		for (uint i = 0; i < sar->n_entries; i++)
			FREE (sar->entries[i].name);
		FREE (sar->entries);
	}
	memset (sar, 0, sizeof (*sar));
}

static inline u16 sar_r16 (const u8 *p, bool be)
{
	return be ? rd_be16 (p) : rd_le16 (p);
}
static inline u32 sar_r32 (const u8 *p, bool be)
{
	return be ? rd_be32 (p) : rd_le32 (p);
}
static inline s32 sar_rs32 (const u8 *p, bool be)
{
	return (s32)sar_r32 (p, be);
}
static inline void sar_w16 (u8 *p, u16 v, bool be)
{
	if (be)
		wr_be16 (p, v);
	else
		wr_le16 (p, v);
}
static inline void sar_w32 (u8 *p, u32 v, bool be)
{
	if (be)
		wr_be32 (p, v);
	else
		wr_le32 (p, v);
}

enumError ScanSoundArchive (sound_archive_t *sar, const u8 *data, size_t size)
{
	if (!sar || !data || size < 0x20)
		return EINVAL;

	memset (sar, 0, sizeof (*sar));
	sar->raw = data;
	sar->raw_size = size;

	memcpy (sar->magic, data, 4);
	sar->magic[4] = 0;

	bool is_csar = !memcmp (data, "CSAR", 4);
	bool is_fsar = !memcmp (data, "FSAR", 4);
	bool is_cwar = !memcmp (data, "CWAR", 4);
	bool is_fwar = !memcmp (data, "FWAR", 4);
	bool is_cgrp = !memcmp (data, "CGRP", 4);
	bool is_fgrp = !memcmp (data, "FGRP", 4);

	if (!is_csar && !is_fsar && !is_cwar && !is_fwar && !is_cgrp && !is_fgrp)
		return ERR_INVALID_DATA;

	sar->is_cafe_or_switch = (data[0] == 'F');
	u16 bom = rd_be16 (data + 4);
	sar->is_big_endian = (bom == 0xFEFF);
	bool be = sar->is_big_endian;

	sar->version = sar_r32 (data + 8, be);
	u32 file_size = sar_r32 (data + 0x0C, be);
	u16 num_blocks = sar_r16 (data + 0x10, be);

	if (num_blocks == 0 || num_blocks > 16 || file_size > size)
		return ERR_INVALID_DATA;

	// Scan block references
	u32 strg_off = 0, strg_size = 0;
	u32 info_off = 0, info_size = 0;
	u32 file_off = 0, file_size_block = 0;

	for (uint b = 0; b < num_blocks; b++)
	{
		const u8 *bp = data + 0x14 + b * 12;
		if (0x14 + b * 12 + 12 > size)
			break;
		u16 type_id = sar_r16 (bp, be);
		u32 boff = sar_r32 (bp + 4, be);
		u32 bsz = sar_r32 (bp + 8, be);

		if (boff + bsz <= size)
		{
			if (type_id == 0x2000 || !memcmp (data + boff, "STRG", 4))
			{
				strg_off = boff;
				strg_size = bsz;
			}
			else if (type_id == 0x2001 || type_id == 0x6800 || type_id == 0x7800
				|| !memcmp (data + boff, "INFO", 4))
			{
				info_off = boff;
				info_size = bsz;
			}
			else if (type_id == 0x2002 || type_id == 0x6801 || type_id == 0x7801
				|| !memcmp (data + boff, "FILE", 4))
			{
				file_off = boff;
				file_size_block = bsz;
			}
		}
	}

	if (!info_off || !file_off)
		return ERR_INVALID_DATA;

	const u8 *info_body = data + info_off + 8;
	const u8 *file_body = data + file_off + 8;
	size_t max_file_payload
		= (file_size_block > 8) ? (file_size_block - 8) : (size - (file_off + 8));

	// Handle CWAR / FWAR (Wave Archive)
	if (is_cwar || is_fwar)
	{
		if (info_size < 12)
			return ERR_INVALID_DATA;
		u32 n_waves = sar_r32 (info_body, be);
		if (n_waves > 0x10000)
			return ERR_INVALID_DATA;

		sar->n_entries = n_waves;
		sar->entries = CALLOC (n_waves, sizeof (sar_file_entry_t));
		for (uint i = 0; i < n_waves; i++)
		{
			const u8 *wp = info_body + 4 + i * 12;
			if (wp + 12 > data + size)
				break;
			sar->entries[i].file_id = i;
			sar->entries[i].offset = sar_r32 (wp + 4, be);
			sar->entries[i].size = sar_r32 (wp + 8, be);
			if (sar->entries[i].offset + sar->entries[i].size <= max_file_payload)
				sar->entries[i].data = file_body + sar->entries[i].offset;
			snprintf (
				sar->entries[i].ext, sizeof (sar->entries[i].ext), is_fwar ? ".bfwav" : ".bcwav");
		}
		return ERR_OK;
	}

	// Handle CGRP / FGRP (Group Archive)
	if (is_cgrp || is_fgrp)
	{
		if (info_size < 12)
			return ERR_INVALID_DATA;
		u32 n_files = sar_r32 (info_body, be);
		if (n_files > 0x10000)
			return ERR_INVALID_DATA;

		sar->n_entries = n_files;
		sar->entries = CALLOC (n_files, sizeof (sar_file_entry_t));
		for (uint i = 0; i < n_files; i++)
		{
			s32 ref_off = sar_rs32 (info_body + 4 + i * 8 + 4, be);
			if (ref_off < 0 || (uint)ref_off + 16 > info_size)
				continue;
			const u8 *ep = info_body + ref_off;
			u32 fid = sar_r32 (ep, be);
			u32 foff = sar_r32 (ep + 8, be);
			u32 fsz = sar_r32 (ep + 12, be);

			sar->entries[i].file_id = fid;
			sar->entries[i].offset = foff;
			sar->entries[i].size = fsz;
			if (foff + fsz <= max_file_payload)
			{
				sar->entries[i].data = file_body + foff;
				const u8 *d = sar->entries[i].data;
				if (fsz >= 4)
				{
					if (!memcmp (d, "CSEQ", 4))
						strcpy (sar->entries[i].ext, ".bcseq");
					else if (!memcmp (d, "FSEQ", 4))
						strcpy (sar->entries[i].ext, ".bfseq");
					else if (!memcmp (d, "CBNK", 4))
						strcpy (sar->entries[i].ext, ".bcbnk");
					else if (!memcmp (d, "FBNK", 4))
						strcpy (sar->entries[i].ext, ".bfbnk");
					else if (!memcmp (d, "CWAR", 4))
						strcpy (sar->entries[i].ext, ".bcwar");
					else if (!memcmp (d, "FWAR", 4))
						strcpy (sar->entries[i].ext, ".bfwar");
					else if (!memcmp (d, "CWSD", 4))
						strcpy (sar->entries[i].ext, ".bcwsd");
					else if (!memcmp (d, "FWSD", 4))
						strcpy (sar->entries[i].ext, ".bfwsd");
					else if (!memcmp (d, "CWAV", 4))
						strcpy (sar->entries[i].ext, ".bcwav");
					else if (!memcmp (d, "FWAV", 4))
						strcpy (sar->entries[i].ext, ".bfwav");
					else
						strcpy (sar->entries[i].ext, ".bin");
				}
				else
					strcpy (sar->entries[i].ext, ".bin");
			}
		}
		return ERR_OK;
	}

	// Handle CSAR / BFSAR (Sound Archive)
	// 1. Read String Table if present
	char **string_pool = 0;
	uint n_strings = 0;
	if (strg_off && strg_size > 16)
	{
		const u8 *strg_body = data + strg_off + 8;
		s32 str_tab_off = sar_rs32 (strg_body + 4, be);
		if (str_tab_off >= 0 && (uint)str_tab_off + 4 <= strg_size)
		{
			const u8 *str_tab = strg_body + str_tab_off;
			n_strings = sar_r32 (str_tab, be);
			if (n_strings > 0 && n_strings < 0x20000)
			{
				string_pool = CALLOC (n_strings, sizeof (char *));
				for (uint s = 0; s < n_strings; s++)
				{
					s32 s_off = sar_rs32 (str_tab + 4 + s * 8 + 4, be);
					if (s_off >= 0 && (uint)s_off < strg_size)
					{
						ccp str_ptr = (ccp)(str_tab + s_off);
						if (str_ptr < (ccp)(data + strg_off + strg_size))
							string_pool[s] = STRDUP (str_ptr);
					}
				}
			}
		}
	}

	// 2. Read INFO Block Section 6 (File Table)
	s32 file_tab_off = sar_rs32 (info_body + 6 * 8 + 4, be);
	if (file_tab_off < 0 || (uint)file_tab_off + 4 > info_size)
	{
		if (string_pool)
		{
			for (uint s = 0; s < n_strings; s++)
				FREE (string_pool[s]);
			FREE (string_pool);
		}
		return ERR_INVALID_DATA;
	}

	const u8 *file_tab = info_body + file_tab_off;
	u32 n_files = sar_r32 (file_tab, be);
	if (n_files > 0x20000)
	{
		if (string_pool)
		{
			for (uint s = 0; s < n_strings; s++)
				FREE (string_pool[s]);
			FREE (string_pool);
		}
		return ERR_INVALID_DATA;
	}

	sar->n_entries = n_files;
	sar->entries = CALLOC (n_files, sizeof (sar_file_entry_t));

	for (uint i = 0; i < n_files; i++)
	{
		sar->entries[i].file_id = i;
		s32 ref_off = sar_rs32 (file_tab + 4 + i * 8 + 4, be);
		if (ref_off < 0 || (uint)ref_off + 8 > info_size)
			continue;
		const u8 *fe = file_tab + ref_off;
		u16 loc_type = sar_r16 (fe, be);
		s32 loc_off = sar_rs32 (fe + 4, be);

		if (loc_type == 0x220c && loc_off >= 0 && (uint)loc_off + 12 <= info_size) // Internal
		{
			const u8 *ib = fe + loc_off;
			u32 foff = sar_r32 (ib + 4, be);
			u32 fsz = sar_r32 (ib + 8, be);
			sar->entries[i].offset = foff;
			sar->entries[i].size = fsz;
			if (foff + fsz <= max_file_payload)
				sar->entries[i].data = file_body + foff;
		}
	}

	// 3. Associate Symbol Names and Extensions
	// Check Sounds table (Section 0: 0x2100)
	s32 snd_tab_off = sar_rs32 (info_body + 0 * 8 + 4, be);
	if (snd_tab_off >= 0 && (uint)snd_tab_off + 4 <= info_size)
	{
		const u8 *snd_tab = info_body + snd_tab_off;
		u32 n_snds = sar_r32 (snd_tab, be);
		for (uint s = 0; s < n_snds && s < 0x20000; s++)
		{
			s32 ref_off = sar_rs32 (snd_tab + 4 + s * 8 + 4, be);
			if (ref_off < 0 || (uint)ref_off + 16 > info_size)
				continue;
			const u8 *sp = snd_tab + ref_off;
			u32 fid = sar_r32 (sp, be);
			if (fid < sar->n_entries)
			{
				// BitFlags at sp + 0x14
				u32 flag_bits = sar_r32 (sp + 0x14, be);
				if ((flag_bits & 1) && string_pool)
				{
					u32 s_idx = sar_r32 (sp + 0x18, be);
					if (s_idx < n_strings && string_pool[s_idx] && !sar->entries[fid].name)
						sar->entries[fid].name = STRDUP (string_pool[s_idx]);
				}
			}
		}
	}

	// Check Banks table (Section 1: 0x2101)
	s32 bnk_tab_off = sar_rs32 (info_body + 1 * 8 + 4, be);
	if (bnk_tab_off >= 0 && (uint)bnk_tab_off + 4 <= info_size)
	{
		const u8 *bnk_tab = info_body + bnk_tab_off;
		u32 n_bnks = sar_r32 (bnk_tab, be);
		for (uint b = 0; b < n_bnks && b < 0x20000; b++)
		{
			s32 ref_off = sar_rs32 (bnk_tab + 4 + b * 8 + 4, be);
			if (ref_off < 0 || (uint)ref_off + 12 > info_size)
				continue;
			const u8 *bp = bnk_tab + ref_off;
			u32 fid = sar_r32 (bp, be);
			if (fid < sar->n_entries)
			{
				u32 flag_bits = sar_r32 (bp + 0x08, be);
				if ((flag_bits & 1) && string_pool)
				{
					u32 s_idx = sar_r32 (bp + 0x0C, be);
					if (s_idx < n_strings && string_pool[s_idx] && !sar->entries[fid].name)
						sar->entries[fid].name = STRDUP (string_pool[s_idx]);
				}
			}
		}
	}

	// Check Wave Archive table (Section 3: 0x2103)
	s32 war_tab_off = sar_rs32 (info_body + 3 * 8 + 4, be);
	if (war_tab_off >= 0 && (uint)war_tab_off + 4 <= info_size)
	{
		const u8 *war_tab = info_body + war_tab_off;
		u32 n_wars = sar_r32 (war_tab, be);
		for (uint w = 0; w < n_wars && w < 0x20000; w++)
		{
			s32 ref_off = sar_rs32 (war_tab + 4 + w * 8 + 4, be);
			if (ref_off < 0 || (uint)ref_off + 12 > info_size)
				continue;
			const u8 *wp = war_tab + ref_off;
			u32 fid = sar_r32 (wp, be);
			if (fid < sar->n_entries)
			{
				u32 flag_bits = sar_r32 (wp + 0x08, be);
				if ((flag_bits & 1) && string_pool)
				{
					u32 s_idx = sar_r32 (wp + 0x0C, be);
					if (s_idx < n_strings && string_pool[s_idx] && !sar->entries[fid].name)
						sar->entries[fid].name = STRDUP (string_pool[s_idx]);
				}
			}
		}
	}

	// Check Group table (Section 5: 0x2105)
	s32 grp_tab_off = sar_rs32 (info_body + 5 * 8 + 4, be);
	if (grp_tab_off >= 0 && (uint)grp_tab_off + 4 <= info_size)
	{
		const u8 *grp_tab = info_body + grp_tab_off;
		u32 n_grps = sar_r32 (grp_tab, be);
		for (uint g = 0; g < n_grps && g < 0x20000; g++)
		{
			s32 ref_off = sar_rs32 (grp_tab + 4 + g * 8 + 4, be);
			if (ref_off < 0 || (uint)ref_off + 8 > info_size)
				continue;
			const u8 *gp = grp_tab + ref_off;
			u32 fid = sar_r32 (gp, be);
			if (fid < sar->n_entries)
			{
				u32 flag_bits = sar_r32 (gp + 0x04, be);
				if ((flag_bits & 1) && string_pool)
				{
					u32 s_idx = sar_r32 (gp + 0x08, be);
					if (s_idx < n_strings && string_pool[s_idx] && !sar->entries[fid].name)
						sar->entries[fid].name = STRDUP (string_pool[s_idx]);
				}
			}
		}
	}

	// 4. Sniff extensions for each file
	for (uint i = 0; i < sar->n_entries; i++)
	{
		if (sar->entries[i].data && sar->entries[i].size >= 4)
		{
			const u8 *d = sar->entries[i].data;
			if (!memcmp (d, "CSEQ", 4))
				strcpy (sar->entries[i].ext, ".bcseq");
			else if (!memcmp (d, "FSEQ", 4))
				strcpy (sar->entries[i].ext, ".bfseq");
			else if (!memcmp (d, "CBNK", 4))
				strcpy (sar->entries[i].ext, ".bcbnk");
			else if (!memcmp (d, "FBNK", 4))
				strcpy (sar->entries[i].ext, ".bfbnk");
			else if (!memcmp (d, "CWAR", 4))
				strcpy (sar->entries[i].ext, ".bcwar");
			else if (!memcmp (d, "FWAR", 4))
				strcpy (sar->entries[i].ext, ".bfwar");
			else if (!memcmp (d, "CWSD", 4))
				strcpy (sar->entries[i].ext, ".bcwsd");
			else if (!memcmp (d, "FWSD", 4))
				strcpy (sar->entries[i].ext, ".bfwsd");
			else if (!memcmp (d, "CGRP", 4))
				strcpy (sar->entries[i].ext, ".bcgrp");
			else if (!memcmp (d, "FGRP", 4))
				strcpy (sar->entries[i].ext, ".bfgrp");
			else if (!memcmp (d, "CWAV", 4))
				strcpy (sar->entries[i].ext, ".bcwav");
			else if (!memcmp (d, "FWAV", 4))
				strcpy (sar->entries[i].ext, ".bfwav");
			else if (!memcmp (d, "CSTM", 4))
				strcpy (sar->entries[i].ext, ".bcstm");
			else if (!memcmp (d, "FSTM", 4))
				strcpy (sar->entries[i].ext, ".bfstm");
			else
				strcpy (sar->entries[i].ext, ".bin");
		}
		else
			strcpy (sar->entries[i].ext, ".bin");
	}

	if (string_pool)
	{
		for (uint s = 0; s < n_strings; s++)
			FREE (string_pool[s]);
		FREE (string_pool);
	}

	return ERR_OK;
}

// Decode RWAV (Wii)/FWAV (Wii U/Switch)/CWAV (3DS) NintendoWare wave audio
// to a standard PCM WAV file. Layout verified byte-for-byte against a real
// retail BFWAV (Super Mario 64 [NABE01] wiiu_shared.bfsar, wave 0000: PCM16
// mono/32000Hz/436 samples) and cross-checked against vgmstream's bfwav.c
// (RWAV/FWAV/CWAV share the INFO/DATA block scheme; only RWAV's INFO layout
// and the FWAV/CWAV block-table position differ -- see per-type branches
// below). ADPCM decode starts from zero history every call (matches
// vgmstream, which also never restores loop-point history for this format
// family), so a looped ADPCM stream's post-loop samples will drift slightly
// from a hardware decode; PCM8/PCM16 and the non-looping case are exact.
enumError DecodeBXWAV (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 0x20)
		return EINVAL;
	*dest = 0;
	*dest_size = 0;

	enum
	{
		T_RWAV,
		T_FWAV,
		T_CWAV
	} type;
	if (!memcmp (src, "RWAV", 4))
		type = T_RWAV;
	else if (!memcmp (src, "FWAV", 4))
		type = T_FWAV;
	else if (!memcmp (src, "CWAV", 4))
		type = T_CWAV;
	else
		return EINVAL;

	bool be;
	if (rd_be16 (src + 4) == 0xFEFF)
		be = true;
	else if (rd_le16 (src + 4) == 0xFEFF)
		be = false;
	else
		return EINVAL;
#define BW16(off) (be ? rd_be16 (src + (off)) : rd_le16 (src + (off)))
#define BW32(off) (be ? rd_be32 (src + (off)) : rd_le32 (src + (off)))

	u32 file_size, info_off, data_off;
	if (type == T_RWAV)
	{
		if (src_size < 0x20)
			return EINVAL;
		file_size = BW32 (0x08);
		info_off = BW32 (0x10);
		data_off = BW32 (0x18);
	}
	else
	{
		if (src_size < 0x2C)
			return EINVAL;
		file_size = BW32 (0x0C);
		info_off = BW32 (0x18);
		data_off = BW32 (0x24);
	}
	if (file_size > src_size || file_size < 0x20)
		return EINVAL;
	if ((u64)info_off + 0x20 > src_size || (u64)data_off + 8 > src_size)
		return EINVAL;
	if (memcmp (src + info_off, "INFO", 4) || memcmp (src + data_off, "DATA", 4))
		return EINVAL;

	u8 codec, loop_flag;
	u32 sample_rate;
	s32 num_samples;
	u32 chtb_off;
	if (type == T_RWAV)
	{
		if (info_off + 0x1C > src_size)
			return EINVAL;
		codec = src[info_off + 0x08];
		loop_flag = src[info_off + 0x09];
		u8 channels_hdr = src[info_off + 0x0A];
		(void)channels_hdr; // channel count is re-derived from the channel table below
		sample_rate = BW16 (info_off + 0x0C);
		num_samples = (s32)BW32 (info_off + 0x14);
		chtb_off = BW32 (info_off + 0x18) + info_off + 0x08;
		// RWAV loop/sample counts are in DSP nibble units (2 nibbles/byte,
		// 1 header nibble per 8-sample frame): samples = (n/16)*14 + n%16 - 2
		num_samples = (num_samples / 16) * 14 + num_samples % 16 - 2;
	}
	else
	{
		if (info_off + 0x20 > src_size)
			return EINVAL;
		codec = src[info_off + 0x08];
		loop_flag = src[info_off + 0x09];
		sample_rate = BW32 (info_off + 0x0C);
		num_samples = (s32)BW32 (info_off + 0x14);
		chtb_off = info_off + 0x1C;
	}
	(void)loop_flag;
	if (num_samples < 0 || (u64)chtb_off + 4 > src_size)
		return EINVAL;

	u32 channels;
	if (type == T_RWAV)
	{
		// Channel table for RWAV has no explicit count field; derive it from
		// the channel-table size stored right before it (already consumed
		// above via chtb_off), so fall back to the header byte instead.
		channels = src[info_off + 0x0A];
	}
	else
	{
		channels = BW32 (chtb_off + 0x00);
	}
	if (channels == 0 || channels > 8)
		return EINVAL;

	if (codec > 3)
		return ERROR0 (ERR_WRONG_FILE_TYPE, "BXWAV: unsupported codec 0x%02x\n", codec);

	// Resolve each channel's sample-data offset (and DSP coefs, if ADPCM).
	u32 ch_data_off[8];
	s16 ch_coef[8][16];
	for (uint ch = 0; ch < channels; ch++)
	{
		u32 chnf_off, coef_off = 0;
		bool has_adpcm = false;
		if (type == T_RWAV)
		{
			if ((u64)chtb_off + ch * 4 + 4 > src_size)
				return EINVAL;
			chnf_off = BW32 (chtb_off + ch * 4) + info_off + 0x08;
			if ((u64)chnf_off + 8 > src_size)
				return EINVAL;
			ch_data_off[ch] = BW32 (chnf_off + 0x00) + data_off + 8;
			u32 adpcm_ref = BW32 (chnf_off + 0x04);
			if (adpcm_ref != 0xFFFFFFFF)
			{
				coef_off = adpcm_ref + info_off + 0x08;
				has_adpcm = true;
			}
		}
		else
		{
			if ((u64)chtb_off + 4 + ch * 8 + 8 > src_size)
				return EINVAL;
			chnf_off = BW32 (chtb_off + 4 + ch * 8 + 4) + chtb_off;
			if ((u64)chnf_off + 0x10 > src_size)
				return EINVAL;
			if ((BW16 (chnf_off + 0x00) & 0x1F00) != 0x1F00)
				return EINVAL;
			ch_data_off[ch] = BW32 (chnf_off + 0x04) + data_off + 8;
			u32 adpcm_ref = BW32 (chnf_off + 0x0C);
			if (adpcm_ref != 0xFFFFFFFF)
			{
				coef_off = adpcm_ref + chnf_off;
				has_adpcm = true;
			}
		}
		if (codec == 2) // DSP-ADPCM: coefs are mandatory
		{
			if (!has_adpcm || (u64)coef_off + 0x20 > src_size)
				return EINVAL;
			for (uint i = 0; i < 16; i++)
				ch_coef[ch][i] = (s16)BW16 (coef_off + i * 2);
		}
		else if (codec == 3) // IMA-ADPCM: hist1(s16) + step_index(s16) seed
		{
			if (!has_adpcm || (u64)coef_off + 4 > src_size)
				return EINVAL;
			ch_coef[ch][0] = (s16)BW16 (coef_off + 0); // initial history
			ch_coef[ch][1] = (s16)BW16 (coef_off + 2); // initial step index
		}
	}

	// Decode every channel to 16-bit signed PCM (or keep PCM8 native for the
	// 8-bit case) into a planar buffer, then interleave into the WAV body.
	uint bytes_per_sample = codec == 0 ? 1 : 2;
	if ((u64)num_samples * channels * bytes_per_sample > NFMT_MAX_OUTPUT)
		return ERR_FILE_TOO_BIG;

	u16 *pcm16[8] = { 0 };
	u8 *pcm8[8] = { 0 };
	enumError err = ERR_OK;
	for (uint ch = 0; ch < channels && !err; ch++)
	{
		switch (codec)
		{
			case 0: // PCM8 (unsigned in the WAV convention, signed on disk)
			{
				if ((u64)ch_data_off[ch] + num_samples > src_size)
				{
					err = EINVAL;
					break;
				}
				pcm8[ch] = MALLOC (num_samples);
				for (s32 i = 0; i < num_samples; i++)
					pcm8[ch][i] = (u8)(src[ch_data_off[ch] + i] ^ 0x80);
				break;
			}
			case 1: // PCM16
			{
				if ((u64)ch_data_off[ch] + (u64)num_samples * 2 > src_size)
				{
					err = EINVAL;
					break;
				}
				pcm16[ch] = MALLOC (num_samples * 2);
				for (s32 i = 0; i < num_samples; i++)
					pcm16[ch][i] = BW16 (ch_data_off[ch] + i * 2);
				break;
			}
			case 2: // Standard GC/Wii/3DS DSP-ADPCM: 8-byte frames, 14 samples each
			{
				pcm16[ch] = MALLOC ((size_t)num_samples * 2 + 2);
				s16 hist1 = 0, hist2 = 0;
				u32 src_off = ch_data_off[ch];
				s32 done = 0;
				while (done < num_samples)
				{
					if (src_off + 8 > src_size)
					{
						err = EINVAL;
						break;
					}
					u8 frame_hdr = src[src_off];
					s32 scale = 1 << (frame_hdr & 0x0F);
					uint coef_idx = frame_hdr >> 4;
					if (coef_idx > 7)
					{
						err = EINVAL;
						break;
					}
					s16 c1 = ch_coef[ch][coef_idx * 2], c2 = ch_coef[ch][coef_idx * 2 + 1];
					for (uint i = 0; i < 14 && done < num_samples; i++, done++)
					{
						u8 byte = src[src_off + 1 + (i >> 1)];
						u8 nib = (i & 1) ? (byte & 0x0F) : (byte >> 4);
						s8 snib = (s8)(nib << 4) >> 4;
						s32 raw = (((s32)snib * scale) << 11) + 1024 + (c1 * hist1 + c2 * hist2);
						raw >>= 11;
						s16 sample = raw > 0x7FFF ? 0x7FFF : raw < -0x8000 ? -0x8000 : (s16)raw;
						pcm16[ch][done] = (u16)sample;
						hist2 = hist1;
						hist1 = sample;
					}
					src_off += 8;
				}
				break;
			}
			case 3: // IMA-ADPCM (mono-style, per channel): hist16 + step_index seed, 4-bit nibbles
			{
				static const int ima_index_table[16]
					= { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
				static const int ima_step_table[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21,
					23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
					143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598,
					658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
					2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
					8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
					24623, 27086, 29794, 32767 };
				uint bytes_needed = (num_samples + 1) / 2;
				if ((u64)ch_data_off[ch] + bytes_needed > src_size)
				{
					err = EINVAL;
					break;
				}
				pcm16[ch] = MALLOC ((size_t)num_samples * 2 + 2);
				s32 predictor = ch_coef[ch][0];
				int step_index = ch_coef[ch][1];
				if (step_index < 0)
					step_index = 0;
				if (step_index > 88)
					step_index = 88;
				u32 src_off = ch_data_off[ch];
				for (s32 i = 0; i < num_samples; i++)
				{
					u8 byte = src[src_off + (i >> 1)];
					uint nib = (i & 1) ? (byte >> 4) : (byte & 0x0F);
					int step = ima_step_table[step_index];
					int diff = step >> 3;
					if (nib & 1)
						diff += step >> 2;
					if (nib & 2)
						diff += step >> 1;
					if (nib & 4)
						diff += step;
					if (nib & 8)
						predictor -= diff;
					else
						predictor += diff;
					predictor = predictor > 32767 ? 32767 : predictor < -32768 ? -32768 : predictor;
					step_index += ima_index_table[nib];
					step_index = step_index < 0 ? 0 : step_index > 88 ? 88 : step_index;
					pcm16[ch][i] = (u16)(s16)predictor;
				}
				break;
			}
			default:
				err = ERROR0 (ERR_WRONG_FILE_TYPE, "BXWAV: unsupported codec 0x%02x\n", codec);
		}
	}

	if (err)
	{
		for (uint ch = 0; ch < channels; ch++)
		{
			FREE (pcm16[ch]);
			FREE (pcm8[ch]);
		}
		return err;
	}

	// Build a canonical 44-byte-header PCM WAV.
	uint bits = bytes_per_sample * 8;
	uint block_align = channels * bytes_per_sample;
	uint data_size = (uint)num_samples * block_align;
	uint total = 44 + data_size;
	u8 *out = MALLOC (total);
	memcpy (out + 0, "RIFF", 4);
	wr_le32 (out + 4, total - 8);
	memcpy (out + 8, "WAVE", 4);
	memcpy (out + 12, "fmt ", 4);
	wr_le32 (out + 16, 16);
	wr_le16 (out + 20, 1); // PCM
	wr_le16 (out + 22, (u16)channels);
	wr_le32 (out + 24, sample_rate);
	wr_le32 (out + 28, sample_rate * block_align);
	wr_le16 (out + 32, (u16)block_align);
	wr_le16 (out + 34, (u16)bits);
	memcpy (out + 36, "data", 4);
	wr_le32 (out + 40, data_size);

	u8 *dp = out + 44;
	for (s32 i = 0; i < num_samples; i++)
		for (uint ch = 0; ch < channels; ch++)
		{
			if (codec == 0)
			{
				*dp++ = pcm8[ch][i];
			}
			else
			{
				wr_le16 (dp, pcm16[ch][i]);
				dp += 2;
			}
		}

	for (uint ch = 0; ch < channels; ch++)
	{
		FREE (pcm16[ch]);
		FREE (pcm8[ch]);
	}
	*dest = out;
	*dest_size = total;
#undef BW16
#undef BW32
	return ERR_OK;
}

enumError CreateSoundArchive (u8 **dest, uint *dest_size, const sound_archive_t *sar)
{
	if (!dest || !dest_size || !sar || sar->n_entries == 0)
		return EINVAL;

	bool be = sar->is_big_endian;
	bool is_cafe = sar->is_cafe_or_switch;
	bool is_war = !strcmp (sar->magic, "CWAR") || !strcmp (sar->magic, "FWAR");
	bool is_grp = !strcmp (sar->magic, "CGRP") || !strcmp (sar->magic, "FGRP");

	// Calculate file offsets within FILE block body (aligned to 32 bytes)
	u32 *file_offsets = CALLOC (sar->n_entries, sizeof (u32));
	u32 cur_file_offset = 0;
	for (uint i = 0; i < sar->n_entries; i++)
	{
		file_offsets[i] = cur_file_offset;
		cur_file_offset += (sar->entries[i].size + 0x1F) & ~0x1F;
	}
	u32 file_payload_size = cur_file_offset;
	u32 file_block_size = 8 + file_payload_size;

	// Handle Wave Archive (CWAR / FWAR)
	if (is_war)
	{
		u32 info_payload_size = 4 + sar->n_entries * 12;
		u32 info_block_size = (8 + info_payload_size + 0x1F) & ~0x1F;
		u32 header_size = 0x20;
		u32 total_size = header_size + info_block_size + file_block_size;

		u8 *out = CALLOC (total_size, 1);

		// Header
		memcpy (out, is_cafe ? "FWAR" : "CWAR", 4);
		sar_w16 (out + 4, be ? 0xFEFF : 0xFFFE, true);
		sar_w16 (out + 6, (u16)header_size, be);
		sar_w32 (out + 8, sar->version ? sar->version : (is_cafe ? 0x00010000 : 0x01000000), be);
		sar_w32 (out + 0x0C, total_size, be);
		sar_w16 (out + 0x10, 2, be); // 2 blocks (INFO, FILE)

		// Block 0: INFO
		sar_w16 (out + 0x14, 0x6800, be);
		sar_w32 (out + 0x14 + 4, header_size, be);
		sar_w32 (out + 0x14 + 8, info_block_size, be);

		// Block 1: FILE
		sar_w16 (out + 0x20, 0x6801, be);
		sar_w32 (out + 0x20 + 4, header_size + info_block_size, be);
		sar_w32 (out + 0x20 + 8, file_block_size, be);

		// INFO block body
		u8 *inf = out + header_size;
		memcpy (inf, "INFO", 4);
		sar_w32 (inf + 4, info_block_size, be);
		sar_w32 (inf + 8, sar->n_entries, be);
		for (uint i = 0; i < sar->n_entries; i++)
		{
			u8 *wp = inf + 12 + i * 12;
			sar_w16 (wp, 0x1F00, be);
			sar_w32 (wp + 4, file_offsets[i], be);
			sar_w32 (wp + 8, sar->entries[i].size, be);
		}

		// FILE block body
		u8 *fil = out + header_size + info_block_size;
		memcpy (fil, "FILE", 4);
		sar_w32 (fil + 4, file_block_size, be);
		for (uint i = 0; i < sar->n_entries; i++)
		{
			if (sar->entries[i].data && sar->entries[i].size)
				memcpy (fil + 8 + file_offsets[i], sar->entries[i].data, sar->entries[i].size);
		}

		FREE (file_offsets);
		*dest = out;
		*dest_size = total_size;
		return ERR_OK;
	}

	// Handle Full Sound Archive (CSAR / BFSAR / CGRP / BFGRP)
	// 1. Build STRG block
	uint n_strings = 0;
	for (uint i = 0; i < sar->n_entries; i++)
		if (sar->entries[i].name && *sar->entries[i].name)
			n_strings++;

	u32 strg_table_size = 4 + n_strings * 8;
	u32 strg_strings_size = 0;
	for (uint i = 0; i < sar->n_entries; i++)
		if (sar->entries[i].name && *sar->entries[i].name)
			strg_strings_size += (u32)strlen (sar->entries[i].name) + 1;

	u32 strg_payload_size = 16 + strg_table_size + strg_strings_size;
	u32 strg_block_size = (8 + strg_payload_size + 0x1F) & ~0x1F;
	if (strg_block_size < 0x20)
		strg_block_size = 0x20;

	// 2. Build INFO block
	// Header has 7 references (0 to 6) = 56 bytes
	// File table at offset 56: 4 bytes count, n_entries * 8 bytes refs, n_entries * 24 bytes file
	// entries
	u32 file_tab_size = 4 + sar->n_entries * 8 + sar->n_entries * 24;
	u32 info_payload_size = 56 + file_tab_size;
	u32 info_block_size = (8 + info_payload_size + 0x1F) & ~0x1F;

	u32 header_size = 0x40;
	u32 total_size = header_size + strg_block_size + info_block_size + file_block_size;

	u8 *out = CALLOC (total_size, 1);

	// Header
	const char *hdr_magic = is_grp ? (is_cafe ? "FGRP" : "CGRP") : (is_cafe ? "FSAR" : "CSAR");
	memcpy (out, hdr_magic, 4);
	sar_w16 (out + 4, be ? 0xFEFF : 0xFFFE, true);
	sar_w16 (out + 6, (u16)header_size, be);
	sar_w32 (out + 8, sar->version ? sar->version : (is_cafe ? 0x00020200 : 0x02000000), be);
	sar_w32 (out + 0x0C, total_size, be);
	sar_w16 (out + 0x10, 3, be); // 3 blocks (STRG, INFO, FILE)

	// SizedReference 0: STRG
	sar_w16 (out + 0x14, 0x2000, be);
	sar_w32 (out + 0x14 + 4, header_size, be);
	sar_w32 (out + 0x14 + 8, strg_block_size, be);

	// SizedReference 1: INFO
	sar_w16 (out + 0x20, 0x2001, be);
	sar_w32 (out + 0x20 + 4, header_size + strg_block_size, be);
	sar_w32 (out + 0x20 + 8, info_block_size, be);

	// SizedReference 2: FILE
	sar_w16 (out + 0x2C, 0x2002, be);
	sar_w32 (out + 0x2C + 4, header_size + strg_block_size + info_block_size, be);
	sar_w32 (out + 0x2C + 8, file_block_size, be);

	// STRG block
	u8 *stb = out + header_size;
	memcpy (stb, "STRG", 4);
	sar_w32 (stb + 4, strg_block_size, be);
	sar_w16 (stb + 8, 0x2400, be); // Reference to String Table
	sar_w32 (stb + 8 + 4, 16, be); // String table starts at offset 16 from STRG body
	sar_w16 (stb + 16, 0x2401, be); // Patricia tree (null ref = -1)
	sar_w32 (stb + 16 + 4, 0xFFFFFFFF, be);

	u8 *str_tab = stb + 8 + 16;
	sar_w32 (str_tab, n_strings, be);
	u32 cur_str_off = strg_table_size;
	uint s_idx = 0;
	for (uint i = 0; i < sar->n_entries; i++)
	{
		if (sar->entries[i].name && *sar->entries[i].name)
		{
			u8 *sref = str_tab + 4 + s_idx * 8;
			sar_w16 (sref, 0x1F01, be);
			sar_w32 (sref + 4, cur_str_off, be);
			size_t slen = strlen (sar->entries[i].name);
			memcpy (str_tab + cur_str_off, sar->entries[i].name, slen + 1);
			cur_str_off += (u32)slen + 1;
			s_idx++;
		}
	}

	// INFO block
	u8 *inf = out + header_size + strg_block_size;
	memcpy (inf, "INFO", 4);
	sar_w32 (inf + 4, info_block_size, be);

	// Set null references for Sections 0..5 (-1 offset)
	for (uint sec = 0; sec < 6; sec++)
	{
		sar_w16 (inf + 8 + sec * 8, 0x2100 + sec, be);
		sar_w32 (inf + 8 + sec * 8 + 4, 0xFFFFFFFF, be);
	}

	// Section 6: File Table
	sar_w16 (inf + 8 + 6 * 8, 0x2106, be);
	sar_w32 (inf + 8 + 6 * 8 + 4, 56, be); // starts right after the 7 references

	u8 *ftab = inf + 8 + 56;
	sar_w32 (ftab, sar->n_entries, be);
	u32 cur_entry_rel = 4 + sar->n_entries * 8;
	for (uint i = 0; i < sar->n_entries; i++)
	{
		u8 *fref = ftab + 4 + i * 8;
		sar_w16 (fref, 0x220A, be); // SAR_Info_File
		sar_w32 (fref + 4, cur_entry_rel, be);

		u8 *fe = ftab + cur_entry_rel;
		sar_w16 (fe, 0x220C, be); // SAR_Info_InternalFile
		sar_w32 (fe + 4, 12, be); // internal struct at +12

		u8 *fib = fe + 12;
		sar_w16 (fib, 0x1F00, be);
		sar_w32 (fib + 4, file_offsets[i], be);
		sar_w32 (fib + 8, sar->entries[i].size, be);

		cur_entry_rel += 24;
	}

	// FILE block
	u8 *fil = out + header_size + strg_block_size + info_block_size;
	memcpy (fil, "FILE", 4);
	sar_w32 (fil + 4, file_block_size, be);
	for (uint i = 0; i < sar->n_entries; i++)
	{
		if (sar->entries[i].data && sar->entries[i].size)
			memcpy (fil + 8 + file_offsets[i], sar->entries[i].data, sar->entries[i].size);
	}

	FREE (file_offsets);
	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		Arika INFO.DAT / GAME.DAT archives		///////////////
///////////////////////////////////////////////////////////////////////////////

// See the header comment above ExtractArika() for the full layout. Ported
// from GBATEK's "DS Encrypted Arika Archives with ALZ1 compression" page
// plus aluigi's arika.bms/endless_ocean.bms (for the RF2 sub-container and
// the older "ZALZ"-tagged variant, neither of which GBATEK documents).

enumError DecryptArikaInfo (u8 *buf, uint size)
{
	if (!buf)
		return EINVAL;
	if (size < 0x10 || buf[0] == 0)
		return ERR_OK; // already unencrypted
	u8 key[0x10];
	memcpy (key, buf, 0x10);
	for (uint i = 0x10; i < size; i++)
	{
		u8 b = buf[i];
		b = (u8)(b >> 4 | b << 4); // ror 4 (== rol 4 on a byte)
		b ^= 0xff;
		b = (u8)(b - key[i & 0xf]);
		buf[i] = b;
	}
	return ERR_OK;
}

enumError EncryptArikaInfo (u8 *buf, uint size)
{
	if (!buf)
		return EINVAL;
	if (size < 0x10 || buf[0] == 0)
		return ERR_OK; // stays unencrypted
	u8 key[0x10];
	memcpy (key, buf, 0x10);
	for (uint i = 0x10; i < size; i++)
	{
		u8 b = buf[i];
		b = (u8)(b + key[i & 0xf]);
		b ^= 0xff;
		b = (u8)(b >> 4 | b << 4); // rol 4 (== ror 4 on a byte)
		buf[i] = b;
	}
	return ERR_OK;
}

enumError DecodeALZ1 (u8 *dest, uint dest_size, const u8 *src, uint src_size)
{
	if (!dest && dest_size)
		return EINVAL;
	if (!src && src_size)
		return EINVAL;
	u8 ring[0x1000];
	memset (ring, 0, sizeof (ring));
	uint p = 0xfee;
	uint sp = 0, dp = 0;
	uint flags = 0, flag_bits = 0;

	while (dp < dest_size)
	{
		if (!flag_bits)
		{
			if (sp >= src_size)
				return EINVAL;
			flags = src[sp++];
			flag_bits = 8;
		}
		const bool literal = flags & 1;
		flags >>= 1;
		flag_bits--;

		if (literal)
		{
			if (sp >= src_size)
				return EINVAL;
			const u8 b = src[sp++];
			dest[dp++] = b;
			ring[p & 0xfff] = b;
			p++;
		}
		else
		{
			if (sp + 2 > src_size)
				return EINVAL;
			const u8 b0 = src[sp], b1 = src[sp + 1];
			sp += 2;
			uint q = b0 | (uint)(b1 >> 4) << 8;
			uint len = (b1 & 0xf) + 3;
			for (uint i = 0; i < len && dp < dest_size; i++)
			{
				const u8 b = ring[q & 0xfff];
				dest[dp++] = b;
				ring[p & 0xfff] = b;
				p++;
				q++;
			}
		}
	}
	return ERR_OK;
}

enumError EncodeALZ1 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size)
		return EINVAL;
	if (!src && src_size)
		return EINVAL;

	// Worst case: every byte literal -> one flag bit + one byte per byte,
	// flags reloaded every 8 tokens.
	const uint capacity = src_size + (src_size + 7) / 8 + 8;
	u8 *out = MALLOC (capacity ? capacity : 1);
	if (!out)
		return ERR_CANT_CREATE;

	uint dp = 0, sp = 0;
	while (sp < src_size)
	{
		const uint flag_pos = dp++;
		u8 flag = 0;
		for (uint bit = 0; bit < 8 && sp < src_size; bit++)
		{
			uint best_len = 0, best_back = 0;
			const uint max_back = sp < 0x1000 ? sp : 0x1000;
			const uint max_len = src_size - sp < 18 ? src_size - sp : 18;
			// Same backward search style as EncodeLZ10LZ11(): nearby matches
			// first, matches may self-overlap (back < len) exactly like the
			// ring-buffer decoder above naturally allows.
			for (uint back = 1; back <= max_back; back++)
			{
				uint len = 0;
				while (len < max_len && src[sp + len] == src[sp - back + len])
					len++;
				if (len > best_len)
				{
					best_len = len;
					best_back = back;
					if (len == max_len)
						break;
				}
			}
			if (best_len >= 3)
			{
				const uint p = 0xfee + sp;
				const uint q = (p - best_back) & 0xfff;
				out[dp++] = (u8)q;
				out[dp++] = (u8)((q >> 8 & 0xf) << 4 | (best_len - 3));
				sp += best_len;
				// flag bit left 0 -- 0 means "compressed" for ALZ1
			}
			else
			{
				flag |= (u8)(1u << bit);
				out[dp++] = src[sp++];
			}
		}
		out[flag_pos] = flag;
	}
	*dest = out;
	*dest_size = dp;
	return ERR_OK;
}

typedef struct arika_list_t
{
	nintendo_sarc_entry_t *entries;
	uint n, cap;
} arika_list_t;

static void arika_list_push (arika_list_t *list, ccp name, const u8 *data, u32 size)
{
	if (list->n == list->cap)
	{
		list->cap = list->cap ? list->cap * 2 : 16;
		list->entries = REALLOC (list->entries, list->cap * sizeof (*list->entries));
	}
	nintendo_sarc_entry_t *e = list->entries + list->n++;
	e->name = STRDUP (name);
	u8 *copy = size ? MALLOC (size) : 0;
	if (copy)
		memcpy (copy, data, size);
	e->data = copy;
	e->size = size;
}

// Splits an "RF2"-tagged sub-container (Endless Ocean: Blue World groups
// related assets this way) into its member entries, recursing into any
// member that is itself another RF2 container. Falls back to a plain leaf
// push when [off] isn't actually an RF2 tag -- this is also how ordinary,
// non-grouped members reach the list. Ported from EXTRACT_RF2() in aluigi's
// arika.bms/endless_ocean.bms.
//
// RF2 header (16 bytes), all fields little-endian:
//   00h 3   "RF2" signature
//   03h 3   Type tag (unused here)
//   06h 2   Version
//   08h 3   Info size (sub-entry table size in bytes == n_sub * 20h)
//   0Bh 1   Unused
//   0Ch 4   Header size (unused here)
// Sub-entry (20h bytes): 14h-byte name, 4-byte size, 4-byte offset (relative
// to the RF2 tag's own offset), 4-byte flags (unused).
static void walk_rf2_or_leaf (
	arika_list_t *list, const u8 *data, uint size, uint off, u32 leaf_size, ccp name)
{
	if (off + 16 <= size && !memcmp (data + off, "RF2", 3))
	{
		const uint base_off = off;
		const u32 info_size = data[off + 8] | (u32)data[off + 9] << 8 | (u32)data[off + 10] << 16;
		const uint n_sub = info_size / 0x20;
		if (n_sub && n_sub <= 0x10000 && (u64)off + 16 + (u64)n_sub * 0x20 <= size)
		{
			const u8 *rec = data + off + 16;
			for (uint i = 0; i < n_sub; i++, rec += 0x20)
			{
				char raw[21];
				memcpy (raw, rec, 20);
				raw[20] = 0;
				char sub[300];
				snprintf (sub, sizeof (sub), "%s/%.20s", name, raw);
				const u32 ssize = rd_le32 (rec + 20);
				const u32 soff = rd_le32 (rec + 24) + base_off;
				if (ssize)
					walk_rf2_or_leaf (list, data, size, soff, ssize, sub);
			}
			return;
		}
	}
	if (off <= size && leaf_size <= size - off)
		arika_list_push (list, name, data + off, leaf_size);
}

enumError ExtractArika (nintendo_sarc_entry_t **out_entries, uint *out_n_entries,
	const u8 *info_data, uint info_size, const u8 *game_data, uint game_size)
{
	if (!out_entries || !out_n_entries || !info_data || info_size < 0x30 || !game_data)
		return EINVAL;

	u8 *info = MALLOC (info_size);
	if (!info)
		return ERR_CANT_CREATE;
	memcpy (info, info_data, info_size);
	DecryptArikaInfo (info, info_size);

	u32 sector_size = rd_le32 (info + 0x24);
	if (!sector_size)
		sector_size = 0x800;
	const u32 n_entries = rd_le32 (info + 0x2c);
	if (n_entries > 0x40000 || (u64)0x30 + (u64)n_entries * 0x30 > info_size)
	{
		FREE (info);
		return EINVAL;
	}

	arika_list_t list;
	memset (&list, 0, sizeof (list));

	const u8 *rec = info + 0x30;
	for (u32 i = 0; i < n_entries; i++, rec += 0x30)
	{
		char name[0x21];
		memcpy (name, rec, 0x20);
		name[0x20] = 0;
		if (!*name)
			continue; // unused directory slot

		const u32 zsize = rd_le32 (rec + 0x20);
		const u64 byte_off = (u64)rd_le32 (rec + 0x24) * sector_size;
		const u32 dsize = rd_le32 (rec + 0x2c);

		if (!zsize && !dsize)
			continue;
		if (byte_off > game_size)
			continue;
		const u32 avail = (u32)(game_size - byte_off);

		if (zsize == dsize)
		{
			// Stored raw (and possibly itself an RF2 group).
			if (zsize <= avail)
				walk_rf2_or_leaf (&list, game_data, game_size, (uint)byte_off, zsize, name);
			continue;
		}

		if (!dsize || dsize > NFMT_MAX_OUTPUT || zsize > avail)
			continue; // out-of-range entry: skip it, don't abort the archive

		const u8 *e = game_data + byte_off;
		uint hdr;
		if (zsize >= 4 && !memcmp (e, "ALZ1", 4))
			hdr = 4;
		else if (zsize >= 8 && !memcmp (e, "ZALZ", 4))
			hdr = 8; // older titles; same LZSS bitstream (see arika.bms)
		else
			continue; // unrecognised magic on a size-mismatched entry

		u8 *blob = MALLOC (dsize);
		if (!blob)
			continue;
		if (!DecodeALZ1 (blob, dsize, e + hdr, zsize - hdr))
			walk_rf2_or_leaf (&list, blob, dsize, 0, dsize, name);
		FREE (blob);
	}

	FREE (info);
	*out_entries = list.entries;
	*out_n_entries = list.n;
	return ERR_OK;
}

enumError CreateArika (u8 **dest_info, uint *dest_info_size, u8 **dest_game, uint *dest_game_size,
	const nintendo_sarc_entry_t *entries, uint n_entries, ccp title, bool compress)
{
	if (!dest_info || !dest_info_size || !dest_game || !dest_game_size || !entries || !n_entries
		|| n_entries > 0x40000)
		return EINVAL;

	const u32 sector_size = 0x800;
	const uint info_size = 0x30 + n_entries * 0x30;
	u8 *info = CALLOC (1, info_size);
	if (!info)
		return ERR_CANT_CREATE;

	if (title)
		memcpy (info, title, strnlen (title, 0x10));

	wr_le32 (info + 0x24, sector_size);
	wr_le32 (info + 0x28, 1);
	wr_le32 (info + 0x2c, n_entries);

	u8 **payload = CALLOC (n_entries, sizeof (u8 *));
	uint *psize = CALLOC (n_entries, sizeof (uint));
	if (!payload || !psize)
	{
		FREE (info);
		FREE (payload);
		FREE (psize);
		return ERR_CANT_CREATE;
	}

	u64 game_size = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		const u8 *src = entries[i].data;
		const uint ssize = entries[i].size;

		u8 *zdata = 0;
		uint zsize = 0;
		if (compress && ssize)
			EncodeALZ1 (&zdata, &zsize, src, ssize);

		if (zdata && zsize + 4 < ssize)
		{
			payload[i] = MALLOC (zsize + 4);
			memcpy (payload[i], "ALZ1", 4);
			memcpy (payload[i] + 4, zdata, zsize);
			psize[i] = zsize + 4;
		}
		else
		{
			payload[i] = ssize ? MALLOC (ssize) : 0;
			if (ssize)
				memcpy (payload[i], src, ssize);
			psize[i] = ssize;
		}
		FREE (zdata);

		const uint blocks = (psize[i] + sector_size - 1) / sector_size;
		game_size += (u64)blocks * sector_size;
	}

	if (game_size > NFMT_MAX_OUTPUT)
	{
		for (uint i = 0; i < n_entries; i++)
			FREE (payload[i]);
		FREE (payload);
		FREE (psize);
		FREE (info);
		return EFBIG;
	}

	u8 *game = CALLOC (1, game_size ? game_size : 1);
	if (!game)
	{
		for (uint i = 0; i < n_entries; i++)
			FREE (payload[i]);
		FREE (payload);
		FREE (psize);
		FREE (info);
		return ERR_CANT_CREATE;
	}

	uint cur = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		u8 *rec = info + 0x30 + i * 0x30;
		ccp nm = entries[i].name ? entries[i].name : "";
		strncpy ((char *)rec, nm, 0x1f);

		wr_le32 (rec + 0x20, psize[i]);
		wr_le32 (rec + 0x24, cur / sector_size);
		const uint blocks = (psize[i] + sector_size - 1) / sector_size;
		wr_le32 (rec + 0x28, blocks);
		wr_le32 (rec + 0x2c, entries[i].size);

		if (psize[i])
			memcpy (game + cur, payload[i], psize[i]);
		FREE (payload[i]);
		cur += blocks * sector_size;
	}
	FREE (payload);
	FREE (psize);

	EncryptArikaInfo (info, info_size);

	*dest_info = info;
	*dest_info_size = info_size;
	*dest_game = game;
	*dest_game_size = (uint)game_size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// JARC / jCMP (Ganbarion archive & compression container, Wii / Wii U / 3DS)

void ResetJARC (jarc_t *jarc)
{
	if (!jarc)
		return;
	if (jarc->entries)
		FREE (jarc->entries);
	if (jarc->decomp_buffer)
		FREE (jarc->decomp_buffer);
	memset (jarc, 0, sizeof (*jarc));
}

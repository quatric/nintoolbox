// SPDX-License-Identifier: GPL-2.0+
#include "lib-mpmess.h"
#include "lib-std.h"
#include "dclib-debug.h"
#include <string.h>
#include <stdlib.h>

bool IsMPMESS (const u8 *data, size_t size)
{
	if (!data || size < 16)
		return false;

	const u32 num_files = be32 (data);
	if (num_files == 0 || num_files > 2000)
		return false;

	const u32 header_table_size = 4 + num_files * 4;
	if (header_table_size > size)
		return false;

	const u32 first_offset = be32 (data + 4);

	// In MP6/7, first_offset == 4 + num_files * 4 (startPos = 4)
	// In MP4/5, first_offset == 8 + num_files * 4 (startPos = 0)
	if (first_offset != header_table_size && first_offset != header_table_size + 4)
		return false;

	// Verify subfile 0 has valid header
	const u32 start_pos = (first_offset == header_table_size) ? 4 : 0;
	const u32 sub_pos = first_offset + start_pos;
	if (sub_pos + 8 > size)
		return false;

	const u32 num_sub_vals = be32 (data + sub_pos);
	if (num_sub_vals > 50000)
		return false;

	return true;
}

static ccp get_mp4_name (uint idx)
{
	static const char *names[] = {
		"IndexTable", "CharacterList", "HiddenBlock", "BattleSpace",
		"BowserSpace", "WrapSpace", "ItemSpace", "Lottery",
		"BooHouse", "ItemList", "DiceRollMenu", "Toad_MerryGoGame",
		"Toad_SpaceRocketGame", "Toad_Star", "Toad_RollerCoaster",
		"Toad_HostBoardDialog", "Toad_ItemShop", "System", "ModeSelect",
		"ItemInfo", "Goomba_Roulette", "LuckMiniGame", "BoardStart",
		"Map3Event", "MiniGameNames", "MG_446", "DebugMessage",
		"PartyMode", "Setup", "Miracle", "MiniGameKoopa", "StoryMode",
		"BowserStory", "Map4Event", "MiniGameInst", "E3", "SAF",
		"MiniGameInst_Sys", "BoardResults", "BoardResults2", "Map5Event",
		"MiniGameMode", "MG_445", "MG_447", "MG_448", "MG_449",
		"MG_450", "Tutorial", "OptionRoom", "Map6Event", "Charley",
		"PresentRoom", "ExtraRoom", "StaffPost", "StaffName", "OpeningDemo",
		"MiniGameExInst"
	};
	if (idx < sizeof (names) / sizeof (names[0]))
		return names[idx];
	return NULL;
}

static const char *get_dialog_speaker (u8 code)
{
	switch (code)
	{
		case 0x01: return "Toad_Normal";
		case 0x02: return "Toad_Excite";
		case 0x03: return "Toad_Disappoint";
		case 0x04: return "Goomba_Normal";
		case 0x05: return "Goomba_Excite";
		case 0x06: return "Goomba_Disappoint";
		case 0x07: return "Shyguy_Normal";
		case 0x08: return "Shyguy_Excite";
		case 0x09: return "Shyguy_Disappoint";
		case 0x0A: return "Boo_Normal";
		case 0x0B: return "Boo_Excite";
		case 0x0C: return "Boo_Disappoint";
		case 0x0D: return "Koopa_Normal";
		case 0x0E: return "Koopa_Excite";
		case 0x0F: return "Koopa_Disappoint";
		case 0x10: return "Bowser";
		case 0x11: return "KoopaKid";
		case 0x13: return "Thwomp";
		case 0x14: return "Whomp";
		default: return "Unknown";
	}
}

static const char *get_icon_name (u8 code)
{
	switch (code)
	{
		case 0x01: return "ControlStick";
		case 0x03: return "A";
		case 0x04: return "B";
		case 0x05: return "X";
		case 0x06: return "Y";
		case 0x07: return "R";
		case 0x09: return "L";
		case 0x0C: return "Z";
		case 0x13: return "Coin";
		default: return "Icon";
	}
}

static const char *get_color_name (u8 code)
{
	switch (code)
	{
		case 0x01: return "BLACK";
		case 0x02: return "BLUE";
		case 0x03: return "PINK";
		case 0x04: return "RED";
		case 0x05: return "GREEN";
		case 0x07: return "YELLOW";
		default: return "DEFAULT";
	}
}

// Decode Mario Party custom escape characters / encoding into UTF-8 text
static void decode_mp_string (char *dest, size_t dest_size, const u8 *src, size_t src_len)
{
	size_t di = 0;
	for (size_t si = 0; si < src_len && src[si] != 0; si++)
	{
		const u8 val = src[si];
		if (di + 32 >= dest_size)
			break;

		switch (val)
		{
			case 0x0A: dest[di++] = '\n'; break;
			case 0x10: dest[di++] = ' '; break;
			case 0x20: dest[di++] = '\t'; break;
			case 0x1D: dest[di++] = '*'; break;
			case 0x3D: dest[di++] = '-'; break;
			case 0x3F: dest[di++] = '/'; break;
			case 0x5B: dest[di++] = '`'; break;
			case 0x5C: dest[di++] = '\''; break;
			case 0x5D: dest[di++] = '('; break;
			case 0x5E: dest[di++] = ')'; break;
			case 0x7B: dest[di++] = ':'; break;
			case 0x7E: dest[di++] = '&'; break;
			case 0x82: dest[di++] = ','; break;
			case 0x83: dest[di++] = '@'; break;
			case 0x84: dest[di++] = '_'; break;
			case 0x85: dest[di++] = '.'; break;
			case 0xC0: dest[di++] = '"'; break;
			case 0xC1: dest[di++] = '"'; break;
			case 0xC2: dest[di++] = '!'; break;
			case 0xC3: dest[di++] = '?'; break;
			case 0xFF: dest[di++] = '\r'; break;
			case 0x0C:
			{
				int count = 1;
				while (si + 1 < src_len && src[si + 1] == 0x0C)
				{
					count++;
					si++;
				}
				di += snprintf (dest + di, dest_size - di, "[Align_%d]", count);
				break;
			}
			case 0x0D:
				di += snprintf (dest + di, dest_size - di, "[Select2]");
				break;
			case 0x0F:
				di += snprintf (dest + di, dest_size - di, "[Select]");
				break;
			case 0x1C:
				if (si + 1 < src_len)
				{
					di += snprintf (dest + di, dest_size - di, "[Dialog:%s]", get_dialog_speaker (src[si + 1]));
					si++;
				}
				break;
			case 0x1E:
				if (si + 1 < src_len)
				{
					if (src[si + 1] == 0x08)
						di += snprintf (dest + di, dest_size - di, "]");
					else
						di += snprintf (dest + di, dest_size - di, "[COLOR:(%s)", get_color_name (src[si + 1]));
					si++;
				}
				break;
			case 0x1F:
				if (si + 1 < src_len)
				{
					di += snprintf (dest + di, dest_size - di, "[INSERT:Option%d]", src[si + 1]);
					si++;
				}
				break;
			case 0x0E:
				if (si + 1 < src_len)
				{
					di += snprintf (dest + di, dest_size - di, "[ICON:%s]", get_icon_name (src[si + 1]));
					si++;
				}
				break;
			default:
				dest[di++] = (char)val;
				break;
		}
	}
	dest[di] = '\0';
}

enumError DecodeMPMESS_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !data || size < 16)
		return ERR_INVALID_DATA;

	const u32 num_files = be32 (data);
	const u32 first_offset = be32 (data + 4);
	const uint version = (first_offset == 4 + num_files * 4) ? 6 : 4;
	const u32 start_pos = (version > 4) ? 4 : 0;

	fprintf (out, "# Mario Party Message Archive (Version %u, %u subfiles)\n\n", version, num_files);

	for (uint i = 0; i < num_files; i++)
	{
		const u32 off = be32 (data + 4 + i * 4) + start_pos;
		u32 next_off = (i == num_files - 1) ? (u32)size : (be32 (data + 4 + (i + 1) * 4) + start_pos);
		if (off >= size || next_off > size || off >= next_off)
			continue;

		ccp name = get_mp4_name (i);
		if (name)
			fprintf (out, "## File %u: %s\n", i, name);
		else
			fprintf (out, "## File %u\n", i);

		const u8 *sub_data = data + off;
		const size_t sub_len = next_off - off;
		if (sub_len < 8)
			continue;

		const u32 num_values = be32 (sub_data);
		const u32 first_sub_offset = be32 (sub_data + 4) + start_pos;

		if (version >= 6 && first_sub_offset != 4 + num_values * 4)
			continue;
		if (version < 6 && first_sub_offset != 8 + num_values * 4)
			continue;

		for (uint v = 0; v < num_values; v++)
		{
			const u32 voff = be32 (sub_data + 4 + v * 4) + start_pos;
			if (voff >= sub_len)
				continue;

			u32 msg_id = 0;
			const u8 *str_ptr = sub_data + voff;
			size_t rem = sub_len - voff;

			if (version >= 6)
			{
				if (rem < 4)
					continue;
				msg_id = be32 (str_ptr);
				str_ptr += 4;
				rem -= 4;
			}
			else
			{
				if (rem < 1)
					continue;
				// Skip 0x0B prefix byte
				if (*str_ptr == 0x0B)
				{
					str_ptr++;
					rem--;
				}
			}

			char decoded[4096];
			decode_mp_string (decoded, sizeof (decoded), str_ptr, rem);
			if (version >= 6)
				fprintf (out, "[%u] ID 0x%08X: %s\n", v, msg_id, decoded);
			else
				fprintf (out, "[%u]: %s\n", v, decoded);
		}
		fprintf (out, "\n");
	}

	return ERR_OK;
}

enumError ScanMPMESS (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size)
{
	if (!entries || !n_entries || !data || !IsMPMESS (data, size))
		return ERR_INVALID_DATA;

	const u32 num_files = be32 (data);
	const u32 first_offset = be32 (data + 4);
	const uint version = (first_offset == 4 + num_files * 4) ? 6 : 4;
	const u32 start_pos = (version > 4) ? 4 : 0;

	nintendo_sarc_entry_t *out = CALLOC (num_files, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint out_cnt = 0;
	for (uint i = 0; i < num_files; i++)
	{
		const u32 off = be32 (data + 4 + i * 4) + start_pos;
		u32 next_off = (i == num_files - 1) ? (u32)size : (be32 (data + 4 + (i + 1) * 4) + start_pos);
		if (off >= size || next_off > size || off >= next_off)
			continue;

		char fname[64];
		ccp mp4_name = (version == 4) ? get_mp4_name (i) : NULL;
		if (mp4_name)
			snprintf (fname, sizeof (fname), "%02u_%s.bin", i, mp4_name);
		else
			snprintf (fname, sizeof (fname), "File%02u.bin", i);

		OwnedEntryAdd (out, out_cnt++, fname, data + off, next_off - off);
	}

	*entries = out;
	*n_entries = out_cnt;
	return ERR_OK;
}

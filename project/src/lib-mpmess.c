// SPDX-License-Identifier: GPL-2.0+
#include "lib-mpmess.h"
#include "lib-std.h"
#include "lib-nintendo.h"
#include "dclib-debug.h"
#include "dclib-basics.h"
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

	// v4 (MaxValue word, absolute offsets), retail v6 (leading word,
	// base-4 offsets) and writer-form v5/v6 (messages immediately after
	// the table, base-4 offsets) are all accepted; the version split for
	// the base-4 family happens on message 0 (see mpmess_detect_version).
	if (first_offset != header_table_size && first_offset != header_table_size + 4
		&& first_offset + 4 != header_table_size)
		return false;

	// Verify subfile 0 has valid header. Base-4 families (retail-word
	// and writer-immediate forms) both add start_pos 4; only the v4
	// MaxValue form uses absolute offsets.
	const u32 start_pos = (first_offset == header_table_size + 4) ? 0 : 4;
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

// Forward declaration: full v4/v5/v6 detector defined below with the
// archive model (accepts the v4 MaxValue form, the retail v6 leading-word
// form and the writer-immediate v5/v6 form).
static uint mpmess_detect_version (const u8 *data, size_t size, uint *msg0_off, uint *msg0_len);

enumError DecodeMPMESS_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !data || size < 16)
		return ERR_INVALID_DATA;

	const u32 num_files = be32 (data);
	uint version = mpmess_detect_version (data, size, 0, 0);
	if (!version)
		return ERR_INVALID_DATA;
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
	const uint version = mpmess_detect_version (data, size, 0, 0);
	if (!version)
		return ERR_INVALID_DATA;
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

// ----------------------------------------------------------------------------
// Full-archive parse + rebuild with v4/v5/v6 support.
// Reference: MPLibrary/GCWii/Message/MessFileData.cs, with FileWriter
// semantics confirmed against KillzXGaming/Toolbox.Core
// (WriteUint32Offset(target, rel) patches target with Position - rel;
// WriteSectionSizeU32(position, size) patches position with size).
//
// One deliberate deviation for v5: the reference message writer stores
// v5 entry offsets message-relative, but its reader adds startPos (4),
// so reference-written v5 messages fail the reference reader's own
// first-offset check and are kept as raw bytes. CreateMPMESS stores v5
// message offsets like v6 (message-relative minus 4), which satisfies
// every check in the reference reader while keeping v5's 0x0B entries
// and MaxValue/size words.

// Encoded-string writer: inverse of decode_mp_string above and of
// MessFileData.WriteString. Returns bytes written (without NUL) or -1 when
// text[i] == '[' does not open a recognized tag (caller then emits '[' raw).
static int encode_tag (u8 *dst, size_t cap, const char *text, uint *consumed);

static int mp_code_of (const char *name, size_t len, char kind)
{
	static const struct { const char *name; u8 code; char kind; } tab[] = {
		{ "Toad_Normal", 0x01, 'D' }, { "Toad_Excite", 0x02, 'D' },
		{ "Toad_Disappoint", 0x03, 'D' }, { "Goomba_Normal", 0x04, 'D' },
		{ "Goomba_Excite", 0x05, 'D' }, { "Goomba_Disappoint", 0x06, 'D' },
		{ "Shyguy_Normal", 0x07, 'D' }, { "Shyguy_Excite", 0x08, 'D' },
		{ "Shyguy_Disappoint", 0x09, 'D' }, { "Boo_Normal", 0x0A, 'D' },
		{ "Boo_Excite", 0x0B, 'D' }, { "Boo_Disappoint", 0x0C, 'D' },
		{ "Koopa_Normal", 0x0D, 'D' }, { "Koopa_Excite", 0x0E, 'D' },
		{ "Koopa_Disappoint", 0x0F, 'D' }, { "Bowser", 0x10, 'D' },
		{ "KoopaKid", 0x11, 'D' }, { "Thwomp", 0x13, 'D' },
		{ "Whomp", 0x14, 'D' },
		{ "ControlStick", 0x01, 'I' }, { "A", 0x03, 'I' },
		{ "B", 0x04, 'I' }, { "X", 0x05, 'I' }, { "Y", 0x06, 'I' },
		{ "R", 0x07, 'I' }, { "L", 0x09, 'I' }, { "Z", 0x0C, 'I' },
		{ "Coin", 0x13, 'I' },
		{ "BLACK", 0x01, 'C' }, { "BLUE", 0x02, 'C' },
		{ "PINK", 0x03, 'C' }, { "RED", 0x04, 'C' },
		{ "GREEN", 0x05, 'C' }, { "YELLOW", 0x07, 'C' },
		{ "Option1", 0x01, 'R' }, { "Option2", 0x02, 'R' },
		{ "Option3", 0x03, 'R' }, { "Option4", 0x04, 'R' },
		{ "Option5", 0x05, 'R' }, { "Option6", 0x06, 'R' },
		{ "Option7", 0x07, 'R' }, { "Option8", 0x08, 'R' },
	};
	for (size_t i = 0; i < sizeof (tab) / sizeof (*tab); i++)
		if (tab[i].kind == kind && strlen (tab[i].name) == len
			&& !memcmp (tab[i].name, name, len))
			return tab[i].code;
	return -1;
}

static int encode_tag (u8 *dst, size_t cap, const char *text, uint *consumed)
{
	// text[0] == '['. Tries "[COLOR:(X]", "[ICON:X]", "[INSERT:X]",
	// "[Dialog:X]", "[Select2]", "[Select]", "[Align_N]" in the reference
	// order (Select2 before Select).
	if (cap < 2)
		return -1;
	if (!strncmp (text + 1, "COLOR:(", 7))
	{
		const char *end = strchr (text + 8, ')');
		if (!end)
			return -1;
		const int code = mp_code_of (text + 8, (size_t)(end - (text + 8)), 'C');
		if (code < 0)
			return -1;
		dst[0] = 0x1E;
		dst[1] = (u8)code;
		*consumed = (uint)(end - text) + 1;
		return 2;
	}
	struct { const char *prefix; size_t plen; u8 lead; char kind; bool close; } fmts[] = {
		{ "[ICON:", 6, 0x0E, 'I', true },
		{ "[INSERT:", 8, 0x1F, 'R', true },
		{ "[Dialog:", 8, 0x1C, 'D', true },
	};
	for (size_t f = 0; f < sizeof (fmts) / sizeof (*fmts); f++)
	{
		if (strncmp (text, fmts[f].prefix, fmts[f].plen))
			continue;
		const char *end = strchr (text + fmts[f].plen, ']');
		if (!end)
			return -1;
		const int code = mp_code_of (text + fmts[f].plen,
			(size_t)(end - (text + fmts[f].plen)), fmts[f].kind);
		if (code < 0)
			return -1;
		dst[0] = fmts[f].lead;
		dst[1] = (u8)code;
		*consumed = (uint)(end - text) + 1;
		return 2;
	}
	if (!strncmp (text + 1, "Select2]", 8))
	{
		dst[0] = 0x0D;
		*consumed = 9;
		return 1;
	}
	if (!strncmp (text + 1, "Select]", 7))
	{
		dst[0] = 0x0F;
		*consumed = 8;
		return 1;
	}
	if (!strncmp (text + 1, "Align_", 6) && text[7] >= '0' && text[7] <= '9' && text[8] == ']')
	{
		const int n = text[7] - '0';
		if ((size_t)n > cap)
			return -1;
		for (int k = 0; k < n; k++)
			dst[k] = 0x0C;
		*consumed = 9;
		return n;
	}
	return -1;
}

static int encode_char_byte (u8 b, bool *quote_open)
{
	switch (b)
	{
		case '\n': return 0x0A;
		case ' ': return 0x10;
		case '\t': return 0x20;
		case '*': return 0x1D;
		case '-': return 0x3D;
		case '/': return 0x3F;
		case '`': return 0x5B;
		case '\'': return 0x5C;
		case '(': return 0x5D;
		case ')': return 0x5E;
		case ':': return 0x7B;
		case '&': return 0x7E;
		case ',': return 0x82;
		case '@': return 0x83;
		case '_': return 0x84;
		case '.': return 0x85;
		case '!': return 0xC2;
		case '?': return 0xC3;
		case '\r': return 0xFF;
		case '"': // alternating start/end quote, like WriteString
		{
			const int v = *quote_open ? 0xC1 : 0xC0;
			*quote_open = !*quote_open;
			return v;
		}
		default: return b;
	}
}

// Encodes decoded text (byte-oriented; tags in [] resolved) to game bytes.
// Returns malloc'd buffer (without NUL) in *dst with *dst_len, or NULL.
static u8 *encode_mp_string (const char *text, uint *dst_len)
{
	if (!text || !dst_len)
		return 0;
	size_t cap = strlen (text) * 2 + 16;
	u8 *out = MALLOC (cap ? cap : 16);
	if (!out)
		return 0;
	size_t pos = 0;
	bool quote_open = false;
	for (size_t i = 0; text[i];)
	{
		if (text[i] == '[')
		{
			uint consumed = 0;
			// Worst case "[Align_9]" needs 9 bytes.
			if (pos + 10 > cap)
			{
				cap *= 2;
				u8 *nb = REALLOC (out, cap);
				if (!nb)
				{
					FREE (out);
					return 0;
				}
				out = nb;
			}
			const int w = encode_tag (out + pos, cap - pos, text + i, &consumed);
			if (w >= 0 && consumed > 0)
			{
				pos += (size_t)w;
				i += consumed;
				continue;
			}
			// Unrecognized '[': emit raw like the reference fallthrough.
			if (pos + 1 > cap)
			{
				cap *= 2;
				u8 *nb = REALLOC (out, cap);
				if (!nb)
				{
					FREE (out);
					return 0;
				}
				out = nb;
			}
			out[pos++] = (u8)text[i++];
		}
		else if (text[i] == ']')
		{
			if (pos + 2 > cap)
			{
				cap *= 2;
				u8 *nb = REALLOC (out, cap);
				if (!nb)
				{
					FREE (out);
					return 0;
				}
				out = nb;
			}
			out[pos++] = 0x1E;
			out[pos++] = 0x08;
			i++;
		}
		else
		{
			if (pos + 1 > cap)
			{
				cap *= 2;
				u8 *nb = REALLOC (out, cap);
				if (!nb)
				{
					FREE (out);
					return 0;
				}
				out = nb;
			}
			out[pos++] = (u8)encode_char_byte ((u8)text[i++], &quote_open);
		}
	}
	*dst_len = (uint)pos;
	return out;
}

static uint mpmess_detect_version (const u8 *data, size_t size, uint *msg0_off, uint *msg0_len)
{
	if (!data || size < 16)
		return 0;
	const u32 num_files = be32 (data);
	if (!num_files || num_files > 2000 || 4 + (u64)num_files * 4 > size)
		return 0;
	const u32 stored_first = be32 (data + 4);
	const u32 header_table_size = 4 + num_files * 4;

	// Three on-disk shapes (see the header comment on CreateMPMESS):
	//   v4:        stored[0] == header + 4 (MaxValue word), base 0.
	//   v6 word:   stored[0] == header (4-byte word belongs to retail v6),
	//              base 4, message at header + 4.
	//   v5/v6 imm: stored[0] + 4 == header (writer form, no word),
	//              base 4, message at header.
	if (msg0_off)
		*msg0_off = 0;
	if (msg0_len)
		*msg0_len = 0;

	// v4 first: its 0x0B entries + MaxValue checks are the strongest.
	if (stored_first == header_table_size + 4)
	{
		const u32 off0 = stored_first;
		const u32 off1 = num_files > 1 ? be32 (data + 8) : (u32)size;
		if (off0 < size && off1 <= size && off0 < off1)
		{
			if (msg0_off)
				*msg0_off = off0;
			if (msg0_len)
				*msg0_len = off1 - off0;
			return 4;
		}
		return 0;
	}
	// Base-4 family: locate message 0, then split v5/v6 on its shape.
	u32 off0 = 0;
	if (stored_first == header_table_size)
		off0 = header_table_size + 4; // word form
	else if (stored_first + 4 == header_table_size)
		off0 = header_table_size; // immediate form
	else
		return 0;
	u32 off1;
	if (num_files > 1)
	{
		// Both forms store base-4 offsets; the second entry resolves the
		// same way regardless of the leading word.
		off1 = be32 (data + 8) + 4;
	}
	else
		off1 = (u32)size;
	if (off0 >= size || off1 > size || off0 >= off1)
		return 0;
	const u8 *msg = data + off0;
	const size_t mlen = off1 - off0;
	if (mlen < 8)
		return 0;
	const u32 nvals = be32 (msg);
	if (!nvals || nvals > 50000 || 4 + (u64)nvals * 4 > mlen)
		return 0;
	const u32 stored_mfirst = be32 (msg + 4);
	uint ver = 0;
	if (stored_mfirst + 4 == 4 + nvals * 4)
		ver = 6;
	else if (stored_mfirst + 4 == 8 + nvals * 4 && mlen >= 8 + nvals * 4 + 1
		&& msg[8 + nvals * 4] == 0x0B)
		ver = 5;
	if (!ver)
		return 0;
	if (msg0_off)
		*msg0_off = off0;
	if (msg0_len)
		*msg0_len = off1 - off0;
	return ver;
}

// Parses one message's entries. Returns true with *entries_out set (caller
// frees each text + array) or false to keep the message raw.
static bool mpmess_parse_message (
	const u8 *msg, size_t mlen, uint version, mpmess_entry_t **entries_out, uint *n_out)
{
	*entries_out = 0;
	*n_out = 0;
	if (mlen < 8)
		return false;
	const u32 nvals = be32 (msg);
	if (!nvals || nvals > 50000)
		return false;
	const u32 start_pos = version > 4 ? 4 : 0;
	const u32 first_rel = be32 (msg + 4) + start_pos;
	const u32 expect = version >= 6 ? 4 + nvals * 4 : 8 + nvals * 4;
	if (first_rel != expect || first_rel > mlen)
		return false;
	if (version < 6)
	{
		// MaxValue word must sit exactly where the table ends.
		if (4 + (u64)nvals * 4 + 4 > mlen)
			return false;
	}

	mpmess_entry_t *entries = CALLOC (nvals, sizeof (*entries));
	if (!entries)
		return false;
	for (uint v = 0; v < nvals; v++)
	{
		const u32 voff = be32 (msg + 4 + v * 4) + start_pos;
		if (voff >= mlen)
			goto fail;
		const u8 *str_ptr = msg + voff;
		size_t rem = mlen - voff;
		u32 msg_id = 0;
		if (version >= 6)
		{
			if (rem < 5)
				goto fail;
			msg_id = be32 (str_ptr);
			str_ptr += 4;
			rem -= 4;
		}
		else
		{
			if (rem < 2 || str_ptr[0] != 0x0B)
				goto fail;
			str_ptr++;
			rem--;
		}
		// Must be NUL-terminated inside the message.
		size_t slen = 0;
		while (slen < rem && str_ptr[slen])
			slen++;
		if (slen >= rem)
			goto fail;
		char decoded[4096];
		decode_mp_string (decoded, sizeof (decoded), str_ptr, rem);
		entries[v].id = msg_id;
		entries[v].text = STRDUP (decoded);
		if (!entries[v].text)
			goto fail;
	}
	*entries_out = entries;
	*n_out = nvals;
	return true;
fail:
	for (uint k = 0; k < nvals; k++)
		FREE (entries[k].text);
	FREE (entries);
	return false;
}

enumError ScanMPMESSArchive (mpmess_archive_t *arc, const u8 *data, size_t size)
{
	if (!arc || !data || !IsMPMESS (data, size))
		return ERR_INVALID_DATA;
	memset (arc, 0, sizeof (*arc));

	const u32 num_files = be32 (data);
	const uint version = mpmess_detect_version (data, size, 0, 0);
	if (!version)
		return ERR_INVALID_DATA;
	const u32 start_pos = version > 4 ? 4 : 0;
	arc->version = version;

	mpmess_file_t *files = CALLOC (num_files, sizeof (*files));
	if (!files)
		return ERR_OUT_OF_MEMORY;
	arc->files = files;
	arc->num_files = num_files;

	for (uint i = 0; i < num_files; i++)
	{
		const u32 off = be32 (data + 4 + i * 4) + start_pos;
		u32 next_off = (i == num_files - 1) ? (u32)size : be32 (data + 4 + (i + 1) * 4) + start_pos;
		mpmess_file_t *f = files + i;
		if (version == 4)
		{
			ccp nm = get_mp4_name (i);
			snprintf (f->name, sizeof (f->name), "%s", nm ? nm : "File");
		}
		else
			snprintf (f->name, sizeof (f->name), "File%u", i);
		if (off >= size || next_off > size || off >= next_off)
			continue;
		mpmess_entry_t *entries = 0;
		uint n_entries = 0;
		if (mpmess_parse_message (data + off, next_off - off, version, &entries, &n_entries))
		{
			f->entries = entries;
			f->num_entries = n_entries;
		}
		else
		{
			// Kept verbatim like the reference reader.
			f->raw = MALLOC (next_off - off ? next_off - off : 1);
			if (!f->raw)
			{
				ResetMPMESSArchive (arc);
				return ERR_OUT_OF_MEMORY;
			}
			memcpy (f->raw, data + off, next_off - off);
			f->raw_size = next_off - off;
		}
	}
	return ERR_OK;
}

void ResetMPMESSArchive (mpmess_archive_t *arc)
{
	if (!arc)
		return;
	for (uint i = 0; i < arc->num_files; i++)
	{
		mpmess_file_t *f = arc->files + i;
		for (uint k = 0; k < f->num_entries; k++)
			FREE (f->entries[k].text);
		FREE (f->entries);
		FREE (f->raw);
	}
	FREE (arc->files);
	memset (arc, 0, sizeof (*arc));
}

// Dynamic little-endian-explicit BE writer (all MESS integers are BE).
typedef struct mess_buf_t
{
	u8 *data;
	size_t size;
	size_t alloc;
} mess_buf_t;

static bool mess_reserve (mess_buf_t *b, size_t need)
{
	if (b->size + need <= b->alloc)
		return true;
	size_t nalloc = b->alloc ? b->alloc * 2 : 256;
	while (nalloc < b->size + need)
		nalloc *= 2;
	u8 *nb = REALLOC (b->data, nalloc);
	if (!nb)
		return false;
	b->data = nb;
	b->alloc = nalloc;
	return true;
}

static bool mess_put (mess_buf_t *b, const void *src, size_t len)
{
	if (!mess_reserve (b, len))
		return false;
	memcpy (b->data + b->size, src, len);
	b->size += len;
	return true;
}

static bool mess_be32 (mess_buf_t *b, u32 v)
{
	u8 tmp[4];
	tmp[0] = v >> 24;
	tmp[1] = v >> 16;
	tmp[2] = v >> 8;
	tmp[3] = v;
	return mess_put (b, tmp, 4);
}

static bool mess_zeros (mess_buf_t *b, size_t n)
{
	if (!mess_reserve (b, n))
		return false;
	memset (b->data + b->size, 0, n);
	b->size += n;
	return true;
}

static bool mess_patch_be32 (mess_buf_t *b, size_t at, u32 v)
{
	if (at + 4 > b->size)
		return false;
	b->data[at] = v >> 24;
	b->data[at + 1] = v >> 16;
	b->data[at + 2] = v >> 8;
	b->data[at + 3] = v;
	return true;
}

static bool mess_write_message (mess_buf_t *b, const mpmess_file_t *f, uint version)
{
	if (!f->entries)
		return mess_put (b, f->raw, f->raw_size);

	const size_t msg_start = b->size;
	if (!mess_be32 (b, f->num_entries))
		return false;
	const size_t tab_at = b->size;
	for (uint k = 0; k < f->num_entries; k++)
		if (!mess_be32 (b, 0))
			return false;
	size_t max_at = 0;
	if (version < 6)
	{
		max_at = b->size;
		if (!mess_be32 (b, 0xFFFFFFFFu))
			return false;
	}
	for (uint k = 0; k < f->num_entries; k++)
	{
		// Offsets: v4 relative to msg_start, v5/v6 relative to
		// msg_start + 4 (see the CreateMPMESS header comment).
		const u32 rel_base = version > 4 ? 4 : 0;
		if (!mess_patch_be32 (b, tab_at + k * 4,
				(u32)(b->size - msg_start - rel_base)))
			return false;
		if (version >= 6)
		{
			if (!mess_be32 (b, f->entries[k].id))
				return false;
		}
		else
		{
			const u8 b11 = 0x0B;
			if (!mess_put (b, &b11, 1))
				return false;
		}
		uint enc_len = 0;
		u8 *enc = encode_mp_string (
			f->entries[k].text ? f->entries[k].text : "", &enc_len);
		if (!enc)
			return false;
		bool w = mess_put (b, enc, enc_len);
		FREE (enc);
		if (!w)
			return false;
		const u8 zero = 0;
		if (!mess_put (b, &zero, 1))
			return false;
		while ((b->size - msg_start) & 3)
			if (!mess_put (b, &zero, 1))
				return false;
	}
	if (version < 6)
	{
		// MaxValue word (message size) + trailing section size, like the
		// reference writer.
		const u32 mlen = (u32)(b->size - msg_start + 4);
		if (!mess_patch_be32 (b, max_at, mlen))
			return false;
		if (!mess_be32 (b, mlen))
			return false;
	}
	return true;
}

enumError CreateMPMESS (u8 **dest, uint *dest_size, const mpmess_archive_t *arc)
{
	if (!dest || !dest_size || !arc || !arc->num_files || !arc->files
		|| (arc->version != 4 && arc->version != 5 && arc->version != 6))
		return ERR_INVALID_DATA;

	mess_buf_t b = { 0, 0, 0 };
	if (!mess_be32 (&b, arc->num_files))
		goto oom;
	const size_t tab_at = b.size;
	for (uint i = 0; i < arc->num_files; i++)
		if (!mess_be32 (&b, 0))
			goto oom;
	size_t max_at = 0;
	if (arc->version < 5)
	{
		max_at = b.size;
		if (!mess_be32 (&b, 0xFFFFFFFFu))
			goto oom;
	}
	for (uint i = 0; i < arc->num_files; i++)
	{
		const u32 rel_base = arc->version > 4 ? 4 : 0;
		if (!mess_patch_be32 (&b, tab_at + i * 4, (u32)(b.size - rel_base)))
			goto oom;
		if (!mess_write_message (&b, arc->files + i, arc->version))
			goto oom;
	}
	if (arc->version < 5)
	{
		if (!mess_patch_be32 (&b, max_at, (u32)b.size))
			goto oom;
	}
	if (b.size > NFMT_MAX_OUTPUT)
	{
		FREE (b.data);
		return EFBIG;
	}
	*dest = b.data;
	*dest_size = (uint)b.size;
	return ERR_OK;
oom:
	FREE (b.data);
	return ERR_OUT_OF_MEMORY;
}

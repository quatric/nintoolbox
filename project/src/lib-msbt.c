#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lib-msbt.h"
#include "lib-szs.h"
#include "dclib-utf8.h"

// Helper reader/writer macros
static inline u32 rd_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}
static inline u32 rd_le32 (const u8 *p)
{
	return (u32)p[3] << 24 | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0];
}
static inline u16 rd_be16 (const u8 *p)
{
	return (u16)p[0] << 8 | p[1];
}
static inline u16 rd_le16 (const u8 *p)
{
	return (u16)p[1] << 8 | p[0];
}
static inline void wr_be16 (u8 *p, u16 v)
{
	p[0] = v >> 8;
	p[1] = (u8)v;
}
static inline void wr_le16 (u8 *p, u16 v)
{
	p[0] = (u8)v;
	p[1] = v >> 8;
}
static inline void wr_be32 (u8 *p, u32 v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = (u8)v;
}
static inline void wr_le32 (u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static inline u16 r16 (const u8 *p, bool be)
{
	return be ? rd_be16 (p) : rd_le16 (p);
}
static inline u32 r32 (const u8 *p, bool be)
{
	return be ? rd_be32 (p) : rd_le32 (p);
}
static inline void w16 (u8 *p, u16 v, bool be)
{
	if (be)
		wr_be16 (p, v);
	else
		wr_le16 (p, v);
}
static inline void w32 (u8 *p, u32 v, bool be)
{
	if (be)
		wr_be32 (p, v);
	else
		wr_le32 (p, v);
}

static u32 msbt_hash (const char *s, u32 num_groups)
{
	u32 hash = 0;
	while (*s)
	{
		hash = hash * 0x492 + (u8)*s;
		s++;
	}
	return num_groups ? hash % num_groups : 0;
}

bool IsMSBT (const u8 *data, uint size)
{
	return data && size >= 0x20 && !memcmp (data, "MsgStdBn", 8);
}

bool IsMSBP (const u8 *data, uint size)
{
	return data && size >= 0x20 && !memcmp (data, "MsgPrjBn", 8);
}

bool IsMSBF (const u8 *data, uint size)
{
	return data && size >= 0x20 && !memcmp (data, "MsgFlwBn", 8);
}

void InitMSBT (msbt_file_t *msbt)
{
	if (!msbt)
		return;
	memset (msbt, 0, sizeof (*msbt));
	msbt->encoding = MSBT_ENC_UTF16;
	msbt->version = 3;
	msbt->label_slot_count = 0; // 0 = auto (101 on create)
}

void ResetMSBT (msbt_file_t *msbt)
{
	if (!msbt)
		return;
	if (msbt->fname)
	{
		FREE (msbt->fname);
		msbt->fname = 0;
	}
	if (msbt->ato1_data)
	{
		FREE (msbt->ato1_data);
		msbt->ato1_data = 0;
	}
	msbt->ato1_size = 0;
	msbt->has_ato1 = false;
	if (msbt->entries)
	{
		for (uint i = 0; i < msbt->num_entries; i++)
		{
			if (msbt->entries[i].label)
				FREE (msbt->entries[i].label);
			if (msbt->entries[i].text)
				FREE (msbt->entries[i].text);
			if (msbt->entries[i].attrib)
				FREE (msbt->entries[i].attrib);
			if (msbt->entries[i].attr_str)
				FREE (msbt->entries[i].attr_str);
		}
		FREE (msbt->entries);
		msbt->entries = 0;
	}
	msbt->num_entries = 0;
	msbt->alloc_entries = 0;
}

// Convert raw UTF-16 string (with embedded control tags) into UTF-8 text with tag formatting
static char *decode_utf16_msbt_string (const u8 *data, uint max_len, bool be)
{
	// Estimate size (UTF-8 text could be longer with hex tags)
	uint cap = max_len * 4 + 64;
	char *out = MALLOC (cap);
	uint out_pos = 0;

	uint pos = 0;
	while (pos + 2 <= max_len)
	{
		u16 ch = r16 (data + pos, be);
		pos += 2;
		if (ch == 0) // Null terminator
			break;

		if (ch == 0x000E) // Control tag start
		{
			if (pos + 6 <= max_len)
			{
				u16 group = r16 (data + pos, be);
				u16 tag = r16 (data + pos + 2, be);
				u16 param_size = r16 (data + pos + 4, be);
				pos += 6;

				char tag_buf[256];
				int n = snprintf (tag_buf, sizeof (tag_buf), "[tag:%u,%u", group, tag);
				if (out_pos + n + param_size * 2 + 10 >= cap)
				{
					cap = cap * 2 + param_size * 2 + 64;
					out = REALLOC (out, cap);
				}
				memcpy (out + out_pos, tag_buf, n);
				out_pos += n;

				if (param_size > 0 && pos + param_size <= max_len)
				{
					out[out_pos++] = ',';
					for (uint p = 0; p < param_size; p++)
					{
						char hex[4];
						snprintf (hex, sizeof (hex), "%02X", data[pos + p]);
						out[out_pos++] = hex[0];
						out[out_pos++] = hex[1];
					}
					pos += param_size;
				}
				out[out_pos++] = ']';
			}
		}
		else if (ch == 0x000F) // Tag end: 0x000F + 4 marker bytes (6 bytes total)
		{
			u8 marker[4] = { 0, 0, 0, 0 };
			if (pos + 4 <= max_len)
			{
				memcpy (marker, data + pos, 4);
				pos += 4;
			}
			bool all_zero = !(marker[0] | marker[1] | marker[2] | marker[3]);
			if (all_zero)
			{
				if (out_pos + 10 >= cap)
				{
					cap *= 2;
					out = REALLOC (out, cap);
				}
				memcpy (out + out_pos, "[/tag]", 6);
				out_pos += 6;
			}
			else
			{
				if (out_pos + 24 >= cap)
				{
					cap *= 2;
					out = REALLOC (out, cap);
				}
				int _n = snprintf (out + out_pos, cap - out_pos, "[/tag:%02X%02X%02X%02X]",
					marker[0], marker[1], marker[2], marker[3]);
				out_pos += _n;
			}
		}
		else
		{
			// Standard unicode code point conversion (UTF-16 to UTF-8)
			u32 cp = ch;
			if (ch >= 0xD800 && ch <= 0xDBFF && pos + 2 <= max_len)
			{
				u16 low = r16 (data + pos, be);
				if (low >= 0xDC00 && low <= 0xDFFF)
				{
					cp = (((ch - 0xD800) << 10) | (low - 0xDC00)) + 0x10000;
					pos += 2;
				}
			}

			if (out_pos + 8 >= cap)
			{
				cap = cap * 2 + 64;
				out = REALLOC (out, cap);
			}
			if (cp < 0x80)
			{
				out[out_pos++] = (char)cp;
			}
			else if (cp < 0x800)
			{
				out[out_pos++] = (char)(0xC0 | (cp >> 6));
				out[out_pos++] = (char)(0x80 | (cp & 0x3F));
			}
			else if (cp < 0x10000)
			{
				out[out_pos++] = (char)(0xE0 | (cp >> 12));
				out[out_pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
				out[out_pos++] = (char)(0x80 | (cp & 0x3F));
			}
			else
			{
				out[out_pos++] = (char)(0xF0 | (cp >> 18));
				out[out_pos++] = (char)(0x80 | ((cp >> 12) & 0x3F));
				out[out_pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
				out[out_pos++] = (char)(0x80 | (cp & 0x3F));
			}
		}
	}

	out[out_pos] = 0;
	return out;
}

// Convert raw UTF-8 string (with embedded control tags) into clean text representation
static char *decode_utf8_msbt_string (const u8 *data, uint max_len, bool be)
{
	uint cap = max_len * 2 + 64;
	char *out = MALLOC (cap);
	uint out_pos = 0;

	uint pos = 0;
	while (pos < max_len)
	{
		u8 b = data[pos++];
		if (b == 0)
			break;

		if (b == 0x0E) // Control tag start
		{
			if (pos + 6 <= max_len)
			{
				u16 group = r16 (data + pos, be);
				u16 tag = r16 (data + pos + 2, be);
				u16 param_size = r16 (data + pos + 4, be);
				pos += 6;

				char tag_buf[256];
				int n = snprintf (tag_buf, sizeof (tag_buf), "[tag:%u,%u", group, tag);
				if (out_pos + n + param_size * 2 + 10 >= cap)
				{
					cap = cap * 2 + param_size * 2 + 64;
					out = REALLOC (out, cap);
				}
				memcpy (out + out_pos, tag_buf, n);
				out_pos += n;

				if (param_size > 0 && pos + param_size <= max_len)
				{
					out[out_pos++] = ',';
					for (uint p = 0; p < param_size; p++)
					{
						char hex[4];
						snprintf (hex, sizeof (hex), "%02X", data[pos + p]);
						out[out_pos++] = hex[0];
						out[out_pos++] = hex[1];
					}
					pos += param_size;
				}
				out[out_pos++] = ']';
			}
		}
		else if (b == 0x0F) // Tag end + 4 marker bytes
		{
			u8 marker[4] = { 0, 0, 0, 0 };
			if (pos + 4 <= max_len)
			{
				memcpy (marker, data + pos, 4);
				pos += 4;
			}
			bool all_zero = !(marker[0] | marker[1] | marker[2] | marker[3]);
			if (all_zero)
			{
				if (out_pos + 10 >= cap)
				{
					cap *= 2;
					out = REALLOC (out, cap);
				}
				memcpy (out + out_pos, "[/tag]", 6);
				out_pos += 6;
			}
			else
			{
				if (out_pos + 24 >= cap)
				{
					cap *= 2;
					out = REALLOC (out, cap);
				}
				int _n = snprintf (out + out_pos, cap - out_pos, "[/tag:%02X%02X%02X%02X]",
					marker[0], marker[1], marker[2], marker[3]);
				out_pos += _n;
			}
		}
		else
		{
			if (out_pos + 2 >= cap)
			{
				cap *= 2;
				out = REALLOC (out, cap);
			}
			out[out_pos++] = (char)b;
		}
	}

	out[out_pos] = 0;
	return out;
}

// Append one unicode code point as UTF-8
static void msbt_append_cp (char **out, uint *out_pos, uint *cap, u32 cp)
{
	if (*out_pos + 8 >= *cap)
	{
		*cap = *cap * 2 + 64;
		*out = REALLOC (*out, *cap);
	}
	if (cp < 0x80)
	{
		(*out)[(*out_pos)++] = (char)cp;
	}
	else if (cp < 0x800)
	{
		(*out)[(*out_pos)++] = (char)(0xC0 | (cp >> 6));
		(*out)[(*out_pos)++] = (char)(0x80 | (cp & 0x3F));
	}
	else if (cp < 0x10000)
	{
		(*out)[(*out_pos)++] = (char)(0xE0 | (cp >> 12));
		(*out)[(*out_pos)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
		(*out)[(*out_pos)++] = (char)(0x80 | (cp & 0x3F));
	}
	else
	{
		(*out)[(*out_pos)++] = (char)(0xF0 | (cp >> 18));
		(*out)[(*out_pos)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
		(*out)[(*out_pos)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
		(*out)[(*out_pos)++] = (char)(0x80 | (cp & 0x3F));
	}
}

// Convert raw UTF-32 string into UTF-8 text (control tags use u16 fields like UTF-16)
static char *decode_utf32_msbt_string (const u8 *data, uint max_len, bool be)
{
	uint cap = max_len * 2 + 64;
	char *out = MALLOC (cap);
	uint out_pos = 0;

	uint pos = 0;
	while (pos + 4 <= max_len)
	{
		u32 ch = r32 (data + pos, be);
		pos += 4;
		if (ch == 0)
			break;
		if (ch == 0x0000000E)
		{
			if (pos + 6 <= max_len)
			{
				u16 group = r16 (data + pos, be);
				u16 tag = r16 (data + pos + 2, be);
				u16 param_size = r16 (data + pos + 4, be);
				pos += 6;
				const u8 *params = 0;
				if (param_size > 0 && pos + param_size <= max_len)
				{
					params = data + pos;
					pos += param_size;
				}
				else
					param_size = 0;
				char tag_buf[256];
				int n = snprintf (tag_buf, sizeof (tag_buf), "[tag:%u,%u", group, tag);
				if (out_pos + (uint)n + param_size * 2 + 10 >= cap)
				{
					cap = cap * 2 + param_size * 2 + 64;
					out = REALLOC (out, cap);
				}
				memcpy (out + out_pos, tag_buf, n);
				out_pos += n;
				if (param_size > 0)
				{
					out[out_pos++] = ',';
					for (uint p = 0; p < param_size; p++)
					{
						char hex[4];
						snprintf (hex, sizeof (hex), "%02X", params[p]);
						out[out_pos++] = hex[0];
						out[out_pos++] = hex[1];
					}
				}
				out[out_pos++] = ']';
			}
		}
		else if (ch == 0x0000000F)
		{
			u8 marker[4] = { 0, 0, 0, 0 };
			if (pos + 4 <= max_len)
			{
				memcpy (marker, data + pos, 4);
				pos += 4;
			}
			bool all_zero = !(marker[0] | marker[1] | marker[2] | marker[3]);
			if (all_zero)
			{
				if (out_pos + 10 >= cap)
				{
					cap *= 2;
					out = REALLOC (out, cap);
				}
				memcpy (out + out_pos, "[/tag]", 6);
				out_pos += 6;
			}
			else
			{
				if (out_pos + 24 >= cap)
				{
					cap *= 2;
					out = REALLOC (out, cap);
				}
				int n = snprintf (out + out_pos, cap - out_pos, "[/tag:%02X%02X%02X%02X]",
					marker[0], marker[1], marker[2], marker[3]);
				out_pos += n;
			}
		}
		else
		{
			msbt_append_cp (&out, &out_pos, &cap, ch);
		}
	}

	out[out_pos] = 0;
	return out;
}

enumError ScanMSBT (msbt_file_t *msbt, const u8 *data, uint data_size, ccp fname)
{
	if (!msbt || !data || data_size < 8)
		return ERR_INVALID_DATA;

	if (data_size >= 8 && !memcmp (data, "FZIP", 4))
	{
		u8 *dec = 0;
		uint dec_sz = 0;
		enumError derr = DecodeFZIP (&dec, &dec_sz, data, data_size);
		if (!derr && dec)
		{
			enumError ret = ScanMSBT (msbt, dec, dec_sz, fname);
			FREE (dec);
			return ret;
		}
	}

	if (data_size < 0x20 || memcmp (data, "MsgStdBn", 8))
		return ERR_WRONG_FILE_TYPE;

	InitMSBT (msbt);
	if (fname)
		msbt->fname = STRDUP (fname);

	u16 bom = rd_be16 (data + 8);
	msbt->is_big_endian = (bom == 0xFEFF);
	bool be = msbt->is_big_endian;

	msbt->encoding = (msbt_encoding_t)data[0x0C];
	msbt->version = data[0x0D];
	u16 num_sections = r16 (data + 0x0E, be);

	// Section pointers (CLMS parity: LBL1/NLI1, ATO1, ATR1, TSY1, TXT2/TXTW)
	const u8 *lbl1_data = 0;
	uint lbl1_size = 0;
	const u8 *nli1_data = 0;
	uint nli1_size = 0;
	const u8 *ato1_data = 0;
	uint ato1_size = 0;
	const u8 *txt2_data = 0;
	uint txt2_size = 0;
	const u8 *atr1_data = 0;
	uint atr1_size = 0;
	const u8 *tsy1_data = 0;
	uint tsy1_size = 0;
	bool is_wmbt = false;

	uint cur = 0x20;
	for (uint s = 0; s < num_sections && cur + 16 <= data_size; s++)
	{
		char sec_magic[5] = { 0 };
		memcpy (sec_magic, data + cur, 4);
		if (cur + 16 > data_size)
			break;
		u32 sec_size = r32 (data + cur + 4, be);
		// Every section parser below trusts sec_size as its body length.
		if (sec_size > data_size - cur - 16)
			sec_size = data_size - cur - 16;
		const u8 *sec_body = data + cur + 16;

		if (!strcmp (sec_magic, "LBL1"))
		{
			lbl1_data = sec_body;
			lbl1_size = sec_size;
		}
		else if (!strcmp (sec_magic, "NLI1"))
		{
			nli1_data = sec_body;
			nli1_size = sec_size;
		}
		else if (!strcmp (sec_magic, "ATO1"))
		{
			ato1_data = sec_body;
			ato1_size = sec_size;
		}
		else if (!strcmp (sec_magic, "TXT2") || !strcmp (sec_magic, "TXTW"))
		{
			txt2_data = sec_body;
			txt2_size = sec_size;
			if (!strcmp (sec_magic, "TXTW"))
				is_wmbt = true;
		}
		else if (!strcmp (sec_magic, "ATR1"))
		{
			atr1_data = sec_body;
			atr1_size = sec_size;
		}
		else if (!strcmp (sec_magic, "TSY1"))
		{
			tsy1_data = sec_body;
			tsy1_size = sec_size;
		}

		// Section stride is 16-byte aligned
		uint aligned_size = (sec_size + 15) & ~15;
		cur += 16 + aligned_size;
	}

	msbt->is_wmbt = is_wmbt;
	if (ato1_data && ato1_size > 0)
	{
		msbt->has_ato1 = true;
		msbt->ato1_size = ato1_size;
		msbt->ato1_data = MALLOC (ato1_size);
		uint avail = ato1_size;
		if (ato1_data + ato1_size > data + data_size)
			avail = (uint)(data + data_size - ato1_data);
		memcpy (msbt->ato1_data, ato1_data, avail);
	}

	if (!txt2_data || txt2_size < 4)
		return ERR_INVALID_DATA;

	u32 num_strings = r32 (txt2_data, be);
	if (num_strings > 200000)
		return ERR_INVALID_DATA;
	if (!num_strings)
		return ERR_OK;

	msbt->entries = CALLOC (num_strings, sizeof (msbt_entry_t));
	msbt->num_entries = num_strings;
	msbt->alloc_entries = num_strings;

	for (uint i = 0; i < num_strings; i++)
	{
		msbt->entries[i].index = i;
		msbt->entries[i].msg_id = i;
		if (4 + (i + 1) * 4 <= txt2_size)
		{
			u32 str_off = r32 (txt2_data + 4 + i * 4, be);
			if (str_off < txt2_size)
			{
				const u8 *str_bytes = txt2_data + str_off;
				uint max_avail = txt2_size - str_off;

				if (msbt->encoding == MSBT_ENC_UTF8)
					msbt->entries[i].text = decode_utf8_msbt_string (str_bytes, max_avail, be);
				else if (msbt->encoding == MSBT_ENC_UTF32)
					msbt->entries[i].text = decode_utf32_msbt_string (str_bytes, max_avail, be);
				else
					msbt->entries[i].text = decode_utf16_msbt_string (str_bytes, max_avail, be);
				if (!msbt->entries[i].text)
					msbt->entries[i].text = STRDUP ("");
			}
			else
				msbt->entries[i].text = STRDUP ("");
		}
		else
			msbt->entries[i].text = STRDUP ("");
	}

	// Parse labels from LBL1 (and remember slot count for byte-stable rebuilds)
	if (lbl1_data && lbl1_size >= 4)
	{
		u32 num_groups = r32 (lbl1_data, be);
		msbt->label_slot_count = num_groups;
		for (uint g = 0; g < num_groups && 4 + (g + 1) * 8 <= lbl1_size; g++)
		{
			u32 count = r32 (lbl1_data + 4 + g * 8, be);
			u32 group_off = r32 (lbl1_data + 4 + g * 8 + 4, be);

			uint pos = group_off;
			for (uint c = 0; c < count && pos < lbl1_size; c++)
			{
				u8 nlen = lbl1_data[pos++];
				if (pos + nlen + 4 <= lbl1_size)
				{
					char name[256];
					memcpy (name, lbl1_data + pos, nlen);
					name[nlen] = 0;
					pos += nlen;

					u32 str_idx = r32 (lbl1_data + pos, be);
					pos += 4;

					if (str_idx < msbt->num_entries)
					{
						if (msbt->entries[str_idx].label)
							FREE (msbt->entries[str_idx].label);
						msbt->entries[str_idx].label = STRDUP (name);
					}
				}
				else
					break;
			}
		}
	}

	// Parse numeric IDs from NLI1 (CLMS UsesMessageID)
	if (nli1_data && nli1_size >= 4)
	{
		u32 num_lines = r32 (nli1_data, be);
		if (num_lines <= msbt->num_entries + 1000000 && 4 + num_lines * 8 <= nli1_size)
		{
			msbt->uses_nli1 = true;
			for (uint i = 0; i < num_lines; i++)
			{
				u32 id = r32 (nli1_data + 4 + i * 8, be);
				u32 idx = r32 (nli1_data + 4 + i * 8 + 4, be);
				if (idx < msbt->num_entries)
					msbt->entries[idx].msg_id = id;
			}
		}
	}

	// Parse attributes from ATR1 (with optional trailing string table).
	// When the section is larger than the attribute table, each item's last
	// 4 bytes are an offset (from ATR1 body start) to a NUL-terminated string.
	if (atr1_data && atr1_size >= 8)
	{
		u32 attr_count = r32 (atr1_data, be);
		u32 item_size = r32 (atr1_data + 4, be);
		msbt->attr_item_size = item_size;

		if (item_size > 0 && attr_count > 0 && 8 + attr_count * item_size <= atr1_size)
		{
			uint table_end = 8 + attr_count * item_size;
			bool has_strings = (atr1_size > table_end);
			msbt->uses_attr_strings = has_strings;
			for (uint i = 0; i < attr_count && i < msbt->num_entries; i++)
			{
				const u8 *raw = atr1_data + 8 + i * item_size;
				if (!has_strings)
				{
					msbt->entries[i].attrib = MALLOC (item_size);
					msbt->entries[i].attrib_size = item_size;
					memcpy (msbt->entries[i].attrib, raw, item_size);
				}
				else
				{
					uint data_len = item_size > 4 ? item_size - 4 : 0;
					u32 str_off = r32 (raw + data_len, be);
					if (data_len > 0)
					{
						msbt->entries[i].attrib = MALLOC (data_len);
						msbt->entries[i].attrib_size = data_len;
						memcpy (msbt->entries[i].attrib, raw, data_len);
					}
					if (str_off < atr1_size)
					{
						uint maxl = atr1_size - str_off;
						uint slen = 0;
						while (slen < maxl && atr1_data[str_off + slen])
							slen++;
						char *s = MALLOC (slen + 1);
						memcpy (s, atr1_data + str_off, slen);
						s[slen] = 0;
						msbt->entries[i].attr_str = s;
					}
				}
			}
		}
	}

	// Parse styles from TSY1 (plain u32 array, no header)
	if (tsy1_data && tsy1_size >= 4 && num_strings > 0)
	{
		uint avail = tsy1_size / 4;
		if (avail > 0)
		{
			msbt->has_tsy1 = true;
			for (uint i = 0; i < num_strings && i < avail; i++)
				msbt->entries[i].style_index = r32 (tsy1_data + i * 4, be);
		}
	}

	return ERR_OK;
}

enumError SaveTextMSBT (const msbt_file_t *msbt, ccp dest_fname)
{
	if (!msbt || !dest_fname)
		return ERR_INVALID_DATA;

	FILE *f = fopen (dest_fname, "w");
	if (!f)
		return ERR_CANT_CREATE;

	const char *enc_name = "UTF-16";
	if (msbt->encoding == MSBT_ENC_UTF8)
		enc_name = "UTF-8";
	else if (msbt->encoding == MSBT_ENC_UTF32)
		enc_name = "UTF-32";
	fprintf (f, "# MSBT: Message Studio Binary Text (%s, %s)\n",
		msbt->is_big_endian ? "BigEndian" : "LittleEndian", enc_name);
	fprintf (f, "# Entries: %u\n", msbt->num_entries);
	// Optional parity headers (ignored by older builds; keeps LBL1 slot
	// count, NLI1 mode, ATO1 blob and WMBT variant round-trippable).
	if (msbt->uses_nli1)
		fprintf (f, "# UseIndices: true\n");
	if (msbt->label_slot_count > 0)
		fprintf (f, "# SlotNum: %u\n", msbt->label_slot_count);
	if (msbt->has_ato1 && msbt->ato1_data && msbt->ato1_size > 0)
	{
		fprintf (f, "# ATO1: ");
		for (uint a = 0; a < msbt->ato1_size; a++)
			fprintf (f, "%02X", msbt->ato1_data[a]);
		fprintf (f, "\n");
	}
	if (msbt->is_wmbt)
		fprintf (f, "# WMBT: true\n");
	fprintf (f, "\n");

	for (uint i = 0; i < msbt->num_entries; i++)
	{
		const msbt_entry_t *e = msbt->entries + i;
		if (msbt->uses_nli1)
			fprintf (f, "[%u]\n", e->msg_id);
		else if (e->label && *e->label)
			fprintf (f, "[%s]\n", e->label);
		else
			fprintf (f, "[#%u]\n", i);

		if (e->attrib && e->attrib_size > 0)
		{
			fprintf (f, "@attr=");
			for (uint a = 0; a < e->attrib_size; a++)
				fprintf (f, "%02X", e->attrib[a]);
			fprintf (f, "\n");
		}
		if (e->attr_str && *e->attr_str)
			fprintf (f, "@attrstr=%s\n", e->attr_str);
		if (msbt->has_tsy1)
			fprintf (f, "@style=%u\n", e->style_index);

		if (e->text)
		{
			// Escape newlines cleanly
			const char *p = e->text;
			while (*p)
			{
				if (*p == '\n')
					fputs ("\\n\n", f);
				else
					fputc (*p, f);
				p++;
			}
		}
		fprintf (f, "\n\n");
	}

	fclose (f);
	return ERR_OK;
}

enumError SaveJSONMSBT (const msbt_file_t *msbt, ccp dest_fname)
{
	if (!msbt || !dest_fname)
		return ERR_INVALID_DATA;

	FILE *f = fopen (dest_fname, "w");
	if (!f)
		return ERR_CANT_CREATE;

	const char *jenc = "utf-16";
	if (msbt->encoding == MSBT_ENC_UTF8)
		jenc = "utf-8";
	else if (msbt->encoding == MSBT_ENC_UTF32)
		jenc = "utf-32";
	fprintf (f, "{\n");
	fprintf (f, "  \"endian\": \"%s\",\n", msbt->is_big_endian ? "big" : "little");
	fprintf (f, "  \"encoding\": \"%s\",\n", jenc);
	fprintf (f, "  \"version\": %u,\n", msbt->version);
	fprintf (f, "  \"messages\": [\n");

	for (uint i = 0; i < msbt->num_entries; i++)
	{
		const msbt_entry_t *e = msbt->entries + i;
		fprintf (f, "    {\n");
		fprintf (f, "      \"index\": %u,\n", e->index);
		fprintf (f, "      \"label\": \"%s\",\n", e->label ? e->label : "");

		if (e->attrib && e->attrib_size > 0)
		{
			fprintf (f, "      \"attribute\": \"");
			for (uint a = 0; a < e->attrib_size; a++)
				fprintf (f, "%02X", e->attrib[a]);
			fprintf (f, "\",\n");
		}

		fprintf (f, "      \"text\": \"");
		if (e->text)
		{
			for (const char *p = e->text; *p; p++)
			{
				if (*p == '"')
					fputs ("\\\"", f);
				else if (*p == '\\')
					fputs ("\\\\", f);
				else if (*p == '\n')
					fputs ("\\n", f);
				else if (*p == '\r')
					fputs ("\\r", f);
				else if (*p == '\t')
					fputs ("\\t", f);
				else
					fputc (*p, f);
			}
		}
		fprintf (f, "\"\n");
		fprintf (f, "    }%s\n", (i + 1 < msbt->num_entries) ? "," : "");
	}

	fprintf (f, "  ]\n}\n");
	fclose (f);
	return ERR_OK;
}

// Encode one code point into the MSBT string buffer
static void encode_msbt_cp (u8 *buf, uint *len, uint cap, u32 cp, msbt_encoding_t enc, bool be)
{
	(void)cap;
	if (enc == MSBT_ENC_UTF16)
	{
		if (cp <= 0xFFFF)
		{
			w16 (buf + *len, (u16)cp, be);
			*len += 2;
		}
		else
		{
			cp -= 0x10000;
			w16 (buf + *len, (u16)(0xD800 | (cp >> 10)), be);
			*len += 2;
			w16 (buf + *len, (u16)(0xDC00 | (cp & 0x3FF)), be);
			*len += 2;
		}
	}
	else if (enc == MSBT_ENC_UTF32)
	{
		w32 (buf + *len, cp, be);
		*len += 4;
	}
	else
	{
		if (cp < 0x80)
		{
			buf[(*len)++] = (u8)cp;
		}
		else if (cp < 0x800)
		{
			buf[(*len)++] = (u8)(0xC0 | (cp >> 6));
			buf[(*len)++] = (u8)(0x80 | (cp & 0x3F));
		}
		else if (cp < 0x10000)
		{
			buf[(*len)++] = (u8)(0xE0 | (cp >> 12));
			buf[(*len)++] = (u8)(0x80 | ((cp >> 6) & 0x3F));
			buf[(*len)++] = (u8)(0x80 | (cp & 0x3F));
		}
		else
		{
			buf[(*len)++] = (u8)(0xF0 | (cp >> 18));
			buf[(*len)++] = (u8)(0x80 | ((cp >> 12) & 0x3F));
			buf[(*len)++] = (u8)(0x80 | ((cp >> 6) & 0x3F));
			buf[(*len)++] = (u8)(0x80 | (cp & 0x3F));
		}
	}
}

static void encode_msbt_newline (u8 *buf, uint *len, msbt_encoding_t enc, bool be)
{
	if (enc == MSBT_ENC_UTF16)
	{
		w16 (buf + *len, '\n', be);
		*len += 2;
	}
	else if (enc == MSBT_ENC_UTF32)
	{
		w32 (buf + *len, '\n', be);
		*len += 4;
	}
	else
	{
		buf[(*len)++] = '\n';
	}
}

static void encode_msbt_tag_open (
	u8 *buf, uint *len, u32 group, u32 tag, const u8 *params, uint num_params, msbt_encoding_t enc,
	bool be)
{
	if (enc == MSBT_ENC_UTF16)
	{
		w16 (buf + *len, 0x000E, be);
		*len += 2;
	}
	else if (enc == MSBT_ENC_UTF32)
	{
		w32 (buf + *len, 0x0000000E, be);
		*len += 4;
	}
	else
	{
		buf[(*len)++] = 0x0E;
	}
	w16 (buf + *len, group, be);
	*len += 2;
	w16 (buf + *len, tag, be);
	*len += 2;
	w16 (buf + *len, num_params, be);
	*len += 2;
	if (num_params > 0)
	{
		memcpy (buf + *len, params, num_params);
		*len += num_params;
	}
}

static void encode_msbt_tag_end (u8 *buf, uint *len, const u8 *marker, msbt_encoding_t enc, bool be)
{
	u8 def[4] = { 0, 0, 0, 0 };
	if (!marker)
		marker = def;
	if (enc == MSBT_ENC_UTF16)
	{
		w16 (buf + *len, 0x000F, be);
		*len += 2;
	}
	else if (enc == MSBT_ENC_UTF32)
	{
		w32 (buf + *len, 0x0000000F, be);
		*len += 4;
	}
	else
	{
		buf[(*len)++] = 0x0F;
	}
	memcpy (buf + *len, marker, 4);
	*len += 4;
}

// Encode UTF-8 string (containing tags like [tag:1,2,0011], [/tag[:HEX]] or \n)
// into binary buffer (UTF-16, UTF-8 or UTF-32)
static void encode_msbt_string (
	u8 **out_buf, uint *out_len, const char *text, msbt_encoding_t enc, bool be)
{
	uint cap = strlen (text ? text : "") * 4 + 64;
	u8 *buf = MALLOC (cap);
	uint len = 0;

	const char *p = text ? text : "";
	while (*p)
	{
		if (*p == '\\' && *(p + 1) == 'n')
		{
			if (len + 8 >= cap)
			{
				cap = cap * 2 + 64;
				buf = REALLOC (buf, cap);
			}
			encode_msbt_newline (buf, &len, enc, be);
			p += 2;
		}
		else if (*p == '[' && !strncmp (p, "[tag:", 5))
		{
			p += 5;
			u32 group = 0, tag = 0;
			char *endp = 0;
			group = strtoul (p, &endp, 10);
			if (endp && *endp == ',')
			{
				p = endp + 1;
				tag = strtoul (p, &endp, 10);
				p = endp;
			}

			u8 hex_params[256];
			uint num_params = 0;
			if (*p == ',')
			{
				p++;
				while (
					isxdigit ((u8)p[0]) && isxdigit ((u8)p[1]) && num_params < sizeof (hex_params))
				{
					char h[3] = { p[0], p[1], 0 };
					hex_params[num_params++] = (u8)strtoul (h, 0, 16);
					p += 2;
				}
			}
			if (*p == ']')
				p++;

			if (len + 16 + num_params >= cap)
			{
				cap = cap * 2 + num_params + 64;
				buf = REALLOC (buf, cap);
			}
			encode_msbt_tag_open (buf, &len, group, tag, hex_params, num_params, enc, be);
		}
		else if (*p == '[' && (!strncmp (p, "[/tag]", 6) || !strncmp (p, "[/tag:", 6)))
		{
			u8 marker[4] = { 0, 0, 0, 0 };
			if (!strncmp (p, "[/tag]", 6))
			{
				p += 6;
			}
			else
			{
				p += 6;
				for (int i = 0; i < 4 && isxdigit ((u8)p[0]) && isxdigit ((u8)p[1]); i++)
				{
					char h[3] = { p[0], p[1], 0 };
					marker[i] = (u8)strtoul (h, 0, 16);
					p += 2;
				}
				if (*p == ']')
					p++;
			}
			if (len + 12 >= cap)
			{
				cap *= 2;
				buf = REALLOC (buf, cap);
			}
			encode_msbt_tag_end (buf, &len, marker, enc, be);
		}
		else
		{
			// Read UTF-8 character
			u32 cp = 0;
			int step = 1;
			u8 b0 = (u8)*p;
			if (b0 < 0x80)
			{
				cp = b0;
				step = 1;
			}
			else if ((b0 & 0xE0) == 0xC0 && p[1])
			{
				cp = ((b0 & 0x1F) << 6) | (p[1] & 0x3F);
				step = 2;
			}
			else if ((b0 & 0xF0) == 0xE0 && p[1] && p[2])
			{
				cp = ((b0 & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
				step = 3;
			}
			else if ((b0 & 0xF8) == 0xF0 && p[1] && p[2] && p[3])
			{
				cp = ((b0 & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6)
					| (p[3] & 0x3F);
				step = 4;
			}
			else
			{
				cp = b0;
				step = 1;
			}
			p += step;

			if (len + 8 >= cap)
			{
				cap = cap * 2 + 64;
				buf = REALLOC (buf, cap);
			}
			encode_msbt_cp (buf, &len, cap, cp, enc, be);
		}
	}

	// Null terminator
	if (len + 8 >= cap)
	{
		cap += 16;
		buf = REALLOC (buf, cap);
	}
	if (enc == MSBT_ENC_UTF16)
	{
		w16 (buf + len, 0, be);
		len += 2;
	}
	else if (enc == MSBT_ENC_UTF32)
	{
		w32 (buf + len, 0, be);
		len += 4;
	}
	else
	{
		buf[len++] = 0;
	}

	*out_buf = buf;
	*out_len = len;
}

enumError CreateMSBT (u8 **out_data, uint *out_size, const msbt_file_t *msbt)
{
	if (!out_data || !out_size || !msbt)
		return ERR_INVALID_DATA;

	bool be = msbt->is_big_endian;
	msbt_encoding_t enc = msbt->encoding;
	bool use_nli1 = msbt->uses_nli1;

	// Check if any labels exist (LBL1 mode only)
	bool has_labels = false;
	if (!use_nli1)
	{
		for (uint i = 0; i < msbt->num_entries; i++)
		{
			if (msbt->entries[i].label && *msbt->entries[i].label)
			{
				has_labels = true;
				break;
			}
		}
	}

	// Check if attributes exist
	bool has_attribs = (msbt->attr_item_size > 0);
	for (uint i = 0; !has_attribs && i < msbt->num_entries; i++)
	{
		if (msbt->entries[i].attrib_size > 0
			|| (msbt->entries[i].attr_str && *msbt->entries[i].attr_str))
			has_attribs = true;
	}
	bool has_attr_strings = msbt->uses_attr_strings;
	for (uint i = 0; !has_attr_strings && i < msbt->num_entries; i++)
	{
		if (msbt->entries[i].attr_str && *msbt->entries[i].attr_str)
			has_attr_strings = true;
	}

	// Check if styles exist
	bool has_styles = msbt->has_tsy1;
	if (!has_styles)
	{
		for (uint i = 0; i < msbt->num_entries; i++)
		{
			if (msbt->entries[i].style_index != 0)
			{
				has_styles = true;
				break;
			}
		}
	}

	// Build LBL1 section (official bucket count is 101; preserve original)
	u8 *lbl1_buf = 0;
	uint lbl1_len = 0;
	if (has_labels && !use_nli1)
	{
		uint num_groups = msbt->label_slot_count > 0 ? msbt->label_slot_count : 101;

		// Group counts
		u32 *group_counts = CALLOC (num_groups, sizeof (u32));
		for (uint i = 0; i < msbt->num_entries; i++)
		{
			if (msbt->entries[i].label && *msbt->entries[i].label)
			{
				u32 g = msbt_hash (msbt->entries[i].label, num_groups);
				group_counts[g]++;
			}
		}

		uint group_table_size = 4 + num_groups * 8;
		uint total_labels_size = 0;
		for (uint i = 0; i < msbt->num_entries; i++)
		{
			if (msbt->entries[i].label && *msbt->entries[i].label)
				total_labels_size += 1 + strlen (msbt->entries[i].label) + 4;
		}

		lbl1_len = group_table_size + total_labels_size;
		lbl1_buf = CALLOC (lbl1_len + 16, 1);

		w32 (lbl1_buf, num_groups, be);
		uint cur_label_off = group_table_size;
		for (uint g = 0; g < num_groups; g++)
		{
			w32 (lbl1_buf + 4 + g * 8, group_counts[g], be);
			w32 (lbl1_buf + 4 + g * 8 + 4, cur_label_off, be);

			for (uint i = 0; i < msbt->num_entries; i++)
			{
				if (msbt->entries[i].label && *msbt->entries[i].label)
				{
					if (msbt_hash (msbt->entries[i].label, num_groups) == g)
					{
						uint nlen = strlen (msbt->entries[i].label);
						lbl1_buf[cur_label_off++] = (u8)nlen;
						memcpy (lbl1_buf + cur_label_off, msbt->entries[i].label, nlen);
						cur_label_off += nlen;
						w32 (lbl1_buf + cur_label_off, i, be);
						cur_label_off += 4;
					}
				}
			}
		}
		FREE (group_counts);
	}

	// Build NLI1 section (numeric IDs instead of LBL1)
	u8 *nli1_buf = 0;
	uint nli1_len = 0;
	if (use_nli1 && msbt->num_entries > 0)
	{
		nli1_len = 4 + msbt->num_entries * 8;
		nli1_buf = CALLOC (nli1_len + 16, 1);
		w32 (nli1_buf, msbt->num_entries, be);
		for (uint i = 0; i < msbt->num_entries; i++)
		{
			w32 (nli1_buf + 4 + i * 8, msbt->entries[i].msg_id, be);
			w32 (nli1_buf + 4 + i * 8 + 4, i, be);
		}
	}

	// Build ATO1 section (opaque preservation)
	u8 *ato1_buf = 0;
	uint ato1_len = 0;
	if (msbt->has_ato1 && msbt->ato1_data && msbt->ato1_size > 0)
	{
		ato1_len = msbt->ato1_size;
		ato1_buf = CALLOC (ato1_len + 16, 1);
		memcpy (ato1_buf, msbt->ato1_data, ato1_len);
	}

	// Build ATR1 section (with optional trailing string table).
	// attrib_size per entry is the data part only; attr_item_size is the
	// total item size on disk (including the 4-byte string offset when
	// attribute strings are used).
	u8 *atr1_buf = 0;
	uint atr1_len = 0;
	if (has_attribs)
	{
		uint data_width = 0;
		for (uint i = 0; i < msbt->num_entries; i++)
		{
			if (msbt->entries[i].attrib_size > data_width)
				data_width = msbt->entries[i].attrib_size;
		}
		if (has_attr_strings && msbt->attr_item_size >= 4)
		{
			uint orig_data = msbt->attr_item_size - 4;
			if (orig_data > data_width)
				data_width = orig_data;
		}
		else if (!has_attr_strings && msbt->attr_item_size > data_width)
			data_width = msbt->attr_item_size;
		if (data_width == 0)
			data_width = 4;
		uint item_sz = data_width + (has_attr_strings ? 4 : 0);

		uint table_size = 8 + msbt->num_entries * item_sz;
		uint str_table_len = 0;
		for (uint i = 0; i < msbt->num_entries; i++)
		{
			if (has_attr_strings && msbt->entries[i].attr_str)
				str_table_len += strlen (msbt->entries[i].attr_str) + 1;
			else if (has_attr_strings)
				str_table_len += 1;
		}
		atr1_len = table_size + str_table_len;
		atr1_buf = CALLOC (atr1_len + 16, 1);
		w32 (atr1_buf, msbt->num_entries, be);
		w32 (atr1_buf + 4, item_sz, be);
		uint str_off = table_size;
		for (uint i = 0; i < msbt->num_entries; i++)
		{
			u8 *dst = atr1_buf + 8 + i * item_sz;
			uint cpy = msbt->entries[i].attrib_size;
			uint data_cap = has_attr_strings ? (item_sz > 4 ? item_sz - 4 : 0) : item_sz;
			if (cpy > data_cap)
				cpy = data_cap;
			if (cpy > 0 && msbt->entries[i].attrib)
				memcpy (dst, msbt->entries[i].attrib, cpy);
			if (has_attr_strings)
			{
				const char *s = msbt->entries[i].attr_str ? msbt->entries[i].attr_str : "";
				w32 (dst + data_cap, str_off, be);
				uint sl = strlen (s) + 1;
				if (str_off + sl <= atr1_len)
				{
					memcpy (atr1_buf + str_off, s, sl);
					str_off += sl;
				}
			}
		}
		atr1_len = str_off;
	}

	// Build TSY1 section (plain u32 array, no header)
	u8 *tsy1_buf = 0;
	uint tsy1_len = 0;
	if (has_styles)
	{
		tsy1_len = msbt->num_entries * 4;
		tsy1_buf = CALLOC (tsy1_len + 16, 1);
		for (uint i = 0; i < msbt->num_entries; i++)
			w32 (tsy1_buf + i * 4, msbt->entries[i].style_index, be);
	}

	// Build TXT2/TXTW section
	uint offsets_size = 4 + msbt->num_entries * 4;
	u8 **str_buffers = CALLOC (msbt->num_entries ? msbt->num_entries : 1, sizeof (u8 *));
	uint *str_lens = CALLOC (msbt->num_entries ? msbt->num_entries : 1, sizeof (uint));
	uint total_str_bytes = 0;

	for (uint i = 0; i < msbt->num_entries; i++)
	{
		encode_msbt_string (&str_buffers[i], &str_lens[i], msbt->entries[i].text, enc, be);
		total_str_bytes += str_lens[i];
	}

	uint txt2_len = offsets_size + total_str_bytes;
	u8 *txt2_buf = CALLOC (txt2_len + 16, 1);
	w32 (txt2_buf, msbt->num_entries, be);

	uint cur_str_off = offsets_size;
	for (uint i = 0; i < msbt->num_entries; i++)
	{
		w32 (txt2_buf + 4 + i * 4, cur_str_off, be);
		memcpy (txt2_buf + cur_str_off, str_buffers[i], str_lens[i]);
		cur_str_off += str_lens[i];
		FREE (str_buffers[i]);
	}
	FREE (str_buffers);
	FREE (str_lens);

	// Calculate total file size (order: LBL1/NLI1, ATO1, ATR1, TSY1, TXT2/TXTW)
	u16 num_sections = 1; // TXT2/TXTW
	if (lbl1_buf)
		num_sections++;
	if (nli1_buf)
		num_sections++;
	if (ato1_buf)
		num_sections++;
	if (atr1_buf)
		num_sections++;
	if (tsy1_buf)
		num_sections++;

	uint total_size = 0x20;
	if (lbl1_buf)
		total_size += 16 + ((lbl1_len + 15) & ~15);
	if (nli1_buf)
		total_size += 16 + ((nli1_len + 15) & ~15);
	if (ato1_buf)
		total_size += 16 + ((ato1_len + 15) & ~15);
	if (atr1_buf)
		total_size += 16 + ((atr1_len + 15) & ~15);
	if (tsy1_buf)
		total_size += 16 + ((tsy1_len + 15) & ~15);
	total_size += 16 + ((txt2_len + 15) & ~15);

	u8 *out = CALLOC (total_size, 1);

	// File header (LMS spec: filesize at 0x12, 10 zero bytes to 0x20)
	memcpy (out, "MsgStdBn", 8);
	wr_be16 (out + 8, be ? 0xFEFF : 0xFFFE);
	out[0x0A] = 0;
	out[0x0B] = 0;
	out[0x0C] = (u8)enc;
	out[0x0D] = msbt->version ? msbt->version : 3;
	w16 (out + 0x0E, num_sections, be);
	out[0x10] = 0;
	out[0x11] = 0;
	w32 (out + 0x12, total_size, be);
	memset (out + 0x16, 0, 0x0A);

	uint cur_sec = 0x20;
#define MSBT_EMIT_SEC(magic_, buf_, len_)                                                                  \
	do {                                                                                               \
		memcpy (out + cur_sec, magic_, 4);                                                         \
		w32 (out + cur_sec + 4, len_, be);                                                         \
		memset (out + cur_sec + 8, 0, 8);                                                          \
		memcpy (out + cur_sec + 16, buf_, len_);                                                  \
		{                                                                                          \
			uint end = cur_sec + 16 + len_;                                                    \
			uint aligned = cur_sec + 16 + ((len_ + 15) & ~15);                                 \
			for (uint p = end; p < aligned; p++)                                              \
				out[p] = 0xAB;                                                             \
			cur_sec = aligned;                                                                 \
		}                                                                                          \
		FREE (buf_);                                                                               \
	} while (0)

	if (lbl1_buf)
		MSBT_EMIT_SEC ("LBL1", lbl1_buf, lbl1_len);
	if (nli1_buf)
		MSBT_EMIT_SEC ("NLI1", nli1_buf, nli1_len);
	if (ato1_buf)
		MSBT_EMIT_SEC ("ATO1", ato1_buf, ato1_len);
	if (atr1_buf)
		MSBT_EMIT_SEC ("ATR1", atr1_buf, atr1_len);
	if (tsy1_buf)
		MSBT_EMIT_SEC ("TSY1", tsy1_buf, tsy1_len);
	if (txt2_buf)
	{
		const char *magic = msbt->is_wmbt ? "TXTW" : "TXT2";
		MSBT_EMIT_SEC (magic, txt2_buf, txt2_len);
	}
#undef MSBT_EMIT_SEC

	*out_data = out;
	*out_size = total_size;
	return ERR_OK;
}

enumError LoadTextMSBT (msbt_file_t *msbt, ccp src_fname)
{
	if (!msbt || !src_fname)
		return ERR_INVALID_DATA;

	FILE *f = fopen (src_fname, "r");
	if (!f)
		return ERR_CANT_OPEN;

	InitMSBT (msbt);
	msbt->fname = STRDUP (src_fname);

	// Larger line buffer: ATO1 hex blobs can be long single lines.
	uint line_cap = 65536;
	char *line = MALLOC (line_cap);
	char cur_label[512] = "";
	char *cur_text = MALLOC (65536);
	cur_text[0] = 0;
	uint cur_text_cap = 65536;
	u8 cur_attr[1024] = { 0 };
	uint cur_attr_len = 0;
	char cur_attrstr[4096] = "";
	uint cur_style = 0;
	bool cur_has_style = false;
	bool in_entry = false;

// Helper to flush one entry (keeps NLI1 IDs, attr strings and styles).
#define MSBT_FLUSH_ENTRY()                                                                             \
	do {                                                                                           \
		uint text_len = strlen (cur_text);                                                     \
		if (text_len && cur_text[text_len - 1] == '\n')                                         \
			cur_text[text_len - 1] = 0;                                                    \
		if (msbt->num_entries >= msbt->alloc_entries)                                          \
		{                                                                                      \
			msbt->alloc_entries = msbt->alloc_entries ? msbt->alloc_entries * 2 : 16;       \
			msbt->entries = REALLOC (                                                     \
				msbt->entries, msbt->alloc_entries * sizeof (msbt_entry_t));            \
		}                                                                                      \
		msbt_entry_t *e = msbt->entries + msbt->num_entries;                                   \
		memset (e, 0, sizeof (*e));                                                           \
		e->index = msbt->num_entries;                                                          \
		e->msg_id = msbt->num_entries;                                                         \
		if (msbt->uses_nli1)                                                                   \
		{                                                                                      \
			if (*cur_label && cur_label[0] != '#')                                          \
				e->msg_id = (u32)strtoul (cur_label, 0, 10);                            \
			else if (*cur_label == '#')                                                    \
				e->msg_id = (u32)strtoul (cur_label + 1, 0, 10);                        \
		}                                                                                      \
		else if (*cur_label && cur_label[0] != '#')                                            \
			e->label = STRDUP (cur_label);                                                 \
		e->text = STRDUP (cur_text);                                                           \
		if (cur_attr_len > 0)                                                                  \
		{                                                                                      \
			e->attrib = MALLOC (cur_attr_len);                                             \
			e->attrib_size = cur_attr_len;                                                 \
			memcpy (e->attrib, cur_attr, cur_attr_len);                                    \
			uint eff = cur_attr_len + ((*cur_attrstr) ? 4 : 0);                            \
			if (msbt->attr_item_size < eff)                                                \
				msbt->attr_item_size = eff;                                            \
		}                                                                                      \
		if (*cur_attrstr)                                                                       \
		{                                                                                      \
			e->attr_str = STRDUP (cur_attrstr);                                            \
			msbt->uses_attr_strings = true;                                                \
			uint eff = cur_attr_len + 4;                                                   \
			if (msbt->attr_item_size < eff)                                                \
				msbt->attr_item_size = eff;                                            \
		}                                                                                      \
		if (cur_has_style)                                                                     \
		{                                                                                      \
			e->style_index = cur_style;                                                    \
			msbt->has_tsy1 = true;                                                         \
		}                                                                                      \
		msbt->num_entries++;                                                                   \
	} while (0)

	while (fgets (line, line_cap, f))
	{
		// Strip trailing CR/LF
		uint len = strlen (line);
		while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n'))
			line[--len] = 0;

		if (line[0] == '#' || (line[0] == '/' && line[1] == '/'))
		{
			if (strstr (line, "BigEndian"))
				msbt->is_big_endian = true;
			if (strstr (line, "LittleEndian"))
				msbt->is_big_endian = false;
			if (strstr (line, "UTF-32"))
				msbt->encoding = MSBT_ENC_UTF32;
			else if (strstr (line, "UTF-8"))
				msbt->encoding = MSBT_ENC_UTF8;
			else if (strstr (line, "UTF-16"))
				msbt->encoding = MSBT_ENC_UTF16;
			if (strstr (line, "UseIndices"))
			{
				if (strstr (line, "true") || strstr (line, "True") || strstr (line, "1"))
					msbt->uses_nli1 = true;
				else
					msbt->uses_nli1 = false;
			}
			if (!strncmp (line, "# SlotNum:", 10))
				msbt->label_slot_count = (u32)strtoul (line + 10, 0, 10);
			else if (strstr (line, "SlotNum"))
			{
				char *col = strchr (line, ':');
				if (col)
					msbt->label_slot_count = (u32)strtoul (col + 1, 0, 10);
			}
			if (!strncmp (line, "# ATO1:", 7))
			{
				const char *h = line + 7;
				while (*h == ' ' || *h == '\t')
					h++;
				uint hlen = strlen (h) / 2;
				if (hlen > 0)
				{
					msbt->ato1_data = MALLOC (hlen);
					msbt->ato1_size = 0;
					while (isxdigit ((u8)h[0]) && isxdigit ((u8)h[1]))
					{
						char hx[3] = { h[0], h[1], 0 };
						msbt->ato1_data[msbt->ato1_size++]
							= (u8)strtoul (hx, 0, 16);
						h += 2;
					}
					msbt->has_ato1 = msbt->ato1_size > 0;
				}
			}
			if (strstr (line, "WMBT"))
			{
				if (strstr (line, "true") || strstr (line, "True") || strstr (line, "1"))
					msbt->is_wmbt = true;
			}
			continue;
		}

		if (line[0] == '[' && line[len - 1] == ']' && len > 2)
		{
			// Save previous entry
			if (in_entry)
				MSBT_FLUSH_ENTRY ();

			in_entry = true;
			snprintf (cur_label, sizeof (cur_label), "%.*s", (int)(len - 2), line + 1);
			cur_text[0] = 0;
			cur_attr_len = 0;
			cur_attrstr[0] = 0;
			cur_style = 0;
			cur_has_style = false;
		}
		else if (in_entry)
		{
			if (!strncmp (line, "@attr=", 6))
			{
				const char *h = line + 6;
				cur_attr_len = 0;
				while (
					isxdigit ((u8)h[0]) && isxdigit ((u8)h[1]) && cur_attr_len < sizeof (cur_attr))
				{
					char hx[3] = { h[0], h[1], 0 };
					cur_attr[cur_attr_len++] = (u8)strtoul (hx, 0, 16);
					h += 2;
				}
			}
			else if (!strncmp (line, "@attrstr=", 9))
			{
				snprintf (cur_attrstr, sizeof (cur_attrstr), "%s", line + 9);
			}
			else if (!strncmp (line, "@style=", 7))
			{
				cur_style = (u32)strtoul (line + 7, 0, 10);
				cur_has_style = true;
			}
			else
			{
				// SaveTextMSBT writes an escaped "\\n" and then a physical
				// newline for readability.  That physical newline is not a
				// second message newline. Hand-written multi-line input that
				// does not use the escape still retains its line boundary.
				const uint text_len = strlen (cur_text);
				if (text_len
					&& !(text_len >= 2 && cur_text[text_len - 2] == '\\'
						&& cur_text[text_len - 1] == 'n'))
				{
					if (strlen (cur_text) + 2 >= cur_text_cap)
					{
						cur_text_cap *= 2;
						cur_text = REALLOC (cur_text, cur_text_cap);
					}
					strncat (cur_text, "\n", cur_text_cap - strlen (cur_text) - 1);
				}
				if (strlen (cur_text) + len + 2 >= cur_text_cap)
				{
					while (strlen (cur_text) + len + 2 >= cur_text_cap)
						cur_text_cap *= 2;
					cur_text = REALLOC (cur_text, cur_text_cap);
				}
				strncat (cur_text, line, cur_text_cap - strlen (cur_text) - 1);
			}
		}
	}

	if (in_entry)
		MSBT_FLUSH_ENTRY ();
#undef MSBT_FLUSH_ENTRY

	FREE (line);
	FREE (cur_text);
	fclose (f);
	return ERR_OK;
}

// MSBP API Implementation
void InitMSBP (msbp_file_t *msbp)
{
	if (!msbp)
		return;
	memset (msbp, 0, sizeof (*msbp));
	msbp->encoding = MSBT_ENC_UTF16;
	msbp->version = 3;
	msbp->label_slot_count = 0; // 0 = auto (29 on create)
}

static void msbp_free_str_list (char **list, uint n)
{
	if (!list)
		return;
	for (uint i = 0; i < n; i++)
		if (list[i])
			FREE (list[i]);
	FREE (list);
}

void ResetMSBP (msbp_file_t *msbp)
{
	if (!msbp)
		return;
	if (msbp->fname)
	{
		FREE (msbp->fname);
		msbp->fname = 0;
	}
	if (msbp->colors)
	{
		for (uint i = 0; i < msbp->num_colors; i++)
			if (msbp->colors[i].name)
				FREE (msbp->colors[i].name);
		FREE (msbp->colors);
		msbp->colors = 0;
		msbp->num_colors = 0;
	}
	if (msbp->attributes)
	{
		for (uint i = 0; i < msbp->num_attributes; i++)
		{
			if (msbp->attributes[i].name)
				FREE (msbp->attributes[i].name);
			msbp_free_str_list (
				msbp->attributes[i].list_items, msbp->attributes[i].num_list_items);
			msbp->attributes[i].list_items = 0;
			msbp->attributes[i].num_list_items = 0;
		}
		FREE (msbp->attributes);
		msbp->attributes = 0;
		msbp->num_attributes = 0;
	}
	if (msbp->tag_groups)
	{
		for (uint i = 0; i < msbp->num_tag_groups; i++)
		{
			if (msbp->tag_groups[i].name)
				FREE (msbp->tag_groups[i].name);
			if (msbp->tag_groups[i].tags)
			{
				for (uint k = 0; k < msbp->tag_groups[i].num_tags; k++)
				{
					if (msbp->tag_groups[i].tags[k].name)
						FREE (msbp->tag_groups[i].tags[k].name);
					if (msbp->tag_groups[i].tags[k].params)
					{
						for (uint p = 0; p < msbp->tag_groups[i].tags[k].num_params; p++)
						{
							if (msbp->tag_groups[i].tags[k].params[p].name)
								FREE (msbp->tag_groups[i].tags[k].params[p].name);
							msbp_free_str_list (
								msbp->tag_groups[i].tags[k].params[p].list_items,
								msbp->tag_groups[i].tags[k].params[p].num_list_items);
						}
						FREE (msbp->tag_groups[i].tags[k].params);
					}
				}
				FREE (msbp->tag_groups[i].tags);
			}
		}
		FREE (msbp->tag_groups);
		msbp->tag_groups = 0;
		msbp->num_tag_groups = 0;
	}
	if (msbp->styles)
	{
		for (uint i = 0; i < msbp->num_styles; i++)
			if (msbp->styles[i].name)
				FREE (msbp->styles[i].name);
		FREE (msbp->styles);
		msbp->styles = 0;
		msbp->num_styles = 0;
	}
	msbp_free_str_list (msbp->source_files, msbp->num_source_files);
	msbp->source_files = 0;
	msbp->num_source_files = 0;
}

// Parse one LMS hash label block (CLB1/ALB1/SLB1/LBL1 layout).
// Returns slot count via out_slots (may be NULL). Labels are returned in item
// order; missing indices stay NULL.
static char **msbp_parse_labels (
	const u8 *data, uint size, bool be, uint *out_n, uint *out_slots, uint expect)
{
	if (out_n)
		*out_n = 0;
	if (out_slots)
		*out_slots = 0;
	if (!data || size < 4)
		return 0;
	u32 num_groups = r32 (data, be);
	if (out_slots)
		*out_slots = num_groups;
	if (num_groups > 10000)
		return 0;
	uint cap = expect > 0 ? expect : 16;
	char **labels = CALLOC (cap, sizeof (char *));
	uint n = 0;
	for (uint g = 0; g < num_groups && 4 + (g + 1) * 8 <= size; g++)
	{
		u32 count = r32 (data + 4 + g * 8, be);
		u32 group_off = r32 (data + 4 + g * 8 + 4, be);
		uint pos = group_off;
		for (uint c = 0; c < count && pos < size; c++)
		{
			u8 nlen = data[pos++];
			if (pos + nlen + 4 > size)
				break;
			u32 idx = r32 (data + pos + nlen, be);
			// idx sizes the label table; a garbage value would wrap idx + 16
			if (idx > 0xfffff)
				break;
			if (idx >= cap)
			{
				uint ncap = idx + 16;
				labels = REALLOC (labels, ncap * sizeof (char *));
				for (uint k = cap; k < ncap; k++)
					labels[k] = 0;
				cap = ncap;
			}
			char *name = MALLOC (nlen + 1);
			memcpy (name, data + pos, nlen);
			name[nlen] = 0;
			pos += nlen + 4;
			if (labels[idx])
				FREE (labels[idx]);
			labels[idx] = name;
			if (idx + 1 > n)
				n = idx + 1;
		}
	}
	if (out_n)
		*out_n = n;
	return labels;
}

// Build one LMS hash label block. labels[i] may be NULL (skipped).
static u8 *msbp_build_labels (
	bool be, u32 num_slots, char *const *labels, uint n, uint *out_len)
{
	if (out_len)
		*out_len = 0;
	if (num_slots == 0)
		num_slots = 29;
	uint *counts = CALLOC (num_slots, sizeof (uint));
	for (uint i = 0; i < n; i++)
	{
		if (labels && labels[i] && *labels[i])
			counts[msbt_hash (labels[i], num_slots)]++;
	}
	uint table = 4 + num_slots * 8;
	uint total = table;
	for (uint i = 0; i < n; i++)
	{
		if (labels && labels[i] && *labels[i])
			total += 1 + strlen (labels[i]) + 4;
	}
	u8 *buf = CALLOC (total + 16, 1);
	w32 (buf, num_slots, be);
	uint off = table;
	for (uint g = 0; g < num_slots; g++)
	{
		w32 (buf + 4 + g * 8, counts[g], be);
		w32 (buf + 4 + g * 8 + 4, off, be);
		for (uint i = 0; i < n; i++)
		{
			if (labels && labels[i] && *labels[i] && msbt_hash (labels[i], num_slots) == g)
			{
				uint nl = strlen (labels[i]);
				if (nl > 255)
					nl = 255;
				buf[off++] = (u8)nl;
				memcpy (buf + off, labels[i], nl);
				off += nl;
				w32 (buf + off, i, be);
				off += 4;
			}
		}
	}
	FREE (counts);
	if (out_len)
		*out_len = total;
	return buf;
}

// Read one NUL-terminated string at body+off (clamped to size).
static char *msbp_read_cstr (const u8 *body, uint size, uint off)
{
	if (off >= size)
		return STRDUP ("");
	uint len = 0;
	while (off + len < size && body[off + len])
		len++;
	char *s = MALLOC (len + 1);
	memcpy (s, body + off, len);
	s[len] = 0;
	return s;
}

enumError ScanMSBP (msbp_file_t *msbp, const u8 *data, uint data_size, ccp fname)
{
	if (!msbp || !data || data_size < 8)
		return ERR_INVALID_DATA;

	if (data_size >= 8 && !memcmp (data, "FZIP", 4))
	{
		u8 *dec = 0;
		uint dec_sz = 0;
		enumError derr = DecodeFZIP (&dec, &dec_sz, data, data_size);
		if (!derr && dec)
		{
			enumError ret = ScanMSBP (msbp, dec, dec_sz, fname);
			FREE (dec);
			return ret;
		}
	}

	if (data_size < 0x20 || memcmp (data, "MsgPrjBn", 8))
		return ERR_WRONG_FILE_TYPE;

	InitMSBP (msbp);
	if (fname)
		msbp->fname = STRDUP (fname);

	u16 bom = rd_be16 (data + 8);
	msbp->is_big_endian = (bom == 0xFEFF);
	bool be = msbp->is_big_endian;

	msbp->encoding = (msbt_encoding_t)data[0x0C];
	msbp->version = data[0x0D];
	u16 num_sections = r16 (data + 0x0E, be);

	const u8 *clr1_data = 0;
	uint clr1_size = 0;
	const u8 *clb1_data = 0;
	uint clb1_size = 0;
	const u8 *ati2_data = 0;
	uint ati2_size = 0;
	const u8 *alb1_data = 0;
	uint alb1_size = 0;
	const u8 *ali2_data = 0;
	uint ali2_size = 0;
	const u8 *tgg2_data = 0;
	uint tgg2_size = 0;
	const u8 *tag2_data = 0;
	uint tag2_size = 0;
	const u8 *tgp2_data = 0;
	uint tgp2_size = 0;
	const u8 *tgl2_data = 0;
	uint tgl2_size = 0;
	const u8 *syl3_data = 0;
	uint syl3_size = 0;
	const u8 *slb1_data = 0;
	uint slb1_size = 0;
	const u8 *cti1_data = 0;
	uint cti1_size = 0;

	uint cur = 0x20;
	for (uint s = 0; s < num_sections && cur + 16 <= data_size; s++)
	{
		char sec_magic[5] = { 0 };
		memcpy (sec_magic, data + cur, 4);
		if (cur + 16 > data_size)
			break;
		u32 sec_size = r32 (data + cur + 4, be);
		// Every section parser below trusts sec_size as its body length.
		if (sec_size > data_size - cur - 16)
			sec_size = data_size - cur - 16;
		const u8 *sec_body = data + cur + 16;

		if (!strcmp (sec_magic, "CLR1"))
		{
			clr1_data = sec_body;
			clr1_size = sec_size;
		}
		else if (!strcmp (sec_magic, "CLB1"))
		{
			clb1_data = sec_body;
			clb1_size = sec_size;
		}
		else if (!strcmp (sec_magic, "ATI2"))
		{
			ati2_data = sec_body;
			ati2_size = sec_size;
		}
		else if (!strcmp (sec_magic, "ALB1"))
		{
			alb1_data = sec_body;
			alb1_size = sec_size;
		}
		else if (!strcmp (sec_magic, "ALI2"))
		{
			ali2_data = sec_body;
			ali2_size = sec_size;
		}
		else if (!strcmp (sec_magic, "TGG2"))
		{
			tgg2_data = sec_body;
			tgg2_size = sec_size;
		}
		else if (!strcmp (sec_magic, "TAG2"))
		{
			tag2_data = sec_body;
			tag2_size = sec_size;
		}
		else if (!strcmp (sec_magic, "TGP2"))
		{
			tgp2_data = sec_body;
			tgp2_size = sec_size;
		}
		else if (!strcmp (sec_magic, "TGL2"))
		{
			tgl2_data = sec_body;
			tgl2_size = sec_size;
		}
		else if (!strcmp (sec_magic, "SYL3"))
		{
			syl3_data = sec_body;
			syl3_size = sec_size;
		}
		else if (!strcmp (sec_magic, "SLB1"))
		{
			slb1_data = sec_body;
			slb1_size = sec_size;
		}
		else if (!strcmp (sec_magic, "CTI1"))
		{
			cti1_data = sec_body;
			cti1_size = sec_size;
		}

		uint aligned_size = (sec_size + 15) & ~15;
		cur += 16 + aligned_size;
	}

	// CLR1 + CLB1 (colors)
	if (clr1_data && clr1_size >= 4)
	{
		u32 num_colors = r32 (clr1_data, be);
		if (num_colors > 0 && num_colors < 100000 && 4 + num_colors * 4 <= clr1_size)
		{
			msbp->colors = CALLOC (num_colors, sizeof (msbp_color_t));
			msbp->num_colors = num_colors;
			msbp->has_colors = true;
			for (uint i = 0; i < num_colors; i++)
			{
				msbp->colors[i].r = clr1_data[4 + i * 4];
				msbp->colors[i].g = clr1_data[4 + i * 4 + 1];
				msbp->colors[i].b = clr1_data[4 + i * 4 + 2];
				msbp->colors[i].a = clr1_data[4 + i * 4 + 3];
			}
		}
	}

	if (clb1_data && clb1_size >= 4 && msbp->colors)
	{
		uint n = 0;
		uint slots = 0;
		char **labels
			= msbp_parse_labels (clb1_data, clb1_size, be, &n, &slots, msbp->num_colors);
		if (slots)
			msbp->label_slot_count = slots;
		if (labels)
		{
			for (uint i = 0; i < n && i < msbp->num_colors; i++)
			{
				if (labels[i])
				{
					if (msbp->colors[i].name)
						FREE (msbp->colors[i].name);
					msbp->colors[i].name = labels[i];
					labels[i] = 0;
				}
			}
			msbp_free_str_list (labels, n);
		}
	}

	// ATI2 + ALB1 + ALI2 (attribute infos)
	if (0)
	{
		// (ALI2 is parsed on demand per attribute below.)
	}

	if (ati2_data && ati2_size >= 4)
	{
		u32 num_attrs = r32 (ati2_data, be);
		if (num_attrs < 100000 && 4 + num_attrs * 8 <= ati2_size)
		{
			msbp->attributes = CALLOC (num_attrs ? num_attrs : 1, sizeof (msbp_attribute_t));
			msbp->num_attributes = num_attrs;
			msbp->has_attributes = true;
			for (uint i = 0; i < num_attrs; i++)
			{
				const u8 *a = ati2_data + 4 + i * 8;
				msbp->attributes[i].type = a[0];
				msbp->attributes[i].list_index = r16 (a + 2, be);
				msbp->attributes[i].offset = r32 (a + 4, be);
			}
			// Attach ALB1 labels
			if (alb1_data && alb1_size >= 4)
			{
				uint n = 0;
				uint slots = 0;
				char **labels = msbp_parse_labels (
					alb1_data, alb1_size, be, &n, &slots, num_attrs);
				if (slots)
					msbp->label_slot_count = slots;
				if (labels)
				{
					for (uint i = 0; i < n && i < num_attrs; i++)
					{
						if (labels[i])
						{
							msbp->attributes[i].name = labels[i];
							labels[i] = 0;
						}
					}
					msbp_free_str_list (labels, n);
				}
			}
			// Attach ALI2 string lists (type 9 only)
			if (ali2_data && ali2_size >= 4)
			{
				u32 num_lists = r32 (ali2_data, be);
				for (uint i = 0; i < num_attrs; i++)
				{
					if (msbp->attributes[i].type != 9)
						continue;
					u32 li = msbp->attributes[i].list_index;
					if (li >= num_lists)
						continue;
					u32 loff = r32 (ali2_data + 4 + li * 4, be);
					if (loff >= ali2_size)
						continue;
					u32 cnt = r32 (ali2_data + loff, be);
					if (cnt > 10000 || 4 + cnt * 4 > ali2_size - loff)
						continue;
					msbp->attributes[i].list_items = CALLOC (cnt ? cnt : 1, sizeof (char *));
					msbp->attributes[i].num_list_items = cnt;
					for (uint k = 0; k < cnt; k++)
					{
						u32 ioff = r32 (ali2_data + loff + 4 + k * 4, be);
						msbp->attributes[i].list_items[k]
							= msbp_read_cstr (ali2_data + loff, ali2_size - loff, ioff);
					}
				}
			}
		}
	}
	// TGL2 (tag parameter string pool, parsed before TGP2)
	char **tgl_strings = 0;
	uint num_tgl_strings = 0;
	if (tgl2_data && tgl2_size >= 4)
	{
		u32 ns = r16 (tgl2_data, be);
		if (4 + ns * 4 <= tgl2_size)
		{
			num_tgl_strings = ns;
			tgl_strings = CALLOC (ns ? ns : 1, sizeof (char *));
			for (uint i = 0; i < ns; i++)
			{
				u32 off = r32 (tgl2_data + 4 + i * 4, be);
				tgl_strings[i] = msbp_read_cstr (tgl2_data, tgl2_size, off);
			}
		}
	}

	// TGP2 (tag parameters)
	typedef struct msbp_raw_param_t
	{
		char *name;
		u8 type;
		u16 *items;
		uint num_items;
	} msbp_raw_param_t;
	msbp_raw_param_t *raw_params = 0;
	uint num_raw_params = 0;
	if (tgp2_data && tgp2_size >= 4)
	{
		u32 np = r16 (tgp2_data, be);
		if (4 + np * 4 <= tgp2_size)
		{
			num_raw_params = np;
			raw_params = CALLOC (np ? np : 1, sizeof (msbp_raw_param_t));
			for (uint i = 0; i < np; i++)
			{
				u32 off = r32 (tgp2_data + 4 + i * 4, be);
				if (off >= tgp2_size)
					continue;
				u8 type = tgp2_data[off];
				raw_params[i].type = type;
				if (type == 9)
				{
					if (off + 4 > tgp2_size)
						continue;
					u16 nc = r16 (tgp2_data + off + 2, be);
					if (nc < 10000 && off + 4 + nc * 2 <= tgp2_size)
					{
						raw_params[i].items = CALLOC (nc ? nc : 1, sizeof (u16));
						raw_params[i].num_items = nc;
						for (uint k = 0; k < nc; k++)
							raw_params[i].items[k] = r16 (tgp2_data + off + 4 + k * 2, be);
						uint name_off = off + 4 + nc * 2;
						raw_params[i].name = msbp_read_cstr (tgp2_data, tgp2_size, name_off);
					}
					else
						raw_params[i].name = STRDUP ("");
				}
				else
				{
					raw_params[i].name = msbp_read_cstr (tgp2_data, tgp2_size, off + 1);
				}
			}
		}
	}

	// TAG2 (tag types)
	typedef struct msbp_raw_tag_t
	{
		char *name;
		u16 *pidx;
		uint num_pidx;
	} msbp_raw_tag_t;
	msbp_raw_tag_t *raw_tags = 0;
	uint num_raw_tags = 0;
	if (tag2_data && tag2_size >= 4)
	{
		u32 nt = r16 (tag2_data, be);
		if (4 + nt * 4 <= tag2_size)
		{
			num_raw_tags = nt;
			raw_tags = CALLOC (nt ? nt : 1, sizeof (msbp_raw_tag_t));
			for (uint i = 0; i < nt; i++)
			{
				u32 off = r32 (tag2_data + 4 + i * 4, be);
				if (off >= tag2_size)
					continue;
				u16 npc = r16 (tag2_data + off, be);
				if (npc < 10000 && off + 2 + npc * 2 <= tag2_size)
				{
					raw_tags[i].pidx = CALLOC (npc ? npc : 1, sizeof (u16));
					raw_tags[i].num_pidx = npc;
					for (uint k = 0; k < npc; k++)
						raw_tags[i].pidx[k] = r16 (tag2_data + off + 2 + k * 2, be);
					raw_tags[i].name = msbp_read_cstr (
						tag2_data, tag2_size, off + 2 + npc * 2);
				}
				else
					raw_tags[i].name = STRDUP ("");
			}
		}
	}

	// TGG2 (tag groups; v3 ids are positional tag-type cursors, v4 explicit)
	if (tgg2_data && tgg2_size >= 4)
	{
		u16 ng = r16 (tgg2_data, be);
		if (4 + (uint)ng * 4 <= tgg2_size)
		{
			msbp->tag_groups = CALLOC (ng ? ng : 1, sizeof (msbp_tag_group_t));
			msbp->num_tag_groups = ng;
			msbp->has_tags = ng > 0;
			u16 cursor = 0;
			for (uint i = 0; i < ng; i++)
			{
				u32 off = r32 (tgg2_data + 4 + i * 4, be);
				msbp_tag_group_t *g = msbp->tag_groups + i;
				g->group_id = i;
				if (off >= tgg2_size)
					continue;
				u16 gid = 0;
				u16 cnt = 0;
				const u8 *gp = tgg2_data + off;
				uint left = tgg2_size - off;
				if (msbp->version > 3)
				{
					if (left < 4)
						continue;
					gid = r16 (gp, be);
					cnt = r16 (gp + 2, be);
					gp += 4;
					left -= 4;
					g->group_id = gid;
				}
				else
				{
					if (left < 2)
						continue;
					cnt = r16 (gp, be);
					gp += 2;
					left -= 2;
					g->group_id = cursor;
				}
				if (cnt > 10000 || (uint)cnt * 2 > left)
					continue;
				u16 *tidx = CALLOC (cnt ? cnt : 1, sizeof (u16));
				for (uint k = 0; k < cnt; k++)
					tidx[k] = r16 (gp + k * 2, be);
				gp += cnt * 2;
				uint name_off = (uint)(gp - tgg2_data);
				g->name = msbp_read_cstr (tgg2_data, tgg2_size, name_off);
				// Resolve tag types
				g->tags = CALLOC (cnt ? cnt : 1, sizeof (msbp_tag_t));
				g->num_tags = cnt;
				for (uint k = 0; k < cnt; k++)
				{
					msbp_tag_t *t = g->tags + k;
					t->tag_id = tidx[k];
					u16 ti = tidx[k];
					if (ti < num_raw_tags && raw_tags[ti].name)
						t->name = STRDUP (raw_tags[ti].name);
					else
						t->name = STRDUP ("");
					// Resolve params
					uint npc = (ti < num_raw_tags) ? raw_tags[ti].num_pidx : 0;
					t->params = CALLOC (npc ? npc : 1, sizeof (msbp_tag_param_t));
					t->num_params = npc;
					for (uint p = 0; p < npc; p++)
					{
						u16 pi = raw_tags[ti].pidx[p];
						msbp_tag_param_t *par = t->params + p;
						if (pi < num_raw_params)
						{
							par->name = raw_params[pi].name ? STRDUP (raw_params[pi].name)
															: STRDUP ("");
							par->type = raw_params[pi].type;
							for (uint li = 0; li < raw_params[pi].num_items; li++)
							{
								u16 si = raw_params[pi].items[li];
								const char *s = (si < num_tgl_strings && tgl_strings[si])
									? tgl_strings[si]
									: "";
								par->list_items = REALLOC (par->list_items,
									(par->num_list_items + 1) * sizeof (char *));
								par->list_items[par->num_list_items++] = STRDUP (s);
							}
						}
						else
							par->name = STRDUP ("");
					}
				}
				FREE (tidx);
				if (msbp->version <= 3)
					cursor += cnt;
			}
		}
	}
	for (uint i = 0; i < num_raw_params; i++)
	{
		if (raw_params[i].name)
			FREE (raw_params[i].name);
		if (raw_params[i].items)
			FREE (raw_params[i].items);
	}
	if (raw_params)
		FREE (raw_params);
	for (uint i = 0; i < num_raw_tags; i++)
	{
		if (raw_tags[i].name)
			FREE (raw_tags[i].name);
		if (raw_tags[i].pidx)
			FREE (raw_tags[i].pidx);
	}
	if (raw_tags)
		FREE (raw_tags);
	msbp_free_str_list (tgl_strings, num_tgl_strings);

	// SYL3 + SLB1 (styles)
	if (syl3_data && syl3_size >= 4)
	{
		u32 ns = r32 (syl3_data, be);
		if (ns < 100000 && 4 + ns * 16 <= syl3_size)
		{
			msbp->styles = CALLOC (ns ? ns : 1, sizeof (msbp_style_t));
			msbp->num_styles = ns;
			msbp->has_styles = true;
			for (uint i = 0; i < ns; i++)
			{
				const u8 *sp = syl3_data + 4 + i * 16;
				msbp->styles[i].region_width = (int)r32 (sp, be);
				msbp->styles[i].line_number = (int)r32 (sp + 4, be);
				msbp->styles[i].font_index = (int)r32 (sp + 8, be);
				msbp->styles[i].base_color_index = (int)r32 (sp + 12, be);
			}
			if (slb1_data && slb1_size >= 4)
			{
				uint n = 0;
				uint slots = 0;
				char **labels = msbp_parse_labels (slb1_data, slb1_size, be, &n, &slots, ns);
				if (slots)
					msbp->label_slot_count = slots;
				if (labels)
				{
					for (uint i = 0; i < n && i < ns; i++)
					{
						if (labels[i])
						{
							msbp->styles[i].name = labels[i];
							labels[i] = 0;
						}
					}
					msbp_free_str_list (labels, n);
				}
			}
		}
	}

	// CTI1 (source files)
	if (cti1_data && cti1_size >= 4)
	{
		u32 nf = r32 (cti1_data, be);
		if (nf < 100000 && 4 + nf * 4 <= cti1_size)
		{
			msbp->source_files = CALLOC (nf ? nf : 1, sizeof (char *));
			msbp->num_source_files = nf;
			msbp->has_source_files = nf > 0;
			for (uint i = 0; i < nf; i++)
			{
				u32 off = r32 (cti1_data + 4 + i * 4, be);
				msbp->source_files[i] = msbp_read_cstr (cti1_data, cti1_size, off);
			}
		}
	}

	return ERR_OK;
}

enumError SaveTextMSBP (const msbp_file_t *msbp, ccp dest_fname)
{
	if (!msbp || !dest_fname)
		return ERR_INVALID_DATA;
	FILE *f = fopen (dest_fname, "w");
	if (!f)
		return ERR_CANT_CREATE;

	const char *penc = "UTF-16";
	if (msbp->encoding == MSBT_ENC_UTF8)
		penc = "UTF-8";
	else if (msbp->encoding == MSBT_ENC_UTF32)
		penc = "UTF-32";
	fprintf (f, "# MSBP: Message Studio Binary Project (%s, %s)\n",
		msbp->is_big_endian ? "BigEndian" : "LittleEndian", penc);
	if (msbp->label_slot_count > 0)
		fprintf (f, "# SlotNum: %u\n", msbp->label_slot_count);

	if (msbp->has_colors && msbp->num_colors > 0)
	{
		fprintf (f, "\n[Colors: %u]\n", msbp->num_colors);
		for (uint i = 0; i < msbp->num_colors; i++)
		{
			fprintf (f, "  #%u: %s = #%02X%02X%02X%02X\n", i,
				msbp->colors[i].name ? msbp->colors[i].name : "unnamed", msbp->colors[i].r,
				msbp->colors[i].g, msbp->colors[i].b, msbp->colors[i].a);
		}
	}

	if (msbp->has_attributes && msbp->num_attributes > 0)
	{
		fprintf (f, "\n[Attributes: %u]\n", msbp->num_attributes);
		for (uint i = 0; i < msbp->num_attributes; i++)
		{
			const msbp_attribute_t *a = msbp->attributes + i;
			fprintf (f, "  #%u: %s = type=%u offset=%u", i, a->name ? a->name : "",
				a->type, a->offset);
			if (a->type == 9 && a->num_list_items > 0)
			{
				fprintf (f, " list=");
				for (uint k = 0; k < a->num_list_items; k++)
					fprintf (f, "%s%s", k ? ";" : "", a->list_items[k] ? a->list_items[k] : "");
			}
			fprintf (f, "\n");
		}
	}

	if (msbp->has_tags && msbp->num_tag_groups > 0)
	{
		fprintf (f, "\n[TagGroups: %u]\n", msbp->num_tag_groups);
		for (uint i = 0; i < msbp->num_tag_groups; i++)
		{
			const msbp_tag_group_t *g = msbp->tag_groups + i;
			fprintf (f, "  Group %u \"%s\"\n", g->group_id, g->name ? g->name : "");
			for (uint k = 0; k < g->num_tags; k++)
			{
				const msbp_tag_t *t = g->tags + k;
				fprintf (f, "    Tag \"%s\"\n", t->name ? t->name : "");
				for (uint p = 0; p < t->num_params; p++)
				{
					const msbp_tag_param_t *par = t->params + p;
					fprintf (f, "      Param \"%s\" type=%u", par->name ? par->name : "",
						par->type);
					if (par->type == 9 && par->num_list_items > 0)
					{
						fprintf (f, " list=");
						for (uint li = 0; li < par->num_list_items; li++)
							fprintf (f, "%s%s", li ? ";" : "",
								par->list_items[li] ? par->list_items[li] : "");
					}
					fprintf (f, "\n");
				}
			}
		}
	}

	if (msbp->has_styles && msbp->num_styles > 0)
	{
		fprintf (f, "\n[Styles: %u]\n", msbp->num_styles);
		for (uint i = 0; i < msbp->num_styles; i++)
		{
			const msbp_style_t *s = msbp->styles + i;
			fprintf (f, "  #%u: %s = %d,%d,%d,%d\n", i, s->name ? s->name : "", s->region_width,
				s->line_number, s->font_index, s->base_color_index);
		}
	}

	if (msbp->has_source_files && msbp->num_source_files > 0)
	{
		fprintf (f, "\n[SourceFiles: %u]\n", msbp->num_source_files);
		for (uint i = 0; i < msbp->num_source_files; i++)
			fprintf (f, "  %s\n", msbp->source_files[i] ? msbp->source_files[i] : "");
	}

	fclose (f);
	return ERR_OK;
}

enumError SaveJSONMSBP (const msbp_file_t *msbp, ccp dest_fname)
{
	if (!msbp || !dest_fname)
		return ERR_INVALID_DATA;
	FILE *f = fopen (dest_fname, "w");
	if (!f)
		return ERR_CANT_CREATE;

	const char *jenc = "UTF-16";
	if (msbp->encoding == MSBT_ENC_UTF8)
		jenc = "UTF-8";
	else if (msbp->encoding == MSBT_ENC_UTF32)
		jenc = "UTF-32";
	fprintf (f, "{\n  \"endian\": \"%s\",\n  \"encoding\": \"%s\",\n",
		msbp->is_big_endian ? "BigEndian" : "LittleEndian", jenc);

	fprintf (f, "  \"colors\": [\n");
	for (uint i = 0; i < msbp->num_colors; i++)
	{
		fprintf (f, "    { \"index\": %u, \"name\": \"%s\", \"rgba\": \"#%02X%02X%02X%02X\" }%s\n",
			i, msbp->colors[i].name ? msbp->colors[i].name : "", msbp->colors[i].r,
			msbp->colors[i].g, msbp->colors[i].b, msbp->colors[i].a,
			(i + 1 < msbp->num_colors) ? "," : "");
	}
	fprintf (f, "  ],\n");
	fprintf (f, "  \"num_attributes\": %u,\n", msbp->num_attributes);
	fprintf (f, "  \"num_tag_groups\": %u,\n", msbp->num_tag_groups);
	fprintf (f, "  \"num_styles\": %u,\n", msbp->num_styles);
	fprintf (f, "  \"num_source_files\": %u\n}\n", msbp->num_source_files);
	fclose (f);
	return ERR_OK;
}

enumError CreateMSBP (u8 **out_data, uint *out_size, const msbp_file_t *msbp)
{
	if (!out_data || !out_size || !msbp)
		return ERR_INVALID_DATA;
	bool be = msbp->is_big_endian;
	u8 version = msbp->version ? msbp->version : 3;
	u32 slots = msbp->label_slot_count > 0 ? msbp->label_slot_count : 29;

	// ---- CLR1 / CLB1 ----
	u8 *clr1_buf = 0;
	uint clr1_len = 0;
	u8 *clb1_buf = 0;
	uint clb1_len = 0;
	if (msbp->has_colors && msbp->num_colors > 0)
	{
		clr1_len = 4 + msbp->num_colors * 4;
		clr1_buf = CALLOC (clr1_len + 16, 1);
		w32 (clr1_buf, msbp->num_colors, be);
		for (uint i = 0; i < msbp->num_colors; i++)
		{
			clr1_buf[4 + i * 4] = msbp->colors[i].r;
			clr1_buf[4 + i * 4 + 1] = msbp->colors[i].g;
			clr1_buf[4 + i * 4 + 2] = msbp->colors[i].b;
			clr1_buf[4 + i * 4 + 3] = msbp->colors[i].a;
		}
		bool has_names = false;
		for (uint i = 0; i < msbp->num_colors; i++)
		{
			if (msbp->colors[i].name && *msbp->colors[i].name)
			{
				has_names = true;
				break;
			}
		}
		if (has_names)
		{
			char **labels = CALLOC (msbp->num_colors, sizeof (char *));
			for (uint i = 0; i < msbp->num_colors; i++)
				labels[i] = msbp->colors[i].name;
			clb1_buf = msbp_build_labels (be, slots, labels, msbp->num_colors, &clb1_len);
			FREE (labels);
		}
	}

	// ---- ATI2 / ALB1 / ALI2 ----
	u8 *ati2_buf = 0;
	uint ati2_len = 0;
	u8 *alb1_buf = 0;
	uint alb1_len = 0;
	u8 *ali2_buf = 0;
	uint ali2_len = 0;
	if (msbp->has_attributes && msbp->num_attributes > 0)
	{
		// Deduplicate type-9 lists by content (stable first-appearance order)
		typedef struct msbp_list_slot_t
		{
			char **items;
			uint n;
		} msbp_list_slot_t;
		msbp_list_slot_t *uniq = CALLOC (msbp->num_attributes, sizeof (msbp_list_slot_t));
		uint num_uniq = 0;
		u16 *attr_list_idx = CALLOC (msbp->num_attributes, sizeof (u16));
		for (uint i = 0; i < msbp->num_attributes; i++)
		{
			const msbp_attribute_t *a = msbp->attributes + i;
			if (a->type != 9 || a->num_list_items == 0)
			{
				attr_list_idx[i] = a->list_index;
				continue;
			}
			uint found = num_uniq;
			for (uint u = 0; u < num_uniq; u++)
			{
				if (uniq[u].n != a->num_list_items)
					continue;
				bool same = true;
				for (uint k = 0; k < a->num_list_items && same; k++)
				{
					const char *x = uniq[u].items[k] ? uniq[u].items[k] : "";
					const char *y = a->list_items[k] ? a->list_items[k] : "";
					if (strcmp (x, y))
						same = false;
				}
				if (same)
					found = u;
			}
			if (found == num_uniq)
			{
				uniq[num_uniq].items = a->list_items;
				uniq[num_uniq].n = a->num_list_items;
				num_uniq++;
			}
			attr_list_idx[i] = (u16)found;
		}
		ati2_len = 4 + msbp->num_attributes * 8;
		ati2_buf = CALLOC (ati2_len + 16, 1);
		w32 (ati2_buf, msbp->num_attributes, be);
		for (uint i = 0; i < msbp->num_attributes; i++)
		{
			const msbp_attribute_t *a = msbp->attributes + i;
			u8 *e = ati2_buf + 4 + i * 8;
			e[0] = a->type;
			e[1] = 0;
			w16 (e + 2, a->type == 9 ? attr_list_idx[i] : a->list_index, be);
			w32 (e + 4, a->offset, be);
		}
		{
			char **labels = CALLOC (msbp->num_attributes, sizeof (char *));
			bool any = false;
			for (uint i = 0; i < msbp->num_attributes; i++)
			{
				labels[i] = msbp->attributes[i].name;
				if (labels[i] && *labels[i])
					any = true;
			}
			if (any)
				alb1_buf = msbp_build_labels (be, slots, labels, msbp->num_attributes, &alb1_len);
			FREE (labels);
		}
		// ALI2: offsets + per-list (count + offsets + strings), 4-byte aligned
		{
			uint *list_sizes = CALLOC (num_uniq ? num_uniq : 1, sizeof (uint));
			for (uint u = 0; u < num_uniq; u++)
			{
				uint sz = 4 + uniq[u].n * 4;
				for (uint k = 0; k < uniq[u].n; k++)
					sz += strlen (uniq[u].items[k] ? uniq[u].items[k] : "") + 1;
				sz = (sz + 3) & ~3;
				list_sizes[u] = sz;
			}
			ali2_len = 4 + num_uniq * 4;
			for (uint u = 0; u < num_uniq; u++)
				ali2_len += list_sizes[u];
			ali2_buf = CALLOC (ali2_len + 16, 1);
			w32 (ali2_buf, num_uniq, be);
			uint base = 4 + num_uniq * 4;
			for (uint u = 0; u < num_uniq; u++)
			{
				w32 (ali2_buf + 4 + u * 4, base, be);
				u8 *lp = ali2_buf + base;
				w32 (lp, uniq[u].n, be);
				uint soff = 4 + uniq[u].n * 4;
				for (uint k = 0; k < uniq[u].n; k++)
				{
					w32 (lp + 4 + k * 4, soff, be);
					const char *s = uniq[u].items[k] ? uniq[u].items[k] : "";
					uint sl = strlen (s) + 1;
					memcpy (lp + soff, s, sl);
					soff += sl;
				}
				while (soff & 3)
					lp[soff++] = 0;
				base += list_sizes[u];
			}
			FREE (list_sizes);
		}
		FREE (uniq);
		FREE (attr_list_idx);
	}

	// ---- TGG2 / TAG2 / TGP2 / TGL2 ----
	u8 *tgg2_buf = 0;
	uint tgg2_len = 0;
	u8 *tag2_buf = 0;
	uint tag2_len = 0;
	u8 *tgp2_buf = 0;
	uint tgp2_len = 0;
	u8 *tgl2_buf = 0;
	uint tgl2_len = 0;
	if (msbp->has_tags && msbp->num_tag_groups > 0)
	{
		// Flatten tags and params in group order
		uint total_tags = 0;
		uint total_params = 0;
		for (uint i = 0; i < msbp->num_tag_groups; i++)
		{
			total_tags += msbp->tag_groups[i].num_tags;
			for (uint k = 0; k < msbp->tag_groups[i].num_tags; k++)
				total_params += msbp->tag_groups[i].tags[k].num_params;
		}
		// Global TGL2 string pool (dedup, first-appearance order)
		char **pool = CALLOC (total_params * 4 + 1, sizeof (char *));
		uint pool_n = 0;
		for (uint i = 0; i < msbp->num_tag_groups; i++)
		{
			for (uint k = 0; k < msbp->tag_groups[i].num_tags; k++)
			{
				for (uint p = 0; p < msbp->tag_groups[i].tags[k].num_params; p++)
				{
					const msbp_tag_param_t *par = msbp->tag_groups[i].tags[k].params + p;
					if (par->type != 9)
						continue;
					for (uint li = 0; li < par->num_list_items; li++)
					{
						const char *s = par->list_items[li] ? par->list_items[li] : "";
						bool seen = false;
						for (uint q = 0; q < pool_n; q++)
						{
							if (!strcmp (pool[q], s))
							{
								seen = true;
								break;
							}
						}
						if (!seen)
							pool[pool_n++] = (char *)s;
					}
				}
			}
		}
		// TGL2
		{
			uint *str_off = CALLOC (pool_n ? pool_n : 1, sizeof (uint));
			uint pos = 4 + pool_n * 4;
			for (uint q = 0; q < pool_n; q++)
			{
				str_off[q] = pos;
				pos += strlen (pool[q]) + 1;
			}
			tgl2_len = pos;
			tgl2_buf = CALLOC (tgl2_len + 16, 1);
			w16 (tgl2_buf, (u16)pool_n, be);
			tgl2_buf[2] = 0;
			tgl2_buf[3] = 0;
			for (uint q = 0; q < pool_n; q++)
			{
				w32 (tgl2_buf + 4 + q * 4, str_off[q], be);
				const char *s = pool[q];
				memcpy (tgl2_buf + str_off[q], s, strlen (s) + 1);
			}
			FREE (str_off);
		}
		// TGP2 entries
		typedef struct msbp_tgp_ent_t
		{
			u8 *data;
			uint len;
		} msbp_tgp_ent_t;
		msbp_tgp_ent_t *pents = CALLOC (total_params ? total_params : 1, sizeof (msbp_tgp_ent_t));
		{
			uint pi = 0;
			for (uint i = 0; i < msbp->num_tag_groups; i++)
			{
				for (uint k = 0; k < msbp->tag_groups[i].num_tags; k++)
				{
					for (uint p = 0; p < msbp->tag_groups[i].tags[k].num_params; p++)
					{
						const msbp_tag_param_t *par
							= msbp->tag_groups[i].tags[k].params + p;
						const char *nm = par->name ? par->name : "";
						uint nl = strlen (nm) + 1;
						uint len;
						if (par->type == 9)
							len = 4 + par->num_list_items * 2 + nl;
						else
							len = 1 + nl;
						len = (len + 3) & ~3;
						u8 *d = CALLOC (len, 1);
						d[0] = par->type;
						if (par->type == 9)
						{
							d[1] = 0;
							w16 (d + 2, (u16)par->num_list_items, be);
							for (uint li = 0; li < par->num_list_items; li++)
							{
								const char *s = par->list_items[li] ? par->list_items[li] : "";
								u16 si = 0;
								for (uint q = 0; q < pool_n; q++)
								{
									if (!strcmp (pool[q], s))
									{
										si = (u16)q;
										break;
									}
								}
								w16 (d + 4 + li * 2, si, be);
							}
							memcpy (d + 4 + par->num_list_items * 2, nm, nl);
						}
						else
							memcpy (d + 1, nm, nl);
						pents[pi].data = d;
						pents[pi].len = len;
						pi++;
					}
				}
			}
			tgp2_len = 4 + total_params * 4;
			for (uint q = 0; q < total_params; q++)
				tgp2_len += pents[q].len;
			tgp2_buf = CALLOC (tgp2_len + 16, 1);
			w16 (tgp2_buf, (u16)total_params, be);
			tgp2_buf[2] = 0;
			tgp2_buf[3] = 0;
			uint pos = 4 + total_params * 4;
			for (uint q = 0; q < total_params; q++)
			{
				w32 (tgp2_buf + 4 + q * 4, pos, be);
				memcpy (tgp2_buf + pos, pents[q].data, pents[q].len);
				pos += pents[q].len;
				FREE (pents[q].data);
			}
			FREE (pents);
		}
		// TAG2 entries (param indices are global TGP2 positions)
		{
			uint *tag_plen = CALLOC (total_tags ? total_tags : 1, sizeof (uint));
			char **tag_names = CALLOC (total_tags ? total_tags : 1, sizeof (char *));
			uint ti = 0;
			for (uint i = 0; i < msbp->num_tag_groups; i++)
			{
				for (uint k = 0; k < msbp->tag_groups[i].num_tags; k++)
				{
					tag_names[ti] = msbp->tag_groups[i].tags[k].name;
					uint nl = strlen (tag_names[ti] ? tag_names[ti] : "") + 1;
					uint len = 2 + msbp->tag_groups[i].tags[k].num_params * 2 + nl;
					tag_plen[ti] = (len + 3) & ~3;
					ti++;
				}
			}
			tag2_len = 4 + total_tags * 4;
			for (uint q = 0; q < total_tags; q++)
				tag2_len += tag_plen[q];
			tag2_buf = CALLOC (tag2_len + 16, 1);
			w16 (tag2_buf, (u16)total_tags, be);
			tag2_buf[2] = 0;
			tag2_buf[3] = 0;
			uint pos = 4 + total_tags * 4;
			ti = 0;
			uint global_pi = 0;
			for (uint i = 0; i < msbp->num_tag_groups; i++)
			{
				for (uint k = 0; k < msbp->tag_groups[i].num_tags; k++)
				{
					w32 (tag2_buf + 4 + ti * 4, pos, be);
					u8 *d = tag2_buf + pos;
					uint npc = msbp->tag_groups[i].tags[k].num_params;
					w16 (d, (u16)npc, be);
					for (uint p = 0; p < npc; p++)
						w16 (d + 2 + p * 2, (u16)(global_pi + p), be);
					global_pi += npc;
					const char *nm = tag_names[ti] ? tag_names[ti] : "";
					memcpy (d + 2 + npc * 2, nm, strlen (nm) + 1);
					pos += tag_plen[ti];
					ti++;
				}
			}
			FREE (tag_plen);
			FREE (tag_names);
		}
		// TGG2 entries
		{
			uint *glen = CALLOC (msbp->num_tag_groups ? msbp->num_tag_groups : 1, sizeof (uint));
			for (uint i = 0; i < msbp->num_tag_groups; i++)
			{
				const msbp_tag_group_t *g = msbp->tag_groups + i;
				uint nl = strlen (g->name ? g->name : "") + 1;
				uint len = (version > 3 ? 4 : 2) + g->num_tags * 2 + nl;
				glen[i] = (len + 3) & ~3;
			}
			tgg2_len = 4 + msbp->num_tag_groups * 4;
			for (uint i = 0; i < msbp->num_tag_groups; i++)
				tgg2_len += glen[i];
			tgg2_buf = CALLOC (tgg2_len + 16, 1);
			w16 (tgg2_buf, (u16)msbp->num_tag_groups, be);
			tgg2_buf[2] = 0;
			tgg2_buf[3] = 0;
			uint pos = 4 + msbp->num_tag_groups * 4;
			uint tag_cursor = 0;
			for (uint i = 0; i < msbp->num_tag_groups; i++)
			{
				const msbp_tag_group_t *g = msbp->tag_groups + i;
				w32 (tgg2_buf + 4 + i * 4, pos, be);
				u8 *d = tgg2_buf + pos;
				if (version > 3)
				{
					w16 (d, g->group_id, be);
					w16 (d + 2, (u16)g->num_tags, be);
					d += 4;
				}
				else
				{
					w16 (d, (u16)g->num_tags, be);
					d += 2;
				}
				for (uint k = 0; k < g->num_tags; k++)
				{
					w16 (d + k * 2, (u16)(tag_cursor + k), be);
				}
				tag_cursor += g->num_tags;
				d += g->num_tags * 2;
				const char *nm = g->name ? g->name : "";
				memcpy (d, nm, strlen (nm) + 1);
				pos += glen[i];
			}
			FREE (glen);
		}
		FREE (pool);
	}

	// ---- SYL3 / SLB1 ----
	u8 *syl3_buf = 0;
	uint syl3_len = 0;
	u8 *slb1_buf = 0;
	uint slb1_len = 0;
	if (msbp->has_styles && msbp->num_styles > 0)
	{
		syl3_len = 4 + msbp->num_styles * 16;
		syl3_buf = CALLOC (syl3_len + 16, 1);
		w32 (syl3_buf, msbp->num_styles, be);
		for (uint i = 0; i < msbp->num_styles; i++)
		{
			w32 (syl3_buf + 4 + i * 16, (u32)msbp->styles[i].region_width, be);
			w32 (syl3_buf + 4 + i * 16 + 4, (u32)msbp->styles[i].line_number, be);
			w32 (syl3_buf + 4 + i * 16 + 8, (u32)msbp->styles[i].font_index, be);
			w32 (syl3_buf + 4 + i * 16 + 12, (u32)msbp->styles[i].base_color_index, be);
		}
		bool any = false;
		for (uint i = 0; i < msbp->num_styles; i++)
		{
			if (msbp->styles[i].name && *msbp->styles[i].name)
			{
				any = true;
				break;
			}
		}
		if (any)
		{
			char **labels = CALLOC (msbp->num_styles, sizeof (char *));
			for (uint i = 0; i < msbp->num_styles; i++)
				labels[i] = msbp->styles[i].name;
			slb1_buf = msbp_build_labels (be, slots, labels, msbp->num_styles, &slb1_len);
			FREE (labels);
		}
	}

	// ---- CTI1 ----
	u8 *cti1_buf = 0;
	uint cti1_len = 0;
	if (msbp->has_source_files && msbp->num_source_files > 0)
	{
		cti1_len = 4 + msbp->num_source_files * 4;
		for (uint i = 0; i < msbp->num_source_files; i++)
			cti1_len += strlen (msbp->source_files[i] ? msbp->source_files[i] : "") + 1;
		cti1_buf = CALLOC (cti1_len + 16, 1);
		w32 (cti1_buf, msbp->num_source_files, be);
		uint pos = 4 + msbp->num_source_files * 4;
		for (uint i = 0; i < msbp->num_source_files; i++)
		{
			w32 (cti1_buf + 4 + i * 4, pos, be);
			const char *s = msbp->source_files[i] ? msbp->source_files[i] : "";
			uint sl = strlen (s) + 1;
			memcpy (cti1_buf + pos, s, sl);
			pos += sl;
		}
		cti1_len = pos;
	}

	// ---- Assemble ----
	u16 num_sections = 0;
	uint total_size = 0x20;
#define MSBP_ADD_SEC(buf_, len_)                                                                       \
	do {                                                                                           \
		if (buf_)                                                                              \
		{                                                                                      \
			num_sections++;                                                                \
			total_size += 16 + ((len_ + 15) & ~15);                                         \
		}                                                                                      \
	} while (0)
	MSBP_ADD_SEC (clr1_buf, clr1_len);
	MSBP_ADD_SEC (clb1_buf, clb1_len);
	MSBP_ADD_SEC (ati2_buf, ati2_len);
	MSBP_ADD_SEC (alb1_buf, alb1_len);
	MSBP_ADD_SEC (ali2_buf, ali2_len);
	MSBP_ADD_SEC (tgg2_buf, tgg2_len);
	MSBP_ADD_SEC (tag2_buf, tag2_len);
	MSBP_ADD_SEC (tgp2_buf, tgp2_len);
	MSBP_ADD_SEC (tgl2_buf, tgl2_len);
	MSBP_ADD_SEC (syl3_buf, syl3_len);
	MSBP_ADD_SEC (slb1_buf, slb1_len);
	MSBP_ADD_SEC (cti1_buf, cti1_len);
#undef MSBP_ADD_SEC

	u8 *out = CALLOC (total_size ? total_size : 0x20, 1);
	memcpy (out, "MsgPrjBn", 8);
	wr_be16 (out + 8, be ? 0xFEFF : 0xFFFE);
	out[0x0A] = 0;
	out[0x0B] = 0;
	out[0x0C] = (u8)msbp->encoding;
	out[0x0D] = version;
	w16 (out + 0x0E, num_sections, be);
	out[0x10] = 0;
	out[0x11] = 0;
	w32 (out + 0x12, total_size, be);
	memset (out + 0x16, 0, 0x0A);

	uint pos = 0x20;
#define MSBP_EMIT_SEC(magic_, buf_, len_)                                                              \
	do {                                                                                           \
		if (buf_)                                                                              \
		{                                                                                      \
			memcpy (out + pos, magic_, 4);                                                 \
			w32 (out + pos + 4, len_, be);                                                 \
			memset (out + pos + 8, 0, 8);                                                  \
			memcpy (out + pos + 16, buf_, len_);                                          \
			{                                                                              \
				uint end = pos + 16 + len_;                                            \
				uint aligned = pos + 16 + ((len_ + 15) & ~15);                         \
				for (uint p = end; p < aligned; p++)                                  \
					out[p] = 0xAB;                                                     \
				pos = aligned;                                                             \
			}                                                                                  \
			FREE (buf_);                                                                       \
		}                                                                                      \
	} while (0)
	MSBP_EMIT_SEC ("CLR1", clr1_buf, clr1_len);
	MSBP_EMIT_SEC ("CLB1", clb1_buf, clb1_len);
	MSBP_EMIT_SEC ("ATI2", ati2_buf, ati2_len);
	MSBP_EMIT_SEC ("ALB1", alb1_buf, alb1_len);
	MSBP_EMIT_SEC ("ALI2", ali2_buf, ali2_len);
	MSBP_EMIT_SEC ("TGG2", tgg2_buf, tgg2_len);
	MSBP_EMIT_SEC ("TAG2", tag2_buf, tag2_len);
	MSBP_EMIT_SEC ("TGP2", tgp2_buf, tgp2_len);
	MSBP_EMIT_SEC ("TGL2", tgl2_buf, tgl2_len);
	MSBP_EMIT_SEC ("SYL3", syl3_buf, syl3_len);
	MSBP_EMIT_SEC ("SLB1", slb1_buf, slb1_len);
	MSBP_EMIT_SEC ("CTI1", cti1_buf, cti1_len);
#undef MSBP_EMIT_SEC

	*out_data = out;
	*out_size = total_size;
	return ERR_OK;
}

enumError LoadTextMSBP (msbp_file_t *msbp, ccp src_fname)
{
	if (!msbp || !src_fname)
		return ERR_INVALID_DATA;
	FILE *f = fopen (src_fname, "r");
	if (!f)
		return ERR_CANT_OPEN;

	InitMSBP (msbp);
	msbp->fname = STRDUP (src_fname);

	char line[4096];
	int section = 0; // 0=none 1=colors 2=attrs 3=tags 4=styles 5=sources
	msbp_tag_group_t *cur_group = 0;
	msbp_tag_t *cur_tag = 0;

	// Split a "a;b;c" list into heap strings
#define MSBP_SPLIT_LIST(dst_, dstn_, src_)                                                                 \
	do {                                                                                               \
		dst_ = 0;                                                                                  \
		dstn_ = 0;                                                                                 \
		const char *_p = src_;                                                                     \
		while (_p && *_p)                                                                          \
		{                                                                                          \
			const char *_e = strchr (_p, ';');                                                  \
			uint _l = _e ? (uint)(_e - _p) : (uint)strlen (_p);                                 \
			char *_s = MALLOC (_l + 1);                                                        \
			memcpy (_s, _p, _l);                                                               \
			_s[_l] = 0;                                                                        \
			dst_ = REALLOC (dst_, (dstn_ + 1) * sizeof (char *));                              \
			dst_[dstn_++] = _s;                                                                \
			_p = _e ? _e + 1 : 0;                                                              \
			if (!_e)                                                                           \
				break;                                                                     \
		}                                                                                          \
	} while (0)

	while (fgets (line, sizeof (line), f))
	{
		uint len = strlen (line);
		while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n'))
			line[--len] = 0;
		char *s = line;
		while (*s == ' ' || *s == '\t')
			s++;
		if (*s == 0)
			continue;
		if (*s == '[')
		{
			cur_group = 0;
			cur_tag = 0;
			if (!strncmp (s, "[Colors", 7))
				section = 1;
			else if (!strncmp (s, "[Attributes", 11))
				section = 2;
			else if (!strncmp (s, "[TagGroups", 10))
				section = 3;
			else if (!strncmp (s, "[Styles", 7))
				section = 4;
			else if (!strncmp (s, "[SourceFiles", 12))
				section = 5;
			else
				section = 0;
			continue;
		}
		if (*s == '#' && section != 1 && section != 2 && section != 4)
		{
			if (strstr (line, "BigEndian"))
				msbp->is_big_endian = true;
			if (strstr (line, "LittleEndian"))
				msbp->is_big_endian = false;
			if (strstr (line, "UTF-32"))
				msbp->encoding = MSBT_ENC_UTF32;
			else if (strstr (line, "UTF-8"))
				msbp->encoding = MSBT_ENC_UTF8;
			else if (strstr (line, "UTF-16"))
				msbp->encoding = MSBT_ENC_UTF16;
			if (strstr (line, "SlotNum"))
			{
				char *col = strchr (line, ':');
				if (col)
					msbp->label_slot_count = (u32)strtoul (col + 1, 0, 10);
			}
			// Legacy color lines live outside [Colors] in old files: fall
			// through to the color parser below.
			if (section == 0)
			{
				char *colon = strchr (s, ':');
				char *eq = colon ? strchr (colon, '=') : 0;
				if (colon && eq && strchr (eq, '#'))
				{
					section = 1;
				}
				else
					continue;
			}
			else
				continue;
		}
		if (section == 1 && *s == '#')
		{
			// Color line: #0: Name = #RRGGBBAA
			char *colon = strchr (s, ':');
			if (!colon)
				continue;
			*colon = 0;
			char *eq = strchr (colon + 1, '=');
			if (!eq)
				continue;
			*eq = 0;
			char *name = colon + 1;
			while (*name == ' ')
				name++;
			char *ne = name + strlen (name);
			while (ne > name && (ne[-1] == ' ' || ne[-1] == '\t'))
				*--ne = 0;
			char *col_val = eq + 1;
			while (*col_val == ' ' || *col_val == '#')
				col_val++;
			unsigned int hexval = 0;
			sscanf (col_val, "%x", &hexval);
			unsigned int r = 0, g = 0, b = 0, a = 255;
			{
				uint hlen = 0;
				while (isxdigit ((u8)col_val[hlen]))
					hlen++;
				if (hlen >= 8)
				{
					r = (hexval >> 24) & 0xFF;
					g = (hexval >> 16) & 0xFF;
					b = (hexval >> 8) & 0xFF;
					a = hexval & 0xFF;
				}
				else if (hlen >= 6)
				{
					r = (hexval >> 16) & 0xFF;
					g = (hexval >> 8) & 0xFF;
					b = hexval & 0xFF;
					a = 0xFF;
				}
			}
			uint idx = msbp->num_colors++;
			msbp->colors = REALLOC (msbp->colors, msbp->num_colors * sizeof (msbp_color_t));
			memset (msbp->colors + idx, 0, sizeof (msbp_color_t));
			msbp->colors[idx].name = STRDUP (*name ? name : "unnamed");
			msbp->colors[idx].r = (u8)r;
			msbp->colors[idx].g = (u8)g;
			msbp->colors[idx].b = (u8)b;
			msbp->colors[idx].a = (u8)a;
			msbp->has_colors = true;
		}
		else if (section == 2 && *s == '#')
		{
			// Attribute line: #i: name = type=T offset=O [list=A;B]
			char *colon = strchr (s, ':');
			if (!colon)
				continue;
			*colon = 0;
			char *eq = strchr (colon + 1, '=');
			if (!eq)
				continue;
			*eq = 0;
			char *name = colon + 1;
			while (*name == ' ')
				name++;
			char *ne = name + strlen (name);
			while (ne > name && (ne[-1] == ' ' || ne[-1] == '\t'))
				*--ne = 0;
			char *rest = eq + 1;
			uint type = 0;
			uint off = 0;
			char *tp = strstr (rest, "type=");
			if (tp)
				type = (uint)strtoul (tp + 5, 0, 10);
			char *op = strstr (rest, "offset=");
			if (op)
				off = (uint)strtoul (op + 7, 0, 10);
			uint idx = msbp->num_attributes++;
			msbp->attributes = REALLOC (
				msbp->attributes, msbp->num_attributes * sizeof (msbp_attribute_t));
			memset (msbp->attributes + idx, 0, sizeof (msbp_attribute_t));
			msbp->attributes[idx].name = STRDUP (name);
			msbp->attributes[idx].type = (u8)type;
			msbp->attributes[idx].offset = off;
			char *lp = strstr (rest, "list=");
			if (lp)
			{
				MSBP_SPLIT_LIST (msbp->attributes[idx].list_items,
					msbp->attributes[idx].num_list_items, lp + 5);
				msbp->attributes[idx].type = 9;
			}
			msbp->has_attributes = true;
		}
		else if (section == 3)
		{
			if (!strncmp (s, "Group ", 6))
			{
				// Group <id> "<name>"
				uint gid = (uint)strtoul (s + 6, 0, 10);
				char *q1 = strchr (s, '"');
				char *q2 = q1 ? strchr (q1 + 1, '"') : 0;
				uint idx = msbp->num_tag_groups++;
				msbp->tag_groups = REALLOC (msbp->tag_groups,
					msbp->num_tag_groups * sizeof (msbp_tag_group_t));
				memset (msbp->tag_groups + idx, 0, sizeof (msbp_tag_group_t));
				msbp->tag_groups[idx].group_id = (u16)gid;
				if (q1 && q2 && q2 > q1 + 1)
				{
					*q2 = 0;
					msbp->tag_groups[idx].name = STRDUP (q1 + 1);
				}
				else
					msbp->tag_groups[idx].name = STRDUP ("");
				cur_group = msbp->tag_groups + idx;
				cur_tag = 0;
				msbp->has_tags = true;
			}
			else if (!strncmp (s, "Tag ", 4) && cur_group)
			{
				char *q1 = strchr (s, '"');
				char *q2 = q1 ? strchr (q1 + 1, '"') : 0;
				uint idx = cur_group->num_tags++;
				cur_group->tags
					= REALLOC (cur_group->tags, cur_group->num_tags * sizeof (msbp_tag_t));
				memset (cur_group->tags + idx, 0, sizeof (msbp_tag_t));
				cur_group->tags[idx].tag_id = (u16)idx;
				if (q1 && q2 && q2 > q1 + 1)
				{
					*q2 = 0;
					cur_group->tags[idx].name = STRDUP (q1 + 1);
				}
				else
					cur_group->tags[idx].name = STRDUP ("");
				cur_tag = cur_group->tags + idx;
			}
			else if (!strncmp (s, "Param ", 6) && cur_tag)
			{
				char *q1 = strchr (s, '"');
				char *q2 = q1 ? strchr (q1 + 1, '"') : 0;
				uint idx = cur_tag->num_params++;
				cur_tag->params = REALLOC (
					cur_tag->params, cur_tag->num_params * sizeof (msbp_tag_param_t));
				memset (cur_tag->params + idx, 0, sizeof (msbp_tag_param_t));
				if (q1 && q2 && q2 > q1 + 1)
				{
					*q2 = 0;
					cur_tag->params[idx].name = STRDUP (q1 + 1);
				}
				else
					cur_tag->params[idx].name = STRDUP ("");
				char *tp = strstr (q2 ? q2 + 1 : s, "type=");
				cur_tag->params[idx].type = tp ? (u8)strtoul (tp + 5, 0, 10) : 0;
				char *lp = strstr (q2 ? q2 + 1 : s, "list=");
				if (lp)
				{
					MSBP_SPLIT_LIST (cur_tag->params[idx].list_items,
						cur_tag->params[idx].num_list_items, lp + 5);
					cur_tag->params[idx].type = 9;
				}
			}
		}
		else if (section == 4 && *s == '#')
		{
			// Style line: #i: name = r,l,f,c
			char *colon = strchr (s, ':');
			if (!colon)
				continue;
			*colon = 0;
			char *eq = strchr (colon + 1, '=');
			if (!eq)
				continue;
			*eq = 0;
			char *name = colon + 1;
			while (*name == ' ')
				name++;
			char *ne = name + strlen (name);
			while (ne > name && (ne[-1] == ' ' || ne[-1] == '\t'))
				*--ne = 0;
			int r = 0, l = 0, fi = 0, c = 0;
			sscanf (eq + 1, "%d,%d,%d,%d", &r, &l, &fi, &c);
			uint idx = msbp->num_styles++;
			msbp->styles = REALLOC (msbp->styles, msbp->num_styles * sizeof (msbp_style_t));
			memset (msbp->styles + idx, 0, sizeof (msbp_style_t));
			msbp->styles[idx].name = STRDUP (name);
			msbp->styles[idx].region_width = r;
			msbp->styles[idx].line_number = l;
			msbp->styles[idx].font_index = fi;
			msbp->styles[idx].base_color_index = c;
			msbp->has_styles = true;
		}
		else if (section == 5)
		{
			if (*s == 0)
				continue;
			uint idx = msbp->num_source_files++;
			msbp->source_files
				= REALLOC (msbp->source_files, msbp->num_source_files * sizeof (char *));
			msbp->source_files[idx] = STRDUP (s);
			msbp->has_source_files = true;
		}
	}
#undef MSBP_SPLIT_LIST

	fclose (f);
	return ERR_OK;
}

// MSBF API Implementation
void InitMSBF (msbf_file_t *msbf)
{
	if (!msbf)
		return;
	memset (msbf, 0, sizeof (*msbf));
	msbf->encoding = MSBT_ENC_UTF16;
	msbf->version = 3;
}

void ResetMSBF (msbf_file_t *msbf)
{
	if (!msbf)
		return;
	if (msbf->fname)
	{
		FREE (msbf->fname);
		msbf->fname = 0;
	}
	if (msbf->nodes)
	{
		for (uint i = 0; i < msbf->num_nodes; i++)
		{
			if (msbf->nodes[i].label)
				FREE (msbf->nodes[i].label);
			if (msbf->nodes[i].msg_label)
				FREE (msbf->nodes[i].msg_label);
			if (msbf->nodes[i].branches)
				FREE (msbf->nodes[i].branches);
		}
		FREE (msbf->nodes);
		msbf->nodes = 0;
	}
	msbf->num_nodes = 0;
	msbf->alloc_nodes = 0;
	if (msbf->flow_string_table)
	{
		FREE (msbf->flow_string_table);
		msbf->flow_string_table = 0;
	}
	msbf->flow_string_table_size = 0;
}

enumError ScanMSBF (msbf_file_t *msbf, const u8 *data, uint data_size, ccp fname)
{
	if (!msbf || !data || data_size < 8)
		return ERR_INVALID_DATA;

	if (data_size >= 8 && !memcmp (data, "FZIP", 4))
	{
		u8 *dec = 0;
		uint dec_sz = 0;
		enumError derr = DecodeFZIP (&dec, &dec_sz, data, data_size);
		if (!derr && dec)
		{
			enumError ret = ScanMSBF (msbf, dec, dec_sz, fname);
			FREE (dec);
			return ret;
		}
	}

	if (data_size < 0x20 || memcmp (data, "MsgFlwBn", 8))
		return ERR_WRONG_FILE_TYPE;

	InitMSBF (msbf);
	if (fname)
		msbf->fname = STRDUP (fname);

	u16 bom = rd_be16 (data + 8);
	msbf->is_big_endian = (bom == 0xFEFF);
	bool be = msbf->is_big_endian;

	msbf->encoding = (msbt_encoding_t)data[0x0C];
	msbf->version = data[0x0D];
	u16 num_sections = r16 (data + 0x0E, be);

	const u8 *flw3_data = 0;
	uint flw3_size = 0;
	const u8 *flw2_data = 0;
	uint flw2_size = 0;
	const u8 *fen1_data = 0;
	uint fen1_size = 0;
	const u8 *lbl1_data = 0;
	uint lbl1_size = 0;

	uint cur = 0x20;
	for (uint s = 0; s < num_sections && cur + 16 <= data_size; s++)
	{
		char sec_magic[5] = { 0 };
		memcpy (sec_magic, data + cur, 4);
		if (cur + 16 > data_size)
			break;
		u32 sec_size = r32 (data + cur + 4, be);
		// Every section parser below trusts sec_size as its body length.
		if (sec_size > data_size - cur - 16)
			sec_size = data_size - cur - 16;
		const u8 *sec_body = data + cur + 16;

		if (!strcmp (sec_magic, "FLW3"))
		{
			flw3_data = sec_body;
			flw3_size = sec_size;
		}
		else if (!strcmp (sec_magic, "FLW2"))
		{
			flw2_data = sec_body;
			flw2_size = sec_size;
		}
		else if (!strcmp (sec_magic, "FEN1"))
		{
			fen1_data = sec_body;
			fen1_size = sec_size;
		}
		else if (!strcmp (sec_magic, "LBL1"))
		{
			lbl1_data = sec_body;
			lbl1_size = sec_size;
		}

		uint aligned_size = (sec_size + 15) & ~15;
		cur += 16 + aligned_size;
	}

	// Legacy FLW2 flows (CLMS parity): 12-byte entries
	// (type, f0, f2, f4, f6, f8 as s16) + u16 branch table.
	if (flw2_data && flw2_size >= 8)
	{
		msbf->is_legacy_flw2 = true;
		u16 num_flows = r16 (flw2_data, be);
		u16 num_branch_ids = r16 (flw2_data + 2, be);
		if (num_flows > 0 && num_flows < 100000 && 8 + num_flows * 12 <= flw2_size)
		{
			msbf->nodes = CALLOC (num_flows, sizeof (msbf_node_t));
			msbf->num_nodes = num_flows;
			msbf->alloc_nodes = num_flows;
			const u8 *branch_base = flw2_data + 8 + num_flows * 12;
			for (uint i = 0; i < num_flows; i++)
			{
				const u8 *fp = flw2_data + 8 + i * 12;
				msbf_node_t *n = msbf->nodes + i;
				n->node_id = i;
				memcpy (n->raw, fp, 12);
				n->has_raw = true;
				short ftype = (short)r16 (fp, be);
				short f2 = (short)r16 (fp + 4, be);
				short f4 = (short)r16 (fp + 6, be);
				short f6 = (short)r16 (fp + 8, be);
				short f8 = (short)r16 (fp + 10, be);
				if (ftype == 1) // Message
				{
					n->type = MSBF_NODE_MESSAGE;
					n->msbt_index = (u16)f2;
					n->msg_index = (u16)f4;
					n->event_param = (u32)(u16)f8;
					n->next_node = (f6 == -1) ? 0xFFFF : (u16)f6;
				}
				else if (ftype == 2) // Condition -> Branch
				{
					n->type = MSBF_NODE_BRANCH;
					n->condition_id = (u16)f4;
					n->event_param = (u32)(u16)f6;
					// Two branch targets via the shared branch table
					if (f8 >= 0 && (uint)f8 + 2 <= num_branch_ids
						&& branch_base + ((uint)f8 + 2) * 2 <= flw2_data + flw2_size)
					{
						n->branches = CALLOC (2, sizeof (u16));
						n->num_branches = 2;
						n->branches[0] = r16 (branch_base + (uint)f8 * 2, be);
						n->branches[1] = r16 (branch_base + ((uint)f8 + 1) * 2, be);
					}
					n->next_node = 0xFFFF;
				}
				else if (ftype == 3) // Event
				{
					n->type = MSBF_NODE_EVENT;
					n->event_id = (u16)f2;
					n->event_param = ((u32)(u16)f6 << 16) | (u16)f8;
					n->next_node = (f4 == -1) ? 0xFFFF : (u16)f4;
				}
				else if (ftype == 4) // Initializer -> Entry
				{
					n->type = MSBF_NODE_ENTRY;
					n->event_param = ((u32)(u16)f4 << 16) | (u16)f6;
					n->next_node = (f2 == -1) ? 0xFFFF : (u16)f2;
				}
				else
				{
					n->type = (u8)ftype;
					n->next_node = 0xFFFF;
				}
			}
		}
	}

	if (!msbf->is_legacy_flw2 && flw3_data && flw3_size >= 4)
	{
		u16 num_nodes = r16 (flw3_data, be);
		u16 num_branches = r16 (flw3_data + 2, be);
		(void)num_branches;

		if (num_nodes > 0 && 4 + num_nodes * 16 <= flw3_size)
		{
			msbf->nodes = CALLOC (num_nodes, sizeof (msbf_node_t));
			msbf->num_nodes = num_nodes;
			msbf->alloc_nodes = num_nodes;

			for (uint i = 0; i < num_nodes; i++)
			{
				const u8 *np = flw3_data + 4 + i * 16;
				msbf->nodes[i].node_id = i;
				msbf->nodes[i].type = np[0];
				msbf->nodes[i].next_node = r16 (np + 2, be);

				if (msbf->nodes[i].type == MSBF_NODE_MESSAGE)
				{
					msbf->nodes[i].msg_index = r16 (np + 4, be);
				}
				else if (msbf->nodes[i].type == MSBF_NODE_BRANCH)
				{
					msbf->nodes[i].condition_id = r16 (np + 4, be);
					u16 branch_count = r16 (np + 6, be);
					u16 branch_off = r16 (np + 8, be);
					if (branch_count > 0
						&& 4 + num_nodes * 16 + (branch_off + branch_count) * 2 <= flw3_size)
					{
						msbf->nodes[i].branches = CALLOC (branch_count, sizeof (u16));
						msbf->nodes[i].num_branches = branch_count;
						for (uint b = 0; b < branch_count; b++)
						{
							const u8 *bp = flw3_data + 4 + num_nodes * 16 + (branch_off + b) * 2;
							msbf->nodes[i].branches[b] = r16 (bp, be);
						}
					}
				}
				else if (msbf->nodes[i].type == MSBF_NODE_EVENT)
				{
					msbf->nodes[i].event_id = r16 (np + 4, be);
					msbf->nodes[i].event_param = r32 (np + 6, be);
				}
			}
		}
	}

	if (lbl1_data && lbl1_size >= 4)
		msbf->has_lbl1 = true;
	if (lbl1_data && lbl1_size >= 4 && msbf->nodes)
	{
		u32 num_groups = r32 (lbl1_data, be);
		for (uint g = 0; g < num_groups && 4 + (g + 1) * 8 <= lbl1_size; g++)
		{
			u32 count = r32 (lbl1_data + 4 + g * 8, be);
			u32 group_off = r32 (lbl1_data + 4 + g * 8 + 4, be);
			uint pos = group_off;
			for (uint c = 0; c < count && pos < lbl1_size; c++)
			{
				u8 nlen = lbl1_data[pos++];
				if (pos + nlen + 4 <= lbl1_size)
				{
					char name[256];
					memcpy (name, lbl1_data + pos, nlen);
					name[nlen] = 0;
					pos += nlen;
					u32 idx = r32 (lbl1_data + pos, be);
					pos += 4;
					if (idx < msbf->num_nodes)
					{
						if (msbf->nodes[idx].label)
							FREE (msbf->nodes[idx].label);
						msbf->nodes[idx].label = STRDUP (name);
					}
				}
				else
					break;
			}
		}
	}

	// FEN1 entry labels (CLMS parity): hash buckets of (label -> flow index).
	if (fen1_data && fen1_size >= 4)
	{
		u32 num_slots = r32 (fen1_data, be);
		if (num_slots > 0 && num_slots < 100000 && 4 + num_slots * 8 <= fen1_size)
		{
			msbf->has_fen1 = true;
			msbf->fen1_slot_count = num_slots;
			for (uint g = 0; g < num_slots; g++)
			{
				u32 count = r32 (fen1_data + 4 + g * 8, be);
				u32 group_off = r32 (fen1_data + 4 + g * 8 + 4, be);
				uint pos = group_off;
				for (uint c = 0; c < count && pos < fen1_size; c++)
				{
					u8 nlen = fen1_data[pos++];
					if (pos + nlen + 4 > fen1_size)
						break;
					u32 idx = r32 (fen1_data + pos + nlen, be);
					if (idx < msbf->num_nodes)
					{
						char *name = MALLOC (nlen + 1);
						memcpy (name, fen1_data + pos, nlen);
						name[nlen] = 0;
						if (msbf->nodes[idx].label)
							FREE (msbf->nodes[idx].label);
						msbf->nodes[idx].label = name;
					}
					pos += nlen + 4;
				}
			}
		}
	}
	return ERR_OK;
}

enumError SaveTextMSBF (const msbf_file_t *msbf, ccp dest_fname)
{
	if (!msbf || !dest_fname)
		return ERR_INVALID_DATA;
	FILE *f = fopen (dest_fname, "w");
	if (!f)
		return ERR_CANT_CREATE;

	fprintf (f, "# MSBF: Message Studio Binary Flowchart (%s)\n",
		msbf->is_big_endian ? "BigEndian" : "LittleEndian");
	fprintf (f, "# Nodes: %u\n", msbf->num_nodes);
	// Variant markers (only emitted for legacy files; plain FLW3/LBL1
	// output is byte-identical to older builds).
	if (msbf->is_legacy_flw2)
		fprintf (f, "# Flow: FLW2\n");
	if (msbf->has_fen1)
		fprintf (f, "# Labels: FEN1\n");
	fprintf (f, "\n");

	for (uint i = 0; i < msbf->num_nodes; i++)
	{
		const msbf_node_t *n = msbf->nodes + i;
		fprintf (f, "[Node #%u", i);
		if (n->label && *n->label)
			fprintf (f, " (%s)", n->label);
		fprintf (f, "]\n");

		if (n->type == MSBF_NODE_MESSAGE)
		{
			if (n->msbt_index != 0)
				fprintf (f, "  type = Message (msg_index=%u, next=%u, group=%u)\n", n->msg_index,
					n->next_node, n->msbt_index);
			else
				fprintf (f, "  type = Message (msg_index=%u, next=%u)\n", n->msg_index,
					n->next_node);
		}
		else if (n->type == MSBF_NODE_BRANCH)
		{
			fprintf (f, "  type = Branch (condition=%u, branches=[", n->condition_id);
			for (uint b = 0; b < n->num_branches; b++)
				fprintf (f, "%u%s", n->branches[b], (b + 1 < n->num_branches) ? ", " : "");
			fprintf (f, "])\n");
		}
		else if (n->type == MSBF_NODE_EVENT)
			fprintf (f, "  type = Event (event_id=%u, param=0x%x, next=%u)\n", n->event_id,
				n->event_param, n->next_node);
		else if (n->type == MSBF_NODE_ENTRY)
			fprintf (f, "  type = EntryPoint (next=%u)\n", n->next_node);
		else
			fprintf (f, "  type = Unknown (%u)\n", n->type);

		fprintf (f, "\n");
	}

	fclose (f);
	return ERR_OK;
}

enumError SaveJSONMSBF (const msbf_file_t *msbf, ccp dest_fname)
{
	if (!msbf || !dest_fname)
		return ERR_INVALID_DATA;
	FILE *f = fopen (dest_fname, "w");
	if (!f)
		return ERR_CANT_CREATE;

	fprintf (f, "{\n  \"endian\": \"%s\",\n  \"nodes\": [\n",
		msbf->is_big_endian ? "BigEndian" : "LittleEndian");
	for (uint i = 0; i < msbf->num_nodes; i++)
	{
		const msbf_node_t *n = msbf->nodes + i;
		fprintf (f, "    {\n");
		fprintf (f, "      \"node_id\": %u,\n", n->node_id);
		fprintf (f, "      \"label\": \"%s\",\n", n->label ? n->label : "");
		fprintf (f, "      \"type\": %u,\n", n->type);
		fprintf (f, "      \"next_node\": %u,\n", n->next_node);
		if (n->type == MSBF_NODE_MESSAGE)
			fprintf (f, "      \"msg_index\": %u\n", n->msg_index);
		else if (n->type == MSBF_NODE_BRANCH)
		{
			fprintf (f, "      \"condition\": %u,\n", n->condition_id);
			fprintf (f, "      \"branches\": [");
			for (uint b = 0; b < n->num_branches; b++)
				fprintf (f, "%u%s", n->branches[b], (b + 1 < n->num_branches) ? ", " : "");
			fprintf (f, "]\n");
		}
		else if (n->type == MSBF_NODE_EVENT)
		{
			fprintf (f, "      \"event_id\": %u,\n", n->event_id);
			fprintf (f, "      \"param\": %u\n", n->event_param);
		}
		else
			fprintf (f, "      \"custom\": 0\n");
		fprintf (f, "    }%s\n", (i + 1 < msbf->num_nodes) ? "," : "");
	}
	fprintf (f, "  ]\n}\n");
	fclose (f);
	return ERR_OK;
}

enumError CreateMSBF (u8 **out_data, uint *out_size, const msbf_file_t *msbf)
{
	if (!out_data || !out_size || !msbf)
		return ERR_INVALID_DATA;
	bool be = msbf->is_big_endian;
	bool legacy = msbf->is_legacy_flw2;

	// ---- Flow section ----
	u8 *flw_buf = 0;
	uint flw_len = 0;
	const char *flw_magic = "FLW3";
	uint total_branches = 0;
	for (uint i = 0; i < msbf->num_nodes; i++)
		total_branches += msbf->nodes[i].num_branches;

	if (legacy)
	{
		// FLW2: u16 flows + u16 branch ids + u32 pad, 12-byte flows, u16 table.
		// Raw 12-byte flows are preserved; only the editable fields below
		// are rewritten so text edits round-trip without data loss.
		flw_magic = "FLW2";
		flw_len = 8 + msbf->num_nodes * 12 + total_branches * 2;
		flw_buf = CALLOC (flw_len + 16, 1);
		w16 (flw_buf, msbf->num_nodes, be);
		w16 (flw_buf + 2, total_branches, be);
		w32 (flw_buf + 4, 0, be);
		uint cur_bid = 0;
		for (uint i = 0; i < msbf->num_nodes; i++)
		{
			u8 *fp = flw_buf + 8 + i * 12;
			if (msbf->nodes[i].has_raw)
				memcpy (fp, msbf->nodes[i].raw, 12);
			u16 ftype = 0;
			short f2 = 0, f4 = 0, f6 = 0, f8 = 0;
			if (msbf->nodes[i].type == MSBF_NODE_MESSAGE)
			{
				ftype = 1;
				f2 = (short)msbf->nodes[i].msbt_index;
				f4 = (short)msbf->nodes[i].msg_index;
				f6 = (msbf->nodes[i].next_node == 0xFFFF) ? -1 : (short)msbf->nodes[i].next_node;
				if (!msbf->nodes[i].has_raw)
					f8 = 0;
				else
					f8 = (short)r16 (msbf->nodes[i].raw + 10, be);
			}
			else if (msbf->nodes[i].type == MSBF_NODE_BRANCH)
			{
				ftype = 2;
				if (!msbf->nodes[i].has_raw)
					f2 = (short)msbf->nodes[i].num_branches;
				else
					f2 = (short)r16 (msbf->nodes[i].raw + 4, be);
				f4 = (short)msbf->nodes[i].condition_id;
				if (!msbf->nodes[i].has_raw)
					f6 = 0;
				else
					f6 = (short)r16 (msbf->nodes[i].raw + 8, be);
				f8 = (short)cur_bid;
				for (uint b = 0; b < msbf->nodes[i].num_branches; b++)
				{
					u8 *bp = flw_buf + 8 + msbf->num_nodes * 12 + (cur_bid + b) * 2;
					w16 (bp, msbf->nodes[i].branches[b], be);
				}
				cur_bid += msbf->nodes[i].num_branches;
			}
			else if (msbf->nodes[i].type == MSBF_NODE_EVENT)
			{
				ftype = 3;
				f2 = (short)msbf->nodes[i].event_id;
				f4 = (msbf->nodes[i].next_node == 0xFFFF) ? -1 : (short)msbf->nodes[i].next_node;
				f6 = (short)(msbf->nodes[i].event_param >> 16);
				f8 = (short)(msbf->nodes[i].event_param & 0xFFFF);
			}
			else if (msbf->nodes[i].type == MSBF_NODE_ENTRY)
			{
				ftype = 4;
				f2 = (msbf->nodes[i].next_node == 0xFFFF) ? -1 : (short)msbf->nodes[i].next_node;
				f4 = (short)(msbf->nodes[i].event_param >> 16);
				f6 = (short)(msbf->nodes[i].event_param & 0xFFFF);
				if (!msbf->nodes[i].has_raw)
					f8 = 0;
				else
					f8 = (short)r16 (msbf->nodes[i].raw + 10, be);
			}
			else
			{
				// Unknown type: keep raw bytes verbatim.
				continue;
			}
			w16 (fp, ftype, be);
			w16 (fp + 4, (u16)f2, be);
			w16 (fp + 6, (u16)f4, be);
			w16 (fp + 8, (u16)f6, be);
			w16 (fp + 10, (u16)f8, be);
		}
	}
	else
	{
		flw_len = 4 + msbf->num_nodes * 16 + total_branches * 2;
		flw_buf = CALLOC (flw_len + 16, 1);

		w16 (flw_buf, msbf->num_nodes, be);
		w16 (flw_buf + 2, total_branches, be);

		uint cur_branch_idx = 0;
		for (uint i = 0; i < msbf->num_nodes; i++)
		{
			u8 *np = flw_buf + 4 + i * 16;
			if (msbf->nodes[i].has_raw)
				memcpy (np, msbf->nodes[i].raw, 16);
			np[0] = msbf->nodes[i].type;
			np[1] = msbf->nodes[i].param_type;
			w16 (np + 2, msbf->nodes[i].next_node, be);

			if (msbf->nodes[i].type == MSBF_NODE_MESSAGE)
			{
				w16 (np + 4, msbf->nodes[i].msg_index, be);
			}
			else if (msbf->nodes[i].type == MSBF_NODE_BRANCH)
			{
				w16 (np + 4, msbf->nodes[i].condition_id, be);
				w16 (np + 6, msbf->nodes[i].num_branches, be);
				w16 (np + 8, cur_branch_idx, be);
				for (uint b = 0; b < msbf->nodes[i].num_branches; b++)
				{
					u8 *bp = flw_buf + 4 + msbf->num_nodes * 16 + (cur_branch_idx + b) * 2;
					w16 (bp, msbf->nodes[i].branches[b], be);
				}
				cur_branch_idx += msbf->nodes[i].num_branches;
			}
			else if (msbf->nodes[i].type == MSBF_NODE_EVENT)
			{
				w16 (np + 4, msbf->nodes[i].event_id, be);
				w32 (np + 6, msbf->nodes[i].event_param, be);
			}
		}
	}

	// Check if labels exist
	bool has_labels = false;
	for (uint i = 0; i < msbf->num_nodes; i++)
	{
		if (msbf->nodes[i].label && *msbf->nodes[i].label)
		{
			has_labels = true;
			break;
		}
	}

	// Label section: FEN1 for legacy files (or when explicitly flagged),
	// otherwise LBL1. Text-created files keep the historical FLW3+LBL1.
	bool want_fen1 = has_labels && (msbf->has_fen1 || legacy);
	bool want_lbl1 = has_labels && !want_fen1;
	if (has_labels && msbf->has_lbl1 && !msbf->has_fen1 && !legacy)
		want_lbl1 = true;

	u8 *lbl_buf = 0;
	uint lbl_len = 0;
	const char *lbl_magic = "LBL1";
	if (want_fen1 || want_lbl1)
	{
		uint num_groups;
		if (want_fen1 && msbf->fen1_slot_count > 0)
			num_groups = msbf->fen1_slot_count;
		else
		{
			num_groups = msbf->num_nodes > 0 ? msbf->num_nodes : 1;
			if (num_groups > 101)
				num_groups = 101;
		}
		if (want_fen1)
			lbl_magic = "FEN1";
		u32 *group_counts = CALLOC (num_groups, sizeof (u32));
		for (uint i = 0; i < msbf->num_nodes; i++)
		{
			if (msbf->nodes[i].label && *msbf->nodes[i].label)
			{
				u32 g = msbt_hash (msbf->nodes[i].label, num_groups);
				group_counts[g]++;
			}
		}
		uint group_table_size = 4 + num_groups * 8;
		uint total_labels_size = 0;
		for (uint i = 0; i < msbf->num_nodes; i++)
		{
			if (msbf->nodes[i].label && *msbf->nodes[i].label)
				total_labels_size += 1 + strlen (msbf->nodes[i].label) + 4;
		}
		lbl_len = group_table_size + total_labels_size;
		lbl_buf = CALLOC (lbl_len + 16, 1);
		w32 (lbl_buf, num_groups, be);
		uint cur_label_off = group_table_size;
		for (uint g = 0; g < num_groups; g++)
		{
			w32 (lbl_buf + 4 + g * 8, group_counts[g], be);
			w32 (lbl_buf + 4 + g * 8 + 4, cur_label_off, be);
			for (uint i = 0; i < msbf->num_nodes; i++)
			{
				if (msbf->nodes[i].label && *msbf->nodes[i].label)
				{
					if (msbt_hash (msbf->nodes[i].label, num_groups) == g)
					{
						uint nlen = strlen (msbf->nodes[i].label);
						if (nlen > 255)
							nlen = 255;
						lbl_buf[cur_label_off++] = (u8)nlen;
						memcpy (lbl_buf + cur_label_off, msbf->nodes[i].label, nlen);
						cur_label_off += nlen;
						w32 (lbl_buf + cur_label_off, i, be);
						cur_label_off += 4;
					}
				}
			}
		}
		FREE (group_counts);
	}

	u16 num_sections = 1;
	uint total_size = 0x20 + 16 + ((flw_len + 15) & ~15);
	if (lbl_buf)
	{
		num_sections++;
		total_size += 16 + ((lbl_len + 15) & ~15);
	}

	u8 *out = CALLOC (total_size, 1);
	memcpy (out, "MsgFlwBn", 8);
	wr_be16 (out + 8, be ? 0xFEFF : 0xFFFE);
	out[0x0A] = 0;
	out[0x0B] = 0;
	out[0x0C] = (u8)msbf->encoding;
	out[0x0D] = msbf->version ? msbf->version : 3;
	w16 (out + 0x0E, num_sections, be);
	out[0x10] = 0;
	out[0x11] = 0;
	w32 (out + 0x12, total_size, be);
	memset (out + 0x16, 0, 0x0A);

	uint pos = 0x20;
	memcpy (out + pos, flw_magic, 4);
	w32 (out + pos + 4, flw_len, be);
	memset (out + pos + 8, 0, 8);
	memcpy (out + pos + 16, flw_buf, flw_len);
	{
		uint end = pos + 16 + flw_len;
		uint aligned = pos + 16 + ((flw_len + 15) & ~15);
		for (uint p = end; p < aligned; p++)
			out[p] = 0xAB;
		pos = aligned;
	}
	FREE (flw_buf);

	if (lbl_buf)
	{
		memcpy (out + pos, lbl_magic, 4);
		w32 (out + pos + 4, lbl_len, be);
		memset (out + pos + 8, 0, 8);
		memcpy (out + pos + 16, lbl_buf, lbl_len);
		{
			uint end = pos + 16 + lbl_len;
			uint aligned = pos + 16 + ((lbl_len + 15) & ~15);
			for (uint p = end; p < aligned; p++)
				out[p] = 0xAB;
		}
		FREE (lbl_buf);
	}

	*out_data = out;
	*out_size = total_size;
	return ERR_OK;
}

enumError LoadTextMSBF (msbf_file_t *msbf, ccp src_fname)
{
	if (!msbf || !src_fname)
		return ERR_INVALID_DATA;
	FILE *f = fopen (src_fname, "r");
	if (!f)
		return ERR_CANT_OPEN;

	InitMSBF (msbf);
	msbf->fname = STRDUP (src_fname);

	char line[1024];
	msbf_node_t *cur_node = 0;

	while (fgets (line, sizeof (line), f))
	{
		char *s = line;
		while (*s == ' ' || *s == '\t')
			s++;
		if (*s == 0 || *s == '\r' || *s == '\n')
			continue;
		if (*s == '#')
		{
			if (strstr (line, "BigEndian"))
				msbf->is_big_endian = true;
			if (strstr (line, "LittleEndian"))
				msbf->is_big_endian = false;
			if (strstr (line, "FLW2"))
				msbf->is_legacy_flw2 = true;
			if (strstr (line, "FEN1"))
				msbf->has_fen1 = true;
			continue;
		}

		if (*s == '[')
		{
			char *label_start = strchr (s, '(');
			char label[128] = "";
			if (label_start)
			{
				label_start++;
				char *label_end = strchr (label_start, ')');
				if (label_end)
				{
					*label_end = 0;
					strncpy (label, label_start, sizeof (label) - 1);
				}
			}

			uint idx = msbf->num_nodes++;
			msbf->nodes = REALLOC (msbf->nodes, msbf->num_nodes * sizeof (msbf_node_t));
			cur_node = msbf->nodes + idx;
			memset (cur_node, 0, sizeof (*cur_node));
			cur_node->node_id = (u16)idx;
			cur_node->next_node = 0xFFFF;
			if (label[0])
				cur_node->label = STRDUP (label);
		}
		else if (cur_node)
		{
			unsigned int tmp1 = 0, tmp2 = 0;
			if (strstr (s, "type = Message"))
			{
				cur_node->type = MSBF_NODE_MESSAGE;
				char *mi = strstr (s, "msg_index=");
				if (mi && sscanf (mi + 10, "%u", &tmp1) == 1)
					cur_node->msg_index = (u16)tmp1;
				char *nxt = strstr (s, "next=");
				if (nxt && sscanf (nxt + 5, "%u", &tmp2) == 1)
					cur_node->next_node = (u16)tmp2;
				char *gr = strstr (s, "group=");
				if (gr && sscanf (gr + 6, "%u", &tmp1) == 1)
					cur_node->msbt_index = (u16)tmp1;
			}
			else if (strstr (s, "type = Branch") || strstr (s, "type = Condition"))
			{
				cur_node->type = MSBF_NODE_BRANCH;
				char *cd = strstr (s, "condition=");
				if (cd && sscanf (cd + 10, "%u", &tmp1) == 1)
					cur_node->condition_id = (u16)tmp1;
				// branches=[a, b, ...] (previously dropped on load)
				char *br = strstr (s, "branches=[");
				if (br)
				{
					br += 10;
					uint cap = 0;
					for (;;)
					{
						while (*br == ' ' || *br == '\t')
							br++;
						if (*br == ']' || *br == 0)
							break;
						if (sscanf (br, "%u", &tmp1) != 1)
							break;
						if (cur_node->num_branches >= cap)
						{
							cap = cap ? cap * 2 : 4;
							cur_node->branches = REALLOC (
								cur_node->branches, cap * sizeof (u16));
						}
						cur_node->branches[cur_node->num_branches++] = (u16)tmp1;
						while (*br >= '0' && *br <= '9')
							br++;
						while (*br == ' ' || *br == '\t')
							br++;
						if (*br == ',')
							br++;
						else
							break;
					}
				}
			}
			else if (strstr (s, "type = Event"))
			{
				cur_node->type = MSBF_NODE_EVENT;
				char *ev = strstr (s, "event_id=");
				if (ev && sscanf (ev + 9, "%u", &tmp1) == 1)
					cur_node->event_id = (u16)tmp1;
				char *pm = strstr (s, "param=");
				if (pm)
					sscanf (pm + 6, "%x", &cur_node->event_param);
				char *nxt = strstr (s, "next=");
				if (nxt && sscanf (nxt + 5, "%u", &tmp2) == 1)
					cur_node->next_node = (u16)tmp2;
			}
			else if (strstr (s, "type = EntryPoint") || strstr (s, "type = Initializer"))
			{
				cur_node->type = MSBF_NODE_ENTRY;
				char *nxt = strstr (s, "next=");
				if (nxt && sscanf (nxt + 5, "%u", &tmp2) == 1)
					cur_node->next_node = (u16)tmp2;
			}
		}
	}

	fclose (f);
	return ERR_OK;
}

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
		else if (ch == 0x000F) // Tag end
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
		else if (b == 0x0F)
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

	// Section pointers
	const u8 *lbl1_data = 0;
	uint lbl1_size = 0;
	const u8 *txt2_data = 0;
	uint txt2_size = 0;
	const u8 *atr1_data = 0;
	uint atr1_size = 0;
	const u8 *tsy1_data = 0;
	uint tsy1_size = 0;

	uint cur = 0x20;
	for (uint s = 0; s < num_sections && cur + 16 <= data_size; s++)
	{
		char sec_magic[5] = { 0 };
		memcpy (sec_magic, data + cur, 4);
		u32 sec_size = r32 (data + cur + 4, be);
		const u8 *sec_body = data + cur + 16;

		if (!strcmp (sec_magic, "LBL1"))
		{
			lbl1_data = sec_body;
			lbl1_size = sec_size;
		}
		else if (!strcmp (sec_magic, "TXT2"))
		{
			txt2_data = sec_body;
			txt2_size = sec_size;
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
		if (4 + (i + 1) * 4 <= txt2_size)
		{
			u32 str_off = r32 (txt2_data + 4 + i * 4, be);
			if (str_off < txt2_size)
			{
				const u8 *str_bytes = txt2_data + str_off;
				uint max_avail = txt2_size - str_off;

				if (msbt->encoding == MSBT_ENC_UTF8)
					msbt->entries[i].text = decode_utf8_msbt_string (str_bytes, max_avail, be);
				else
					msbt->entries[i].text = decode_utf16_msbt_string (str_bytes, max_avail, be);
			}
		}
	}

	// Parse labels from LBL1
	if (lbl1_data && lbl1_size >= 4)
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

					u32 str_idx = r32 (lbl1_data + pos, be);
					pos += 4;

					if (str_idx < msbt->num_entries)
					{
						if (msbt->entries[str_idx].label)
							FREE (msbt->entries[str_idx].label);
						msbt->entries[str_idx].label = STRDUP (name);
					}
				}
			}
		}
	}

	// Parse attributes from ATR1
	if (atr1_data && atr1_size >= 8)
	{
		u32 attr_count = r32 (atr1_data, be);
		u32 item_size = r32 (atr1_data + 4, be);
		msbt->attr_item_size = item_size;

		if (item_size > 0 && 8 + attr_count * item_size <= atr1_size)
		{
			for (uint i = 0; i < attr_count && i < msbt->num_entries; i++)
			{
				msbt->entries[i].attrib = MALLOC (item_size);
				msbt->entries[i].attrib_size = item_size;
				memcpy (msbt->entries[i].attrib, atr1_data + 8 + i * item_size, item_size);
			}
		}
	}

	// Parse styles from TSY1
	if (tsy1_data && tsy1_size >= 4 + num_strings * 4)
	{
		for (uint i = 0; i < num_strings; i++)
			msbt->entries[i].style_index = r32 (tsy1_data + 4 + i * 4, be);
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

	fprintf (f, "# MSBT: Message Studio Binary Text (%s, %s)\n",
		msbt->is_big_endian ? "BigEndian" : "LittleEndian",
		msbt->encoding == MSBT_ENC_UTF8 ? "UTF-8" : "UTF-16");
	fprintf (f, "# Entries: %u\n\n", msbt->num_entries);

	for (uint i = 0; i < msbt->num_entries; i++)
	{
		const msbt_entry_t *e = msbt->entries + i;
		if (e->label && *e->label)
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

	fprintf (f, "{\n");
	fprintf (f, "  \"endian\": \"%s\",\n", msbt->is_big_endian ? "big" : "little");
	fprintf (f, "  \"encoding\": \"%s\",\n", msbt->encoding == MSBT_ENC_UTF8 ? "utf-8" : "utf-16");
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

// Encode UTF-8 string (containing tags like [tag:1,2,0011] or \n) into binary buffer (UTF-16 or
// UTF-8)
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
			if (enc == MSBT_ENC_UTF16)
			{
				w16 (buf + len, '\n', be);
				len += 2;
			}
			else
			{
				buf[len++] = '\n';
			}
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

			if (len + 8 + num_params >= cap)
			{
				cap = cap * 2 + num_params + 64;
				buf = REALLOC (buf, cap);
			}
			if (enc == MSBT_ENC_UTF16)
			{
				w16 (buf + len, 0x000E, be);
				len += 2;
				w16 (buf + len, group, be);
				len += 2;
				w16 (buf + len, tag, be);
				len += 2;
				w16 (buf + len, num_params, be);
				len += 2;
			}
			else
			{
				buf[len++] = 0x0E;
				w16 (buf + len, group, be);
				len += 2;
				w16 (buf + len, tag, be);
				len += 2;
				w16 (buf + len, num_params, be);
				len += 2;
			}
			if (num_params > 0)
			{
				memcpy (buf + len, hex_params, num_params);
				len += num_params;
			}
		}
		else if (*p == '[' && !strncmp (p, "[/tag]", 6))
		{
			p += 6;
			if (len + 4 >= cap)
			{
				cap *= 2;
				buf = REALLOC (buf, cap);
			}
			if (enc == MSBT_ENC_UTF16)
			{
				w16 (buf + len, 0x000F, be);
				len += 2;
			}
			else
			{
				buf[len++] = 0x0F;
			}
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
			if (enc == MSBT_ENC_UTF16)
			{
				if (cp <= 0xFFFF)
				{
					w16 (buf + len, (u16)cp, be);
					len += 2;
				}
				else
				{
					cp -= 0x10000;
					w16 (buf + len, (u16)(0xD800 | (cp >> 10)), be);
					len += 2;
					w16 (buf + len, (u16)(0xDC00 | (cp & 0x3FF)), be);
					len += 2;
				}
			}
			else
			{
				if (cp < 0x80)
				{
					buf[len++] = (u8)cp;
				}
				else if (cp < 0x800)
				{
					buf[len++] = (u8)(0xC0 | (cp >> 6));
					buf[len++] = (u8)(0x80 | (cp & 0x3F));
				}
				else if (cp < 0x10000)
				{
					buf[len++] = (u8)(0xE0 | (cp >> 12));
					buf[len++] = (u8)(0x80 | ((cp >> 6) & 0x3F));
					buf[len++] = (u8)(0x80 | (cp & 0x3F));
				}
				else
				{
					buf[len++] = (u8)(0xF0 | (cp >> 18));
					buf[len++] = (u8)(0x80 | ((cp >> 12) & 0x3F));
					buf[len++] = (u8)(0x80 | ((cp >> 6) & 0x3F));
					buf[len++] = (u8)(0x80 | (cp & 0x3F));
				}
			}
		}
	}

	// Null terminator
	if (len + 4 >= cap)
	{
		cap += 16;
		buf = REALLOC (buf, cap);
	}
	if (enc == MSBT_ENC_UTF16)
	{
		w16 (buf + len, 0, be);
		len += 2;
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

	// Check if any labels exist
	bool has_labels = false;
	for (uint i = 0; i < msbt->num_entries; i++)
	{
		if (msbt->entries[i].label && *msbt->entries[i].label)
		{
			has_labels = true;
			break;
		}
	}

	// Check if attributes exist
	bool has_attribs = (msbt->attr_item_size > 0);
	for (uint i = 0; !has_attribs && i < msbt->num_entries; i++)
	{
		if (msbt->entries[i].attrib_size > 0)
			has_attribs = true;
	}

	// Build LBL1 section
	u8 *lbl1_buf = 0;
	uint lbl1_len = 0;
	if (has_labels)
	{
		uint num_groups = msbt->num_entries > 0 ? msbt->num_entries : 1;
		if (num_groups > 101)
			num_groups = 101;

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

	// Build ATR1 section
	u8 *atr1_buf = 0;
	uint atr1_len = 0;
	if (has_attribs)
	{
		uint item_sz = msbt->attr_item_size > 0 ? msbt->attr_item_size : 4;
		atr1_len = 8 + msbt->num_entries * item_sz;
		atr1_buf = CALLOC (atr1_len + 16, 1);
		w32 (atr1_buf, msbt->num_entries, be);
		w32 (atr1_buf + 4, item_sz, be);
		for (uint i = 0; i < msbt->num_entries; i++)
		{
			if (msbt->entries[i].attrib && msbt->entries[i].attrib_size > 0)
			{
				uint cpy = msbt->entries[i].attrib_size < item_sz ? msbt->entries[i].attrib_size
																  : item_sz;
				memcpy (atr1_buf + 8 + i * item_sz, msbt->entries[i].attrib, cpy);
			}
		}
	}

	// Build TXT2 section
	uint offsets_size = 4 + msbt->num_entries * 4;
	u8 **str_buffers = CALLOC (msbt->num_entries, sizeof (u8 *));
	uint *str_lens = CALLOC (msbt->num_entries, sizeof (uint));
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

	// Calculate total file size
	u16 num_sections = 1; // TXT2
	if (lbl1_buf)
		num_sections++;
	if (atr1_buf)
		num_sections++;

	uint total_size = 0x20;
	if (lbl1_buf)
		total_size += 16 + ((lbl1_len + 15) & ~15);
	if (atr1_buf)
		total_size += 16 + ((atr1_len + 15) & ~15);
	total_size += 16 + ((txt2_len + 15) & ~15);

	u8 *out = CALLOC (total_size, 1);

	// File header
	memcpy (out, "MsgStdBn", 8);
	wr_be16 (out + 8, be ? 0xFEFF : 0xFFFE);
	out[0x0A] = 0;
	out[0x0B] = 0;
	out[0x0C] = (u8)enc;
	out[0x0D] = msbt->version ? msbt->version : 3;
	w16 (out + 0x0E, num_sections, be);
	w32 (out + 0x14, total_size, be);

	uint cur_sec = 0x20;
	if (lbl1_buf)
	{
		memcpy (out + cur_sec, "LBL1", 4);
		w32 (out + cur_sec + 4, lbl1_len, be);
		memcpy (out + cur_sec + 16, lbl1_buf, lbl1_len);
		cur_sec += 16 + ((lbl1_len + 15) & ~15);
		FREE (lbl1_buf);
	}
	if (atr1_buf)
	{
		memcpy (out + cur_sec, "ATR1", 4);
		w32 (out + cur_sec + 4, atr1_len, be);
		memcpy (out + cur_sec + 16, atr1_buf, atr1_len);
		cur_sec += 16 + ((atr1_len + 15) & ~15);
		FREE (atr1_buf);
	}
	if (txt2_buf)
	{
		memcpy (out + cur_sec, "TXT2", 4);
		w32 (out + cur_sec + 4, txt2_len, be);
		memcpy (out + cur_sec + 16, txt2_buf, txt2_len);
		cur_sec += 16 + ((txt2_len + 15) & ~15);
		FREE (txt2_buf);
	}

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

	char line[4096];
	char cur_label[256] = "";
	char cur_text[65536] = "";
	u8 cur_attr[256] = { 0 };
	uint cur_attr_len = 0;
	bool in_entry = false;

	while (fgets (line, sizeof (line), f))
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
			if (strstr (line, "UTF-8"))
				msbt->encoding = MSBT_ENC_UTF8;
			if (strstr (line, "UTF-16"))
				msbt->encoding = MSBT_ENC_UTF16;
			continue;
		}

		if (line[0] == '[' && line[len - 1] == ']' && len > 2)
		{
			// Save previous entry
			if (in_entry)
			{
				// SaveTextMSBT() emits one empty physical line between entries.
				// It is syntax, not message data (embedded newlines are escaped
				// as "\\n"), so don't let it accumulate on every text roundtrip.
				uint text_len = strlen (cur_text);
				if (text_len && cur_text[text_len - 1] == '\n')
					cur_text[text_len - 1] = 0;
				if (msbt->num_entries >= msbt->alloc_entries)
				{
					msbt->alloc_entries = msbt->alloc_entries ? msbt->alloc_entries * 2 : 16;
					msbt->entries
						= REALLOC (msbt->entries, msbt->alloc_entries * sizeof (msbt_entry_t));
				}
				msbt_entry_t *e = msbt->entries + msbt->num_entries;
				memset (e, 0, sizeof (*e));
				e->index = msbt->num_entries;
				if (*cur_label && cur_label[0] != '#')
					e->label = STRDUP (cur_label);
				e->text = STRDUP (cur_text);
				if (cur_attr_len > 0)
				{
					e->attrib = MALLOC (cur_attr_len);
					e->attrib_size = cur_attr_len;
					memcpy (e->attrib, cur_attr, cur_attr_len);
					if (msbt->attr_item_size < cur_attr_len)
						msbt->attr_item_size = cur_attr_len;
				}
				msbt->num_entries++;
			}

			in_entry = true;
			snprintf (cur_label, sizeof (cur_label), "%.*s", (int)(len - 2), line + 1);
			cur_text[0] = 0;
			cur_attr_len = 0;
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
					strncat (cur_text, "\n", sizeof (cur_text) - strlen (cur_text) - 1);
				strncat (cur_text, line, sizeof (cur_text) - strlen (cur_text) - 1);
			}
		}
	}

	if (in_entry)
	{
		// The final entry has the same structural separator as entries followed
		// by another label; discard exactly one, preserving any additional data.
		uint text_len = strlen (cur_text);
		if (text_len && cur_text[text_len - 1] == '\n')
			cur_text[text_len - 1] = 0;
		if (msbt->num_entries >= msbt->alloc_entries)
		{
			msbt->alloc_entries = msbt->alloc_entries ? msbt->alloc_entries * 2 : 16;
			msbt->entries = REALLOC (msbt->entries, msbt->alloc_entries * sizeof (msbt_entry_t));
		}
		msbt_entry_t *e = msbt->entries + msbt->num_entries;
		memset (e, 0, sizeof (*e));
		e->index = msbt->num_entries;
		if (*cur_label && cur_label[0] != '#')
			e->label = STRDUP (cur_label);
		e->text = STRDUP (cur_text);
		if (cur_attr_len > 0)
		{
			e->attrib = MALLOC (cur_attr_len);
			e->attrib_size = cur_attr_len;
			memcpy (e->attrib, cur_attr, cur_attr_len);
			if (msbt->attr_item_size < cur_attr_len)
				msbt->attr_item_size = cur_attr_len;
		}
		msbt->num_entries++;
	}

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
	}
	if (msbp->attributes)
	{
		for (uint i = 0; i < msbp->num_attributes; i++)
		{
			if (msbp->attributes[i].name)
				FREE (msbp->attributes[i].name);
			if (msbp->attributes[i].list_items)
			{
				for (uint k = 0; k < msbp->attributes[i].num_list_items; k++)
					if (msbp->attributes[i].list_items[k])
						FREE (msbp->attributes[i].list_items[k]);
				FREE (msbp->attributes[i].list_items);
			}
		}
		FREE (msbp->attributes);
		msbp->attributes = 0;
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
							if (msbp->tag_groups[i].tags[k].params[p].name)
								FREE (msbp->tag_groups[i].tags[k].params[p].name);
						FREE (msbp->tag_groups[i].tags[k].params);
					}
				}
				FREE (msbp->tag_groups[i].tags);
			}
		}
		FREE (msbp->tag_groups);
		msbp->tag_groups = 0;
	}
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

	uint cur = 0x20;
	for (uint s = 0; s < num_sections && cur + 16 <= data_size; s++)
	{
		char sec_magic[5] = { 0 };
		memcpy (sec_magic, data + cur, 4);
		u32 sec_size = r32 (data + cur + 4, be);
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

		uint aligned_size = (sec_size + 15) & ~15;
		cur += 16 + aligned_size;
	}

	if (clr1_data && clr1_size >= 4)
	{
		u32 num_colors = r32 (clr1_data, be);
		if (num_colors > 0 && 4 + num_colors * 4 <= clr1_size)
		{
			msbp->colors = CALLOC (num_colors, sizeof (msbp_color_t));
			msbp->num_colors = num_colors;
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
		u32 num_groups = r32 (clb1_data, be);
		for (uint g = 0; g < num_groups && 4 + (g + 1) * 8 <= clb1_size; g++)
		{
			u32 count = r32 (clb1_data + 4 + g * 8, be);
			u32 group_off = r32 (clb1_data + 4 + g * 8 + 4, be);

			uint pos = group_off;
			for (uint c = 0; c < count && pos < clb1_size; c++)
			{
				u8 nlen = clb1_data[pos++];
				if (pos + nlen + 4 <= clb1_size)
				{
					char name[256];
					memcpy (name, clb1_data + pos, nlen);
					name[nlen] = 0;
					pos += nlen;
					u32 idx = r32 (clb1_data + pos, be);
					pos += 4;
					if (idx < msbp->num_colors)
						msbp->colors[idx].name = STRDUP (name);
				}
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

	fprintf (f, "# MSBP: Message Studio Binary Project (%s, %s)\n",
		msbp->is_big_endian ? "BigEndian" : "LittleEndian",
		msbp->encoding == MSBT_ENC_UTF8 ? "UTF-8" : "UTF-16");

	if (msbp->num_colors > 0)
	{
		fprintf (f, "\n[Colors: %u]\n", msbp->num_colors);
		for (uint i = 0; i < msbp->num_colors; i++)
		{
			fprintf (f, "  #%u: %s = #%02X%02X%02X%02X\n", i,
				msbp->colors[i].name ? msbp->colors[i].name : "unnamed", msbp->colors[i].r,
				msbp->colors[i].g, msbp->colors[i].b, msbp->colors[i].a);
		}
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

	fprintf (f, "{\n  \"endian\": \"%s\",\n  \"encoding\": \"%s\",\n",
		msbp->is_big_endian ? "BigEndian" : "LittleEndian",
		msbp->encoding == MSBT_ENC_UTF8 ? "UTF-8" : "UTF-16");

	fprintf (f, "  \"colors\": [\n");
	for (uint i = 0; i < msbp->num_colors; i++)
	{
		fprintf (f, "    { \"index\": %u, \"name\": \"%s\", \"rgba\": \"#%02X%02X%02X%02X\" }%s\n",
			i, msbp->colors[i].name ? msbp->colors[i].name : "",
			msbp->colors[i].r, msbp->colors[i].g, msbp->colors[i].b, msbp->colors[i].a,
			(i + 1 < msbp->num_colors) ? "," : "");
	}
	fprintf (f, "  ]\n}\n");
	fclose (f);
	return ERR_OK;
}

enumError CreateMSBP (u8 **out_data, uint *out_size, const msbp_file_t *msbp)
{
	if (!out_data || !out_size || !msbp)
		return ERR_INVALID_DATA;
	bool be = msbp->is_big_endian;

	// Build CLR1 section
	uint clr1_len = 4 + msbp->num_colors * 4;
	u8 *clr1_buf = CALLOC (clr1_len, 1);
	w32 (clr1_buf, msbp->num_colors, be);
	for (uint i = 0; i < msbp->num_colors; i++)
	{
		clr1_buf[4 + i * 4] = msbp->colors[i].r;
		clr1_buf[4 + i * 4 + 1] = msbp->colors[i].g;
		clr1_buf[4 + i * 4 + 2] = msbp->colors[i].b;
		clr1_buf[4 + i * 4 + 3] = msbp->colors[i].a;
	}

	// Check if names exist
	bool has_names = false;
	for (uint i = 0; i < msbp->num_colors; i++)
	{
		if (msbp->colors[i].name && *msbp->colors[i].name)
		{
			has_names = true;
			break;
		}
	}

	u8 *clb1_buf = 0;
	uint clb1_len = 0;
	if (has_names)
	{
		uint num_groups = msbp->num_colors > 0 ? msbp->num_colors : 1;
		if (num_groups > 101)
			num_groups = 101;
		u32 *group_counts = CALLOC (num_groups, sizeof (u32));
		for (uint i = 0; i < msbp->num_colors; i++)
		{
			if (msbp->colors[i].name && *msbp->colors[i].name)
			{
				u32 g = msbt_hash (msbp->colors[i].name, num_groups);
				group_counts[g]++;
			}
		}
		uint group_table_size = 4 + num_groups * 8;
		uint total_labels_size = 0;
		for (uint i = 0; i < msbp->num_colors; i++)
		{
			if (msbp->colors[i].name && *msbp->colors[i].name)
				total_labels_size += 1 + strlen (msbp->colors[i].name) + 4;
		}
		clb1_len = group_table_size + total_labels_size;
		clb1_buf = CALLOC (clb1_len + 16, 1);
		w32 (clb1_buf, num_groups, be);
		uint cur_label_off = group_table_size;
		for (uint g = 0; g < num_groups; g++)
		{
			w32 (clb1_buf + 4 + g * 8, group_counts[g], be);
			w32 (clb1_buf + 4 + g * 8 + 4, cur_label_off, be);
			for (uint i = 0; i < msbp->num_colors; i++)
			{
				if (msbp->colors[i].name && *msbp->colors[i].name)
				{
					if (msbt_hash (msbp->colors[i].name, num_groups) == g)
					{
						uint nlen = strlen (msbp->colors[i].name);
						clb1_buf[cur_label_off++] = (u8)nlen;
						memcpy (clb1_buf + cur_label_off, msbp->colors[i].name, nlen);
						cur_label_off += nlen;
						w32 (clb1_buf + cur_label_off, i, be);
						cur_label_off += 4;
					}
				}
			}
		}
		FREE (group_counts);
	}

	uint total_size = 0x20 + 16 + ((clr1_len + 15) & ~15);
	if (clb1_buf)
		total_size += 16 + ((clb1_len + 15) & ~15);

	u8 *out = CALLOC (total_size, 1);
	memcpy (out, "MsgPrjBn", 8);
	wr_be16 (out + 8, be ? 0xFEFF : 0xFFFE);
	out[0x0A] = 0;
	out[0x0B] = 0;
	out[0x0C] = (u8)msbp->encoding;
	out[0x0D] = msbp->version ? msbp->version : 3;
	w16 (out + 0x0E, clb1_buf ? 2 : 1, be);
	w32 (out + 0x14, total_size, be);

	uint pos = 0x20;
	memcpy (out + pos, "CLR1", 4);
	w32 (out + pos + 4, clr1_len, be);
	memcpy (out + pos + 16, clr1_buf, clr1_len);
	FREE (clr1_buf);
	pos += 16 + ((clr1_len + 15) & ~15);

	if (clb1_buf)
	{
		memcpy (out + pos, "CLB1", 4);
		w32 (out + pos + 4, clb1_len, be);
		memcpy (out + pos + 16, clb1_buf, clb1_len);
		FREE (clb1_buf);
	}

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

	char line[1024];
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
				msbp->is_big_endian = true;
			if (strstr (line, "LittleEndian"))
				msbp->is_big_endian = false;
			if (strstr (line, "UTF-8"))
				msbp->encoding = MSBT_ENC_UTF8;

			// Color line: #0: Name = #RRGGBBAA
			char *colon = strchr (s, ':');
			if (colon)
			{
				colon++;
				while (*colon == ' ')
					colon++;
				char *eq = strchr (colon, '=');
				if (eq)
				{
					*eq = 0;
					char *col_val = eq + 1;
					while (*col_val == ' ' || *col_val == '#')
						col_val++;
					unsigned int hexval = 0;
					sscanf (col_val, "%x", &hexval);
					unsigned int r = 0, g = 0, b = 0, a = 255;
					if (strlen (col_val) >= 8)
					{
						r = (hexval >> 24) & 0xFF;
						g = (hexval >> 16) & 0xFF;
						b = (hexval >> 8) & 0xFF;
						a = hexval & 0xFF;
					}
					else
					{
						r = (hexval >> 16) & 0xFF;
						g = (hexval >> 8) & 0xFF;
						b = hexval & 0xFF;
						a = 0xFF;
					}
					char *ne = colon + strlen (colon) - 1;
					while (ne >= colon && (*ne == ' ' || *ne == '\t' || *ne == '\r' || *ne == '\n'))
						*ne-- = 0;

					uint idx = msbp->num_colors++;
					msbp->colors = REALLOC (msbp->colors, msbp->num_colors * sizeof (msbp_color_t));
					msbp->colors[idx].name = STRDUP (colon);
					msbp->colors[idx].r = (u8)r;
					msbp->colors[idx].g = (u8)g;
					msbp->colors[idx].b = (u8)b;
					msbp->colors[idx].a = (u8)a;
				}
			}
		}
	}

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
	const u8 *fen1_data = 0;
	uint fen1_size = 0;
	const u8 *lbl1_data = 0;
	uint lbl1_size = 0;

	uint cur = 0x20;
	for (uint s = 0; s < num_sections && cur + 16 <= data_size; s++)
	{
		char sec_magic[5] = { 0 };
		memcpy (sec_magic, data + cur, 4);
		u32 sec_size = r32 (data + cur + 4, be);
		const u8 *sec_body = data + cur + 16;

		if (!strcmp (sec_magic, "FLW3"))
		{
			flw3_data = sec_body;
			flw3_size = sec_size;
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

	if (flw3_data && flw3_size >= 4)
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
						msbf->nodes[idx].label = STRDUP (name);
				}
			}
		}
	}

	(void)fen1_data;
	(void)fen1_size;
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
	fprintf (f, "# Nodes: %u\n\n", msbf->num_nodes);

	for (uint i = 0; i < msbf->num_nodes; i++)
	{
		const msbf_node_t *n = msbf->nodes + i;
		fprintf (f, "[Node #%u", i);
		if (n->label && *n->label)
			fprintf (f, " (%s)", n->label);
		fprintf (f, "]\n");

		if (n->type == MSBF_NODE_MESSAGE)
			fprintf (f, "  type = Message (msg_index=%u, next=%u)\n", n->msg_index, n->next_node);
		else if (n->type == MSBF_NODE_BRANCH)
		{
			fprintf (f, "  type = Branch (condition=%u, branches=[", n->condition_id);
			for (uint b = 0; b < n->num_branches; b++)
				fprintf (f, "%u%s", n->branches[b], (b + 1 < n->num_branches) ? ", " : "");
			fprintf (f, "])\n");
		}
		else if (n->type == MSBF_NODE_EVENT)
			fprintf (f, "  type = Event (event_id=%u, param=0x%x, next=%u)\n",
				n->event_id, n->event_param, n->next_node);
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

	uint total_branches = 0;
	for (uint i = 0; i < msbf->num_nodes; i++)
		total_branches += msbf->nodes[i].num_branches;

	uint flw3_len = 4 + msbf->num_nodes * 16 + total_branches * 2;
	u8 *flw3_buf = CALLOC (flw3_len, 1);

	w16 (flw3_buf, msbf->num_nodes, be);
	w16 (flw3_buf + 2, total_branches, be);

	uint cur_branch_idx = 0;
	for (uint i = 0; i < msbf->num_nodes; i++)
	{
		u8 *np = flw3_buf + 4 + i * 16;
		np[0] = msbf->nodes[i].type;
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
				u8 *bp = flw3_buf + 4 + msbf->num_nodes * 16 + (cur_branch_idx + b) * 2;
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

	u8 *lbl1_buf = 0;
	uint lbl1_len = 0;
	if (has_labels)
	{
		uint num_groups = msbf->num_nodes > 0 ? msbf->num_nodes : 1;
		if (num_groups > 101)
			num_groups = 101;
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
		lbl1_len = group_table_size + total_labels_size;
		lbl1_buf = CALLOC (lbl1_len + 16, 1);
		w32 (lbl1_buf, num_groups, be);
		uint cur_label_off = group_table_size;
		for (uint g = 0; g < num_groups; g++)
		{
			w32 (lbl1_buf + 4 + g * 8, group_counts[g], be);
			w32 (lbl1_buf + 4 + g * 8 + 4, cur_label_off, be);
			for (uint i = 0; i < msbf->num_nodes; i++)
			{
				if (msbf->nodes[i].label && *msbf->nodes[i].label)
				{
					if (msbt_hash (msbf->nodes[i].label, num_groups) == g)
					{
						uint nlen = strlen (msbf->nodes[i].label);
						lbl1_buf[cur_label_off++] = (u8)nlen;
						memcpy (lbl1_buf + cur_label_off, msbf->nodes[i].label, nlen);
						cur_label_off += nlen;
						w32 (lbl1_buf + cur_label_off, i, be);
						cur_label_off += 4;
					}
				}
			}
		}
		FREE (group_counts);
	}

	uint total_size = 0x20 + 16 + ((flw3_len + 15) & ~15);
	if (lbl1_buf)
		total_size += 16 + ((lbl1_len + 15) & ~15);

	u8 *out = CALLOC (total_size, 1);
	memcpy (out, "MsgFlwBn", 8);
	wr_be16 (out + 8, be ? 0xFEFF : 0xFFFE);
	out[0x0A] = 0;
	out[0x0B] = 0;
	out[0x0C] = (u8)msbf->encoding;
	out[0x0D] = msbf->version ? msbf->version : 3;
	w16 (out + 0x0E, lbl1_buf ? 2 : 1, be);
	w32 (out + 0x14, total_size, be);

	uint pos = 0x20;
	memcpy (out + pos, "FLW3", 4);
	w32 (out + pos + 4, flw3_len, be);
	memcpy (out + pos + 16, flw3_buf, flw3_len);
	FREE (flw3_buf);
	pos += 16 + ((flw3_len + 15) & ~15);

	if (lbl1_buf)
	{
		memcpy (out + pos, "LBL1", 4);
		w32 (out + pos + 4, lbl1_len, be);
		memcpy (out + pos + 16, lbl1_buf, lbl1_len);
		FREE (lbl1_buf);
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
			}
			else if (strstr (s, "type = Branch"))
			{
				cur_node->type = MSBF_NODE_BRANCH;
				char *cd = strstr (s, "condition=");
				if (cd && sscanf (cd + 10, "%u", &tmp1) == 1)
					cur_node->condition_id = (u16)tmp1;
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
			else if (strstr (s, "type = EntryPoint"))
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

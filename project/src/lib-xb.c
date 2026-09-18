// SPDX-License-Identifier: GPL-2.0+
#include "lib-xb.h"
#include "lib-std.h"
#include "dclib-debug.h"
#include <string.h>
#include <stdlib.h>

#define XB_MAGIC 0x5842 // 'X' 'B'
#define DT_UINT32 0x4C
#define DT_UINT16 0x53

bool IsXB (const u8 *data, size_t size)
{
	if (!data || size < 10)
		return false;
	const u16 magic = be16 (data);
	if (magic != XB_MAGIC)
		return false;
	const u16 flags = be16 (data + 2);
	const u8 dt = (u8)(flags & 0xFF);
	if (dt != DT_UINT32 && dt != DT_UINT16)
		return false;
	return true;
}

static inline u32 xb_read_val (const u8 *data, size_t size, size_t *pos, u8 dt)
{
	if (dt == DT_UINT32)
	{
		if (*pos + 4 > size)
			return 0xFFFFFFFF;
		const u32 v = be32 (data + *pos);
		*pos += 4;
		return v;
	}
	else
	{
		if (*pos + 2 > size)
			return 0xFFFF;
		const u32 v = be16 (data + *pos);
		*pos += 2;
		return v;
	}
}

static inline bool xb_is_end (u32 val, u8 dt)
{
	return dt == DT_UINT32 ? (val == 0xFFFFFFFF) : (val == 0xFFFF);
}

static ccp xb_get_string (const u8 *data, size_t size, u32 offset)
{
	if (!offset || offset >= size)
		return "";
	return (ccp)(data + offset);
}

// Simple dynamic string builder
typedef struct xb_sb_t
{
	char *buf;
	size_t len;
	size_t cap;
} xb_sb_t;

static void sb_init (xb_sb_t *sb)
{
	sb->cap = 4096;
	sb->len = 0;
	sb->buf = MALLOC (sb->cap);
	if (sb->buf)
		sb->buf[0] = '\0';
}

static void sb_putc (xb_sb_t *sb, char c)
{
	if (!sb->buf)
		return;
	if (sb->len + 2 > sb->cap)
	{
		size_t ncap = sb->cap * 2 + 64;
		char *nb = REALLOC (sb->buf, ncap);
		if (!nb)
			return;
		sb->buf = nb;
		sb->cap = ncap;
	}
	sb->buf[sb->len++] = c;
	sb->buf[sb->len] = '\0';
}

static void sb_puts (xb_sb_t *sb, const char *s)
{
	if (!sb->buf || !s)
		return;
	const size_t slen = strlen (s);
	if (sb->len + slen + 1 > sb->cap)
	{
		size_t ncap = sb->cap * 2 + slen + 64;
		char *nb = REALLOC (sb->buf, ncap);
		if (!nb)
			return;
		sb->buf = nb;
		sb->cap = ncap;
	}
	memcpy (sb->buf + sb->len, s, slen);
	sb->len += slen;
	sb->buf[sb->len] = '\0';
}

static void sb_indent (xb_sb_t *sb, int level)
{
	for (int i = 0; i < level * 2; i++)
		sb_putc (sb, ' ');
}

// Escapes special XML characters: & < > " '
static void sb_puts_escaped (xb_sb_t *sb, const char *s)
{
	if (!s)
		return;
	for (const char *p = s; *p; p++)
	{
		switch (*p)
		{
			case '&': sb_puts (sb, "&amp;"); break;
			case '<': sb_puts (sb, "&lt;"); break;
			case '>': sb_puts (sb, "&gt;"); break;
			case '"': sb_puts (sb, "&quot;"); break;
			case '\'': sb_puts (sb, "&apos;"); break;
			default: sb_putc (sb, *p); break;
		}
	}
}

enumError DecodeXB_String (char **out_str, size_t *out_size, const u8 *data, size_t size)
{
	if (!out_str || !data || size < 10)
		return ERR_INVALID_DATA;
	*out_str = NULL;
	if (out_size)
		*out_size = 0;

	if (!IsXB (data, size))
		return ERR_INVALID_DATA;

	const u16 flags = be16 (data + 2);
	const u8 dt = (u8)(flags & 0xFF);
	const u16 num_root_elements = be16 (data + 4);
	const u16 num_total_elements = be16 (data + 6);
	(void)num_root_elements;
	(void)num_total_elements;

	size_t pos = 8;
	const u32 num_elements = xb_read_val (data, size, &pos, dt);
	(void)num_elements;

	size_t cur_pos = (dt == DT_UINT32) ? 0x0C : 0x0A;
	if (cur_pos >= size)
		return ERR_INVALID_DATA;

	// Peek first offset
	size_t tmp_pos = cur_pos;
	const u32 first_offset = xb_read_val (data, size, &tmp_pos, dt);

	// Tag stack for XML hierarchy
	#define MAX_XB_TAGS 256
	ccp tag_stack[MAX_XB_TAGS];
	int stack_top = 0;

	xb_sb_t sb;
	sb_init (&sb);
	sb_puts (&sb, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");

	bool first_line = true;

	while (cur_pos < size && cur_pos < first_offset)
	{
		const u32 elem_name_off = xb_read_val (data, size, &cur_pos, dt);
		const u32 elem_val_off = xb_read_val (data, size, &cur_pos, dt);

		ccp elem_name = xb_get_string (data, size, elem_name_off);
		ccp elem_val = xb_get_string (data, size, elem_val_off);

		if (!elem_name[0])
			break;

		if (stack_top < MAX_XB_TAGS)
			tag_stack[stack_top++] = elem_name;

		if (cur_pos >= first_offset)
		{
			while (stack_top > 0)
			{
				ccp end_tag = tag_stack[--stack_top];
				if (end_tag[0])
				{
					sb_indent (&sb, stack_top);
					sb_puts (&sb, "</");
					sb_puts (&sb, end_tag);
					sb_puts (&sb, ">\n");
				}
			}
			break;
		}

		const u32 num_attrs = xb_read_val (data, size, &cur_pos, dt);
		// Collect attributes
		#define MAX_XB_ATTRS 64
		struct { ccp name; ccp val; } attrs[MAX_XB_ATTRS];
		uint attr_count = num_attrs < MAX_XB_ATTRS ? num_attrs : MAX_XB_ATTRS;
		for (uint i = 0; i < num_attrs; i++)
		{
			const u32 aname_off = xb_read_val (data, size, &cur_pos, dt);
			const u32 aval_off = xb_read_val (data, size, &cur_pos, dt);
			if (i < attr_count)
			{
				attrs[i].name = xb_get_string (data, size, aname_off);
				attrs[i].val = xb_get_string (data, size, aval_off);
			}
		}

		// Peek for end element flags
		bool first_pass = true;
		while (cur_pos < size)
		{
			size_t peek_pos = cur_pos;
			const u32 val = xb_read_val (data, size, &peek_pos, dt);
			if (!xb_is_end (val, dt))
			{
				// Not end tag: emit open tag
				if (first_pass)
				{
					sb_indent (&sb, stack_top - 1);
					sb_putc (&sb, '<');
					sb_puts (&sb, elem_name);
					for (uint i = 0; i < attr_count; i++)
					{
						sb_putc (&sb, ' ');
						sb_puts (&sb, attrs[i].name);
						sb_puts (&sb, "=\"");
						sb_puts_escaped (&sb, attrs[i].val);
						sb_putc (&sb, '"');
					}
					if (elem_val[0])
					{
						sb_putc (&sb, '>');
						sb_puts_escaped (&sb, elem_val);
					}
					else
					{
						sb_puts (&sb, ">\n");
					}
				}
				break;
			}
			else
			{
				cur_pos = peek_pos;
				ccp end_tag = (stack_top > 0) ? tag_stack[--stack_top] : "";

				if (first_line)
				{
					// XML declaration handled
				}
				else if (first_pass)
				{
					// Single-line element with or without value
					sb_indent (&sb, stack_top);
					sb_putc (&sb, '<');
					sb_puts (&sb, elem_name);
					for (uint i = 0; i < attr_count; i++)
					{
						sb_putc (&sb, ' ');
						sb_puts (&sb, attrs[i].name);
						sb_puts (&sb, "=\"");
						sb_puts_escaped (&sb, attrs[i].val);
						sb_putc (&sb, '"');
					}
					if (elem_val[0])
					{
						sb_putc (&sb, '>');
						sb_puts_escaped (&sb, elem_val);
						sb_puts (&sb, "</");
						sb_puts (&sb, end_tag);
						sb_puts (&sb, ">\n");
					}
					else
					{
						sb_puts (&sb, " />\n");
					}
				}
				else
				{
					// Pop closing tag
					sb_indent (&sb, stack_top);
					sb_puts (&sb, "</");
					sb_puts (&sb, end_tag);
					sb_puts (&sb, ">\n");
				}
			}
			first_pass = false;
		}

		first_line = false;
	}

	while (stack_top > 0)
	{
		ccp end_tag = tag_stack[--stack_top];
		if (end_tag[0])
		{
			sb_indent (&sb, stack_top);
			sb_puts (&sb, "</");
			sb_puts (&sb, end_tag);
			sb_puts (&sb, ">\n");
		}
	}

	*out_str = sb.buf;
	if (out_size)
		*out_size = sb.len;
	return ERR_OK;
}

enumError DecodeXB (FILE *out, const u8 *data, size_t size)
{
	char *str = NULL;
	size_t len = 0;
	enumError err = DecodeXB_String (&str, &len, data, size);
	if (err == ERR_OK && str)
	{
		fputs (str, out ? out : stdout);
		FREE (str);
	}
	return err;
}

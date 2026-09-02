#include "lib-xmsg.h"
#include "lib-std.h"

#include <ctype.h>
#include <stdarg.h>

bool IsXMSG (const u8 *data, size_t size)
{
	if (!data || size < 12)
		return false;
	return !memcmp (data, XMSG_MAGIC, XMSG_MAGIC_LEN);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			String Buffer Helper			///////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct strbuf_t
{
	char *buf;
	size_t len;
	size_t cap;
} strbuf_t;

static void sb_init (strbuf_t *sb)
{
	sb->cap = 4096;
	sb->len = 0;
	sb->buf = MALLOC (sb->cap);
	if (sb->buf)
		sb->buf[0] = 0;
}

static void sb_putc (strbuf_t *sb, char c)
{
	if (!sb->buf)
		return;
	if (sb->len + 2 > sb->cap)
	{
		size_t new_cap = sb->cap * 2 + 64;
		char *nb = REALLOC (sb->buf, new_cap);
		if (!nb)
			return;
		sb->buf = nb;
		sb->cap = new_cap;
	}
	sb->buf[sb->len++] = c;
	sb->buf[sb->len] = 0;
}

static void sb_puts (strbuf_t *sb, const char *s)
{
	if (!sb->buf || !s)
		return;
	size_t slen = strlen (s);
	if (sb->len + slen + 1 > sb->cap)
	{
		size_t new_cap = (sb->cap * 2) + slen + 64;
		char *nb = REALLOC (sb->buf, new_cap);
		if (!nb)
			return;
		sb->buf = nb;
		sb->cap = new_cap;
	}
	memcpy (sb->buf + sb->len, s, slen);
	sb->len += slen;
	sb->buf[sb->len] = 0;
}

static void sb_printf (strbuf_t *sb, const char *fmt, ...)
{
	if (!sb->buf)
		return;

	va_list args;
	va_start (args, fmt);
	int need = vsnprintf (0, 0, fmt, args);
	va_end (args);

	if (need <= 0)
		return;

	if (sb->len + need + 1 > sb->cap)
	{
		size_t new_cap = (sb->cap * 2) + need + 256;
		char *nb = REALLOC (sb->buf, new_cap);
		if (!nb)
			return;
		sb->buf = nb;
		sb->cap = new_cap;
	}

	va_start (args, fmt);
	vsnprintf (sb->buf + sb->len, need + 1, fmt, args);
	va_end (args);
	sb->len += need;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			UTF-8 / UTF-16BE Helpers		///////////////
///////////////////////////////////////////////////////////////////////////////

static inline u32 utf8_append (char *out, u32 cp)
{
	if (cp < 0x80)
	{
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800)
	{
		out[0] = (char)(0xC0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000)
	{
		out[0] = (char)(0xE0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (char)(0xF0 | (cp >> 18));
	out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
	out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
	out[3] = (char)(0x80 | (cp & 0x3F));
	return 4;
}

static char *utf16be_decode (const u8 *p, const u8 *end)
{
	if (!p || p >= end)
	{
		char *empty = MALLOC (1);
		if (empty)
			empty[0] = 0;
		return empty;
	}

	size_t n_u16 = 0;
	while (p + n_u16 * 2 + 1 < end)
	{
		u16 val = (p[n_u16 * 2] << 8) | p[n_u16 * 2 + 1];
		if (val == 0)
			break;
		n_u16++;
	}

	char *out = MALLOC (n_u16 * 4 + 1);
	if (!out)
		return 0;

	size_t o = 0;
	for (size_t i = 0; i < n_u16; i++)
	{
		u16 u = (p[i * 2] << 8) | p[i * 2 + 1];
		u32 cp;
		if (u >= 0xD800 && u <= 0xDBFF && i + 1 < n_u16)
		{
			u16 lo = (p[(i + 1) * 2] << 8) | p[(i + 1) * 2 + 1];
			if (lo >= 0xDC00 && lo <= 0xDFFF)
			{
				cp = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
				i++;
			}
			else
				cp = 0xFFFD;
		}
		else if (u >= 0xD800 && u <= 0xDFFF)
			cp = 0xFFFD;
		else
			cp = u;

		o += utf8_append (out + o, cp);
	}
	out[o] = 0;
	return out;
}

static u8 *utf8_to_utf16be (const char *s, size_t *out_bytes)
{
	if (!s)
		s = "";
	size_t len = strlen (s);
	u8 *out = MALLOC (len * 4 + 4);
	if (!out)
		return 0;

	size_t o = 0;
	const u8 *p = (const u8 *)s;
	size_t i = 0;
	while (i < len)
	{
		u32 cp;
		u8 c = p[i];
		if (c < 0x80)
		{
			cp = c;
			i += 1;
		}
		else if ((c >> 5) == 6 && i + 1 < len)
		{
			cp = ((c & 0x1F) << 6) | (p[i + 1] & 0x3F);
			i += 2;
		}
		else if ((c >> 4) == 14 && i + 2 < len)
		{
			cp = ((c & 0x0F) << 12) | ((p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F);
			i += 3;
		}
		else if ((c >> 3) == 30 && i + 3 < len)
		{
			cp = ((c & 0x07) << 18) | ((p[i + 1] & 0x3F) << 12) | ((p[i + 2] & 0x3F) << 6) | (p[i + 3] & 0x3F);
			i += 4;
		}
		else
		{
			cp = 0xFFFD;
			i += 1;
		}

		if (cp >= 0x10000)
		{
			u32 v = cp - 0x10000;
			u16 hi = 0xD800 + (v >> 10);
			u16 lo = 0xDC00 + (v & 0x3FF);
			out[o++] = (u8)(hi >> 8);
			out[o++] = (u8)hi;
			out[o++] = (u8)(lo >> 8);
			out[o++] = (u8)lo;
		}
		else
		{
			out[o++] = (u8)(cp >> 8);
			out[o++] = (u8)cp;
		}
	}
	out[o++] = 0;
	out[o++] = 0;
	if (out_bytes)
		*out_bytes = o;
	return out;
}

static void xml_escape_append (strbuf_t *sb, const char *s)
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
			case '\"': sb_puts (sb, "&quot;"); break;
			case '\'': sb_puts (sb, "&apos;"); break;
			default: sb_putc (sb, *p); break;
		}
	}
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			Scan & Parse XMSG				///////////////
///////////////////////////////////////////////////////////////////////////////

enumError ScanXMSG (xmsg_t *xmsg, const u8 *data, size_t size)
{
	if (!xmsg || !data || size < 12 || !IsXMSG (data, size))
		return ERR_INVALID_DATA;

	memset (xmsg, 0, sizeof (*xmsg));
	xmsg->data = data;
	xmsg->size = size;

	u32 num_messages = be32 (data + 8);
	if (12 + (size_t)num_messages * 16 > size)
		return ERR_INVALID_DATA;

	xmsg->n_messages = num_messages;
	if (num_messages > 0)
	{
		xmsg->messages = CALLOC (num_messages, sizeof (xmsg_message_t));
		if (!xmsg->messages)
			return ERR_OUT_OF_MEMORY;
	}

	u32 min_style_offset = (u32)size;

	for (u32 i = 0; i < num_messages; i++)
	{
		const u8 *entry = data + 12 + i * 16;
		u32 name_off = be32 (entry + 0);
		u32 text_off = be32 (entry + 4);
		u32 type_off = be32 (entry + 8);
		u32 style_off = be32 (entry + 12);

		if (style_off < min_style_offset)
			min_style_offset = style_off;

		// Read name (UTF-8, null terminated)
		if (name_off < size)
		{
			const char *p = (const char *)(data + name_off);
			size_t maxlen = size - name_off;
			size_t nlen = strnlen (p, maxlen);
			char *name = MALLOC (nlen + 1);
			if (name)
			{
				memcpy (name, p, nlen);
				name[nlen] = 0;
				xmsg->messages[i].name = name;
			}
		}

		// Read type (UTF-8, null terminated)
		if (type_off < size)
		{
			const char *p = (const char *)(data + type_off);
			size_t maxlen = size - type_off;
			size_t tlen = strnlen (p, maxlen);
			char *type = MALLOC (tlen + 1);
			if (type)
			{
				memcpy (type, p, tlen);
				type[tlen] = 0;
				xmsg->messages[i].type = type;
			}
		}

		// Read text (UTF-16BE)
		if (text_off < size)
		{
			xmsg->messages[i].text = utf16be_decode (data + text_off, data + size);
		}
	}

	// Expressions / Styles section
	if (min_style_offset < size && min_style_offset >= 12 + num_messages * 16)
	{
		u32 style_bytes = (u32)(size - min_style_offset);
		u32 num_styles = style_bytes / 16;
		xmsg->n_styles = num_styles;
		if (num_styles > 0)
		{
			xmsg->styles = CALLOC (num_styles, sizeof (xmsg_style_t));
			if (!xmsg->styles)
			{
				ResetXMSG (xmsg);
				return ERR_OUT_OF_MEMORY;
			}

			for (u32 s = 0; s < num_styles; s++)
			{
				const u8 *sp = data + min_style_offset + s * 16;
				xmsg->styles[s].color = be32 (sp + 0);
				xmsg->styles[s].outline = be32 (sp + 4);
				xmsg->styles[s].width = sp[8];
				xmsg->styles[s].height = sp[9];
				xmsg->styles[s].horizontal_spacing = sp[10];
				xmsg->styles[s].vertical_spacing = sp[11];
				// sp[12] is padding
				xmsg->styles[s].state_start = sp[13];
				xmsg->styles[s].state_middle = sp[14];
				xmsg->styles[s].state_end = sp[15];
			}
		}

		// Assign style indices to messages
		for (u32 i = 0; i < num_messages; i++)
		{
			const u8 *entry = data + 12 + i * 16;
			u32 style_off = be32 (entry + 12);
			if (style_off >= min_style_offset)
				xmsg->messages[i].style_index = (style_off - min_style_offset) / 16;
		}
	}

	return ERR_OK;
}

void ResetXMSG (xmsg_t *xmsg)
{
	if (!xmsg)
		return;

	if (xmsg->messages)
	{
		for (uint i = 0; i < xmsg->n_messages; i++)
		{
			FREE (xmsg->messages[i].name);
			FREE (xmsg->messages[i].type);
			FREE (xmsg->messages[i].text);
		}
		FREE (xmsg->messages);
	}

	FREE (xmsg->styles);
	memset (xmsg, 0, sizeof (*xmsg));
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			Extract XMSG Formats			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError ExtractXMSGXml (const xmsg_t *xmsg, char **out_text, size_t *out_size)
{
	if (!xmsg || !out_text || !out_size)
		return ERR_INVALID_DATA;

	strbuf_t sb;
	sb_init (&sb);

	sb_puts (&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<XMSG>\n");

	for (uint i = 0; i < xmsg->n_messages; i++)
	{
		const xmsg_message_t *m = &xmsg->messages[i];
		sb_puts (&sb, "  <message name=\"");
		xml_escape_append (&sb, m->name ? m->name : "");
		sb_puts (&sb, "\" type=\"");
		xml_escape_append (&sb, m->type ? m->type : "");
		sb_printf (&sb, "\">\n    <text style=\"%u\">", m->style_index);
		xml_escape_append (&sb, m->text ? m->text : "");
		sb_puts (&sb, "</text>\n  </message>\n");
	}

	sb_puts (&sb, "  <styles>\n");
	for (uint i = 0; i < xmsg->n_styles; i++)
	{
		const xmsg_style_t *st = &xmsg->styles[i];
		sb_printf (&sb, "    <style id=\"%u\">\n", i);
		sb_printf (&sb, "      <color>%08x</color>\n", st->color);
		sb_printf (&sb, "      <outline>%08x</outline>\n", st->outline);
		sb_printf (&sb, "      <width>%u</width>\n", st->width);
		sb_printf (&sb, "      <height>%u</height>\n", st->height);
		sb_printf (&sb, "      <horizontal_spacing>%u</horizontal_spacing>\n", st->horizontal_spacing);
		sb_printf (&sb, "      <vertical_spacing>%u</vertical_spacing>\n", st->vertical_spacing);
		sb_puts (&sb, "      <states>\n");
		sb_printf (&sb, "        <start>%u</start>\n", st->state_start);
		sb_printf (&sb, "        <middle>%u</middle>\n", st->state_middle);
		sb_printf (&sb, "        <end>%u</end>\n", st->state_end);
		sb_puts (&sb, "      </states>\n");
		sb_puts (&sb, "    </style>\n");
	}
	sb_puts (&sb, "  </styles>\n</XMSG>\n");

	*out_text = sb.buf;
	*out_size = sb.len;
	return ERR_OK;
}

enumError ExtractXMSGText (const xmsg_t *xmsg, char **out_text, size_t *out_size)
{
	if (!xmsg || !out_text || !out_size)
		return ERR_INVALID_DATA;

	strbuf_t sb;
	sb_init (&sb);

	sb_puts (&sb, "# Wii Party XMSG (mess.bin) Text Export\n");
	sb_printf (&sb, "# Messages: %u, Styles: %u\n\n", xmsg->n_messages, xmsg->n_styles);

	for (uint i = 0; i < xmsg->n_styles; i++)
	{
		const xmsg_style_t *st = &xmsg->styles[i];
		sb_printf (&sb, "@STYLE[%u] color=#%08x outline=#%08x size=%ux%u spacing=%u,%u states=(%u,%u,%u)\n",
			i, st->color, st->outline, st->width, st->height,
			st->horizontal_spacing, st->vertical_spacing,
			st->state_start, st->state_middle, st->state_end);
	}
	if (xmsg->n_styles > 0)
		sb_putc (&sb, '\n');

	for (uint i = 0; i < xmsg->n_messages; i++)
	{
		const xmsg_message_t *m = &xmsg->messages[i];
		sb_printf (&sb, "[%s] type=\"%s\" style=%u\n%s\n\n",
			m->name ? m->name : "",
			m->type ? m->type : "",
			m->style_index,
			m->text ? m->text : "");
	}

	*out_text = sb.buf;
	*out_size = sb.len;
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			Create / Compile XMSG			///////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct dyn_buf_t
{
	u8 *data;
	size_t size;
	size_t cap;
} dyn_buf_t;

static void dyn_init (dyn_buf_t *b)
{
	b->cap = 4096;
	b->size = 0;
	b->data = MALLOC (b->cap);
}

static void dyn_append (dyn_buf_t *b, const void *src, size_t n)
{
	if (!b->data || n == 0)
		return;
	if (b->size + n > b->cap)
	{
		size_t new_cap = (b->cap * 2) + n + 256;
		u8 *nd = REALLOC (b->data, new_cap);
		if (!nd)
			return;
		b->data = nd;
		b->cap = new_cap;
	}
	memcpy (b->data + b->size, src, n);
	b->size += n;
}

static void dyn_append_u32_be (dyn_buf_t *b, u32 v)
{
	u8 buf[4];
	buf[0] = (u8)(v >> 24);
	buf[1] = (u8)(v >> 16);
	buf[2] = (u8)(v >> 8);
	buf[3] = (u8)v;
	dyn_append (b, buf, 4);
}

enumError CreateXMSG (u8 **dest, size_t *dest_size, const xmsg_t *xmsg)
{
	if (!dest || !dest_size || !xmsg)
		return ERR_INVALID_DATA;

	dyn_buf_t b;
	dyn_init (&b);
	if (!b.data)
		return ERR_OUT_OF_MEMORY;

	// Magic: 8 bytes
	dyn_append (&b, XMSG_MAGIC, XMSG_MAGIC_LEN);
	// Message count: 4 bytes
	dyn_append_u32_be (&b, xmsg->n_messages);

	// Reserve space for message descriptors: n_messages * 16 bytes
	size_t table_start = b.size;
	size_t table_size = (size_t)xmsg->n_messages * 16;
	u8 *zero_table = CALLOC (1, table_size);
	if (table_size > 0 && !zero_table)
	{
		FREE (b.data);
		return ERR_OUT_OF_MEMORY;
	}
	if (table_size > 0)
	{
		dyn_append (&b, zero_table, table_size);
		FREE (zero_table);
	}

	// Name bank, type bank, and text pools
	typedef struct str_entry_t
	{
		char *str;
		u32 offset;
	} str_entry_t;

	uint n_names = 0;
	str_entry_t *name_pool = CALLOC (xmsg->n_messages + 1, sizeof (str_entry_t));
	uint n_types = 0;
	str_entry_t *type_pool = CALLOC (xmsg->n_messages + 1, sizeof (str_entry_t));
	uint n_texts = 0;
	str_entry_t *text_pool = CALLOC (xmsg->n_messages + 1, sizeof (str_entry_t));

	u32 *msg_name_offsets = CALLOC (xmsg->n_messages + 1, sizeof (u32));
	u32 *msg_type_offsets = CALLOC (xmsg->n_messages + 1, sizeof (u32));
	u32 *msg_text_offsets = CALLOC (xmsg->n_messages + 1, sizeof (u32));

	for (uint i = 0; i < xmsg->n_messages; i++)
	{
		const char *nm = xmsg->messages[i].name ? xmsg->messages[i].name : "";
		u32 name_off = 0;
		for (uint k = 0; k < n_names; k++)
		{
			if (!strcmp (name_pool[k].str, nm))
			{
				name_off = name_pool[k].offset;
				break;
			}
		}
		if (!name_off)
		{
			name_off = (u32)b.size;
			name_pool[n_names].str = (char *)nm;
			name_pool[n_names].offset = name_off;
			n_names++;
			dyn_append (&b, nm, strlen (nm) + 1);
		}
		msg_name_offsets[i] = name_off;

		const char *tp = xmsg->messages[i].type ? xmsg->messages[i].type : "";
		u32 type_off = 0;
		for (uint k = 0; k < n_types; k++)
		{
			if (!strcmp (type_pool[k].str, tp))
			{
				type_off = type_pool[k].offset;
				break;
			}
		}
		if (!type_off)
		{
			type_off = (u32)b.size;
			type_pool[n_types].str = (char *)tp;
			type_pool[n_types].offset = type_off;
			n_types++;
			dyn_append (&b, tp, strlen (tp) + 1);
		}
		msg_type_offsets[i] = type_off;

		const char *tx = xmsg->messages[i].text ? xmsg->messages[i].text : "";
		u32 text_off = 0;
		for (uint k = 0; k < n_texts; k++)
		{
			if (!strcmp (text_pool[k].str, tx))
			{
				text_off = text_pool[k].offset;
				break;
			}
		}
		if (!text_off)
		{
			text_off = (u32)b.size;
			text_pool[n_texts].str = (char *)tx;
			text_pool[n_texts].offset = text_off;
			n_texts++;
			size_t u16_bytes = 0;
			u8 *u16 = utf8_to_utf16be (tx, &u16_bytes);
			if (u16)
			{
				dyn_append (&b, u16, u16_bytes + 2); // including \0\0
				FREE (u16);
			}
			else
			{
				u8 zeros[2] = { 0, 0 };
				dyn_append (&b, zeros, 2);
			}
		}
		msg_text_offsets[i] = text_off;
	}

	// Expressions / Styles section
	u32 style_section_start = (u32)b.size;
	for (uint s = 0; s < xmsg->n_styles; s++)
	{
		const xmsg_style_t *st = &xmsg->styles[s];
		dyn_append_u32_be (&b, st->color);
		dyn_append_u32_be (&b, st->outline);
		u8 extra[8];
		extra[0] = st->width;
		extra[1] = st->height;
		extra[2] = st->horizontal_spacing;
		extra[3] = st->vertical_spacing;
		extra[4] = 0; // padding
		extra[5] = st->state_start;
		extra[6] = st->state_middle;
		extra[7] = st->state_end;
		dyn_append (&b, extra, 8);
	}

	// Patch message descriptors table
	for (uint i = 0; i < xmsg->n_messages; i++)
	{
		u8 *slot = b.data + table_start + i * 16;
		u32 noff = msg_name_offsets[i];
		u32 toff = msg_text_offsets[i];
		u32 yoff = msg_type_offsets[i];
		u32 soff = style_section_start + (xmsg->messages[i].style_index * 16);

		slot[0] = (u8)(noff >> 24);
		slot[1] = (u8)(noff >> 16);
		slot[2] = (u8)(noff >> 8);
		slot[3] = (u8)noff;

		slot[4] = (u8)(toff >> 24);
		slot[5] = (u8)(toff >> 16);
		slot[6] = (u8)(toff >> 8);
		slot[7] = (u8)toff;

		slot[8] = (u8)(yoff >> 24);
		slot[9] = (u8)(yoff >> 16);
		slot[10] = (u8)(yoff >> 8);
		slot[11] = (u8)yoff;

		slot[12] = (u8)(soff >> 24);
		slot[13] = (u8)(soff >> 16);
		slot[14] = (u8)(soff >> 8);
		slot[15] = (u8)soff;
	}

	FREE (name_pool);
	FREE (type_pool);
	FREE (text_pool);
	FREE (msg_name_offsets);
	FREE (msg_type_offsets);
	FREE (msg_text_offsets);

	*dest = b.data;
	*dest_size = b.size;
	return ERR_OK;
}

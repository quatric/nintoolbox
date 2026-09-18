// SPDX-License-Identifier: GPL-2.0+
#include "lib-xb.h"
#include "lib-std.h"
#include "lib-nintendo.h"
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

// ----------------------------------------------------------------------------
// Canonical XB encoder (inverse of DecodeXB_String; see lib-xb.h for the
// documented limitations).

typedef struct xb_attr_t
{
	char *name;
	char *value;
} xb_attr_t;

typedef struct xb_node_t
{
	char *name;
	char *value; // "" for parents
	xb_attr_t *attrs;
	size_t n_attrs;
	struct xb_node_t **children;
	size_t n_children;
} xb_node_t;

static void xb_free_tree (xb_node_t *n)
{
	if (!n)
		return;
	for (size_t i = 0; i < n->n_children; i++)
		xb_free_tree (n->children[i]);
	FREE (n->children);
	for (size_t i = 0; i < n->n_attrs; i++)
	{
		FREE (n->attrs[i].name);
		FREE (n->attrs[i].value);
	}
	FREE (n->attrs);
	FREE (n->name);
	FREE (n->value);
	FREE (n);
}

// In-place unescape of the five entities the decoder emits. Returns new len.
static size_t xb_unescape (char *s)
{
	size_t r = 0, w = 0;
	while (s[r])
	{
		if (s[r] == '&')
		{
			struct { const char *e; char c; } tab[] = {
				{ "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' },
				{ "&quot;", '"' }, { "&apos;", '\'' },
			};
			bool hit = false;
			for (size_t k = 0; k < sizeof (tab) / sizeof (*tab); k++)
			{
				const size_t el = strlen (tab[k].e);
				if (!strncmp (s + r, tab[k].e, el))
				{
					s[w++] = tab[k].c;
					r += el;
					hit = true;
					break;
				}
			}
			if (!hit)
				s[w++] = s[r++];
		}
		else
			s[w++] = s[r++];
	}
	s[w] = 0;
	return w;
}

static char *xb_strndup (const char *s, size_t n)
{
	char *o = MALLOC (n + 1);
	if (!o)
		return 0;
	memcpy (o, s, n);
	o[n] = 0;
	return o;
}

// Parses the decoder's XML subset into a forest. Returns 0 on success with
// *roots_out (array of *n_out, caller frees trees + array).
static int xb_parse_xml (
	const char *xml, size_t len, xb_node_t ***roots_out, size_t *n_out)
{
	*roots_out = 0;
	*n_out = 0;
	xb_node_t **roots = 0;
	size_t n_roots = 0, cap_roots = 0;
	xb_node_t *stack[256];
	int depth = 0;
	// Text accumulator for the current open element.
	char *text = 0;
	size_t text_len = 0, text_cap = 0;
	size_t i = 0;
	int ok = 0;

	while (i < len)
	{
		if (xml[i] == '<')
		{
			// Flush pending text to the open element (only kept when it
			// ends up childless).
			if (text_len && depth > 0)
			{
				xb_node_t *top = stack[depth - 1];
				if (!top->n_children)
				{
					FREE (top->value);
					top->value = xb_strndup (text, text_len);
					if (!top->value)
						goto done;
				}
			}
			text_len = 0;
			if (i + 1 < len && xml[i + 1] == '?')
			{
				const char *e = strstr (xml + i, "?>");
				if (!e)
					goto done;
				i = (size_t)(e - xml) + 2;
				continue;
			}
			const char *gt = memchr (xml + i, '>', len - i);
			if (!gt)
				goto done;
			const size_t inner_len = (size_t)(gt - (xml + i + 1));
			const char *inner = xml + i + 1;
			i = (size_t)(gt - xml) + 1;
			if (inner_len && inner[0] == '/')
			{
				// Close tag.
				if (!depth)
					goto done;
				depth--;
				continue;
			}
			bool selfclose = inner_len && inner[inner_len - 1] == '/';
			// Tag name ends at space, tab, newline, CR or '/'.
			size_t nlen = 0;
			while (nlen < inner_len && inner[nlen] != ' ' && inner[nlen] != '\t'
				&& inner[nlen] != '\n' && inner[nlen] != '\r' && inner[nlen] != '/')
				nlen++;
			if (!nlen)
				goto done;
			xb_node_t *node = CALLOC (1, sizeof (*node));
			if (!node)
				goto done;
			node->name = xb_strndup (inner, nlen);
			node->value = xb_strndup ("", 0);
			if (!node->name || !node->value)
			{
				xb_free_tree (node);
				goto done;
			}
			// Attributes: name="value" pairs.
			size_t p = nlen;
			while (p < inner_len)
			{
				while (p < inner_len
					&& (inner[p] == ' ' || inner[p] == '\t' || inner[p] == '\n'
						|| inner[p] == '\r' || inner[p] == '/'))
					p++;
				if (p >= inner_len)
					break;
				size_t an = 0;
				while (p + an < inner_len && inner[p + an] != '='
					&& inner[p + an] != ' ' && inner[p + an] != '\t')
					an++;
				if (!an || p + an >= inner_len || inner[p + an] != '=')
					break; // trailing junk (e.g. lone '/'); stop
				const char *vs = inner + p + an + 1;
				if (vs >= inner + inner_len || *vs != '"')
					break;
				vs++;
				const char *ve = memchr (vs, '"', (size_t)(inner + inner_len - vs));
				if (!ve)
					break;
				xb_attr_t *na = REALLOC (node->attrs,
					(node->n_attrs + 1) * sizeof (*na));
				if (!na)
				{
					xb_free_tree (node);
					goto done;
				}
				node->attrs = na;
				node->attrs[node->n_attrs].name = xb_strndup (inner + p, an);
				node->attrs[node->n_attrs].value = xb_strndup (vs, (size_t)(ve - vs));
				if (!node->attrs[node->n_attrs].name
					|| !node->attrs[node->n_attrs].value)
				{
					xb_free_tree (node);
					goto done;
				}
				xb_unescape (node->attrs[node->n_attrs].value);
				node->n_attrs++;
				p = (size_t)(ve - inner) + 1;
			}
			if (selfclose)
			{
				if (depth > 0)
				{
					xb_node_t *par = stack[depth - 1];
					xb_node_t **nc = REALLOC (par->children,
						(par->n_children + 1) * sizeof (*nc));
					if (!nc)
					{
						xb_free_tree (node);
						goto done;
					}
					par->children = nc;
					par->children[par->n_children++] = node;
				}
				else
				{
					if (n_roots == cap_roots)
					{
						cap_roots = cap_roots ? cap_roots * 2 : 4;
						xb_node_t **nr = REALLOC (roots, cap_roots * sizeof (*nr));
						if (!nr)
						{
							xb_free_tree (node);
							goto done;
						}
						roots = nr;
					}
					roots[n_roots++] = node;
				}
			}
			else
			{
				if (depth > 0)
				{
					xb_node_t *par = stack[depth - 1];
					xb_node_t **nc = REALLOC (par->children,
						(par->n_children + 1) * sizeof (*nc));
					if (!nc)
					{
						xb_free_tree (node);
						goto done;
					}
					par->children = nc;
					par->children[par->n_children++] = node;
				}
				else
				{
					if (n_roots == cap_roots)
					{
						cap_roots = cap_roots ? cap_roots * 2 : 4;
						xb_node_t **nr = REALLOC (roots, cap_roots * sizeof (*nr));
						if (!nr)
						{
							xb_free_tree (node);
							goto done;
						}
						roots = nr;
					}
					roots[n_roots++] = node;
				}
				if (depth >= 256)
				{
					goto done; // still owns node via parent/roots; freed below
				}
				stack[depth++] = node;
			}
		}
		else
		{
			// Text content.
			const char *lt = memchr (xml + i, '<', len - i);
			const size_t tend = lt ? (size_t)(lt - xml) : len;
			const size_t chunk = tend - i;
			if (chunk)
			{
				if (text_len + chunk + 1 > text_cap)
				{
					text_cap = text_len + chunk + 64;
					char *nt = REALLOC (text, text_cap);
					if (!nt)
						goto done;
					text = nt;
				}
				memcpy (text + text_len, xml + i, chunk);
				text_len += chunk;
				text[text_len] = 0;
			}
			i = tend;
		}
	}
	if (depth != 0)
		goto done; // unbalanced
	// Unescape element values.
	{
		// iterative walk
		xb_node_t **work = 0;
		size_t n_work = 0, cap_work = 0;
		for (size_t r = 0; r < n_roots; r++)
		{
			if (n_work == cap_work)
			{
				cap_work = cap_work ? cap_work * 2 : 16;
				xb_node_t **nw = REALLOC (work, cap_work * sizeof (*nw));
				if (!nw)
				{
					FREE (work);
					goto done;
				}
				work = nw;
			}
			work[n_work++] = roots[r];
		}
		while (n_work)
		{
			xb_node_t *nd = work[--n_work];
			xb_unescape (nd->value);
			for (size_t c = 0; c < nd->n_children; c++)
			{
				if (n_work == cap_work)
				{
					cap_work *= 2;
					xb_node_t **nw = REALLOC (work, cap_work * sizeof (*nw));
					if (!nw)
					{
						FREE (work);
						goto done;
					}
					work = nw;
				}
				work[n_work++] = nd->children[c];
			}
		}
		FREE (work);
	}
	ok = 1;
done:
	FREE (text);
	if (!ok)
	{
		for (size_t r = 0; r < n_roots; r++)
			xb_free_tree (roots[r]);
		FREE (roots);
		return -1;
	}
	*roots_out = roots;
	*n_out = n_roots;
	return 0;
}

// String table: unique non-empty strings in pre-order appearance.
typedef struct
{
	char **strs;
	u32 *offs; // absolute file offsets, filled on emit
	size_t n;
	size_t cap;
} xb_strtab_t;

static u32 xb_strtab_add (xb_strtab_t *t, const char *s, bool *is_new)
{
	if (!s || !s[0])
	{
		if (is_new)
			*is_new = false;
		return 0;
	}
	for (size_t i = 0; i < t->n; i++)
		if (!strcmp (t->strs[i], s))
		{
			if (is_new)
				*is_new = false;
			return 0xFFFFFFFEu; // present; offset resolved on emit
		}
	if (t->n == t->cap)
	{
		size_t nc = t->cap ? t->cap * 2 : 32;
		char **ns = REALLOC (t->strs, nc * sizeof (*ns));
		u32 *no = REALLOC (t->offs, nc * sizeof (*no));
		if (!ns || !no)
			return 0xFFFFFFFFu;
		t->strs = ns;
		t->offs = no;
		t->cap = nc;
	}
	char *cp = MALLOC (strlen (s) + 1);
	if (!cp)
		return 0xFFFFFFFFu;
	strcpy (cp, s);
	t->strs[t->n] = cp;
	t->offs[t->n] = 0;
	t->n++;
	if (is_new)
		*is_new = true;
	return 0xFFFFFFFEu;
}

static u32 xb_strtab_find (const xb_strtab_t *t, const char *s)
{
	if (!s || !s[0])
		return 0;
	for (size_t i = 0; i < t->n; i++)
		if (!strcmp (t->strs[i], s))
			return t->offs[i];
	return 0;
}

static void xb_collect_strings (xb_strtab_t *t, const xb_node_t *n, bool *oom)
{
	if (*oom)
		return;
	if (xb_strtab_add (t, n->name, 0) == 0xFFFFFFFFu)
	{
		*oom = true;
		return;
	}
	if (n->n_children == 0 && xb_strtab_add (t, n->value, 0) == 0xFFFFFFFFu)
	{
		*oom = true;
		return;
	}
	for (size_t i = 0; i < n->n_attrs; i++)
	{
		if (xb_strtab_add (t, n->attrs[i].name, 0) == 0xFFFFFFFFu
			|| xb_strtab_add (t, n->attrs[i].value, 0) == 0xFFFFFFFFu)
		{
			*oom = true;
			return;
		}
	}
	for (size_t c = 0; c < n->n_children; c++)
		xb_collect_strings (t, n->children[c], oom);
}

typedef struct
{
	u8 *data;
	size_t size;
	size_t cap;
} xb_buf_t;

static bool xb_reserve (xb_buf_t *b, size_t need)
{
	if (b->size + need <= b->cap)
		return true;
	size_t nc = b->cap ? b->cap * 2 : 256;
	while (nc < b->size + need)
		nc *= 2;
	u8 *nb = REALLOC (b->data, nc);
	if (!nb)
		return false;
	b->data = nb;
	b->cap = nc;
	return true;
}

static bool xb_put (xb_buf_t *b, const void *src, size_t len)
{
	if (!xb_reserve (b, len))
		return false;
	memcpy (b->data + b->size, src, len);
	b->size += len;
	return true;
}

static bool xb_val (xb_buf_t *b, u32 v, bool wide)
{
	if (wide)
	{
		u8 tmp[4];
		tmp[0] = v >> 24;
		tmp[1] = v >> 16;
		tmp[2] = v >> 8;
		tmp[3] = v;
		return xb_put (b, tmp, 4);
	}
	if (v > 0xFFFEu)
		return false;
	u8 tmp[2];
	tmp[0] = v >> 8;
	tmp[1] = v;
	return xb_put (b, tmp, 2);
}

// Pre-order record list with depths, then END runs from depth diffs.
typedef struct
{
	const xb_node_t *node;
	int depth;
} xb_item_t;

static void xb_flatten (
	xb_item_t **items, size_t *n, size_t *cap, const xb_node_t *node, int depth, bool *oom)
{
	if (*oom)
		return;
	if (*n == *cap)
	{
		size_t nc = *cap ? *cap * 2 : 32;
		xb_item_t *ni = REALLOC (*items, nc * sizeof (*ni));
		if (!ni)
		{
			*oom = true;
			return;
		}
		*items = ni;
		*cap = nc;
	}
	(*items)[(*n)].node = node;
	(*items)[(*n)].depth = depth;
	(*n)++;
	for (size_t c = 0; c < node->n_children; c++)
		xb_flatten (items, n, cap, node->children[c], depth + 1, oom);
}

enumError EncodeXB (u8 **dest, uint *dest_size, const char *xml, size_t xml_len, int wide)
{
	if (!dest || !dest_size || !xml)
		return ERR_INVALID_DATA;
	*dest = 0;
	*dest_size = 0;
	if (!xml_len)
		xml_len = strlen (xml);

	xb_node_t **roots = 0;
	size_t n_roots = 0;
	if (xb_parse_xml (xml, xml_len, &roots, &n_roots) || !n_roots)
		return ERR_INVALID_DATA;

	xb_strtab_t tab = { 0, 0, 0, 0 };
	bool oom = false;
	for (size_t r = 0; r < n_roots; r++)
		xb_collect_strings (&tab, roots[r], &oom);
	if (oom)
		goto fail;

	xb_item_t *items = 0;
	size_t n_items = 0, cap_items = 0;
	for (size_t r = 0; r < n_roots; r++)
		xb_flatten (&items, &n_items, &cap_items, roots[r], 1, &oom);
	if (oom || !n_items)
		goto fail;

	// Try narrow first unless wide is forced; overflow (>0xFFFE, since
	// 0xFFFF is the END marker) retries wide.
	for (int pass = 0; pass < 2; pass++)
	{
		const bool w = wide ? true : pass == 1;
		const size_t vsz = w ? 4 : 2;
		// Record bytes: per element 3 vals + 2 per attr; END runs from
		// depth diffs (stack after push minus next depth, EOR = 0).
		size_t recs_size = 0;
		for (size_t i = 0; i < n_items; i++)
		{
			recs_size += (3 + 2 * items[i].node->n_attrs) * vsz;
			const int next_depth = i + 1 < n_items ? items[i + 1].depth : 0;
			const int closes = (items[i].depth + 1) - next_depth;
			if (closes < 0)
			{
				recs_size = 0;
				break; // depth jump > 1: malformed forest
			}
			recs_size += (size_t)closes * vsz;
		}
		if (!recs_size)
		{
			if (pass == 1 || wide)
				goto fail;
			continue;
		}
		const size_t hdr_size = 8 + vsz;
		const size_t str_start = hdr_size + recs_size;
		// Assign string offsets.
		size_t sp = str_start;
		for (size_t i = 0; i < tab.n; i++)
		{
			if (!w && sp > 0xFFFEu)
				break; // retry wide
			tab.offs[i] = (u32)sp;
			sp += strlen (tab.strs[i]) + 1;
		}
		if (!w && sp > str_start)
		{
			bool overflow = false;
			for (size_t i = 0; i < tab.n; i++)
				if (tab.offs[i] > 0xFFFEu)
					overflow = true;
			if (overflow)
				continue; // retry wide
		}
		if (sp > NFMT_MAX_OUTPUT)
			goto fail;
		xb_buf_t out = { 0, 0, 0 };
		bool ok = xb_reserve (&out, sp);
		// Header.
		u8 hdr[12];
		hdr[0] = 'X';
		hdr[1] = 'B';
		hdr[2] = 0;
		hdr[3] = (u8)(w ? 0x4C : 0x53);
		hdr[4] = (u8)(n_roots >> 8);
		hdr[5] = (u8)n_roots;
		// numTotal + numElements.
		size_t hp = 6;
		u8 tmpc[4];
		tmpc[0] = (u8)(n_items >> 8);
		tmpc[1] = (u8)n_items;
		if (!ok || !xb_put (&out, hdr, 6))
		{
			FREE (out.data);
			goto fail;
		}
		// numTotalElements (u16, always narrow per the reference layout).
		if (!xb_put (&out, tmpc, 2))
		{
			FREE (out.data);
			goto fail;
		}
		hp += 2;
		// numElements in the record width.
		if (w)
		{
			u8 t4[4] = { 0, 0, (u8)(n_items >> 8), (u8)n_items };
			if (!xb_put (&out, t4, 4))
			{
				FREE (out.data);
				goto fail;
			}
		}
		else if (!xb_put (&out, tmpc, 2))
		{
			FREE (out.data);
			goto fail;
		}
		(void)hp;
		// Records + END runs.
		for (size_t i = 0; ok && i < n_items; i++)
		{
			const xb_node_t *nd = items[i].node;
			ok = ok && xb_val (&out, xb_strtab_find (&tab, nd->name), w);
			ok = ok && xb_val (&out,
				nd->n_children == 0 ? xb_strtab_find (&tab, nd->value) : 0, w);
			ok = ok && xb_val (&out, (u32)nd->n_attrs, w);
			for (size_t a = 0; ok && a < nd->n_attrs; a++)
				ok = ok && xb_val (&out, xb_strtab_find (&tab, nd->attrs[a].name), w)
					&& xb_val (&out, xb_strtab_find (&tab, nd->attrs[a].value), w);
			const int next_depth = i + 1 < n_items ? items[i + 1].depth : 0;
			const int closes = (items[i].depth + 1) - next_depth;
			for (int c = 0; ok && c < closes; c++)
				ok = ok && xb_val (&out, w ? 0xFFFFFFFFu : 0xFFFFu, w);
		}
		// Strings.
		for (size_t i = 0; ok && i < tab.n; i++)
			ok = ok && xb_put (&out, tab.strs[i], strlen (tab.strs[i]) + 1);
		if (!ok || out.size != sp)
		{
			FREE (out.data);
			if (!w && !wide)
				continue; // mismatched size estimate: retry wide
			goto fail;
		}
		for (size_t r = 0; r < n_roots; r++)
			xb_free_tree (roots[r]);
		FREE (roots);
		FREE (items);
		for (size_t i = 0; i < tab.n; i++)
			FREE (tab.strs[i]);
		FREE (tab.strs);
		FREE (tab.offs);
		*dest = out.data;
		*dest_size = (uint)out.size;
		return ERR_OK;
	}
fail:
	for (size_t r = 0; r < n_roots; r++)
		xb_free_tree (roots[r]);
	FREE (roots);
	FREE (items);
	for (size_t i = 0; i < tab.n; i++)
		FREE (tab.strs[i]);
	FREE (tab.strs);
	FREE (tab.offs);
	return ERR_CANT_CREATE;
}

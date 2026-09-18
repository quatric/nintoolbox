#include "lib-xmb.h"
#include "lib-std.h"

#include <string.h>

#define XMB_HDR_SIZE 44
#define XMB_ENTRY_SIZE 0x10
#define XMB_PROP_SIZE 8
#define XMB_MAP_SIZE 8
#define XMB_MAX_NODES 0x100000
#define XMB_MAX_DEPTH 1024

typedef struct
{
	u32 name_off;
	int prop_count;
	int child_count;
	int first_prop;
	int parent;
	u32 name_at; // absolute offset of the name string
} xmb_node_t;

// Bounded NUL-terminated string at an absolute offset. Returns NULL when the
// offset is out of range or no terminator fits before 'limit'.
static ccp xmb_string (const u8 *data, size_t size, u64 off, u64 limit,
	char *tmp, uint tmpsz, int *ok)
{
	if (off >= size || off >= limit)
	{
		*ok = 0;
		return "";
	}
	u64 max = limit < size ? limit : size;
	u64 len = 0;
	while (off + len < max && data[off + len])
		len++;
	if (off + len >= max)
	{
		*ok = 0;
		return "";
	}
	u64 n = len < tmpsz - 1 ? len : tmpsz - 1;
	memcpy (tmp, data + off, (size_t)n);
	tmp[n] = 0;
	*ok = 1;
	return tmp;
}

// XML attribute values escape &, <, >, ". Tag names are sanitised to the
// XML Name production ([A-Za-z0-9_.:-], must not start with a digit).
static void xmb_write_escaped (FILE *out, ccp s, int attr)
{
	for (; *s; s++)
	{
		switch (*s)
		{
			case '&': fputs ("&amp;", out); break;
			case '<': fputs ("&lt;", out); break;
			case '>': fputs ("&gt;", out); break;
			case '"': fputs (attr ? "&quot;" : "\"", out); break;
			default: fputc (*s, out); break;
		}
	}
}

static int xmb_tag_char (int c, int first)
{
	if (c >= 'A' && c <= 'Z')
		return 1;
	if (c >= 'a' && c <= 'z')
		return 1;
	if (c == '_' || c == '.' || c == '-' || c == ':')
		return !first ? 1 : c != '-';
	if (c >= '0' && c <= '9')
		return !first;
	return 0;
}

static void xmb_write_tag (FILE *out, ccp s)
{
	if (!*s || !xmb_tag_char ((unsigned char)*s, 1))
		fputc ('_', out);
	for (; *s; s++)
		fputc (xmb_tag_char ((unsigned char)*s, 0) ? *s : '_', out);
}

bool IsXMB (const u8 *data, size_t size)
{
	if (!data || size < XMB_HDR_SIZE || memcmp (data, "XMB ", 4))
		return false;
	const u32 nodes = rd_le32 (data + 4);
	const u32 values = rd_le32 (data + 8);
	const u32 mapped = rd_le32 (data + 16);
	if (!nodes || nodes > XMB_MAX_NODES || values > XMB_MAX_NODES * 64
		|| mapped > XMB_MAX_NODES)
		return false;
	const u32 node_tab = rd_le32 (data + 0x18);
	const u32 prop_tab = rd_le32 (data + 0x1c);
	const u32 map_off = rd_le32 (data + 0x20);
	const u32 names_off = rd_le32 (data + 0x24);
	const u32 values_off = rd_le32 (data + 0x28);
	if ((u64)node_tab + (u64)nodes * XMB_ENTRY_SIZE > size
		|| (u64)prop_tab + (u64)values * XMB_PROP_SIZE > size
		|| (u64)map_off + (u64)mapped * XMB_MAP_SIZE > size
		|| names_off >= size || values_off >= size)
		return false;
	return true;
}

static void xmb_write_node (FILE *out, const u8 *data, size_t size,
	const xmb_node_t *nodes, u32 node_count, u32 prop_base, u32 prop_count,
	u32 names_off, u32 values_off, int idx, int depth)
{
	if (depth > XMB_MAX_DEPTH)
		return;
	char tmp[512], pname[512], pval[1024];
	int ok = 0;
	ccp name = xmb_string (data, size, nodes[idx].name_at, size,
		tmp, sizeof (tmp), &ok);
	if (!ok || !*name)
		name = "node";

	for (int d = 0; d < depth; d++)
		fputs ("    ", out);
	fputc ('<', out);
	xmb_write_tag (out, name);

	// Properties become XML attributes, in file order like XMBDec.py.
	for (int p = 0; p < nodes[idx].prop_count; p++)
	{
		if ((u64)nodes[idx].first_prop + (u64)p >= prop_count)
			break;
		u64 pe = (u64)prop_base + (u64)(nodes[idx].first_prop + p) * XMB_PROP_SIZE;
		if (pe + XMB_PROP_SIZE > size)
			break;
		const u32 noff = rd_le32 (data + pe);
		const u32 voff = rd_le32 (data + pe + 4);
		int ok1 = 0, ok2 = 0;
		ccp pn = xmb_string (data, size, (u64)names_off + noff, size,
			pname, sizeof (pname), &ok1);
		ccp pv = xmb_string (data, size, (u64)values_off + voff, size,
			pval, sizeof (pval), &ok2);
		if (!ok1 || !*pn)
			continue;
		fputc (' ', out);
		xmb_write_tag (out, pn);
		fputs ("=\"", out);
		if (ok2)
			xmb_write_escaped (out, pv, 1);
		fputc ('"', out);
	}

	// Children in file order; a missing child list closes the element.
	int have_child = 0;
	for (u32 i = 0; i < node_count; i++)
		if (nodes[i].parent == idx)
		{
			have_child = 1;
			break;
		}
	if (!have_child)
	{
		fputs ("/>\n", out);
		return;
	}
	fputs (">\n", out);
	for (u32 i = 0; i < node_count; i++)
		if (nodes[i].parent == idx)
			xmb_write_node (out, data, size, nodes, node_count,
				prop_base, prop_count, names_off, values_off, (int)i, depth + 1);
	for (int d = 0; d < depth; d++)
		fputs ("    ", out);
	fputs ("</", out);
	xmb_write_tag (out, name);
	fputs (">\n", out);
	(void)prop_count;
}

enumError DecodeXMB_XML (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsXMB (data, size))
		return ERR_INVALID_DATA;

	const u32 node_count = rd_le32 (data + 4);
	const u32 value_count = rd_le32 (data + 8);
	const u32 node_tab = rd_le32 (data + 0x18);
	const u32 prop_tab = rd_le32 (data + 0x1c);
	const u32 names_off = rd_le32 (data + 0x24);
	const u32 values_off = rd_le32 (data + 0x28);

	xmb_node_t *nodes = MALLOC (node_count * sizeof (*nodes));
	if (!nodes)
		return ERR_OUT_OF_MEMORY;
	memset (nodes, 0, node_count * sizeof (*nodes));

	// Parse the node table first so each property index can be range
	// checked against the real property-table span before use.
	u32 i;
	for (i = 0; i < node_count; i++)
	{
		const u8 *e = data + node_tab + (size_t)i * XMB_ENTRY_SIZE;
		const int nprop = (int)(int16_t)rd_le16 (e + 4);
		const int nchild = (int)(int16_t)rd_le16 (e + 6);
		const int fprop = (int)(int16_t)rd_le16 (e + 8);
		const int parent = (int)(int16_t)rd_le16 (e + 12);
		nodes[i].name_off = rd_le32 (e);
		nodes[i].prop_count = nprop < 0 ? 0 : nprop;
		nodes[i].child_count = nchild < 0 ? 0 : nchild;
		nodes[i].first_prop = fprop < 0 ? 0 : fprop;
		nodes[i].parent = parent;
		nodes[i].name_at = names_off + nodes[i].name_off;
		if (nodes[i].parent != -1
			&& (nodes[i].parent < 0 || nodes[i].parent >= (int)node_count))
			nodes[i].parent = -2; // orphaned: never emitted as a child
	}

	// The root is the last parentless node, matching XMBDec.py/XMBLib
	// (both overwrite their root on every parentless entry).
	int root = -1;
	for (i = 0; i < node_count; i++)
		if (nodes[i].parent == -1)
			root = (int)i;

	fprintf (out, "<?xml version=\"1.0\" ?>\n");
	if (root >= 0)
		xmb_write_node (out, data, size, nodes, node_count,
			prop_tab, value_count, names_off, values_off, root, 0);
	FREE (nodes);
	return ERR_OK;
}

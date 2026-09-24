// SPDX-License-Identifier: GPL-2.0+
#include "lib-mpboard.h"
#include "lib-std.h"
#include "lib-nintendo.h"
#include "dclib-debug.h"
#include "dclib-basics.h"
#include "dclib-utf8.h"
#include "dclib-mingw-compat.h" // memmem() shim
#include <string.h>
#include <stdlib.h>

static inline float rd_befloat (const u8 *p)
{
	union
	{
		u32 u;
		float f;
	} u;
	u.u = be32 (p);
	return u.f;
}

bool IsMPBoard (const u8 *data, size_t size)
{
	if (!data || size < 4 + 40)
		return false;

	const u32 num_spaces = be32 (data);
	if (num_spaces == 0 || num_spaces > 1000)
		return false;

	// Check if sizes plausibly align with space nodes
	// Each space has: pos(12) + rot(12) + scale(12) + p1(2) + p2(2) + [p3(2)] + type(2) + links(2) = 42 or 44 bytes + links*2
	const size_t min_size = 4 + num_spaces * 42;
	if (min_size > size)
		return false;

	// Scale is usually 1.0f or reasonable positive float
	const float sx = rd_befloat (data + 4 + 24);
	const float sy = rd_befloat (data + 4 + 28);
	const float sz = rd_befloat (data + 4 + 32);
	if (sx > 0.001f && sx < 1000.0f && sy > 0.001f && sy < 1000.0f && sz > 0.001f && sz < 1000.0f)
		return true;

	return false;
}

enumError ScanMPBoard (mp_board_t *board, const u8 *data, size_t size, uint version)
{
	if (!board || !data || size < 4)
		return ERR_INVALID_DATA;

	memset (board, 0, sizeof (*board));
	const u32 num_spaces = be32 (data);
	if (num_spaces == 0 || num_spaces > 2000)
		return ERR_INVALID_DATA;

	board->num_spaces = num_spaces;
	board->version = version ? version : 6;
	board->spaces = CALLOC (num_spaces, sizeof (mp_space_node_t));
	if (!board->spaces)
		return ERR_CANT_CREATE;

	size_t pos = 4;
	const bool has_param3 = (board->version > 5);

	for (uint i = 0; i < num_spaces; i++)
	{
		const size_t base_size = 36 + (has_param3 ? 6 : 4) + 4; // pos/rot/scale(36) + params + type(2) + num_links(2)
		if (pos + base_size > size)
		{
			ResetMPBoard (board);
			return ERR_INVALID_DATA;
		}

		mp_space_node_t *s = board->spaces + i;
		for (int c = 0; c < 3; c++)
		{
			s->pos[c] = rd_befloat (data + pos);
			pos += 4;
		}
		for (int c = 0; c < 3; c++)
		{
			s->rot[c] = rd_befloat (data + pos);
			pos += 4;
		}
		for (int c = 0; c < 3; c++)
		{
			s->scale[c] = rd_befloat (data + pos);
			pos += 4;
		}

		s->param1 = be16 (data + pos); pos += 2;
		s->param2 = be16 (data + pos); pos += 2;
		if (has_param3)
		{
			s->param3 = be16 (data + pos);
			pos += 2;
		}
		s->type_id = be16 (data + pos); pos += 2;
		const u16 file_links = be16 (data + pos); pos += 2;

		if (pos + (size_t)file_links * 2 > size)
		{
			ResetMPBoard (board);
			return ERR_INVALID_DATA;
		}

		s->num_links = file_links > 16 ? 16 : file_links;
		for (uint k = 0; k < s->num_links; k++)
			s->links[k] = be16 (data + pos + k * 2);
		pos += (size_t)file_links * 2;
	}

	return ERR_OK;
}

void ResetMPBoard (mp_board_t *board)
{
	if (board)
	{
		if (board->spaces)
			FREE (board->spaces);
		memset (board, 0, sizeof (*board));
	}
}

enumError DecodeMPBoard_Text (FILE *out, const mp_board_t *board)
{
	if (!out || !board)
		return ERR_INVALID_DATA;

	fprintf (out, "# Mario Party Board (Spaces: %u, Version: %u)\n\n", board->num_spaces, board->version);
	for (uint i = 0; i < board->num_spaces; i++)
	{
		const mp_space_node_t *s = board->spaces + i;
		fprintf (out, "Space %3u: Type=%u, Param1=%u, Param2=%u, Param3=%u\n",
			i, s->type_id, s->param1, s->param2, s->param3);
		fprintf (out, "  Pos=(%.2f, %.2f, %.2f) Rot=(%.2f, %.2f, %.2f) Scale=(%.2f, %.2f, %.2f)\n",
			s->pos[0], s->pos[1], s->pos[2],
			s->rot[0], s->rot[1], s->rot[2],
			s->scale[0], s->scale[1], s->scale[2]);
		fprintf (out, "  Links (%u): [", s->num_links);
		for (uint k = 0; k < s->num_links; k++)
			fprintf (out, "%s%u", k == 0 ? "" : ", ", s->links[k]);
		fprintf (out, "]\n\n");
	}
	return ERR_OK;
}

enumError DecodeMPBoard_CSV (FILE *out, const mp_board_t *board)
{
	if (!out || !board)
		return ERR_INVALID_DATA;

	fprintf (out, "ID,PosX,PosY,PosZ,RotX,RotY,RotZ,ScaleX,ScaleY,ScaleZ,Type,Param1,Param2,Param3,Links\n");
	for (uint i = 0; i < board->num_spaces; i++)
	{
		const mp_space_node_t *s = board->spaces + i;
		fprintf (out, "%u,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%u,%u,%u,%u,",
			i, s->pos[0], s->pos[1], s->pos[2],
			s->rot[0], s->rot[1], s->rot[2],
			s->scale[0], s->scale[1], s->scale[2],
			s->type_id, s->param1, s->param2, s->param3);
		for (uint k = 0; k < s->num_links; k++)
			fprintf (out, "%s%u", k == 0 ? "" : ";", s->links[k]);
		fprintf (out, "\n");
	}
	return ERR_OK;
}

enumError DecodeSMPBoard_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !data || size == 0)
		return ERR_INVALID_DATA;

	fprintf (out, "# Super Mario Party Board CSV\n\n");
	fwrite (data, 1, size, out);
	fprintf (out, "\n");
	return ERR_OK;
}

// ----------------------------------------------------------------------------
// GC/Wii board encode (MPLibrary/GCWii/Board.cs Write).
static inline void wr_befloat (u8 *p, float f)
{
	union { u32 u; float f; } u;
	u.f = f;
	p[0] = u.u >> 24;
	p[1] = u.u >> 16;
	p[2] = u.u >> 8;
	p[3] = u.u;
}

enumError CreateMPBoard (u8 **dest, uint *dest_size, const mp_board_t *board)
{
	if (!dest || !dest_size || !board || !board->num_spaces || !board->spaces)
		return ERR_INVALID_DATA;
	if (board->num_spaces > 2000)
		return ERR_INVALID_DATA;

	const bool has_param3 = (board->version > 5);
	size_t total = 4;
	for (uint i = 0; i < board->num_spaces; i++)
	{
		const mp_space_node_t *s = board->spaces + i;
		if (s->num_links > 16)
			return ERR_INVALID_DATA;
		total += 36 + (has_param3 ? 6 : 4) + 4 + (size_t)s->num_links * 2;
	}
	if (total > NFMT_MAX_OUTPUT)
		return EFBIG;

	u8 *out = CALLOC (1, total);
	if (!out)
		return ERR_OUT_OF_MEMORY;

	out[0] = board->num_spaces >> 24;
	out[1] = board->num_spaces >> 16;
	out[2] = board->num_spaces >> 8;
	out[3] = board->num_spaces;
	size_t pos = 4;
	for (uint i = 0; i < board->num_spaces; i++)
	{
		const mp_space_node_t *s = board->spaces + i;
		for (int c = 0; c < 3; c++)
		{
			wr_befloat (out + pos, s->pos[c]);
			pos += 4;
		}
		for (int c = 0; c < 3; c++)
		{
			wr_befloat (out + pos, s->rot[c]);
			pos += 4;
		}
		for (int c = 0; c < 3; c++)
		{
			wr_befloat (out + pos, s->scale[c]);
			pos += 4;
		}
		out[pos] = s->param1 >> 8; out[pos + 1] = s->param1; pos += 2;
		out[pos] = s->param2 >> 8; out[pos + 1] = s->param2; pos += 2;
		if (has_param3)
		{
			out[pos] = s->param3 >> 8; out[pos + 1] = s->param3; pos += 2;
		}
		out[pos] = s->type_id >> 8; out[pos + 1] = s->type_id; pos += 2;
		out[pos] = s->num_links >> 8; out[pos + 1] = s->num_links; pos += 2;
		for (uint k = 0; k < s->num_links; k++)
		{
			out[pos] = s->links[k] >> 8; out[pos + 1] = s->links[k]; pos += 2;
		}
	}

	*dest = out;
	*dest_size = (uint)total;
	return ERR_OK;
}

// ----------------------------------------------------------------------------
// SMP Switch board (MPLibrary/Switch/SMP/Board.cs + SpaceNode.cs).
//
// Shift-JIS helpers: the CSV is CP932; ASCII bytes pass through, lead bytes
// are converted via the project's own Shift-JIS tables (same pattern as
// lib-byml.c's byml_sjis_to_utf8).
static char *smp_sjis_to_utf8 (const u8 *src, size_t len)
{
	if (len > 0x3fffffff)
		return 0;
	char *out = MALLOC (len * 4 + 1);
	if (!out)
		return 0;
	char *dst = out;
	const u8 *p = src, *end = src + len;
	while (p < end && *p)
	{
		cucp pp = p;
		int code = ScanShiftJISChar (&pp);
		if (code < 0)
		{
			*dst++ = (char)*p++;
		}
		else if (code > 0)
		{
			p = pp;
			dst = PrintUTF8Char (dst, (u32)code);
		}
		else
			p++;
	}
	*dst = 0;
	return out;
}

static void smp_copy_field (char *dst, size_t cap, const char *src, size_t len)
{
	if (len >= cap)
		len = cap - 1;
	memcpy (dst, src, len);
	dst[len] = 0;
}

bool IsSMPBoard (const u8 *data, size_t size)
{
	if (!data || size < 16 || size > NFMT_MAX_OUTPUT)
		return false;
	// Binary GC/Wii boards start with a BE space count + floats; SMP is
	// text CSV. Reject anything with NUL bytes early.
	for (size_t i = 0; i < size && i < 512; i++)
		if (!data[i])
			return false;
	// Must have at least a header line + one data line with commas.
	const u8 *nl = memchr (data, '\n', size);
	if (!nl)
		return false;
	size_t first_len = (size_t)(nl - data);
	if (first_len > 512)
		return false;
	// Our own MPBOARD CSV output starts with "ID,PosX" -- never SMP.
	if (first_len >= 7 && !memcmp (data, "ID,PosX", 7))
		return false;
	// Second line must have >= 7 commas (8 SMP columns).
	const u8 *second = nl + 1;
	const u8 *nl2 = memchr (second, '\n', size - (size_t)(second - data));
	size_t second_len = nl2 ? (size_t)(nl2 - second) : size - (size_t)(second - data);
	if (second_len < 8 || second_len > 2048)
		return false;
	int commas = 0;
	for (size_t i = 0; i < second_len; i++)
		if (second[i] == ',')
			commas++;
	return commas >= 7;
}

enumError ScanSMPBoard (smp_board_t *board, const u8 *data, size_t size)
{
	if (!board || !data || !size)
		return ERR_INVALID_DATA;
	memset (board, 0, sizeof (*board));

	char *text = smp_sjis_to_utf8 (data, size);
	if (!text)
		return ERR_OUT_OF_MEMORY;

	// Count lines (skip first/header).
	uint n_lines = 0;
	for (char *p = text; *p; p++)
		if (*p == '\n')
			n_lines++;
	if (n_lines < 1)
	{
		FREE (text);
		return ERR_INVALID_DATA;
	}
	uint n_spaces = n_lines >= 1 ? n_lines - 1 : 0;
	if (n_spaces > 100000)
		n_spaces = 100000;
	// Trailing newline creates an empty last "line"; drop empties later.
	smp_space_t *spaces = CALLOC (n_spaces ? n_spaces : 1, sizeof (*spaces));
	if (!spaces)
	{
		FREE (text);
		return ERR_OUT_OF_MEMORY;
	}

	char *line = strchr (text, '\n');
	if (line)
		line++;
	uint idx = 0;
	while (line && *line && idx < n_spaces)
	{
		char *eol = strchr (line, '\n');
		if (eol)
			*eol = 0;
		// Strip trailing \r.
		size_t llen = strlen (line);
		while (llen && (line[llen - 1] == '\r'))
			line[--llen] = 0;
		if (llen)
		{
			// Split into 8 columns (ID,c0..c3,Type,Attr1,Attr2).
			const char *cols[8] = { 0 };
			size_t lens[8] = { 0 };
			int ncols = 0;
			const char *start = line;
			for (const char *p = line; ; p++)
			{
				if (*p == ',' || !*p)
				{
					if (ncols < 8)
					{
						cols[ncols] = start;
						lens[ncols] = (size_t)(p - start);
						ncols++;
					}
					if (!*p)
						break;
					start = p + 1;
				}
			}
			if (ncols >= 6)
			{
				smp_space_t *s = spaces + idx;
				smp_copy_field (s->id, sizeof (s->id), cols[0], lens[0]);
				s->num_links = 0;
				for (int c = 0; c < 4 && 1 + c < ncols; c++)
				{
					if (lens[1 + c] == 0)
						continue;
					char tmp[16];
					smp_copy_field (tmp, sizeof (tmp), cols[1 + c], lens[1 + c]);
					char *end = 0;
					long v = strtol (tmp, &end, 10);
					if (end != tmp && v >= 0 && v <= 0xFFFF && s->num_links < 4)
						s->links[s->num_links++] = (u16)v;
				}
				if (5 < ncols)
					smp_copy_field (s->type, sizeof (s->type), cols[5], lens[5]);
				if (6 < ncols)
					smp_copy_field (s->attr1, sizeof (s->attr1), cols[6], lens[6]);
				if (7 < ncols)
					smp_copy_field (s->attr2, sizeof (s->attr2), cols[7], lens[7]);
				if (!s->type[0])
					snprintf (s->type, sizeof (s->type), "EMPTY");
				idx++;
			}
		}
		line = eol ? eol + 1 : 0;
	}

	FREE (text);
	if (!idx)
	{
		FREE (spaces);
		return ERR_INVALID_DATA;
	}
	board->spaces = spaces;
	board->num_spaces = idx;
	return ERR_OK;
}

void ResetSMPBoard (smp_board_t *board)
{
	if (board)
	{
		FREE (board->spaces);
		memset (board, 0, sizeof (*board));
	}
}

enumError DecodeSMPBoard_CSV (FILE *out, const smp_board_t *board)
{
	if (!out || !board || !board->spaces)
		return ERR_INVALID_DATA;
	fprintf (out, "ID,Child0,Child1,Child2,Child3,Type,Attr1,Attr2\n");
	for (uint i = 0; i < board->num_spaces; i++)
	{
		const smp_space_t *s = board->spaces + i;
		fprintf (out, "%s,", s->id);
		for (int c = 0; c < 4; c++)
		{
			if ((uint)c < s->num_links)
				fprintf (out, "%u", s->links[c]);
			fprintf (out, ",");
		}
		fprintf (out, "%s,%s,%s\n", s->type, s->attr1, s->attr2);
	}
	return ERR_OK;
}

enumError CreateSMPBoard (u8 **dest, uint *dest_size, const smp_board_t *board)
{
	if (!dest || !dest_size || !board || !board->spaces || !board->num_spaces)
		return ERR_INVALID_DATA;
	SetupGetShiftJISCache ();

	// Estimate: header + ~128 bytes/space in UTF8 (SJIS is shorter or equal
	// for ASCII-heavy boards, so UTF8 length is a safe upper bound x2).
	size_t cap = 256 + (size_t)board->num_spaces * 256;
	char *tmp = MALLOC (cap);
	if (!tmp)
		return ERR_OUT_OF_MEMORY;
	int n = snprintf (tmp, cap, "ID,Child0,Child1,Child2,Child3,Type,Attr1,Attr2\n");
	size_t pos = n > 0 ? (size_t)n : 0;
	for (uint i = 0; i < board->num_spaces; i++)
	{
		const smp_space_t *s = board->spaces + i;
		char c0[16] = "", c1[16] = "", c2[16] = "", c3[16] = "";
		if (s->num_links > 0)
			snprintf (c0, sizeof (c0), "%u", s->links[0]);
		if (s->num_links > 1)
			snprintf (c1, sizeof (c1), "%u", s->links[1]);
		if (s->num_links > 2)
			snprintf (c2, sizeof (c2), "%u", s->links[2]);
		if (s->num_links > 3)
			snprintf (c3, sizeof (c3), "%u", s->links[3]);
		int w = snprintf (tmp + pos, cap - pos, "%s,%s,%s,%s,%s,%s,%s,%s\n",
			s->id, c0, c1, c2, c3, s->type[0] ? s->type : "EMPTY", s->attr1, s->attr2);
		if (w < 0 || pos + (size_t)w >= cap)
		{
			FREE (tmp);
			return ERR_CANT_CREATE;
		}
		pos += (size_t)w;
	}

	// Convert UTF8 -> Shift-JIS.
	u8 *sjis = MALLOC (pos * 2 + 1);
	if (!sjis)
	{
		FREE (tmp);
		return ERR_OUT_OF_MEMORY;
	}
	size_t spos = 0;
	ccp p = tmp;
	while (*p)
	{
		u32 ch = ScanUTF8Char (&p);
		if (ch & S32_MIN)
			continue;
		int sc = GetShiftJISChar (ch);
		if (sc < 0)
			sc = '?';
		if (sc > 0xFF)
		{
			sjis[spos++] = (sc >> 8) & 0xFF;
			sjis[spos++] = sc & 0xFF;
		}
		else
			sjis[spos++] = sc & 0xFF;
	}
	FREE (tmp);
	*dest = sjis;
	*dest_size = (uint)spos;
	return ERR_OK;
}

// ----------------------------------------------------------------------------
// MP10 Wii U board XML (MPLibrary/WiiU/Board.cs).
//
// Minimal tag scanner: the reference schema is flat (root/XmlFile/Version +
// repeated MasuData blocks with scalar children + NextNoList/PrevNoList link
// lists + Position/Quaternion). No external XML library is used.
static const char *mp10_find_tag (const char *p, const char *tag, const char *end)
{
	size_t tlen = strlen (tag);
	while (p && p < end)
	{
		const char *lt = strchr (p, '<');
		if (!lt || lt >= end)
			return 0;
		if (!strncmp (lt + 1, tag, tlen)
			&& (lt[1 + tlen] == '>' || lt[1 + tlen] == ' '
				|| lt[1 + tlen] == '/'))
			return lt;
		p = lt + 1;
	}
	return 0;
}

static bool mp10_get_text (const char *xml, const char *end, const char *tag,
	char *dst, size_t cap)
{
	const char *open = mp10_find_tag (xml, tag, end);
	if (!open)
		return false;
	const char *gt = strchr (open, '>');
	if (!gt || gt >= end)
		return false;
	if (gt[-1] == '/')
	{
		if (dst && cap)
			dst[0] = 0;
		return true;
	}
	char close[128];
	snprintf (close, sizeof (close), "</%s>", tag);
	const char *ce = strstr (gt + 1, close);
	if (!ce || ce > end)
		return false;
	size_t len = (size_t)(ce - (gt + 1));
	if (len >= cap)
		len = cap - 1;
	memcpy (dst, gt + 1, len);
	dst[len] = 0;
	return true;
}

static int mp10_get_int (const char *xml, const char *end, const char *tag, int def)
{
	char buf[64];
	if (!mp10_get_text (xml, end, tag, buf, sizeof (buf)))
		return def;
	return atoi (buf);
}

static float mp10_get_float (const char *xml, const char *end, const char *tag, float def)
{
	char buf[64];
	if (!mp10_get_text (xml, end, tag, buf, sizeof (buf)))
		return def;
	return (float)atof (buf);
}

bool IsMP10Board (const u8 *data, size_t size)
{
	if (!data || size < 32 || size > 16 * 1024 * 1024)
		return false;
	for (size_t i = 0; i < size; i++)
		if (!data[i])
			return false;
	// XB-decoded board XML always carries MasuData + position/quaternion.
	if (!memmem (data, size, "MasuData", 8))
		return false;
	if (!memmem (data, size, "NextNoList", 10) && !memmem (data, size, "Position", 8))
		return false;
	return true;
}

enumError ScanMP10Board (mp10_board_t *board, const u8 *data, size_t size)
{
	if (!board || !data || !size)
		return ERR_INVALID_DATA;
	memset (board, 0, sizeof (*board));

	char *xml = MALLOC (size + 1);
	if (!xml)
		return ERR_OUT_OF_MEMORY;
	memcpy (xml, data, size);
	xml[size] = 0;
	const char *end = xml + size;

	mp10_get_text (xml, end, "XmlFile", board->xmlfile, sizeof (board->xmlfile));
	{
		char vbuf[32];
		if (mp10_get_text (xml, end, "Version", vbuf, sizeof (vbuf)))
			board->version = (float)atof (vbuf);
	}

	// Count MasuData blocks.
	uint n = 0;
	for (const char *p = xml; (p = mp10_find_tag (p, "MasuData", end)); )
	{
		n++;
		p++;
		if (n > 10000)
			break;
	}
	if (!n)
	{
		FREE (xml);
		return ERR_INVALID_DATA;
	}
	mp10_masu_t *masu = CALLOC (n, sizeof (*masu));
	if (!masu)
	{
		FREE (xml);
		return ERR_OUT_OF_MEMORY;
	}

	uint idx = 0;
	const char *p = xml;
	while (idx < n && (p = mp10_find_tag (p, "MasuData", end)))
	{
		const char *gt = strchr (p, '>');
		if (!gt)
			break;
		const char *ce = strstr (gt + 1, "</MasuData>");
		if (!ce)
			break;
		mp10_masu_t *m = masu + idx;
		m->id = mp10_get_int (gt, ce, "No", -1);
		m->area = mp10_get_int (gt, ce, "Area", 0);
		mp10_get_text (gt, ce, "NodeName", m->name, sizeof (m->name));
		mp10_get_text (gt, ce, "MasuName", m->type, sizeof (m->type));
		m->param = mp10_get_int (gt, ce, "Param", 0);
		m->uncountble = mp10_get_int (gt, ce, "Uncountble", 0);
		m->oneway = mp10_get_int (gt, ce, "OneWay", 0);
		m->jumpstart = mp10_get_int (gt, ce, "JumpStart", 0);
		m->jumpend = mp10_get_int (gt, ce, "JumpEnd", 0);
		m->punish = mp10_get_int (gt, ce, "PunishNotReturn", 0);

		// Next/Prev link lists: <NextNo Index="i">value</...>.
		const char *nl = mp10_find_tag (gt, "NextNoList", ce);
		if (nl)
		{
			const char *ngt = strchr (nl, '>');
			const char *nce = strstr (nl, "</NextNoList>");
			if (ngt && nce && nce <= ce)
			{
				const char *q = ngt + 1;
				while (m->num_next < 16)
				{
					const char *no = mp10_find_tag (q, "NextNo", nce);
					if (!no)
						break;
					const char *nogt = strchr (no, '>');
					const char *noce = strstr (no, "</NextNo>");
					if (!nogt || !noce || noce > nce)
						break;
					char vbuf[32];
					size_t vlen = (size_t)(noce - (nogt + 1));
					if (vlen >= sizeof (vbuf))
						vlen = sizeof (vbuf) - 1;
					memcpy (vbuf, nogt + 1, vlen);
					vbuf[vlen] = 0;
					m->next[m->num_next++] = atoi (vbuf);
					q = noce + 9;
				}
			}
		}
		const char *pl = mp10_find_tag (gt, "PrevNoList", ce);
		if (pl)
		{
			const char *pgt = strchr (pl, '>');
			const char *pce = strstr (pl, "</PrevNoList>");
			if (pgt && pce && pce <= ce)
			{
				const char *q = pgt + 1;
				while (m->num_prev < 16)
				{
					const char *no = mp10_find_tag (q, "PrevNo", pce);
					if (!no)
						break;
					const char *nogt = strchr (no, '>');
					const char *noce = strstr (no, "</PrevNo>");
					if (!nogt || !noce || noce > pce)
						break;
					char vbuf[32];
					size_t vlen = (size_t)(noce - (nogt + 1));
					if (vlen >= sizeof (vbuf))
						vlen = sizeof (vbuf) - 1;
					memcpy (vbuf, nogt + 1, vlen);
					vbuf[vlen] = 0;
					m->prev[m->num_prev++] = atoi (vbuf);
					q = noce + 9;
				}
			}
		}

		// Position (X/Y/Z) and Quaternion (X/Y/Z/W) sub-objects.
		const char *pos = mp10_find_tag (gt, "Position", ce);
		if (pos)
		{
			const char *pgt = strchr (pos, '>');
			const char *pce = strstr (pos, "</Position>");
			if (pgt && pce && pce <= ce)
			{
				m->pos[0] = mp10_get_float (pgt, pce, "X", 0);
				m->pos[1] = mp10_get_float (pgt, pce, "Y", 0);
				m->pos[2] = mp10_get_float (pgt, pce, "Z", 0);
			}
		}
		const char *quat = mp10_find_tag (gt, "Quaternion", ce);
		if (quat)
		{
			const char *qgt = strchr (quat, '>');
			const char *qce = strstr (quat, "</Quaternion>");
			if (qgt && qce && qce <= ce)
			{
				m->quat[0] = mp10_get_float (qgt, qce, "X", 0);
				m->quat[1] = mp10_get_float (qgt, qce, "Y", 0);
				m->quat[2] = mp10_get_float (qgt, qce, "Z", 0);
				m->quat[3] = mp10_get_float (qgt, qce, "W", 1);
			}
			else
				m->quat[3] = 1;
		}
		else
			m->quat[3] = 1;

		idx++;
		p = ce + 11;
	}

	FREE (xml);
	board->masu = masu;
	board->num_masu = idx;
	return idx ? ERR_OK : ERR_INVALID_DATA;
}

void ResetMP10Board (mp10_board_t *board)
{
	if (board)
	{
		FREE (board->masu);
		memset (board, 0, sizeof (*board));
	}
}

enumError DecodeMP10Board_Text (FILE *out, const mp10_board_t *board)
{
	if (!out || !board || !board->masu)
		return ERR_INVALID_DATA;
	fprintf (out, "# Mario Party 10 Board (Masu: %u, Version: %.1f, XmlFile: %s)\n\n",
		board->num_masu, board->version, board->xmlfile);
	for (uint i = 0; i < board->num_masu; i++)
	{
		const mp10_masu_t *m = board->masu + i;
		fprintf (out, "Masu %d: Area=%d Name=%s Type=%s Param=%d\n",
			m->id, m->area, m->name, m->type, m->param);
		fprintf (out, "  Pos=(%.2f, %.2f, %.2f) Quat=(%.3f, %.3f, %.3f, %.3f)\n",
			m->pos[0], m->pos[1], m->pos[2], m->quat[0], m->quat[1], m->quat[2], m->quat[3]);
		fprintf (out, "  Next (%u): [", m->num_next);
		for (uint k = 0; k < m->num_next; k++)
			fprintf (out, "%s%d", k ? ", " : "", m->next[k]);
		fprintf (out, "] Prev (%u): [", m->num_prev);
		for (uint k = 0; k < m->num_prev; k++)
			fprintf (out, "%s%d", k ? ", " : "", m->prev[k]);
		fprintf (out, "]\n\n");
	}
	return ERR_OK;
}

enumError DecodeMP10Board_CSV (FILE *out, const mp10_board_t *board)
{
	if (!out || !board || !board->masu)
		return ERR_INVALID_DATA;
	fprintf (out, "No,Area,NodeName,MasuName,Param,PosX,PosY,PosZ,QuatX,QuatY,QuatZ,QuatW,Next,Prev\n");
	for (uint i = 0; i < board->num_masu; i++)
	{
		const mp10_masu_t *m = board->masu + i;
		fprintf (out, "%d,%d,%s,%s,%d,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.4f,\"",
			m->id, m->area, m->name, m->type, m->param,
			m->pos[0], m->pos[1], m->pos[2],
			m->quat[0], m->quat[1], m->quat[2], m->quat[3]);
		for (uint k = 0; k < m->num_next; k++)
			fprintf (out, "%s%d", k ? ";" : "", m->next[k]);
		fprintf (out, "\",\"");
		for (uint k = 0; k < m->num_prev; k++)
			fprintf (out, "%s%d", k ? ";" : "", m->prev[k]);
		fprintf (out, "\"\n");
	}
	return ERR_OK;
}

enumError CreateMP10Board (u8 **dest, uint *dest_size, const mp10_board_t *board)
{
	if (!dest || !dest_size || !board || !board->masu || !board->num_masu)
		return ERR_INVALID_DATA;

	size_t cap = 1024 + (size_t)board->num_masu * 1024;
	char *buf = MALLOC (cap);
	if (!buf)
		return ERR_OUT_OF_MEMORY;
	size_t pos = 0;
	pos += snprintf (buf + pos, cap - pos,
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<root>\n<XmlFile>%s</XmlFile>\n<Version>%.1f</Version>\n",
		board->xmlfile[0] ? board->xmlfile : "board",
		board->version ? board->version : 1.0f);
	for (uint i = 0; i < board->num_masu; i++)
	{
		const mp10_masu_t *m = board->masu + i;
		if (pos + 2048 > cap)
		{
			cap *= 2;
			char *nb = REALLOC (buf, cap);
			if (!nb)
			{
				FREE (buf);
				return ERR_OUT_OF_MEMORY;
			}
			buf = nb;
		}
		pos += snprintf (buf + pos, cap - pos,
			"<MasuData><No>%d</No><Area>%d</Area>"
			"<NodeName>%s</NodeName><MasuName>%s</MasuName>"
			"<Param>%d</Param><Uncountble>%d</Uncountble>"
			"<OneWay>%d</OneWay><JumpStart>%d</JumpStart><JumpEnd>%d</JumpEnd>"
			"<PunishNotReturn>%d</PunishNotReturn>",
			m->id, m->area, m->name, m->type, m->param,
			m->uncountble, m->oneway, m->jumpstart, m->jumpend, m->punish);
		pos += snprintf (buf + pos, cap - pos, "<NextNoList Size=\"%u\">", m->num_next);
		for (uint k = 0; k < m->num_next; k++)
			pos += snprintf (buf + pos, cap - pos,
				"<NextNo Index=\"%u\">%d</NextNo>", k, m->next[k]);
		pos += snprintf (buf + pos, cap - pos, "</NextNoList>");
		pos += snprintf (buf + pos, cap - pos, "<PrevNoList Size=\"%u\">", m->num_prev);
		for (uint k = 0; k < m->num_prev; k++)
			pos += snprintf (buf + pos, cap - pos,
				"<PrevNo Index=\"%u\">%d</PrevNo>", k, m->prev[k]);
		pos += snprintf (buf + pos, cap - pos, "</PrevNoList>");
		pos += snprintf (buf + pos, cap - pos,
			"<Position><X>%.4f</X><Y>%.4f</Y><Z>%.4f</Z></Position>"
			"<Quaternion><X>%.5f</X><Y>%.5f</Y><Z>%.5f</Z><W>%.5f</W></Quaternion>"
			"</MasuData>\n",
			m->pos[0], m->pos[1], m->pos[2],
			m->quat[0], m->quat[1], m->quat[2], m->quat[3]);
	}
	pos += snprintf (buf + pos, cap - pos, "</root>\n");

	*dest = (u8 *)buf;
	*dest_size = (uint)pos;
	return ERR_OK;
}

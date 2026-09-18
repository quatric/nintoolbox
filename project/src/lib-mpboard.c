// SPDX-License-Identifier: GPL-2.0+
#include "lib-mpboard.h"
#include "lib-std.h"
#include "dclib-debug.h"
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
		s->num_links = be16 (data + pos); pos += 2;

		if (s->num_links > 16)
			s->num_links = 16;

		if (pos + s->num_links * 2 > size)
		{
			ResetMPBoard (board);
			return ERR_INVALID_DATA;
		}

		for (uint k = 0; k < s->num_links; k++)
		{
			s->links[k] = be16 (data + pos);
			pos += 2;
		}
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

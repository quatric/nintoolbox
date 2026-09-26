// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 SALT parameter files (fighter_param.bin,
// stage params, *.bin with a leading 0xFFFF magic).
//
// Reference: Sammi-Husky/Sm4sh-Tools, SALT/Params/ParamFile.cs
// (via KillzXGaming/Smash-Forge's SALT.dll usage), MIT licensed;
// clean-room C port of the documented binary layout.

#include "lib-saltparam.h"
#include "lib-std.h"
#include <string.h>

typedef struct saltparam_val_t
{
	u8 type; // 1..8
	s32 i; // integer payload (sign-extended)
	u32 u; // raw bits (floats) / string length
	const u8 *str; // type 8 payload (not NUL-terminated)
} saltparam_val_t;

typedef struct saltparam_group_t
{
	u32 entry_count; // 0 = flat list (values before the first group)
	u32 first, count; // value slice
} saltparam_group_t;

// Walk the value stream; on success fills dynamic value/group arrays
// (caller frees) and returns ERR_OK. Rejects unknown type codes,
// out-of-bounds payloads and trailing garbage.
static enumError saltparam_walk (const u8 *data, size_t size, saltparam_val_t **vals, uint *n_vals,
	saltparam_group_t **groups, uint *n_groups)
{
	*vals = 0;
	*n_vals = 0;
	*groups = 0;
	*n_groups = 0;
	if (!data || size < 8 || rd_be16 (data) != 0xFFFF)
		return ERR_INVALID_DATA;

	uint vcap = 0, gcap = 0;
	size_t pos = 8;
	while (pos < size)
	{
		const u8 type = data[pos++];
		size_t need = 0;
		switch (type)
		{
			case 0x01:
			case 0x02:
				need = 1;
				break;
			case 0x03:
			case 0x04:
				need = 2;
				break;
			case 0x05:
			case 0x06:
			case 0x07:
				need = 4;
				break;
			case 0x08:
				if (pos + 4 > size)
					return ERR_INVALID_DATA;
				need = 4 + (size_t)(s32)rd_be32 (data + pos);
				if ((s32)rd_be32 (data + pos) < 0)
					return ERR_INVALID_DATA;
				break;
			case 0x20:
				need = 4;
				break;
			default:
				return ERR_INVALID_DATA;
		}
		if (pos + need > size)
			return ERR_INVALID_DATA;

		if (type == 0x20)
		{
			const u32 ec = rd_be32 (data + pos);
			if (!ec || ec > 1000000)
				return ERR_INVALID_DATA;
			if (*n_groups + 1 >= gcap)
			{
				gcap = gcap ? gcap * 2 : 8;
				saltparam_group_t *ng = REALLOC (*groups, gcap * sizeof (**groups));
				if (!ng)
					return ERR_OUT_OF_MEMORY;
				*groups = ng;
			}
			// seal the previous collection: a group must hold a whole
			// number of entries
			if (*n_groups)
			{
				saltparam_group_t *prev = &(*groups)[*n_groups - 1];
				uint span = *n_vals - prev->first;
				if (!span || (prev->entry_count && span % prev->entry_count))
				{
					FREE (*vals);
					FREE (*groups);
					*vals = 0;
					*groups = 0;
					return ERR_INVALID_DATA;
				}
			}
			saltparam_group_t *g = &(*groups)[(*n_groups)++];
			g->entry_count = ec;
			g->first = *n_vals;
			g->count = 0;
			pos += 4;
			continue;
		}

		if (*n_vals + 1 >= vcap)
		{
			vcap = vcap ? vcap * 2 : 64;
			saltparam_val_t *nv = REALLOC (*vals, vcap * sizeof (**vals));
			if (!nv)
			{
				FREE (*vals);
				FREE (*groups);
				*vals = 0;
				*groups = 0;
				return ERR_OUT_OF_MEMORY;
			}
			*vals = nv;
		}
		saltparam_val_t *v = &(*vals)[(*n_vals)++];
		v->type = type;
		v->str = 0;
		switch (type)
		{
			case 0x01:
				v->i = (s8)data[pos];
				v->u = data[pos];
				break;
			case 0x02:
				v->i = data[pos];
				v->u = data[pos];
				break;
			case 0x03:
				v->i = (s16)rd_be16 (data + pos);
				v->u = rd_be16 (data + pos);
				break;
			case 0x04:
				v->i = rd_be16 (data + pos);
				v->u = rd_be16 (data + pos);
				break;
			case 0x05:
				v->i = (s32)rd_be32 (data + pos);
				v->u = rd_be32 (data + pos);
				break;
			case 0x06:
				v->i = (s32)rd_be32 (data + pos);
				v->u = rd_be32 (data + pos);
				break;
			case 0x07:
				v->u = rd_be32 (data + pos);
				memcpy (&v->i, &v->u, 4);
				break;
			case 0x08:
				v->u = rd_be32 (data + pos);
				v->str = data + pos + 4;
				v->i = (s32)v->u;
				break;
			default:
				break;
		}
		pos += need;
	}

	// seal the trailing collection
	if (*n_groups)
	{
		saltparam_group_t *prev = &(*groups)[*n_groups - 1];
		uint span = *n_vals - prev->first;
		if (!span || (prev->entry_count && span % prev->entry_count))
		{
			FREE (*vals);
			FREE (*groups);
			*vals = 0;
			*groups = 0;
			return ERR_INVALID_DATA;
		}
	}
	// finalise slices: values before the first group stay a flat list
	// (modelled as a leading segment with entry_count 0); every other
	// segment runs from its group start to the next group start (or
	// the end of the values).
	if (*n_groups && (*groups)[0].first > 0)
	{
		if (*n_groups + 1 >= gcap)
		{
			gcap = gcap ? gcap * 2 : 8;
			saltparam_group_t *ng = REALLOC (*groups, gcap * sizeof (**groups));
			if (!ng)
			{
				FREE (*vals);
				FREE (*groups);
				*vals = 0;
				*groups = 0;
				return ERR_OUT_OF_MEMORY;
			}
			*groups = ng;
		}
		memmove (&(*groups)[1], &(*groups)[0], *n_groups * sizeof (**groups));
		(*groups)[0].entry_count = 0;
		(*groups)[0].first = 0;
		(*groups)[0].count = (*groups)[1].first;
		(*n_groups)++;
	}
	for (uint i = 0; i < *n_groups; i++)
	{
		saltparam_group_t *g = &(*groups)[i];
		if (!g->entry_count)
			continue;
		const uint next_first = i + 1 < *n_groups ? (*groups)[i + 1].first : *n_vals;
		g->count = next_first - g->first;
	}
	return ERR_OK;
}

bool IsSaltParam (const u8 *data, size_t size)
{
	saltparam_val_t *vals = 0;
	uint n_vals = 0;
	saltparam_group_t *groups = 0;
	uint n_groups = 0;
	enumError err = saltparam_walk (data, size, &vals, &n_vals, &groups, &n_groups);
	FREE (vals);
	FREE (groups);
	return err == ERR_OK && (n_vals || n_groups);
}

static void saltparam_print_val (FILE *out, const saltparam_val_t *v)
{
	switch (v->type)
	{
		case 0x01:
		case 0x03:
		case 0x05:
			fprintf (out, "%d", v->i);
			break;
		case 0x02:
		case 0x04:
		case 0x06:
			fprintf (out, "%u", v->u);
			break;
		case 0x07:
		{
			float f;
			memcpy (&f, &v->u, 4);
			fprintf (out, "%.7g", f);
			break;
		}
		case 0x08:
			fprintf (out, "\"%.*s\"", v->u, (const char *)v->str);
			break;
		default:
			fprintf (out, "?");
			break;
	}
}

enumError DecodeSaltParam_Text (FILE *out, const u8 *data, size_t size)
{
	saltparam_val_t *vals = 0;
	uint n_vals = 0;
	saltparam_group_t *groups = 0;
	uint n_groups = 0;
	if (!out || saltparam_walk (data, size, &vals, &n_vals, &groups, &n_groups))
	{
		FREE (vals);
		FREE (groups);
		return ERR_INVALID_DATA;
	}

	fprintf (out,
		"#PARAM\n# Super Smash Bros. 4 SALT parameter file\n\n"
		"values = %u\ngroups = %u\n",
		n_vals, n_groups);
	for (uint i = 0; i < n_groups; i++)
	{
		const saltparam_group_t *g = &groups[i];
		if (!g->entry_count)
		{
			fprintf (out, "\n[list %u: %u values]\n", i, g->count);
			for (uint k = 0; k < g->count; k++)
			{
				fprintf (out, "value%u = ", k);
				saltparam_print_val (out, &vals[g->first + k]);
				fprintf (out, "\n");
			}
			continue;
		}
		const uint esize = g->count / g->entry_count;
		fprintf (out, "\n[group %u: %u entries x %u values]\n", i, g->entry_count, esize);
		for (uint e = 0; e < g->entry_count; e++)
		{
			fprintf (out, "entry%u =", e);
			for (uint k = 0; k < esize; k++)
			{
				fprintf (out, " ");
				saltparam_print_val (out, &vals[g->first + e * esize + k]);
			}
			fprintf (out, "\n");
		}
	}
	FREE (vals);
	FREE (groups);
	return ERR_OK;
}

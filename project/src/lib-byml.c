#include "lib-std.h"
#include "lib-byml.h"
#include "mxml.h"
#include <yaml.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

///////////////////////////////////////////////////////////////////////////////
// BYML Constants & Definitions
///////////////////////////////////////////////////////////////////////////////

enum {
	BYML_T_HASHMAP32         = 0x20,
	BYML_T_HASHMAP64         = 0x21,
	BYML_T_RELOC_HASHMAP32   = 0x30,
	BYML_T_RELOC_HASHMAP64   = 0x31,
	BYML_T_STRING            = 0xA0,
	BYML_T_BINARY            = 0xA1,
	BYML_T_BINARY_ALIGNED    = 0xA2,
	BYML_T_ARRAY             = 0xC0,
	BYML_T_MAP               = 0xC1,
	BYML_T_STRING_TABLE      = 0xC2,
	BYML_T_PATH_ARRAY        = 0xC3,
	BYML_T_BOOL              = 0xD0,
	BYML_T_INT               = 0xD1,
	BYML_T_FLOAT             = 0xD2,
	BYML_T_UINT              = 0xD3,
	BYML_T_INT64             = 0xD4,
	BYML_T_UINT64            = 0xD5,
	BYML_T_DOUBLE            = 0xD6,
	BYML_T_NULL              = 0xFF,
};

static inline u16 byml_u16 (const u8 *p, bool is_le)
{
	return is_le ? rd_le16 (p) : rd_be16 (p);
}

static inline u32 byml_u24 (const u8 *p, bool is_le)
{
	return is_le ? ((u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16))
				 : (((u32)p[0] << 16) | ((u32)p[1] << 8) | (u32)p[2]);
}

static inline u32 byml_u32 (const u8 *p, bool is_le)
{
	return is_le ? rd_le32 (p) : rd_be32 (p);
}

static inline u64 byml_u64 (const u8 *p, bool is_le)
{
	if (is_le)
		return (u64)rd_le32 (p) | ((u64)rd_le32 (p + 4) << 32);
	else
		return ((u64)rd_be32 (p) << 32) | (u64)rd_be32 (p + 4);
}

static inline float byml_float (const u8 *p, bool is_le)
{
	u32 v = byml_u32 (p, is_le);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static inline void byml_wr_le64 (u8 *p, u64 v)
{
	wr_le32 (p, (u32)v);
	wr_le32 (p + 4, (u32)(v >> 32));
}

static inline void byml_wr_be64 (u8 *p, u64 v)
{
	wr_be32 (p, (u32)(v >> 32));
	wr_be32 (p + 4, (u32)v);
}

///////////////////////////////////////////////////////////////////////////////
// Base64 helper
///////////////////////////////////////////////////////////////////////////////

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *byml_b64_encode (const u8 *src, size_t size)
{
	if (!src || !size)
		return STRDUP ("");
	if (size > (SIZE_MAX - 4) / 4 * 3)
		return NULL;
	const size_t out_size = (size + 2) / 3 * 4;
	char *out = MALLOC (out_size + 1);
	if (!out)
		return NULL;
	size_t j = 0;
	for (size_t i = 0; i < size; i += 3)
	{
		const unsigned v = (unsigned)src[i] << 16
			| (unsigned)(i + 1 < size ? src[i + 1] : 0) << 8
			| (unsigned)(i + 2 < size ? src[i + 2] : 0);
		out[j++] = b64_table[v >> 18];
		out[j++] = b64_table[(v >> 12) & 63];
		out[j++] = i + 1 < size ? b64_table[(v >> 6) & 63] : =;
		out[j++] = i + 2 < size ? b64_table[v & 63] : =;
	}
	out[out_size] = 0;
	return out;
}

static int byml_b64_val (char c)
{
	if (c >= A && c <= Z) return c - A;
	if (c >= a && c <= z) return c - a + 26;
	if (c >= 0 && c <= 9) return c - 0 + 52;
	if (c == +) return 62;
	if (c == /) return 63;
	return -1;
}

static u8 *byml_b64_decode (const char *src, size_t len, size_t *out_size)
{
	*out_size = 0;
	if (!src || !len)
		return NULL;
	size_t clean_len = 0;
	for (size_t i = 0; i < len; i++)
		if (byml_b64_val (src[i]) >= 0 || src[i] == =)
			clean_len++;
	if (!clean_len)
		return NULL;
	char *clean = MALLOC (clean_len);
	size_t k = 0;
	for (size_t i = 0; i < len; i++)
		if (byml_b64_val (src[i]) >= 0 || src[i] == =)
			clean[k++] = src[i];

	size_t alloc_sz = (clean_len / 4 + 1) * 3;
	u8 *out = MALLOC (alloc_sz);
	size_t out_len = 0;
	for (size_t i = 0; i + 3 < clean_len; i += 4)
	{
		int a = byml_b64_val (clean[i]);
		int b = byml_b64_val (clean[i + 1]);
		int c = clean[i + 2] == = ? 0 : byml_b64_val (clean[i + 2]);
		int d = clean[i + 3] == = ? 0 : byml_b64_val (clean[i + 3]);
		if (a < 0 || b < 0 || c < 0 || d < 0)
			continue;
		unsigned v = (a << 18) | (b << 12) | (c << 6) | d;
		out[out_len++] = (v >> 16) & 0xFF;
		if (clean[i + 2] != =)
			out[out_len++] = (v >> 8) & 0xFF;
		if (clean[i + 3] != =)
			out[out_len++] = v & 0xFF;
	}
	FREE (clean);
	*out_size = out_len;
	return out;
}

///////////////////////////////////////////////////////////////////////////////
// In-Memory BYML Tree Node
///////////////////////////////////////////////////////////////////////////////

typedef struct byml_point_t
{
	float x, y, z;
	float nx, ny, nz;
	u32 val;
} byml_point_t;

typedef struct byml_node_t byml_node_t;

typedef struct byml_entry_t
{
	char *key;       // string key for Map (0xC1)
	u32 hash32;      // for HashMap32 (0x20)
	u64 hash64;      // for HashMap64 (0x21)
	byml_node_t *val;
} byml_entry_t;

struct byml_node_t
{
	u8 type;
	union
	{
		bool b;
		int32_t i;
		uint32_t u;
		float f;
		int64_t i64;
		uint64_t u64;
		double d;
		char *s;
		struct
		{
			u8 *data;
			uint size;
			uint align;
		} bin;
		struct
		{
			byml_node_t **items;
			uint count;
			uint cap;
		} arr;
		struct
		{
			byml_entry_t *entries;
			uint count;
			uint cap;
		} map;
		struct
		{
			byml_point_t *points;
			uint count;
			uint cap;
		} path;
	} u;
};

static byml_node_t *byml_node_new (u8 type)
{
	byml_node_t *n = CALLOC (1, sizeof (byml_node_t));
	n->type = type;
	return n;
}

static void byml_node_free (byml_node_t *n)
{
	if (!n)
		return;
	if (n->type == BYML_T_STRING)
	{
		FREE (n->u.s);
	}
	else if (n->type == BYML_T_BINARY || n->type == BYML_T_BINARY_ALIGNED)
	{
		FREE (n->u.bin.data);
	}
	else if (n->type == BYML_T_ARRAY)
	{
		for (uint i = 0; i < n->u.arr.count; i++)
			byml_node_free (n->u.arr.items[i]);
		FREE (n->u.arr.items);
	}
	else if (n->type == BYML_T_MAP || n->type == BYML_T_HASHMAP32 || n->type == BYML_T_HASHMAP64
		|| n->type == BYML_T_RELOC_HASHMAP32 || n->type == BYML_T_RELOC_HASHMAP64)
	{
		for (uint i = 0; i < n->u.map.count; i++)
		{
			FREE (n->u.map.entries[i].key);
			byml_node_free (n->u.map.entries[i].val);
		}
		FREE (n->u.map.entries);
	}
	else if (n->type == BYML_T_PATH_ARRAY)
	{
		FREE (n->u.path.points);
	}
	FREE (n);
}

static void byml_arr_add (byml_node_t *arr, byml_node_t *item)
{
	if (arr->u.arr.count >= arr->u.arr.cap)
	{
		arr->u.arr.cap = arr->u.arr.cap ? arr->u.arr.cap * 2 : 8;
		arr->u.arr.items = REALLOC (arr->u.arr.items, arr->u.arr.cap * sizeof (byml_node_t *));
	}
	arr->u.arr.items[arr->u.arr.count++] = item;
}

static void byml_map_add (byml_node_t *map, ccp key, byml_node_t *val)
{
	if (map->u.map.count >= map->u.map.cap)
	{
		map->u.map.cap = map->u.map.cap ? map->u.map.cap * 2 : 8;
		map->u.map.entries = REALLOC (map->u.map.entries, map->u.map.cap * sizeof (byml_entry_t));
	}
	byml_entry_t *e = &map->u.map.entries[map->u.map.count++];
	memset (e, 0, sizeof (*e));
	e->key = STRDUP (key ? key : "");
	e->val = val;
}

static void byml_h32_add (byml_node_t *map, u32 hash, byml_node_t *val)
{
	if (map->u.map.count >= map->u.map.cap)
	{
		map->u.map.cap = map->u.map.cap ? map->u.map.cap * 2 : 8;
		map->u.map.entries = REALLOC (map->u.map.entries, map->u.map.cap * sizeof (byml_entry_t));
	}
	byml_entry_t *e = &map->u.map.entries[map->u.map.count++];
	memset (e, 0, sizeof (*e));
	e->hash32 = hash;
	e->val = val;
}

static void byml_h64_add (byml_node_t *map, u64 hash, byml_node_t *val)
{
	if (map->u.map.count >= map->u.map.cap)
	{
		map->u.map.cap = map->u.map.cap ? map->u.map.cap * 2 : 8;
		map->u.map.entries = REALLOC (map->u.map.entries, map->u.map.cap * sizeof (byml_entry_t));
	}
	byml_entry_t *e = &map->u.map.entries[map->u.map.count++];
	memset (e, 0, sizeof (*e));
	e->hash64 = hash;
	e->val = val;
}

static void byml_path_add (byml_node_t *path, byml_point_t pt)
{
	if (path->u.path.count >= path->u.path.cap)
	{
		path->u.path.cap = path->u.path.cap ? path->u.path.cap * 2 : 8;
		path->u.path.points = REALLOC (path->u.path.points, path->u.path.cap * sizeof (byml_point_t));
	}
	path->u.path.points[path->u.path.count++] = pt;
}

///////////////////////////////////////////////////////////////////////////////
// Context for Binary Parsing
///////////////////////////////////////////////////////////////////////////////

typedef struct byml_path_raw_t
{
	byml_point_t *points;
	uint count;
} byml_path_raw_t;

typedef struct byml_ctx_t
{
	const u8 *data;
	size_t size;
	bool is_le;
	u16 version;
	bool supports_paths;
	byml_path_raw_t *paths;
	uint n_paths;
	const char **hash_keys;
	uint n_hash_keys;
	const char **strings;
	uint n_strings;
	u32 visited_stack[256];
	uint visited_depth;
} byml_ctx_t;

static bool byml_is_visited (const byml_ctx_t *ctx, u32 off)
{
	for (uint i = 0; i < ctx->visited_depth; i++)
		if (ctx->visited_stack[i] == off)
			return true;
	return false;
}

static enumError byml_parse_str_table (
	byml_ctx_t *ctx, u32 off, const char ***table_out, uint *count_out)
{
	*table_out = 0;
	*count_out = 0;
	if (!off)
		return ERR_OK;
	if (off + 4 > ctx->size)
		return ERR_INVALID_DATA;
	const u8 *p = ctx->data + off;
	if (p[0] != BYML_T_STRING_TABLE)
		return ERR_INVALID_DATA;
	uint count = byml_u24 (p + 1, ctx->is_le);
	if (!count)
		return ERR_OK;
	if (off + 4 + (count + 1) * 4 > ctx->size)
		return ERR_INVALID_DATA;

	const char **table = CALLOC (count, sizeof (char *));
	for (uint i = 0; i < count; i++)
	{
		u32 st_off = byml_u32 (p + 4 + i * 4, ctx->is_le);
		if (off + st_off >= ctx->size)
			continue;
		table[i] = (const char *)(ctx->data + off + st_off);
	}
	*table_out = table;
	*count_out = count;
	return ERR_OK;
}

static enumError byml_parse_path_table (byml_ctx_t *ctx, u32 off)
{
	if (!off)
		return ERR_OK;
	if (off + 4 > ctx->size)
		return ERR_INVALID_DATA;
	const u8 *p = ctx->data + off;
	if (p[0] != BYML_T_PATH_ARRAY)
		return ERR_INVALID_DATA;
	uint count = byml_u24 (p + 1, ctx->is_le);
	if (!count)
		return ERR_OK;
	if (off + 4 + (count + 1) * 4 > ctx->size)
		return ERR_INVALID_DATA;

	ctx->paths = CALLOC (count, sizeof (byml_path_raw_t));
	ctx->n_paths = count;

	for (uint i = 0; i < count; i++)
	{
		u32 start_rel = byml_u32 (p + 4 + i * 4, ctx->is_le);
		u32 end_rel = byml_u32 (p + 4 + (i + 1) * 4, ctx->is_le);
		if (end_rel < start_rel || off + end_rel > ctx->size)
			continue;
		uint byte_len = end_rel - start_rel;
		uint n_pts = byte_len / 28;
		if (!n_pts)
			continue;
		ctx->paths[i].count = n_pts;
		ctx->paths[i].points = CALLOC (n_pts, sizeof (byml_point_t));
		const u8 *pt_ptr = ctx->data + off + start_rel;
		for (uint j = 0; j < n_pts; j++)
		{
			const u8 *cur = pt_ptr + j * 28;
			ctx->paths[i].points[j].x = byml_float (cur, ctx->is_le);
			ctx->paths[i].points[j].y = byml_float (cur + 4, ctx->is_le);
			ctx->paths[i].points[j].z = byml_float (cur + 8, ctx->is_le);
			ctx->paths[i].points[j].nx = byml_float (cur + 12, ctx->is_le);
			ctx->paths[i].points[j].ny = byml_float (cur + 16, ctx->is_le);
			ctx->paths[i].points[j].nz = byml_float (cur + 20, ctx->is_le);
			ctx->paths[i].points[j].val = byml_u32 (cur + 24, ctx->is_le);
		}
	}
	return ERR_OK;
}

static byml_node_t *byml_parse_binary_node (byml_ctx_t *ctx, u8 type, u32 val, int depth)
{
	if (depth > 128)
		return NULL;

	switch (type)
	{
		case BYML_T_NULL:
			return byml_node_new (BYML_T_NULL);

		case BYML_T_BOOL:
		{
			byml_node_t *n = byml_node_new (BYML_T_BOOL);
			n->u.b = (val != 0);
			return n;
		}

		case BYML_T_INT:
		{
			byml_node_t *n = byml_node_new (BYML_T_INT);
			n->u.i = (int32_t)val;
			return n;
		}

		case BYML_T_FLOAT:
		{
			byml_node_t *n = byml_node_new (BYML_T_FLOAT);
			memcpy (&n->u.f, &val, 4);
			return n;
		}

		case BYML_T_UINT:
		{
			byml_node_t *n = byml_node_new (BYML_T_UINT);
			n->u.u = val;
			return n;
		}

		case BYML_T_INT64:
		{
			byml_node_t *n = byml_node_new (BYML_T_INT64);
			n->u.i64 = (val + 8 <= ctx->size) ? (int64_t)byml_u64 (ctx->data + val, ctx->is_le) : 0;
			return n;
		}

		case BYML_T_UINT64:
		{
			byml_node_t *n = byml_node_new (BYML_T_UINT64);
			n->u.u64 = (val + 8 <= ctx->size) ? byml_u64 (ctx->data + val, ctx->is_le) : 0;
			return n;
		}

		case BYML_T_DOUBLE:
		{
			byml_node_t *n = byml_node_new (BYML_T_DOUBLE);
			if (val + 8 <= ctx->size)
			{
				u64 uv = byml_u64 (ctx->data + val, ctx->is_le);
				memcpy (&n->u.d, &uv, 8);
			}
			return n;
		}

		case BYML_T_STRING:
		{
			byml_node_t *n = byml_node_new (BYML_T_STRING);
			n->u.s = STRDUP (val < ctx->n_strings && ctx->strings[val] ? ctx->strings[val] : "");
			return n;
		}

		case BYML_T_BINARY: // 0xA1: Path index or Binary blob
		{
			if (ctx->supports_paths && val < ctx->n_paths)
			{
				byml_node_t *n = byml_node_new (BYML_T_PATH_ARRAY);
				uint count = ctx->paths[val].count;
				for (uint i = 0; i < count; i++)
					byml_path_add (n, ctx->paths[val].points[i]);
				return n;
			}
			byml_node_t *n = byml_node_new (BYML_T_BINARY);
			if (val + 4 <= ctx->size)
			{
				u32 len = byml_u32 (ctx->data + val, ctx->is_le);
				if (val + 4 + len <= ctx->size)
				{
					n->u.bin.size = len;
					n->u.bin.data = MALLOC (len + 1);
					memcpy (n->u.bin.data, ctx->data + val + 4, len);
					n->u.bin.data[len] = 0;
				}
			}
			return n;
		}

		case BYML_T_BINARY_ALIGNED:
		{
			byml_node_t *n = byml_node_new (BYML_T_BINARY_ALIGNED);
			if (val + 8 <= ctx->size)
			{
				u32 len = byml_u32 (ctx->data + val, ctx->is_le);
				u32 align = byml_u32 (ctx->data + val + 4, ctx->is_le);
				n->u.bin.align = align;
				if (val + 8 + len <= ctx->size)
				{
					n->u.bin.size = len;
					n->u.bin.data = MALLOC (len + 1);
					memcpy (n->u.bin.data, ctx->data + val + 8, len);
					n->u.bin.data[len] = 0;
				}
			}
			return n;
		}

		case BYML_T_ARRAY:
		{
			byml_node_t *n = byml_node_new (BYML_T_ARRAY);
			u32 off = val;
			if (off + 4 > ctx->size || ctx->data[off] != BYML_T_ARRAY || byml_is_visited (ctx, off))
				return n;
			uint count = byml_u24 (ctx->data + off + 1, ctx->is_le);
			if (off + 4 + count > ctx->size) count = 0;
			u32 val_start = off + 4 + ((count + 3) & ~3);
			if (val_start + count * 4 > ctx->size) count = 0;

			if (ctx->visited_depth < 256)
				ctx->visited_stack[ctx->visited_depth++] = off;

			const u8 *tags = ctx->data + off + 4;
			for (uint i = 0; i < count; i++)
			{
				u8 c_type = tags[i];
				u32 c_val = byml_u32 (ctx->data + val_start + i * 4, ctx->is_le);
				byml_node_t *child = byml_parse_binary_node (ctx, c_type, c_val, depth + 1);
				byml_arr_add (n, child ? child : byml_node_new (BYML_T_NULL));
			}

			if (ctx->visited_depth > 0 && ctx->visited_stack[ctx->visited_depth - 1] == off)
				ctx->visited_depth--;
			return n;
		}

		case BYML_T_MAP:
		{
			byml_node_t *n = byml_node_new (BYML_T_MAP);
			u32 off = val;
			if (off + 4 > ctx->size || ctx->data[off] != BYML_T_MAP || byml_is_visited (ctx, off))
				return n;
			uint count = byml_u24 (ctx->data + off + 1, ctx->is_le);
			if (off + 4 + count * 8 > ctx->size) count = 0;

			if (ctx->visited_depth < 256)
				ctx->visited_stack[ctx->visited_depth++] = off;

			for (uint i = 0; i < count; i++)
			{
				const u8 *entry = ctx->data + off + 4 + i * 8;
				uint key_idx = byml_u24 (entry, ctx->is_le);
				u8 c_type = entry[3];
				u32 c_val = byml_u32 (entry + 4, ctx->is_le);

				char kbuf[64];
				ccp key_name = (key_idx < ctx->n_hash_keys && ctx->hash_keys[key_idx])
					? ctx->hash_keys[key_idx] : (snprintf (kbuf, sizeof (kbuf), "key_%u", key_idx), kbuf);

				byml_node_t *child = byml_parse_binary_node (ctx, c_type, c_val, depth + 1);
				byml_map_add (n, key_name, child ? child : byml_node_new (BYML_T_NULL));
			}

			if (ctx->visited_depth > 0 && ctx->visited_stack[ctx->visited_depth - 1] == off)
				ctx->visited_depth--;
			return n;
		}

		case BYML_T_HASHMAP32:
		case BYML_T_RELOC_HASHMAP32:
		{
			byml_node_t *n = byml_node_new (BYML_T_HASHMAP32);
			u32 off = val;
			if (off + 4 > ctx->size || byml_is_visited (ctx, off))
				return n;
			uint count = byml_u24 (ctx->data + off + 1, ctx->is_le);
			if (off + 4 + count * 8 + count > ctx->size) count = 0;

			if (ctx->visited_depth < 256)
				ctx->visited_stack[ctx->visited_depth++] = off;

			const u8 *types = ctx->data + off + 4 + count * 8;
			for (uint i = 0; i < count; i++)
			{
				const u8 *entry = ctx->data + off + 4 + i * 8;
				u32 hash = byml_u32 (entry, ctx->is_le);
				u32 c_val = byml_u32 (entry + 4, ctx->is_le);
				u8 c_type = types[i];

				byml_node_t *child = byml_parse_binary_node (ctx, c_type, c_val, depth + 1);
				byml_h32_add (n, hash, child ? child : byml_node_new (BYML_T_NULL));
			}

			if (ctx->visited_depth > 0 && ctx->visited_stack[ctx->visited_depth - 1] == off)
				ctx->visited_depth--;
			return n;
		}

		case BYML_T_HASHMAP64:
		case BYML_T_RELOC_HASHMAP64:
		{
			byml_node_t *n = byml_node_new (BYML_T_HASHMAP64);
			u32 off = val;
			if (off + 4 > ctx->size || byml_is_visited (ctx, off))
				return n;
			uint count = byml_u24 (ctx->data + off + 1, ctx->is_le);
			if (off + 4 + count * 12 + count > ctx->size) count = 0;

			if (ctx->visited_depth < 256)
				ctx->visited_stack[ctx->visited_depth++] = off;

			const u8 *types = ctx->data + off + 4 + count * 12;
			for (uint i = 0; i < count; i++)
			{
				const u8 *entry = ctx->data + off + 4 + i * 12;
				u64 hash = byml_u64 (entry, ctx->is_le);
				u32 c_val = byml_u32 (entry + 8, ctx->is_le);
				u8 c_type = types[i];

				byml_node_t *child = byml_parse_binary_node (ctx, c_type, c_val, depth + 1);
				byml_h64_add (n, hash, child ? child : byml_node_new (BYML_T_NULL));
			}

			if (ctx->visited_depth > 0 && ctx->visited_stack[ctx->visited_depth - 1] == off)
				ctx->visited_depth--;
			return n;
		}

		case BYML_T_PATH_ARRAY:
		{
			byml_node_t *n = byml_node_new (BYML_T_ARRAY);
			u32 off = val;
			if (off + 4 <= ctx->size)
			{
				uint count = byml_u24 (ctx->data + off + 1, ctx->is_le);
				const u8 *p = ctx->data + off;
				for (uint i = 0; i < count; i++)
				{
					u32 start_rel = byml_u32 (p + 4 + i * 4, ctx->is_le);
					u32 end_rel = byml_u32 (p + 4 + (i + 1) * 4, ctx->is_le);
					if (end_rel < start_rel || off + end_rel > ctx->size) continue;
					uint byte_len = end_rel - start_rel;
					uint n_pts = byte_len / 28;
					byml_node_t *path_node = byml_node_new (BYML_T_PATH_ARRAY);
					for (uint j = 0; j < n_pts; j++)
					{
						const u8 *cur = ctx->data + off + start_rel + j * 28;
						byml_point_t pt;
						pt.x = byml_float (cur, ctx->is_le);
						pt.y = byml_float (cur + 4, ctx->is_le);
						pt.z = byml_float (cur + 8, ctx->is_le);
						pt.nx = byml_float (cur + 12, ctx->is_le);
						pt.ny = byml_float (cur + 16, ctx->is_le);
						pt.nz = byml_float (cur + 20, ctx->is_le);
						pt.val = byml_u32 (cur + 24, ctx->is_le);
						byml_path_add (path_node, pt);
					}
					byml_arr_add (n, path_node);
				}
			}
			return n;
		}

		default:
		{
			byml_node_t *n = byml_node_new (BYML_T_STRING);
			char buf[64];
			snprintf (buf, sizeof (buf), "<unknown_0x%02x_%u>", type, val);
			n->u.s = STRDUP (buf);
			return n;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
// YAML Emission
///////////////////////////////////////////////////////////////////////////////

static bool is_valid_utf8 (const char *s)
{
	const u8 *p = (const u8 *)s;
	while (*p)
	{
		if (*p < 0x80) p++;
		else if ((*p & 0xE0) == 0xC0)
		{
			if ((p[1] & 0xC0) != 0x80) return false;
			p += 2;
		}
		else if ((*p & 0xF0) == 0xE0)
		{
			if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return false;
			p += 3;
		}
		else if ((*p & 0xF8) == 0xF0)
		{
			if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) return false;
			p += 4;
		}
		else return false;
	}
	return true;
}

static int byml_yaml_string (yaml_document_t *doc, const char *str)
{
	if (!str) str = "";
	if (is_valid_utf8 (str))
		return yaml_document_add_scalar (doc, (yaml_char_t *)YAML_STR_TAG,
			(const yaml_char_t *)str, -1, YAML_PLAIN_SCALAR_STYLE);

	size_t len = strlen (str);
	char *escaped = CALLOC (1, len * 4 + 1), *dest = escaped;
	for (const u8 *src = (const u8 *)str; *src; src++)
	{
		if (*src < 0x20 || *src >= 0x80)
		{
			snprintf (dest, 5, "\\x%02x", *src);
			dest += 4;
		}
		else
			*dest++ = *src;
	}
	int node = yaml_document_add_scalar (doc, (yaml_char_t *)YAML_STR_TAG,
		(const yaml_char_t *)escaped, -1, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
	FREE (escaped);
	return node;
}

static int byml_yaml_scalar (yaml_document_t *doc, const char *tag, const char *val)
{
	return yaml_document_add_scalar (doc, (yaml_char_t *)tag,
		(const yaml_char_t *)val, -1, YAML_PLAIN_SCALAR_STYLE);
}

static int byml_node_to_yaml_doc (yaml_document_t *doc, const byml_node_t *n)
{
	char buf[128];
	if (!n)
		return byml_yaml_scalar (doc, YAML_NULL_TAG, "null");

	switch (n->type)
	{
		case BYML_T_NULL:
			return byml_yaml_scalar (doc, YAML_NULL_TAG, "null");

		case BYML_T_BOOL:
			return byml_yaml_scalar (doc, YAML_BOOL_TAG, n->u.b ? "true" : "false");

		case BYML_T_INT:
			snprintf (buf, sizeof (buf), "%d", n->u.i);
			return byml_yaml_scalar (doc, YAML_INT_TAG, buf);

		case BYML_T_FLOAT:
		{
			if (isnan (n->u.f)) return byml_yaml_scalar (doc, YAML_FLOAT_TAG, ".nan");
			if (isinf (n->u.f)) return byml_yaml_scalar (doc, YAML_FLOAT_TAG, n->u.f < 0 ? "-.inf" : ".inf");
			snprintf (buf, sizeof (buf), "%.8g", n->u.f);
			if (!strchr (buf, .) && !strchr (buf, e) && !strchr (buf, E)) strcat (buf, ".0");
			return byml_yaml_scalar (doc, YAML_FLOAT_TAG, buf);
		}

		case BYML_T_UINT:
			snprintf (buf, sizeof (buf), "%u", n->u.u);
			return byml_yaml_scalar (doc, "!u", buf);

		case BYML_T_INT64:
			snprintf (buf, sizeof (buf), "%lld", (long long)n->u.i64);
			return byml_yaml_scalar (doc, "!l", buf);

		case BYML_T_UINT64:
			snprintf (buf, sizeof (buf), "%llu", (unsigned long long)n->u.u64);
			return byml_yaml_scalar (doc, "!ul", buf);

		case BYML_T_DOUBLE:
		{
			if (isnan (n->u.d)) return byml_yaml_scalar (doc, "!d", ".nan");
			if (isinf (n->u.d)) return byml_yaml_scalar (doc, "!d", n->u.d < 0 ? "-.inf" : ".inf");
			snprintf (buf, sizeof (buf), "%.16g", n->u.d);
			if (!strchr (buf, .) && !strchr (buf, e) && !strchr (buf, E)) strcat (buf, ".0");
			return byml_yaml_scalar (doc, "!d", buf);
		}

		case BYML_T_STRING:
			return byml_yaml_string (doc, n->u.s);

		case BYML_T_BINARY:
		{
			char *b64 = byml_b64_encode (n->u.bin.data, n->u.bin.size);
			int item = byml_yaml_scalar (doc, "tag:yaml.org,2002:binary", b64 ? b64 : "");
			FREE (b64);
			return item;
		}

		case BYML_T_BINARY_ALIGNED:
		{
			int map = yaml_document_add_mapping (doc, (yaml_char_t *)"!file", YAML_BLOCK_MAPPING_STYLE);
			int k_align = byml_yaml_string (doc, "Alignment");
			snprintf (buf, sizeof (buf), "%u", n->u.bin.align);
			int v_align = byml_yaml_scalar (doc, YAML_INT_TAG, buf);
			yaml_document_append_mapping_pair (doc, map, k_align, v_align);

			int k_data = byml_yaml_string (doc, "Data");
			char *b64 = byml_b64_encode (n->u.bin.data, n->u.bin.size);
			int v_data = byml_yaml_scalar (doc, "tag:yaml.org,2002:binary", b64 ? b64 : "");
			FREE (b64);
			yaml_document_append_mapping_pair (doc, map, k_data, v_data);
			return map;
		}

		case BYML_T_PATH_ARRAY:
		{
			int seq = yaml_document_add_sequence (doc, (yaml_char_t *)YAML_SEQ_TAG, YAML_BLOCK_SEQUENCE_STYLE);
			for (uint i = 0; i < n->u.path.count; i++)
			{
				const byml_point_t *pt = &n->u.path.points[i];
				int map = yaml_document_add_mapping (doc, (yaml_char_t *)YAML_MAP_TAG, YAML_FLOW_MAPPING_STYLE);

				char pbuf[64];
				#define ADD_PT_F(key, val) do { \
					int k = byml_yaml_string (doc, key); \
					snprintf (pbuf, sizeof (pbuf), "%.8g", (double)(val)); \
					if (!strchr (pbuf, \x27.\x27) && !strchr (pbuf, \x27e\x27) && !strchr (pbuf, \x27E\x27)) strcat (pbuf, ".0"); \
					int v = byml_yaml_scalar (doc, YAML_FLOAT_TAG, pbuf); \
					yaml_document_append_mapping_pair (doc, map, k, v); \
				} while (0)

				ADD_PT_F ("X", pt->x);
				ADD_PT_F ("Y", pt->y);
				ADD_PT_F ("Z", pt->z);
				ADD_PT_F ("NX", pt->nx);
				ADD_PT_F ("NY", pt->ny);
				ADD_PT_F ("NZ", pt->nz);

				int kv = byml_yaml_string (doc, "Value");
				snprintf (pbuf, sizeof (pbuf), "%u", pt->val);
				int vv = byml_yaml_scalar (doc, YAML_INT_TAG, pbuf);
				yaml_document_append_mapping_pair (doc, map, kv, vv);

				yaml_document_append_sequence_item (doc, seq, map);
			}
			return seq;
		}

		case BYML_T_ARRAY:
		{
			int seq = yaml_document_add_sequence (doc, (yaml_char_t *)YAML_SEQ_TAG, YAML_BLOCK_SEQUENCE_STYLE);
			for (uint i = 0; i < n->u.arr.count; i++)
			{
				int item = byml_node_to_yaml_doc (doc, n->u.arr.items[i]);
				yaml_document_append_sequence_item (doc, seq, item);
			}
			return seq;
		}

		case BYML_T_MAP:
		{
			int map = yaml_document_add_mapping (doc, (yaml_char_t *)YAML_MAP_TAG, YAML_BLOCK_MAPPING_STYLE);
			for (uint i = 0; i < n->u.map.count; i++)
			{
				int k = byml_yaml_string (doc, n->u.map.entries[i].key);
				int v = byml_node_to_yaml_doc (doc, n->u.map.entries[i].val);
				yaml_document_append_mapping_pair (doc, map, k, v);
			}
			return map;
		}

		case BYML_T_HASHMAP32:
		case BYML_T_RELOC_HASHMAP32:
		{
			int map = yaml_document_add_mapping (doc, (yaml_char_t *)"!h32", YAML_BLOCK_MAPPING_STYLE);
			for (uint i = 0; i < n->u.map.count; i++)
			{
				snprintf (buf, sizeof (buf), "%u", n->u.map.entries[i].hash32);
				int k = byml_yaml_scalar (doc, YAML_INT_TAG, buf);
				int v = byml_node_to_yaml_doc (doc, n->u.map.entries[i].val);
				yaml_document_append_mapping_pair (doc, map, k, v);
			}
			return map;
		}

		case BYML_T_HASHMAP64:
		case BYML_T_RELOC_HASHMAP64:
		{
			int map = yaml_document_add_mapping (doc, (yaml_char_t *)"!h64", YAML_BLOCK_MAPPING_STYLE);
			for (uint i = 0; i < n->u.map.count; i++)
			{
				snprintf (buf, sizeof (buf), "%llu", (unsigned long long)n->u.map.entries[i].hash64);
				int k = byml_yaml_scalar (doc, YAML_INT_TAG, buf);
				int v = byml_node_to_yaml_doc (doc, n->u.map.entries[i].val);
				yaml_document_append_mapping_pair (doc, map, k, v);
			}
			return map;
		}

		default:
			return byml_yaml_string (doc, "<unknown>");
	}
}

///////////////////////////////////////////////////////////////////////////////
// DecodeBYML_YAML
///////////////////////////////////////////////////////////////////////////////

enumError DecodeBYML_YAML (FILE *out, const u8 *data, size_t size)
{
	if (!out || !data || size < 16)
		return ERR_INVALID_DATA;
	bool is_le = false;
	if (!memcmp (data, "YB", 2))
		is_le = true;
	else if (!memcmp (data, "BY", 2))
		is_le = false;
	else
		return ERR_INVALID_DATA;

	u16 version = byml_u16 (data + 2, is_le);
	if (version < 1 || version > 7)
		return ERR_INVALID_DATA;

	u32 hash_key_table_off = byml_u32 (data + 4, is_le);
	u32 str_table_off = byml_u32 (data + 8, is_le);
	u32 path_table_off = 0;
	u32 root_node_off = byml_u32 (data + 12, is_le);
	bool supports_paths = false;

	if (version == 1 && size >= 20)
	{
		u32 third = byml_u32 (data + 12, is_le);
		u32 fourth = byml_u32 (data + 16, is_le);
		if ((third == 0 || (third + 4 <= size && data[third] == BYML_T_PATH_ARRAY))
			&& fourth + 4 <= size && (data[fourth] == BYML_T_ARRAY || data[fourth] == BYML_T_MAP
				|| data[fourth] == BYML_T_HASHMAP32 || data[fourth] == BYML_T_HASHMAP64))
		{
			supports_paths = true;
			path_table_off = third;
			root_node_off = fourth;
		}
	}

	byml_ctx_t ctx = { 0 };
	ctx.data = data;
	ctx.size = size;
	ctx.is_le = is_le;
	ctx.version = version;
	ctx.supports_paths = supports_paths;

	enumError err = byml_parse_str_table (&ctx, hash_key_table_off, &ctx.hash_keys, &ctx.n_hash_keys);
	if (err) return err;
	err = byml_parse_str_table (&ctx, str_table_off, &ctx.strings, &ctx.n_strings);
	if (err)
	{
		FREE (ctx.hash_keys);
		return err;
	}
	if (supports_paths && path_table_off)
		byml_parse_path_table (&ctx, path_table_off);

	byml_node_t *root_tree = NULL;
	if (root_node_off < size)
		root_tree = byml_parse_binary_node (&ctx, data[root_node_off], root_node_off, 0);

	yaml_document_t document;
	if (!yaml_document_initialize (&document, 0, 0, 0, 1, 1))
		err = ERR_OUT_OF_MEMORY;
	else
	{
		int root_id = byml_node_to_yaml_doc (&document, root_tree);
		if (!root_id)
		{
			yaml_document_delete (&document);
			err = ERR_OUT_OF_MEMORY;
		}
		else
		{
			yaml_emitter_t emitter;
			if (!yaml_emitter_initialize (&emitter))
			{
				yaml_document_delete (&document);
				err = ERR_OUT_OF_MEMORY;
			}
			else
			{
				yaml_emitter_set_output_file (&emitter, out);
				if (!yaml_emitter_dump (&emitter, &document))
					err = ERR_WRITE_FAILED;
				yaml_emitter_delete (&emitter);
			}
		}
	}

	byml_node_free (root_tree);
	FREE (ctx.hash_keys);
	FREE (ctx.strings);
	if (ctx.paths)
	{
		for (uint i = 0; i < ctx.n_paths; i++)
			FREE (ctx.paths[i].points);
		FREE (ctx.paths);
	}
	return err;
}

///////////////////////////////////////////////////////////////////////////////
// XML Generation (Matching ByamlXmlConverter)
///////////////////////////////////////////////////////////////////////////////

static void format_single_val (const byml_node_t *n, char *buf, size_t buf_sz)
{
	switch (n->type)
	{
		case BYML_T_NULL: snprintf (buf, buf_sz, "null"); break;
		case BYML_T_BOOL: snprintf (buf, buf_sz, "%s", n->u.b ? "true" : "false"); break;
		case BYML_T_INT: snprintf (buf, buf_sz, "%d", n->u.i); break;
		case BYML_T_UINT: snprintf (buf, buf_sz, "%uu", n->u.u); break;
		case BYML_T_INT64: snprintf (buf, buf_sz, "%lldi64", (long long)n->u.i64); break;
		case BYML_T_UINT64: snprintf (buf, buf_sz, "%lluu64", (unsigned long long)n->u.u64); break;
		case BYML_T_FLOAT:
			snprintf (buf, buf_sz, "%.8gf", (double)n->u.f);
			break;
		case BYML_T_DOUBLE:
			snprintf (buf, buf_sz, "%.16gd", n->u.d);
			break;
		default: snprintf (buf, buf_sz, ""); break;
	}
}

static bool is_primitive_node (const byml_node_t *n)
{
	return n && (n->type == BYML_T_NULL || n->type == BYML_T_BOOL || n->type == BYML_T_INT
		|| n->type == BYML_T_UINT || n->type == BYML_T_INT64 || n->type == BYML_T_UINT64
		|| n->type == BYML_T_FLOAT || n->type == BYML_T_DOUBLE);
}

static void byml_node_to_xml_elem (mxml_node_t *parent, ccp name, const byml_node_t *n, bool is_arr_elem)
{
	if (!n)
		return;

	char val_buf[128];
	if (is_primitive_node (n))
	{
		format_single_val (n, val_buf, sizeof (val_buf));
		if (is_arr_elem)
		{
			mxml_node_t *val_el = mxmlNewElement (parent, name ? name : "value");
			mxmlNewText (val_el, 0, val_buf);
		}
		else
		{
			mxmlElementSetAttr (parent, name, val_buf);
		}
		return;
	}

	mxml_node_t *el = mxmlNewElement (parent, name ? name : "value");

	switch (n->type)
	{
		case BYML_T_STRING:
			mxmlElementSetAttr (el, "type", "string");
			mxmlNewText (el, 0, n->u.s ? n->u.s : "");
			break;

		case BYML_T_BINARY:
		case BYML_T_BINARY_ALIGNED:
		{
			mxmlElementSetAttr (el, "type", "binary");
			if (n->type == BYML_T_BINARY_ALIGNED)
			{
				snprintf (val_buf, sizeof (val_buf), "%u", n->u.bin.align);
				mxmlElementSetAttr (el, "alignment", val_buf);
			}
			char *b64 = byml_b64_encode (n->u.bin.data, n->u.bin.size);
			if (b64)
			{
				mxmlNewText (el, 0, b64);
				FREE (b64);
			}
			break;
		}

		case BYML_T_PATH_ARRAY:
		{
			mxmlElementSetAttr (el, "type", "path");
			for (uint i = 0; i < n->u.path.count; i++)
			{
				const byml_point_t *pt = &n->u.path.points[i];
				mxml_node_t *pt_el = mxmlNewElement (el, "point");

				#define SET_PT_ATTR(attr, fval) do { \
					snprintf (val_buf, sizeof (val_buf), "%.8gf", (double)(fval)); \
					mxmlElementSetAttr (pt_el, attr, val_buf); \
				} while (0)

				SET_PT_ATTR ("x", pt->x);
				SET_PT_ATTR ("y", pt->y);
				SET_PT_ATTR ("z", pt->z);
				SET_PT_ATTR ("nx", pt->nx);
				SET_PT_ATTR ("ny", pt->ny);
				SET_PT_ATTR ("nz", pt->nz);
				snprintf (val_buf, sizeof (val_buf), "%uu", pt->val);
				mxmlElementSetAttr (pt_el, "val", val_buf);
			}
			break;
		}

		case BYML_T_ARRAY:
		{
			mxmlElementSetAttr (el, "type", "array");
			for (uint i = 0; i < n->u.arr.count; i++)
				byml_node_to_xml_elem (el, "value", n->u.arr.items[i], true);
			break;
		}

		case BYML_T_MAP:
		{
			for (uint i = 0; i < n->u.map.count; i++)
				byml_node_to_xml_elem (el, n->u.map.entries[i].key, n->u.map.entries[i].val, false);
			break;
		}

		case BYML_T_HASHMAP32:
		case BYML_T_RELOC_HASHMAP32:
		{
			mxmlElementSetAttr (el, "type", "hash32");
			for (uint i = 0; i < n->u.map.count; i++)
			{
				snprintf (val_buf, sizeof (val_buf), "h_%08x", n->u.map.entries[i].hash32);
				byml_node_to_xml_elem (el, val_buf, n->u.map.entries[i].val, false);
			}
			break;
		}

		case BYML_T_HASHMAP64:
		case BYML_T_RELOC_HASHMAP64:
		{
			mxmlElementSetAttr (el, "type", "hash64");
			for (uint i = 0; i < n->u.map.count; i++)
			{
				snprintf (val_buf, sizeof (val_buf), "h_%016llx", (unsigned long long)n->u.map.entries[i].hash64);
				byml_node_to_xml_elem (el, val_buf, n->u.map.entries[i].val, false);
			}
			break;
		}

		default:
			break;
	}
}

enumError DecodeBYML_XML (FILE *out, const u8 *data, size_t size)
{
	if (!out || !data || size < 16)
		return ERR_INVALID_DATA;
	bool is_le = false;
	if (!memcmp (data, "YB", 2))
		is_le = true;
	else if (!memcmp (data, "BY", 2))
		is_le = false;
	else
		return ERR_INVALID_DATA;

	u16 version = byml_u16 (data + 2, is_le);
	if (version < 1 || version > 7)
		return ERR_INVALID_DATA;

	u32 hash_key_table_off = byml_u32 (data + 4, is_le);
	u32 str_table_off = byml_u32 (data + 8, is_le);
	u32 path_table_off = 0;
	u32 root_node_off = byml_u32 (data + 12, is_le);
	bool supports_paths = false;

	if (version == 1 && size >= 20)
	{
		u32 third = byml_u32 (data + 12, is_le);
		u32 fourth = byml_u32 (data + 16, is_le);
		if ((third == 0 || (third + 4 <= size && data[third] == BYML_T_PATH_ARRAY))
			&& fourth + 4 <= size && (data[fourth] == BYML_T_ARRAY || data[fourth] == BYML_T_MAP
				|| data[fourth] == BYML_T_HASHMAP32 || data[fourth] == BYML_T_HASHMAP64))
		{
			supports_paths = true;
			path_table_off = third;
			root_node_off = fourth;
		}
	}

	byml_ctx_t ctx = { 0 };
	ctx.data = data;
	ctx.size = size;
	ctx.is_le = is_le;
	ctx.version = version;
	ctx.supports_paths = supports_paths;

	enumError err = byml_parse_str_table (&ctx, hash_key_table_off, &ctx.hash_keys, &ctx.n_hash_keys);
	if (err) return err;
	err = byml_parse_str_table (&ctx, str_table_off, &ctx.strings, &ctx.n_strings);
	if (err)
	{
		FREE (ctx.hash_keys);
		return err;
	}
	if (supports_paths && path_table_off)
		byml_parse_path_table (&ctx, path_table_off);

	byml_node_t *root_tree = NULL;
	if (root_node_off < size)
		root_tree = byml_parse_binary_node (&ctx, data[root_node_off], root_node_off, 0);

	mxml_node_t *xml_doc = mxmlNewXML ("1.0");
	mxml_node_t *yaml_el = mxmlNewElement (xml_doc, "yaml");
	mxmlElementSetAttr (yaml_el, "xmlns:yamlconv", "yamlconv");
	mxmlElementSetAttr (yaml_el, "yamlconv:endianness", is_le ? "little" : "big");
	char num_buf[32];
	snprintf (num_buf, sizeof (num_buf), "%u", supports_paths ? 4 : 3);
	mxmlElementSetAttr (yaml_el, "yamlconv:offsetCount", num_buf);
	snprintf (num_buf, sizeof (num_buf), "%u", version);
	mxmlElementSetAttr (yaml_el, "yamlconv:byamlVersion", num_buf);

	if (root_tree)
	{
		if (root_tree->type == BYML_T_MAP)
		{
			for (uint i = 0; i < root_tree->u.map.count; i++)
				byml_node_to_xml_elem (yaml_el, root_tree->u.map.entries[i].key,
					root_tree->u.map.entries[i].val, false);
		}
		else if (root_tree->type == BYML_T_ARRAY)
		{
			mxmlElementSetAttr (yaml_el, "type", "array");
			for (uint i = 0; i < root_tree->u.arr.count; i++)
				byml_node_to_xml_elem (yaml_el, "value", root_tree->u.arr.items[i], true);
		}
		else
		{
			byml_node_to_xml_elem (yaml_el, "root", root_tree, false);
		}
	}

	char *xml_str = mxmlSaveAllocString (xml_doc, MXML_NO_CALLBACK);
	if (xml_str)
	{
		fputs (xml_str, out);
		fputc (\x27\n\x27, out);
		FREE (xml_str);
	}
	mxmlDelete (xml_doc);

	byml_node_free (root_tree);
	FREE (ctx.hash_keys);
	FREE (ctx.strings);
	if (ctx.paths)
	{
		for (uint i = 0; i < ctx.n_paths; i++)
			FREE (ctx.paths[i].points);
		FREE (ctx.paths);
	}
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
// JSON Generation
///////////////////////////////////////////////////////////////////////////////

static void byml_node_to_json_file (FILE *out, const byml_node_t *n, int indent)
{
	if (!n)
	{
		fputs ("null", out);
		return;
	}

	switch (n->type)
	{
		case BYML_T_NULL: fputs ("null", out); break;
		case BYML_T_BOOL: fputs (n->u.b ? "true" : "false", out); break;
		case BYML_T_INT: fprintf (out, "%d", n->u.i); break;
		case BYML_T_UINT: fprintf (out, "%u", n->u.u); break;
		case BYML_T_INT64: fprintf (out, "%lld", (long long)n->u.i64); break;
		case BYML_T_UINT64: fprintf (out, "%llu", (unsigned long long)n->u.u64); break;
		case BYML_T_FLOAT:
		{
			if (isnan (n->u.f)) fputs ("\"NaN\"", out);
			else if (isinf (n->u.f)) fputs (n->u.f < 0 ? "\"-Infinity\"" : "\"Infinity\"", out);
			else fprintf (out, "%.8g", (double)n->u.f);
			break;
		}
		case BYML_T_DOUBLE:
		{
			if (isnan (n->u.d)) fputs ("\"NaN\"", out);
			else if (isinf (n->u.d)) fputs (n->u.d < 0 ? "\"-Infinity\"" : "\"Infinity\"", out);
			else fprintf (out, "%.16g", n->u.d);
			break;
		}
		case BYML_T_STRING:
		{
			fputc (\x27"\x27, out);
			for (const char *p = n->u.s ? n->u.s : ""; *p; p++)
			{
				if (*p == \x27"\x27) fputs ("\\\"", out);
				else if (*p == \x27\\\\\x27) fputs ("\\\\\\\\", out);
				else if (*p == \x27\\n\x27) fputs ("\\n", out);
				else if (*p == \x27\\r\x27) fputs ("\\r", out);
				else if (*p == \x27\\t\x27) fputs ("\\t", out);
				else fputc (*p, out);
			}
			fputc (\x27"\x27, out);
			break;
		}
		case BYML_T_BINARY:
		case BYML_T_BINARY_ALIGNED:
		{
			char *b64 = byml_b64_encode (n->u.bin.data, n->u.bin.size);
			fprintf (out, "\"%s\"", b64 ? b64 : "");
			FREE (b64);
			break;
		}
		case BYML_T_PATH_ARRAY:
		{
			fputs ("[\n", out);
			for (uint i = 0; i < n->u.path.count; i++)
			{
				const byml_point_t *pt = &n->u.path.points[i];
				for (int s = 0; s < indent + 2; s++) fputc (\x27 \x27, out);
				fprintf (out, "{\"x\": %.8g, \"y\": %.8g, \"z\": %.8g, \"nx\": %.8g, \"ny\": %.8g, \"nz\": %.8g, \"val\": %u}%s\n",
					(double)pt->x, (double)pt->y, (double)pt->z, (double)pt->nx, (double)pt->ny, (double)pt->nz, pt->val,
					i + 1 < n->u.path.count ? "," : "");
			}
			for (int s = 0; s < indent; s++) fputc (\x27 \x27, out);
			fputc (\x27]\x27, out);
			break;
		}
		case BYML_T_ARRAY:
		{
			if (!n->u.arr.count)
			{
				fputs ("[]", out);
				break;
			}
			fputs ("[\n", out);
			for (uint i = 0; i < n->u.arr.count; i++)
			{
				for (int s = 0; s < indent + 2; s++) fputc (\x27 \x27, out);
				byml_node_to_json_file (out, n->u.arr.items[i], indent + 2);
				if (i + 1 < n->u.arr.count) fputc (\x27,\x27, out);
				fputc (\x27\\n\x27, out);
			}
			for (int s = 0; s < indent; s++) fputc (\x27 \x27, out);
			fputc (\x27]\x27, out);
			break;
		}
		case BYML_T_MAP:
		case BYML_T_HASHMAP32:
		case BYML_T_HASHMAP64:
		{
			if (!n->u.map.count)
			{
				fputs ("{}", out);
				break;
			}
			fputs ("{\n", out);
			for (uint i = 0; i < n->u.map.count; i++)
			{
				for (int s = 0; s < indent + 2; s++) fputc (\x27 \x27, out);
				if (n->type == BYML_T_MAP)
					fprintf (out, "\"%s\": ", n->u.map.entries[i].key);
				else if (n->type == BYML_T_HASHMAP32)
					fprintf (out, "\"%u\": ", n->u.map.entries[i].hash32);
				else
					fprintf (out, "\"%llu\": ", (unsigned long long)n->u.map.entries[i].hash64);
				byml_node_to_json_file (out, n->u.map.entries[i].val, indent + 2);
				if (i + 1 < n->u.map.count) fputc (\x27,\x27, out);
				fputc (\x27\\n\x27, out);
			}
			for (int s = 0; s < indent; s++) fputc (\x27 \x27, out);
			fputc (\x27}\x27, out);
			break;
		}
		default:
			fputs ("\"<unknown>\"", out);
			break;
	}
}

enumError DecodeBYML_JSON (FILE *out, const u8 *data, size_t size)
{
	if (!out || !data || size < 16)
		return ERR_INVALID_DATA;
	bool is_le = false;
	if (!memcmp (data, "YB", 2))
		is_le = true;
	else if (!memcmp (data, "BY", 2))
		is_le = false;
	else
		return ERR_INVALID_DATA;

	u16 version = byml_u16 (data + 2, is_le);
	if (version < 1 || version > 7)
		return ERR_INVALID_DATA;

	u32 hash_key_table_off = byml_u32 (data + 4, is_le);
	u32 str_table_off = byml_u32 (data + 8, is_le);
	u32 path_table_off = 0;
	u32 root_node_off = byml_u32 (data + 12, is_le);
	bool supports_paths = false;

	if (version == 1 && size >= 20)
	{
		u32 third = byml_u32 (data + 12, is_le);
		u32 fourth = byml_u32 (data + 16, is_le);
		if ((third == 0 || (third + 4 <= size && data[third] == BYML_T_PATH_ARRAY))
			&& fourth + 4 <= size && (data[fourth] == BYML_T_ARRAY || data[fourth] == BYML_T_MAP
				|| data[fourth] == BYML_T_HASHMAP32 || data[fourth] == BYML_T_HASHMAP64))
		{
			supports_paths = true;
			path_table_off = third;
			root_node_off = fourth;
		}
	}

	byml_ctx_t ctx = { 0 };
	ctx.data = data;
	ctx.size = size;
	ctx.is_le = is_le;
	ctx.version = version;
	ctx.supports_paths = supports_paths;

	enumError err = byml_parse_str_table (&ctx, hash_key_table_off, &ctx.hash_keys, &ctx.n_hash_keys);
	if (err) return err;
	err = byml_parse_str_table (&ctx, str_table_off, &ctx.strings, &ctx.n_strings);
	if (err)
	{
		FREE (ctx.hash_keys);
		return err;
	}
	if (supports_paths && path_table_off)
		byml_parse_path_table (&ctx, path_table_off);

	byml_node_t *root_tree = NULL;
	if (root_node_off < size)
		root_tree = byml_parse_binary_node (&ctx, data[root_node_off], root_node_off, 0);

	byml_node_to_json_file (out, root_tree, 0);
	fputc (\x27\n\x27, out);

	byml_node_free (root_tree);
	FREE (ctx.hash_keys);
	FREE (ctx.strings);
	if (ctx.paths)
	{
		for (uint i = 0; i < ctx.n_paths; i++)
			FREE (ctx.paths[i].points);
		FREE (ctx.paths);
	}
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
// BYML Binary Writing
///////////////////////////////////////////////////////////////////////////////

typedef struct str_list_t
{
	char **items;
	uint count;
	uint cap;
} str_list_t;

static void str_list_init (str_list_t *l)
{
	l->items = 0;
	l->count = 0;
	l->cap = 0;
}

static void str_list_free (str_list_t *l)
{
	if (l->items)
	{
		for (uint i = 0; i < l->count; i++)
			FREE (l->items[i]);
		FREE (l->items);
	}
	memset (l, 0, sizeof (*l));
}

static int str_list_find (const str_list_t *l, const char *s)
{
	for (uint i = 0; i < l->count; i++)
		if (!strcmp (l->items[i], s))
			return (int)i;
	return -1;
}

static int str_list_add (str_list_t *l, const char *s)
{
	if (!s) s = "";
	int idx = str_list_find (l, s);
	if (idx >= 0)
		return idx;
	if (l->count >= l->cap)
	{
		l->cap = l->cap ? l->cap * 2 : 16;
		l->items = REALLOC (l->items, l->cap * sizeof (char *));
	}
	l->items[l->count] = STRDUP (s);
	return (int)l->count++;
}

static int str_cmp_qsort (const void *a, const void *b)
{
	const char *const *sa = a;
	const char *const *sb = b;
	return strcmp (*sa, *sb);
}

typedef struct byml_writer_t
{
	u8 *buf;
	uint len;
	uint cap;
	bool is_le;
} byml_writer_t;

static void bw_init (byml_writer_t *w, bool is_le)
{
	w->cap = 1024;
	w->buf = CALLOC (1, w->cap);
	w->len = 0;
	w->is_le = is_le;
}

static void bw_align (byml_writer_t *w, uint alignment)
{
	if (!alignment) alignment = 4;
	uint rem = w->len % alignment;
	if (rem)
	{
		uint pad = alignment - rem;
		while (w->len + pad > w->cap)
		{
			w->cap *= 2;
			w->buf = REALLOC (w->buf, w->cap);
		}
		memset (w->buf + w->len, 0, pad);
		w->len += pad;
	}
}

static void bw_append (byml_writer_t *w, const void *data, uint size)
{
	while (w->len + size > w->cap)
	{
		w->cap *= 2;
		w->buf = REALLOC (w->buf, w->cap);
	}
	if (data)
		memcpy (w->buf + w->len, data, size);
	else
		memset (w->buf + w->len, 0, size);
	w->len += size;
}

static void bw_u8 (byml_writer_t *w, u8 v)
{
	bw_append (w, &v, 1);
}

static void bw_u24 (byml_writer_t *w, u32 v)
{
	u8 b[4];
	if (w->is_le)
	{
		wr_le32 (b, v);
		bw_append (w, b, 3);
	}
	else
	{
		wr_be32 (b, v);
		bw_append (w, b + 1, 3);
	}
}

static void bw_put_u32 (byml_writer_t *w, uint pos, u32 v)
{
	if (pos + 4 <= w->len)
	{
		if (w->is_le)
			wr_le32 (w->buf + pos, v);
		else
			wr_be32 (w->buf + pos, v);
	}
}

static uint write_byml_str_table (byml_writer_t *w, const str_list_t *list)
{
	if (!list || !list->count)
		return 0;
	bw_align (w, 4);
	uint start = w->len;
	bw_u8 (w, BYML_T_STRING_TABLE);
	bw_u24 (w, list->count);
	uint offsets_pos = w->len;
	bw_append (w, 0, (list->count + 1) * 4);

	for (uint i = 0; i < list->count; i++)
	{
		uint str_off = w->len - start;
		bw_put_u32 (w, offsets_pos + i * 4, str_off);
		const char *s = list->items[i];
		bw_append (w, s, (uint)strlen (s) + 1);
	}
	bw_put_u32 (w, offsets_pos + list->count * 4, w->len - start);
	bw_align (w, 4);
	return start;
}

typedef struct path_list_t
{
	const byml_node_t **paths;
	uint count;
	uint cap;
} path_list_t;

static int path_list_add (path_list_t *pl, const byml_node_t *path)
{
	if (pl->count >= pl->cap)
	{
		pl->cap = pl->cap ? pl->cap * 2 : 8;
		pl->paths = REALLOC (pl->paths, pl->cap * sizeof (byml_node_t *));
	}
	int idx = (int)pl->count++;
	pl->paths[idx] = path;
	return idx;
}

static uint write_byml_path_table (byml_writer_t *w, const path_list_t *pl)
{
	if (!pl || !pl->count)
		return 0;
	bw_align (w, 4);
	uint start = w->len;
	bw_u8 (w, BYML_T_PATH_ARRAY);
	bw_u24 (w, pl->count);
	uint offsets_pos = w->len;
	bw_append (w, 0, (pl->count + 1) * 4);

	for (uint i = 0; i < pl->count; i++)
	{
		uint cur_off = w->len - start;
		bw_put_u32 (w, offsets_pos + i * 4, cur_off);
		const byml_node_t *pn = pl->paths[i];
		for (uint j = 0; j < pn->u.path.count; j++)
		{
			const byml_point_t *pt = &pn->u.path.points[j];
			u8 pbuf[28];
			#define WR_F(buf_off, val) do { \
				float f = (val); \
				u32 uv; memcpy (&uv, &f, 4); \
				if (w->is_le) wr_le32 (pbuf + (buf_off), uv); \
				else wr_be32 (pbuf + (buf_off), uv); \
			} while (0)

			WR_F (0, pt->x);
			WR_F (4, pt->y);
			WR_F (8, pt->z);
			WR_F (12, pt->nx);
			WR_F (16, pt->ny);
			WR_F (20, pt->nz);
			if (w->is_le) wr_le32 (pbuf + 24, pt->val);
			else wr_be32 (pbuf + 24, pt->val);
			bw_append (w, pbuf, 28);
		}
	}
	bw_put_u32 (w, offsets_pos + pl->count * 4, w->len - start);
	bw_align (w, 4);
	return start;
}

static void collect_symbols (
	const byml_node_t *n, str_list_t *keys, str_list_t *strs, path_list_t *paths)
{
	if (!n)
		return;
	switch (n->type)
	{
		case BYML_T_STRING:
			str_list_add (strs, n->u.s);
			break;
		case BYML_T_PATH_ARRAY:
			path_list_add (paths, n);
			break;
		case BYML_T_ARRAY:
			for (uint i = 0; i < n->u.arr.count; i++)
				collect_symbols (n->u.arr.items[i], keys, strs, paths);
			break;
		case BYML_T_MAP:
			for (uint i = 0; i < n->u.map.count; i++)
			{
				str_list_add (keys, n->u.map.entries[i].key);
				collect_symbols (n->u.map.entries[i].val, keys, strs, paths);
			}
			break;
		case BYML_T_HASHMAP32:
		case BYML_T_HASHMAP64:
		case BYML_T_RELOC_HASHMAP32:
		case BYML_T_RELOC_HASHMAP64:
			for (uint i = 0; i < n->u.map.count; i++)
				collect_symbols (n->u.map.entries[i].val, keys, strs, paths);
			break;
		default:
			break;
	}
}

typedef struct map_sort_t
{
	uint key_idx;
	uint orig_idx;
	u32 hash32;
	u64 hash64;
} map_sort_t;

static int map_sort_cmp_key (const void *a, const void *b)
{
	const map_sort_t *ea = a, *eb = b;
	return (ea->key_idx > eb->key_idx) - (ea->key_idx < eb->key_idx);
}

static int map_sort_cmp_h32 (const void *a, const void *b)
{
	const map_sort_t *ea = a, *eb = b;
	return (ea->hash32 > eb->hash32) - (ea->hash32 < eb->hash32);
}

static int map_sort_cmp_h64 (const void *a, const void *b)
{
	const map_sort_t *ea = a, *eb = b;
	return (ea->hash64 > eb->hash64) - (ea->hash64 < eb->hash64);
}

static uint write_byml_node_data (
	byml_writer_t *w, const byml_node_t *n,
	const str_list_t *keys, const str_list_t *strs, const path_list_t *paths,
	u8 *out_type, u32 *out_val)
{
	if (!n)
	{
		*out_type = BYML_T_NULL;
		*out_val = 0;
		return 0;
	}

	switch (n->type)
	{
		case BYML_T_NULL:
			*out_type = BYML_T_NULL;
			*out_val = 0;
			return 0;

		case BYML_T_BOOL:
			*out_type = BYML_T_BOOL;
			*out_val = n->u.b ? 1 : 0;
			return 0;

		case BYML_T_INT:
			*out_type = BYML_T_INT;
			*out_val = (u32)n->u.i;
			return 0;

		case BYML_T_FLOAT:
			*out_type = BYML_T_FLOAT;
			memcpy (out_val, &n->u.f, 4);
			return 0;

		case BYML_T_UINT:
			*out_type = BYML_T_UINT;
			*out_val = n->u.u;
			return 0;

		case BYML_T_STRING:
			*out_type = BYML_T_STRING;
			*out_val = (u32)str_list_find (strs, n->u.s ? n->u.s : "");
			return 0;

		case BYML_T_PATH_ARRAY:
		{
			*out_type = BYML_T_BINARY; // In path mode, 0xA1 represents path index
			for (uint i = 0; i < paths->count; i++)
			{
				if (paths->paths[i] == n)
				{
					*out_val = i;
					return 0;
				}
			}
			*out_val = 0;
			return 0;
		}

		case BYML_T_INT64:
		{
			bw_align (w, 4);
			uint start = w->len;
			u8 b[8];
			if (w->is_le) byml_wr_le64 (b, (u64)n->u.i64);
			else byml_wr_be64 (b, (u64)n->u.i64);
			bw_append (w, b, 8);
			*out_type = BYML_T_INT64;
			*out_val = start;
			return start;
		}

		case BYML_T_UINT64:
		{
			bw_align (w, 4);
			uint start = w->len;
			u8 b[8];
			if (w->is_le) byml_wr_le64 (b, n->u.u64);
			else byml_wr_be64 (b, n->u.u64);
			bw_append (w, b, 8);
			*out_type = BYML_T_UINT64;
			*out_val = start;
			return start;
		}

		case BYML_T_DOUBLE:
		{
			bw_align (w, 4);
			uint start = w->len;
			u64 uv;
			memcpy (&uv, &n->u.d, 8);
			u8 b[8];
			if (w->is_le) byml_wr_le64 (b, uv);
			else byml_wr_be64 (b, uv);
			bw_append (w, b, 8);
			*out_type = BYML_T_DOUBLE;
			*out_val = start;
			return start;
		}

		case BYML_T_BINARY:
		{
			bw_align (w, 4);
			uint start = w->len;
			u8 b[4];
			if (w->is_le) wr_le32 (b, n->u.bin.size);
			else wr_be32 (b, n->u.bin.size);
			bw_append (w, b, 4);
			if (n->u.bin.size)
				bw_append (w, n->u.bin.data, n->u.bin.size);
			bw_align (w, 4);
			*out_type = BYML_T_BINARY;
			*out_val = start;
			return start;
		}

		case BYML_T_BINARY_ALIGNED:
		{
			uint align = n->u.bin.align ? n->u.bin.align : 16;
			bw_align (w, 4);
			uint start = w->len;
			u8 b[8];
			if (w->is_le)
			{
				wr_le32 (b, n->u.bin.size);
				wr_le32 (b + 4, align);
			}
			else
			{
				wr_be32 (b, n->u.bin.size);
				wr_be32 (b + 4, align);
			}
			bw_append (w, b, 8);
			bw_align (w, align);
			if (n->u.bin.size)
				bw_append (w, n->u.bin.data, n->u.bin.size);
			bw_align (w, 4);
			*out_type = BYML_T_BINARY_ALIGNED;
			*out_val = start;
			return start;
		}

		case BYML_T_ARRAY:
		{
			bw_align (w, 4);
			uint start = w->len;
			uint count = n->u.arr.count;
			bw_u8 (w, BYML_T_ARRAY);
			bw_u24 (w, count);
			uint tags_pos = w->len;
			bw_append (w, 0, count);
			bw_align (w, 4);
			uint vals_pos = w->len;
			bw_append (w, 0, count * 4);

			for (uint i = 0; i < count; i++)
			{
				u8 c_type = BYML_T_NULL;
				u32 c_val = 0;
				write_byml_node_data (w, n->u.arr.items[i], keys, strs, paths, &c_type, &c_val);
				w->buf[tags_pos + i] = c_type;
				bw_put_u32 (w, vals_pos + i * 4, c_val);
			}
			*out_type = BYML_T_ARRAY;
			*out_val = start;
			return start;
		}

		case BYML_T_MAP:
		{
			bw_align (w, 4);
			uint start = w->len;
			uint count = n->u.map.count;
			bw_u8 (w, BYML_T_MAP);
			bw_u24 (w, count);

			map_sort_t *st = CALLOC (count, sizeof (map_sort_t));
			for (uint i = 0; i < count; i++)
			{
				st[i].orig_idx = i;
				st[i].key_idx = (uint)str_list_find (keys, n->u.map.entries[i].key ? n->u.map.entries[i].key : "");
			}
			if (count > 1)
				qsort (st, count, sizeof (map_sort_t), map_sort_cmp_key);

			uint entries_pos = w->len;
			bw_append (w, 0, count * 8);

			for (uint i = 0; i < count; i++)
			{
				uint oi = st[i].orig_idx;
				uint ki = st[i].key_idx;
				u8 c_type = BYML_T_NULL;
				u32 c_val = 0;
				write_byml_node_data (w, n->u.map.entries[oi].val, keys, strs, paths, &c_type, &c_val);

				uint epos = entries_pos + i * 8;
				u8 b[4];
				if (w->is_le)
				{
					wr_le32 (b, ki);
					w->buf[epos] = b[0];
					w->buf[epos + 1] = b[1];
					w->buf[epos + 2] = b[2];
					w->buf[epos + 3] = c_type;
					wr_le32 (w->buf + epos + 4, c_val);
				}
				else
				{
					wr_be32 (b, ki);
					w->buf[epos] = b[1];
					w->buf[epos + 1] = b[2];
					w->buf[epos + 2] = b[3];
					w->buf[epos + 3] = c_type;
					wr_be32 (w->buf + epos + 4, c_val);
				}
			}
			FREE (st);
			*out_type = BYML_T_MAP;
			*out_val = start;
			return start;
		}

		case BYML_T_HASHMAP32:
		case BYML_T_RELOC_HASHMAP32:
		{
			bw_align (w, 4);
			uint start = w->len;
			uint count = n->u.map.count;
			bw_u8 (w, BYML_T_HASHMAP32);
			bw_u24 (w, count);

			map_sort_t *st = CALLOC (count, sizeof (map_sort_t));
			for (uint i = 0; i < count; i++)
			{
				st[i].orig_idx = i;
				st[i].hash32 = n->u.map.entries[i].hash32;
			}
			if (count > 1)
				qsort (st, count, sizeof (map_sort_t), map_sort_cmp_h32);

			uint entries_pos = w->len;
			bw_append (w, 0, count * 8);
			uint types_pos = w->len;
			bw_append (w, 0, count);
			bw_align (w, 4);

			for (uint i = 0; i < count; i++)
			{
				uint oi = st[i].orig_idx;
				u8 c_type = BYML_T_NULL;
				u32 c_val = 0;
				write_byml_node_data (w, n->u.map.entries[oi].val, keys, strs, paths, &c_type, &c_val);

				uint epos = entries_pos + i * 8;
				if (w->is_le)
				{
					wr_le32 (w->buf + epos, st[i].hash32);
					wr_le32 (w->buf + epos + 4, c_val);
				}
				else
				{
					wr_be32 (w->buf + epos, st[i].hash32);
					wr_be32 (w->buf + epos + 4, c_val);
				}
				w->buf[types_pos + i] = c_type;
			}
			FREE (st);
			*out_type = BYML_T_HASHMAP32;
			*out_val = start;
			return start;
		}

		case BYML_T_HASHMAP64:
		case BYML_T_RELOC_HASHMAP64:
		{
			bw_align (w, 4);
			uint start = w->len;
			uint count = n->u.map.count;
			bw_u8 (w, BYML_T_HASHMAP64);
			bw_u24 (w, count);

			map_sort_t *st = CALLOC (count, sizeof (map_sort_t));
			for (uint i = 0; i < count; i++)
			{
				st[i].orig_idx = i;
				st[i].hash64 = n->u.map.entries[i].hash64;
			}
			if (count > 1)
				qsort (st, count, sizeof (map_sort_t), map_sort_cmp_h64);

			uint entries_pos = w->len;
			bw_append (w, 0, count * 12);
			uint types_pos = w->len;
			bw_append (w, 0, count);
			bw_align (w, 4);

			for (uint i = 0; i < count; i++)
			{
				uint oi = st[i].orig_idx;
				u8 c_type = BYML_T_NULL;
				u32 c_val = 0;
				write_byml_node_data (w, n->u.map.entries[oi].val, keys, strs, paths, &c_type, &c_val);

				uint epos = entries_pos + i * 12;
				if (w->is_le)
				{
					byml_wr_le64 (w->buf + epos, st[i].hash64);
					wr_le32 (w->buf + epos + 8, c_val);
				}
				else
				{
					byml_wr_be64 (w->buf + epos, st[i].hash64);
					wr_be32 (w->buf + epos + 8, c_val);
				}
				w->buf[types_pos + i] = c_type;
			}
			FREE (st);
			*out_type = BYML_T_HASHMAP64;
			*out_val = start;
			return start;
		}

		default:
			*out_type = BYML_T_NULL;
			*out_val = 0;
			return 0;
	}
}

static enumError byml_write_binary (
	const byml_node_t *root, u8 **dest, uint *dest_size, bool is_le, u16 version, bool support_paths)
{
	if (!dest || !dest_size)
		return ERR_SEMANTIC;
	*dest = 0;
	*dest_size = 0;

	str_list_t keys, strs;
	str_list_init (&keys);
	str_list_init (&strs);
	path_list_t paths = { 0 };

	collect_symbols (root, &keys, &strs, &paths);

	if (paths.count > 0)
		support_paths = true;

	if (keys.count > 1)
		qsort (keys.items, keys.count, sizeof (char *), str_cmp_qsort);
	if (strs.count > 1)
		qsort (strs.items, strs.count, sizeof (char *), str_cmp_qsort);

	byml_writer_t w;
	bw_init (&w, is_le);

	uint header_size = (support_paths && version == 1) ? 20 : 16;
	bw_append (&w, 0, header_size);

	uint key_table_off = write_byml_str_table (&w, &keys);
	uint str_table_off = write_byml_str_table (&w, &strs);
	uint path_table_off = support_paths ? write_byml_path_table (&w, &paths) : 0;

	u8 root_type = BYML_T_NULL;
	u32 root_val = 0;
	write_byml_node_data (&w, root, &keys, &strs, &paths, &root_type, &root_val);

	w.buf[0] = is_le ? \x27Y\x27 : \x27B\x27;
	w.buf[1] = is_le ? \x27B\x27 : \x27Y\x27;
	u16 out_ver = version ? version : (support_paths ? 1 : 2);

	if (is_le)
	{
		wr_le16 (w.buf + 2, out_ver);
		wr_le32 (w.buf + 4, key_table_off);
		wr_le32 (w.buf + 8, str_table_off);
		if (support_paths && out_ver == 1)
		{
			wr_le32 (w.buf + 12, path_table_off);
			wr_le32 (w.buf + 16, root_val);
		}
		else
		{
			wr_le32 (w.buf + 12, root_val);
		}
	}
	else
	{
		wr_be16 (w.buf + 2, out_ver);
		wr_be32 (w.buf + 4, key_table_off);
		wr_be32 (w.buf + 8, str_table_off);
		if (support_paths && out_ver == 1)
		{
			wr_be32 (w.buf + 12, path_table_off);
			wr_be32 (w.buf + 16, root_val);
		}
		else
		{
			wr_be32 (w.buf + 12, root_val);
		}
	}

	str_list_free (&keys);
	str_list_free (&strs);
	FREE (paths.paths);

	*dest = w.buf;
	*dest_size = w.len;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
// YAML Reading -> byml_node_t
///////////////////////////////////////////////////////////////////////////////

static byml_node_t *byml_node_from_yaml (yaml_document_t *doc, int node_id)
{
	yaml_node_t *node = yaml_document_get_node (doc, node_id);
	if (!node)
		return byml_node_new (BYML_T_NULL);

	ccp tag = (ccp)node->tag;

	if (node->type == YAML_SCALAR_NODE)
	{
		ccp val = (ccp)node->data.scalar.value;
		size_t len = node->data.scalar.length;

		if (tag)
		{
			if (!strcmp (tag, "!u") || !strcmp (tag, "!u32"))
			{
				byml_node_t *n = byml_node_new (BYML_T_UINT);
				n->u.u = (uint32_t)strtoul (val, NULL, 0);
				return n;
			}
			if (!strcmp (tag, "!l") || !strcmp (tag, "!s64"))
			{
				byml_node_t *n = byml_node_new (BYML_T_INT64);
				n->u.i64 = (int64_t)strtoll (val, NULL, 0);
				return n;
			}
			if (!strcmp (tag, "!ul") || !strcmp (tag, "!u64"))
			{
				byml_node_t *n = byml_node_new (BYML_T_UINT64);
				n->u.u64 = (uint64_t)strtoull (val, NULL, 0);
				return n;
			}
			if (!strcmp (tag, "!d") || !strcmp (tag, "!f64"))
			{
				byml_node_t *n = byml_node_new (BYML_T_DOUBLE);
				if (!strcasecmp (val, ".nan") || !strcasecmp (val, "nan")) n->u.d = 0.0 / 0.0;
				else if (!strcasecmp (val, ".inf") || !strcasecmp (val, "inf")) n->u.d = 1.0 / 0.0;
				else if (!strcasecmp (val, "-.inf") || !strcasecmp (val, "-inf")) n->u.d = -1.0 / 0.0;
				else n->u.d = strtod (val, NULL);
				return n;
			}
			if (!strcmp (tag, "!f") || !strcmp (tag, "!f32"))
			{
				byml_node_t *n = byml_node_new (BYML_T_FLOAT);
				if (!strcasecmp (val, ".nan") || !strcasecmp (val, "nan")) n->u.f = 0.0f / 0.0f;
				else if (!strcasecmp (val, ".inf") || !strcasecmp (val, "inf")) n->u.f = 1.0f / 0.0f;
				else if (!strcasecmp (val, "-.inf") || !strcasecmp (val, "-inf")) n->u.f = -1.0f / 0.0f;
				else n->u.f = (float)strtod (val, NULL);
				return n;
			}
			if (!strcmp (tag, "tag:yaml.org,2002:binary") || !strcmp (tag, "!binary") || !strcmp (tag, "!!binary"))
			{
				byml_node_t *n = byml_node_new (BYML_T_BINARY);
				size_t sz = 0;
				n->u.bin.data = byml_b64_decode (val, len, &sz);
				n->u.bin.size = (uint)sz;
				return n;
			}
			if (!strcmp (tag, "tag:yaml.org,2002:bool"))
			{
				byml_node_t *n = byml_node_new (BYML_T_BOOL);
				n->u.b = !strcasecmp (val, "true") || !strcmp (val, "1");
				return n;
			}
			if (!strcmp (tag, "tag:yaml.org,2002:null"))
			{
				return byml_node_new (BYML_T_NULL);
			}
			if (!strcmp (tag, "tag:yaml.org,2002:str") || !strcmp (tag, YAML_STR_TAG))
			{
				byml_node_t *n = byml_node_new (BYML_T_STRING);
				n->u.s = STRDUP (val ? val : "");
				return n;
			}
		}

		if (!val || !*val || !strcmp (val, "~") || !strcasecmp (val, "null"))
			return byml_node_new (BYML_T_NULL);
		if (!strcasecmp (val, "true"))
		{
			byml_node_t *n = byml_node_new (BYML_T_BOOL);
			n->u.b = true;
			return n;
		}
		if (!strcasecmp (val, "false"))
		{
			byml_node_t *n = byml_node_new (BYML_T_BOOL);
			n->u.b = false;
			return n;
		}
		if (!strcasecmp (val, ".nan") || !strcasecmp (val, "nan"))
		{
			byml_node_t *n = byml_node_new (BYML_T_FLOAT);
			n->u.f = 0.0f / 0.0f;
			return n;
		}
		if (!strcasecmp (val, ".inf") || !strcasecmp (val, "inf"))
		{
			byml_node_t *n = byml_node_new (BYML_T_FLOAT);
			n->u.f = 1.0f / 0.0f;
			return n;
		}
		if (!strcasecmp (val, "-.inf") || !strcasecmp (val, "-inf"))
		{
			byml_node_t *n = byml_node_new (BYML_T_FLOAT);
			n->u.f = -1.0f / 0.0f;
			return n;
		}

		char *endp = NULL;
		if (val[0] == \x270\x27 && (val[1] == \x27x\x27 || val[1] == \x27X\x27))
		{
			u64 hval = strtoull (val, &endp, 16);
			if (endp && !*endp)
			{
				if (hval <= UINT32_MAX)
				{
					byml_node_t *n = byml_node_new (BYML_T_UINT);
					n->u.u = (uint32_t)hval;
					return n;
				}
				byml_node_t *n = byml_node_new (BYML_T_UINT64);
				n->u.u64 = hval;
				return n;
			}
		}

		long long lval = strtoll (val, &endp, 0);
		if (endp && !*endp && val[0] != \x27\\0\x27)
		{
			if (val[0] != \x27-\x27 && (unsigned long long)lval > INT32_MAX && (unsigned long long)lval <= UINT32_MAX)
			{
				byml_node_t *n = byml_node_new (BYML_T_UINT);
				n->u.u = (uint32_t)lval;
				return n;
			}
			if (lval < INT32_MIN || lval > INT32_MAX)
			{
				byml_node_t *n = byml_node_new (BYML_T_INT64);
				n->u.i64 = lval;
				return n;
			}
			byml_node_t *n = byml_node_new (BYML_T_INT);
			n->u.i = (int32_t)lval;
			return n;
		}

		double dval = strtod (val, &endp);
		if (endp && !*endp && (strchr (val, \x27.\x27) || strchr (val, \x27e\x27) || strchr (val, \x27E\x27)))
		{
			byml_node_t *n = byml_node_new (BYML_T_FLOAT);
			n->u.f = (float)dval;
			return n;
		}

		byml_node_t *n = byml_node_new (BYML_T_STRING);
		n->u.s = STRDUP (val);
		return n;
	}

	if (node->type == YAML_MAPPING_NODE)
	{
		if (tag && (!strcmp (tag, "!h32") || !strcmp (tag, "!h64")))
		{
			bool is_64 = !strcmp (tag, "!h64");
			byml_node_t *m = byml_node_new (is_64 ? BYML_T_HASHMAP64 : BYML_T_HASHMAP32);
			for (yaml_node_pair_t *p = node->data.mapping.pairs.start; p < node->data.mapping.pairs.top; p++)
			{
				yaml_node_t *kn = yaml_document_get_node (doc, p->key);
				if (!kn || kn->type != YAML_SCALAR_NODE) continue;
				ccp kval = (ccp)kn->data.scalar.value;
				byml_node_t *v = byml_node_from_yaml (doc, p->value);
				if (is_64)
					byml_h64_add (m, (u64)strtoull (kval, NULL, 0), v);
				else
					byml_h32_add (m, (u32)strtoul (kval, NULL, 0), v);
			}
			return m;
		}

		if (tag && (!strcmp (tag, "!file") || !strcmp (tag, "tag:yaml.org,2002:file")))
		{
			uint align = 16;
			u8 *bin_data = NULL;
			size_t bin_sz = 0;
			for (yaml_node_pair_t *p = node->data.mapping.pairs.start; p < node->data.mapping.pairs.top; p++)
			{
				yaml_node_t *kn = yaml_document_get_node (doc, p->key);
				yaml_node_t *vn = yaml_document_get_node (doc, p->value);
				if (!kn || kn->type != YAML_SCALAR_NODE || !vn) continue;
				ccp k = (ccp)kn->data.scalar.value;
				if (!strcasecmp (k, "Alignment") && vn->type == YAML_SCALAR_NODE)
					align = (uint)strtoul ((ccp)vn->data.scalar.value, NULL, 0);
				else if (!strcasecmp (k, "Data") && vn->type == YAML_SCALAR_NODE)
					bin_data = byml_b64_decode ((ccp)vn->data.scalar.value, vn->data.scalar.length, &bin_sz);
			}
			byml_node_t *n = byml_node_new (BYML_T_BINARY_ALIGNED);
			n->u.bin.align = align;
			n->u.bin.data = bin_data;
			n->u.bin.size = (uint)bin_sz;
			return n;
		}

		byml_node_t *m = byml_node_new (BYML_T_MAP);
		for (yaml_node_pair_t *p = node->data.mapping.pairs.start; p < node->data.mapping.pairs.top; p++)
		{
			yaml_node_t *kn = yaml_document_get_node (doc, p->key);
			if (!kn || kn->type != YAML_SCALAR_NODE) continue;
			ccp k = (ccp)kn->data.scalar.value;
			byml_node_t *v = byml_node_from_yaml (doc, p->value);
			byml_map_add (m, k, v);
		}
		return m;
	}

	if (node->type == YAML_SEQUENCE_NODE)
	{
		bool all_points = (node->data.sequence.items.top > node->data.sequence.items.start);
		for (yaml_node_item_t *i = node->data.sequence.items.start; i < node->data.sequence.items.top; i++)
		{
			yaml_node_t *in = yaml_document_get_node (doc, *i);
			if (!in || in->type != YAML_MAPPING_NODE) { all_points = false; break; }
			bool has_x = false, has_y = false, has_z = false;
			for (yaml_node_pair_t *p = in->data.mapping.pairs.start; p < in->data.mapping.pairs.top; p++)
			{
				yaml_node_t *kn = yaml_document_get_node (doc, p->key);
				if (kn && kn->type == YAML_SCALAR_NODE)
				{
					ccp k = (ccp)kn->data.scalar.value;
					if (!strcasecmp (k, "X")) has_x = true;
					else if (!strcasecmp (k, "Y")) has_y = true;
					else if (!strcasecmp (k, "Z")) has_z = true;
				}
			}
			if (!has_x || !has_y || !has_z) { all_points = false; break; }
		}

		if (all_points)
		{
			byml_node_t *pn = byml_node_new (BYML_T_PATH_ARRAY);
			for (yaml_node_item_t *i = node->data.sequence.items.start; i < node->data.sequence.items.top; i++)
			{
				yaml_node_t *in = yaml_document_get_node (doc, *i);
				byml_point_t pt = { 0 };
				for (yaml_node_pair_t *p = in->data.mapping.pairs.start; p < in->data.mapping.pairs.top; p++)
				{
					yaml_node_t *kn = yaml_document_get_node (doc, p->key);
					yaml_node_t *vn = yaml_document_get_node (doc, p->value);
					if (!kn || kn->type != YAML_SCALAR_NODE || !vn || vn->type != YAML_SCALAR_NODE) continue;
					ccp k = (ccp)kn->data.scalar.value;
					float fval = (float)strtod ((ccp)vn->data.scalar.value, NULL);
					if (!strcasecmp (k, "X")) pt.x = fval;
					else if (!strcasecmp (k, "Y")) pt.y = fval;
					else if (!strcasecmp (k, "Z")) pt.z = fval;
					else if (!strcasecmp (k, "NX")) pt.nx = fval;
					else if (!strcasecmp (k, "NY")) pt.ny = fval;
					else if (!strcasecmp (k, "NZ")) pt.nz = fval;
					else if (!strcasecmp (k, "Value")) pt.val = (u32)strtoul ((ccp)vn->data.scalar.value, NULL, 0);
				}
				byml_path_add (pn, pt);
			}
			return pn;
		}

		byml_node_t *arr = byml_node_new (BYML_T_ARRAY);
		for (yaml_node_item_t *i = node->data.sequence.items.start; i < node->data.sequence.items.top; i++)
		{
			byml_node_t *child = byml_node_from_yaml (doc, *i);
			byml_arr_add (arr, child);
		}
		return arr;
	}

	return byml_node_new (BYML_T_NULL);
}

///////////////////////////////////////////////////////////////////////////////
// XML Reading -> byml_node_t (Matching ByamlXmlConverter)
///////////////////////////////////////////////////////////////////////////////

static byml_node_t *byml_parse_xml_val_str (ccp val)
{
	if (!val || !*val || !strcmp (val, "null"))
		return byml_node_new (BYML_T_NULL);
	if (!strcmp (val, "true"))
	{
		byml_node_t *n = byml_node_new (BYML_T_BOOL);
		n->u.b = true;
		return n;
	}
	if (!strcmp (val, "false"))
	{
		byml_node_t *n = byml_node_new (BYML_T_BOOL);
		n->u.b = false;
		return n;
	}

	size_t len = strlen (val);
	if (len > 3 && !strcmp (val + len - 3, "i64"))
	{
		byml_node_t *n = byml_node_new (BYML_T_INT64);
		n->u.i64 = (int64_t)strtoll (val, NULL, 0);
		return n;
	}
	if (len > 3 && !strcmp (val + len - 3, "u64"))
	{
		byml_node_t *n = byml_node_new (BYML_T_UINT64);
		n->u.u64 = (uint64_t)strtoull (val, NULL, 0);
		return n;
	}
	if (len > 1 && val[len - 1] == \x27f\x27)
	{
		byml_node_t *n = byml_node_new (BYML_T_FLOAT);
		n->u.f = (float)strtod (val, NULL);
		return n;
	}
	if (len > 1 && val[len - 1] == \x27u\x27)
	{
		byml_node_t *n = byml_node_new (BYML_T_UINT);
		n->u.u = (uint32_t)strtoul (val, NULL, 0);
		return n;
	}
	if (len > 1 && val[len - 1] == \x27d\x27)
	{
		byml_node_t *n = byml_node_new (BYML_T_DOUBLE);
		n->u.d = strtod (val, NULL);
		return n;
	}

	char *endp = NULL;
	long long lval = strtoll (val, &endp, 0);
	if (endp && !*endp)
	{
		byml_node_t *n = byml_node_new (BYML_T_INT);
		n->u.i = (int32_t)lval;
		return n;
	}

	byml_node_t *n = byml_node_new (BYML_T_STRING);
	n->u.s = STRDUP (val);
	return n;
}

static ccp mxml_get_opaque_text (mxml_node_t *node)
{
	for (mxml_node_t *child = mxmlGetFirstChild (node); child; child = mxmlGetNextSibling (child))
	{
		int ws = 0;
		ccp t = mxmlGetText (child, &ws);
		if (t && *t)
			return t;
		t = mxmlGetOpaque (child);
		if (t && *t)
			return t;
	}
	return "";
}

static byml_node_t *byml_node_from_xml (mxml_node_t *elem)
{
	if (!elem)
		return byml_node_new (BYML_T_NULL);

	ccp type_attr = mxmlElementGetAttr (elem, "type");

	if (type_attr && !strcmp (type_attr, "string"))
	{
		byml_node_t *n = byml_node_new (BYML_T_STRING);
		n->u.s = STRDUP (mxml_get_opaque_text (elem));
		return n;
	}
	if (type_attr && !strcmp (type_attr, "binary"))
	{
		ccp b64 = mxml_get_opaque_text (elem);
		byml_node_t *n = byml_node_new (BYML_T_BINARY);
		size_t sz = 0;
		n->u.bin.data = byml_b64_decode (b64, strlen (b64), &sz);
		n->u.bin.size = (uint)sz;
		return n;
	}
	if (type_attr && !strcmp (type_attr, "array"))
	{
		byml_node_t *arr = byml_node_new (BYML_T_ARRAY);
		for (mxml_node_t *c = mxmlGetFirstChild (elem); c; c = mxmlGetNextSibling (c))
		{
			if (mxmlGetType (c) != MXML_ELEMENT) continue;
			byml_arr_add (arr, byml_node_from_xml (c));
		}
		return arr;
	}
	if (type_attr && !strcmp (type_attr, "path"))
	{
		byml_node_t *pn = byml_node_new (BYML_T_PATH_ARRAY);
		for (mxml_node_t *c = mxmlGetFirstChild (elem); c; c = mxmlGetNextSibling (c))
		{
			if (mxmlGetType (c) != MXML_ELEMENT || strcmp (mxmlGetElement (c), "point")) continue;
			byml_point_t pt = { 0 };
			#define GET_XML_PT(attr, field) do { \
				ccp a = mxmlElementGetAttr (c, attr); \
				if (a) field = (float)strtod (a, NULL); \
			} while (0)
			GET_XML_PT ("x", pt.x);
			GET_XML_PT ("y", pt.y);
			GET_XML_PT ("z", pt.z);
			GET_XML_PT ("nx", pt.nx);
			GET_XML_PT ("ny", pt.ny);
			GET_XML_PT ("nz", pt.nz);
			ccp va = mxmlElementGetAttr (c, "val");
			if (va) pt.val = (u32)strtoul (va, NULL, 0);
			byml_path_add (pn, pt);
		}
		return pn;
	}

	bool has_elements = false;
	for (mxml_node_t *c = mxmlGetFirstChild (elem); c; c = mxmlGetNextSibling (c))
	{
		if (mxmlGetType (c) == MXML_ELEMENT)
		{
			has_elements = true;
			break;
		}
	}

	uint attr_count = 0;
	mxml_attr_t *attrs = elem->value.element.attrs;
	uint n_attrs = elem->value.element.num_attrs;
	for (uint i = 0; i < n_attrs; i++)
	{
		ccp aname = attrs[i].name;
		if (strchr (aname, \x27:\x27) || !strcmp (aname, "type") || !strcmp (aname, "xmlns"))
			continue;
		attr_count++;
	}

	if (has_elements || attr_count > 0)
	{
		byml_node_t *m = byml_node_new (BYML_T_MAP);
		for (uint i = 0; i < n_attrs; i++)
		{
			ccp aname = attrs[i].name;
			if (strchr (aname, \x27:\x27) || !strcmp (aname, "type") || !strcmp (aname, "xmlns"))
				continue;
			byml_map_add (m, aname, byml_parse_xml_val_str (attrs[i].value));
		}
		for (mxml_node_t *c = mxmlGetFirstChild (elem); c; c = mxmlGetNextSibling (c))
		{
			if (mxmlGetType (c) != MXML_ELEMENT) continue;
			byml_map_add (m, mxmlGetElement (c), byml_node_from_xml (c));
		}
		return m;
	}

	ccp text = mxml_get_opaque_text (elem);
	return byml_parse_xml_val_str (text);
}

///////////////////////////////////////////////////////////////////////////////
// Public Encoders
///////////////////////////////////////////////////////////////////////////////

enumError EncodeBYML_Text (
	u8 **dest, uint *dest_size, const char *text, uint text_len, bool is_le, u16 version)
{
	if (!dest || !dest_size || !text)
		return ERR_SEMANTIC;
	*dest = 0;
	*dest_size = 0;

	yaml_parser_t parser;
	yaml_document_t document;
	if (!yaml_parser_initialize (&parser))
		return ERR_OUT_OF_MEMORY;
	yaml_parser_set_input_string (&parser, (const unsigned char *)text, text_len);
	if (!yaml_parser_load (&parser, &document))
	{
		yaml_parser_delete (&parser);
		return ERR_SEMANTIC;
	}

	byml_node_t *root = NULL;
	yaml_node_t *root_node = yaml_document_get_root_node (&document);
	if (root_node)
	{
		int root_id = (int)(root_node - document.nodes.start + 1);
		root = byml_node_from_yaml (&document, root_id);
	}
	else
	{
		root = byml_node_new (BYML_T_MAP);
	}

	yaml_document_delete (&document);
	yaml_parser_delete (&parser);

	bool support_paths = (version == 1);
	enumError err = byml_write_binary (root, dest, dest_size, is_le, version, support_paths);
	byml_node_free (root);
	return err;
}

enumError EncodeBYML_XML (
	u8 **dest, uint *dest_size, const char *xml, uint xml_len, bool is_le, u16 version)
{
	if (!dest || !dest_size || !xml || !xml_len)
		return ERR_SEMANTIC;
	*dest = 0;
	*dest_size = 0;

	mxml_node_t *doc = mxmlLoadString (NULL, xml, MXML_OPAQUE_CALLBACK);
	if (!doc)
		return ERR_INVALID_DATA;

	mxml_node_t *yaml_el = mxmlFindElement (doc, doc, "yaml", NULL, NULL, MXML_DESCEND);
	if (!yaml_el)
	{
		mxmlDelete (doc);
		return ERR_INVALID_DATA;
	}

	ccp endian_attr = mxmlElementGetAttr (yaml_el, "yamlconv:endianness");
	if (endian_attr)
		is_le = !strcasecmp (endian_attr, "little");

	ccp ver_attr = mxmlElementGetAttr (yaml_el, "yamlconv:byamlVersion");
	if (ver_attr)
		version = (u16)strtoul (ver_attr, NULL, 0);

	ccp off_attr = mxmlElementGetAttr (yaml_el, "yamlconv:offsetCount");
	bool support_paths = false;
	if (off_attr && strtoul (off_attr, NULL, 0) == 4)
		support_paths = true;

	byml_node_t *root = byml_node_from_xml (yaml_el);
	mxmlDelete (doc);

	enumError err = byml_write_binary (root, dest, dest_size, is_le, version ? version : 2, support_paths);
	byml_node_free (root);
	return err;
}

enumError encode_byml_file (ccp source, ccp dest)
{
	u8 *text = 0;
	size_t text_len = 0;
	enumError err = LoadFileAlloc (source, 0, 0, &text, &text_len, 64 << 20, 0, 0, false);
	if (err)
		return err;

	u8 *byml = 0;
	uint byml_size = 0;
	ccp ext = strrchr (dest, \x27.\x27);
	bool is_le = true;
	if (ext && !strcasecmp (ext, ".be"))
		is_le = false;

	ccp src_ext = strrchr (source, \x27.\x27);
	if (src_ext && !strcasecmp (src_ext, ".xml"))
		err = EncodeBYML_XML (&byml, &byml_size, (const char *)text, (uint)text_len, is_le, 0);
	else
		err = EncodeBYML_Text (&byml, &byml_size, (const char *)text, (uint)text_len, is_le, 0);

	FREE (text);
	if (err)
		return err;

	if (!testmode)
	{
		File_t F;
		CreateFILE (&F, true, dest, testmode, false, true, false, false);
		if (F.f && fwrite (byml, 1, byml_size, F.f) != byml_size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing BYML failed: %s\n", dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (byml);
	return err;
}

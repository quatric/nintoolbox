#include "lib-aamp.h"
#include "lib-std.h"
#include <zlib.h>
#include <yaml.h>
#include <ctype.h>
#include <math.h>

#include "aamp-hash-db.inc"

///////////////////////////////////////////////////////////////////////////////
// Hash Table & Database
///////////////////////////////////////////////////////////////////////////////

#define HASH_TABLE_SIZE (1 << 19) // 524288 entries
#define HASH_TABLE_MASK (HASH_TABLE_SIZE - 1)

typedef struct aamp_hash_node_t
{
	u32 hash;
	const char *name;
} aamp_hash_node_t;

static aamp_hash_node_t *s_hash_table = NULL;
static char *s_raw_hash_strings = NULL;
static char *s_procedural_arena = NULL;
static size_t s_procedural_arena_used = 0;
static size_t s_procedural_arena_cap = 0;
static bool s_hash_db_initialized = false;

static void aamp_insert_hash (u32 hash, const char *name)
{
	if (!s_hash_table || !name)
		return;
	u32 idx = hash & HASH_TABLE_MASK;
	while (s_hash_table[idx].name)
	{
		if (s_hash_table[idx].hash == hash)
			return; // already present
		idx = (idx + 1) & HASH_TABLE_MASK;
	}
	s_hash_table[idx].hash = hash;
	s_hash_table[idx].name = name;
}

static void aamp_add_procedural (const char *prefix, int count)
{
	for (int i = 0; i < count; i++)
	{
		char buf[128];
		snprintf (buf, sizeof (buf), "%s%d", prefix, i);
		u32 h = (u32)crc32 (0, (const Bytef *)buf, (uInt)strlen (buf));
		size_t len = strlen (buf) + 1;
		if (s_procedural_arena_used + len < s_procedural_arena_cap)
		{
			char *dst = s_procedural_arena + s_procedural_arena_used;
			memcpy (dst, buf, len);
			s_procedural_arena_used += len;
			aamp_insert_hash (h, dst);
		}
	}
}

void AAMP_InitHashDB (void)
{
	if (s_hash_db_initialized)
		return;
	s_hash_db_initialized = true;

	s_hash_table = CALLOC (HASH_TABLE_SIZE, sizeof (aamp_hash_node_t));
	if (!s_hash_table)
		return;

	s_raw_hash_strings = MALLOC (AAMP_HASH_DB_RAW_SIZE + 2);
	if (s_raw_hash_strings)
	{
		uLongf dest_len = AAMP_HASH_DB_RAW_SIZE;
		int zerr = uncompress ((Bytef *)s_raw_hash_strings, &dest_len,
			aamp_hash_db_compressed, AAMP_HASH_DB_COMP_SIZE);
		if (zerr == Z_OK)
		{
			s_raw_hash_strings[dest_len] = '\0';
			char *p = s_raw_hash_strings;
			char *end = s_raw_hash_strings + dest_len;
			while (p < end)
			{
				while (p < end && (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t'))
					p++;
				if (p >= end)
					break;
				char *start = p;
				while (p < end && *p != '\r' && *p != '\n')
					p++;
				*p = '\0';
				if (*start)
				{
					if (strchr (start, '%'))
					{
						for (int d = 0; d < 6; d++)
						{
							char nbuf[128];
							char *fmt = strstr (start, "%d");
							if (fmt)
							{
								snprintf (nbuf, sizeof (nbuf), "%.*s%d%s",
									(int)(fmt - start), start, d, fmt + 2);
								u32 h = (u32)crc32 (0, (const Bytef *)nbuf, (uInt)strlen (nbuf));
								aamp_insert_hash (h, STRDUP (nbuf));
							}
						}
					}
					else
					{
						u32 h = (u32)crc32 (0, (const Bytef *)start, (uInt)strlen (start));
						aamp_insert_hash (h, start);
					}
				}
				p++;
			}
		}
	}

	s_procedural_arena_cap = 256 * 1024;
	s_procedural_arena = MALLOC (s_procedural_arena_cap);
	if (s_procedural_arena)
	{
		aamp_add_procedural ("PointLightRig", 50);
		aamp_add_procedural ("SpotLightRig", 50);
		aamp_add_procedural ("AI_", 1000);
		aamp_add_procedural ("Action_", 1000);
		aamp_add_procedural ("HemisphereLight", 30);
		aamp_add_procedural ("Fog", 30);
		aamp_add_procedural ("DirectionalLight", 30);
		aamp_add_procedural ("BloomObj", 30);
		aamp_add_procedural ("OfxLargeLensFlareRig", 30);
		aamp_add_procedural ("name", 50);
		aamp_add_procedural ("intensity", 50);
		aamp_add_procedural ("connection_curve_", 100);
		aamp_add_procedural ("bone_", 100);
		aamp_add_procedural ("output_single_", 100);
		aamp_add_procedural ("support_bone_", 100);
	}
}

const char *AAMP_HashToName (u32 hash, char *buf, size_t buf_size)
{
	AAMP_InitHashDB ();
	if (s_hash_table)
	{
		u32 idx = hash & HASH_TABLE_MASK;
		while (s_hash_table[idx].name)
		{
			if (s_hash_table[idx].hash == hash)
				return s_hash_table[idx].name;
			idx = (idx + 1) & HASH_TABLE_MASK;
		}
	}
	if (buf && buf_size > 0)
	{
		snprintf (buf, buf_size, "0x%08X", hash);
		return buf;
	}
	return NULL;
}

u32 AAMP_NameToHash (const char *name)
{
	if (!name || !*name)
		return 0;
	if (name[0] == '0' && (name[1] == 'x' || name[1] == 'X'))
	{
		char *end = NULL;
		unsigned long val = strtoul (name, &end, 16);
		if (end && !*end)
			return (u32)val;
	}
	bool is_digits = true;
	for (const char *c = name; *c; c++)
	{
		if (!isdigit ((unsigned char)*c))
		{
			is_digits = false;
			break;
		}
	}
	if (is_digits)
	{
		char *end = NULL;
		unsigned long val = strtoul (name, &end, 10);
		if (end && !*end)
		{
			AAMP_InitHashDB ();
			if (s_hash_table)
			{
				u32 idx = (u32)val & HASH_TABLE_MASK;
				while (s_hash_table[idx].name)
				{
					if (s_hash_table[idx].hash == (u32)val)
					{
						if (strcmp (s_hash_table[idx].name, name) == 0)
							return (u32)val;
						break;
					}
					idx = (idx + 1) & HASH_TABLE_MASK;
				}
			}
			return (u32)val;
		}
	}
	u32 h = (u32)crc32 (0, (const Bytef *)name, (uInt)strlen (name));
	AAMP_InitHashDB ();
	aamp_insert_hash (h, STRDUP (name));
	return h;
}

///////////////////////////////////////////////////////////////////////////////
// Initialize & Reset
///////////////////////////////////////////////////////////////////////////////

static void aamp_free_entry (aamp_param_entry_t *entry)
{
	if (!entry)
		return;
	if (entry->type == AAMP_TYPE_STRING32 || entry->type == AAMP_TYPE_STRING64
		|| entry->type == AAMP_TYPE_STRING256 || entry->type == AAMP_TYPE_STRING_REF)
	{
		if (entry->str)
		{
			FREE (entry->str);
			entry->str = NULL;
		}
	}
	else if (entry->type == AAMP_TYPE_BUFFER_INT || entry->type == AAMP_TYPE_BUFFER_FLOAT
		|| entry->type == AAMP_TYPE_BUFFER_UINT || entry->type == AAMP_TYPE_BUFFER_BIN)
	{
		if (entry->buf.data)
		{
			FREE (entry->buf.data);
			entry->buf.data = NULL;
		}
	}
	else if (entry->type >= AAMP_TYPE_CURVE1 && entry->type <= AAMP_TYPE_CURVE4)
	{
		if (entry->curve.curves)
		{
			FREE (entry->curve.curves);
			entry->curve.curves = NULL;
		}
	}
}

static void aamp_free_object (aamp_param_object_t *obj)
{
	if (!obj)
		return;
	if (obj->entries)
	{
		for (u32 i = 0; i < obj->entry_count; i++)
			aamp_free_entry (&obj->entries[i]);
		FREE (obj->entries);
		obj->entries = NULL;
	}
	obj->entry_count = 0;
	obj->entry_alloc = 0;
}

static void aamp_free_list (aamp_param_list_t *list)
{
	if (!list)
		return;
	if (list->objects)
	{
		for (u32 i = 0; i < list->object_count; i++)
			aamp_free_object (&list->objects[i]);
		FREE (list->objects);
		list->objects = NULL;
	}
	list->object_count = 0;
	list->object_alloc = 0;

	if (list->lists)
	{
		for (u32 i = 0; i < list->list_count; i++)
			aamp_free_list (&list->lists[i]);
		FREE (list->lists);
		list->lists = NULL;
	}
	list->list_count = 0;
	list->list_alloc = 0;
}

void InitializeAAMP (aamp_file_t *aamp)
{
	if (!aamp)
		return;
	memset (aamp, 0, sizeof (*aamp));
	aamp->version = 2;
	aamp->is_le = true;
}

void ResetAAMP (aamp_file_t *aamp)
{
	if (!aamp)
		return;
	aamp_free_list (&aamp->root);
	memset (aamp, 0, sizeof (*aamp));
}

bool IsAAMP (const u8 *data, size_t size)
{
	return data && size >= 12 && !memcmp (data, "AAMP", 4);
}

///////////////////////////////////////////////////////////////////////////////
// Binary Scanning (V1 & V2)
///////////////////////////////////////////////////////////////////////////////

static inline u16 read_u16 (const u8 *p, bool le)
{
	return le ? rd_le16 (p) : rd_be16 (p);
}

static inline u32 read_u32 (const u8 *p, bool le)
{
	return le ? rd_le32 (p) : rd_be32 (p);
}

static inline float read_f32 (const u8 *p, bool le)
{
	u32 u = read_u32 (p, le);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

// V1 Scanners
static enumError aamp_scan_v1_entry (
	aamp_param_entry_t *entry, const u8 *data, size_t size, size_t offset, bool le)
{
	if (offset + 12 > size)
		return ERR_INVALID_DATA;
	u32 entry_size = read_u32 (data + offset, le);
	if (offset + entry_size > size || entry_size < 12)
		return ERR_INVALID_DATA;

	entry->type = (aamp_param_type_t)read_u32 (data + offset + 4, le);
	entry->hash = read_u32 (data + offset + 8, le);
	size_t payload_off = offset + 12;
	size_t payload_len = entry_size - 12;

	switch (entry->type)
	{
		case AAMP_TYPE_BOOL:
			entry->b = data[payload_off] != 0;
			break;
		case AAMP_TYPE_FLOAT:
			if (payload_len >= 4)
				entry->f = read_f32 (data + payload_off, le);
			break;
		case AAMP_TYPE_INT:
			if (payload_len >= 4)
				entry->i = (s32)read_u32 (data + payload_off, le);
			break;
		case AAMP_TYPE_UINT:
			if (payload_len >= 4)
				entry->u = read_u32 (data + payload_off, le);
			break;
		case AAMP_TYPE_VEC2:
			if (payload_len >= 8)
			{
				entry->vec[0] = read_f32 (data + payload_off, le);
				entry->vec[1] = read_f32 (data + payload_off + 4, le);
			}
			break;
		case AAMP_TYPE_VEC3:
			if (payload_len >= 12)
			{
				entry->vec[0] = read_f32 (data + payload_off, le);
				entry->vec[1] = read_f32 (data + payload_off + 4, le);
				entry->vec[2] = read_f32 (data + payload_off + 8, le);
			}
			break;
		case AAMP_TYPE_VEC4:
		case AAMP_TYPE_COLOR:
		case AAMP_TYPE_QUAT:
			if (payload_len >= 16)
			{
				entry->vec[0] = read_f32 (data + payload_off, le);
				entry->vec[1] = read_f32 (data + payload_off + 4, le);
				entry->vec[2] = read_f32 (data + payload_off + 8, le);
				entry->vec[3] = read_f32 (data + payload_off + 12, le);
			}
			break;
		case AAMP_TYPE_STRING32:
		case AAMP_TYPE_STRING64:
		case AAMP_TYPE_STRING256:
		case AAMP_TYPE_STRING_REF:
		{
			size_t max_l = (entry->type == AAMP_TYPE_STRING32) ? 32
				: (entry->type == AAMP_TYPE_STRING64)           ? 64
				: (entry->type == AAMP_TYPE_STRING256)          ? 256
																: 1024;
			size_t act_l = 0;
			while (act_l < payload_len && act_l < max_l && data[payload_off + act_l])
				act_l++;
			entry->str = MALLOC (act_l + 1);
			memcpy (entry->str, data + payload_off, act_l);
			entry->str[act_l] = '\0';
			break;
		}
		case AAMP_TYPE_BUFFER_INT:
		case AAMP_TYPE_BUFFER_UINT:
		case AAMP_TYPE_BUFFER_FLOAT:
		{
			u32 count = (u32)(payload_len / 4);
			entry->buf.count = count;
			entry->buf.data = MALLOC (payload_len);
			if (le)
				memcpy (entry->buf.data, data + payload_off, payload_len);
			else
			{
				u32 *dst = (u32 *)entry->buf.data;
				for (u32 i = 0; i < count; i++)
					dst[i] = read_u32 (data + payload_off + i * 4, le);
			}
			break;
		}
		case AAMP_TYPE_BUFFER_BIN:
			entry->buf.count = (u32)payload_len;
			entry->buf.data = MALLOC (payload_len);
			memcpy (entry->buf.data, data + payload_off, payload_len);
			break;
		case AAMP_TYPE_CURVE1:
		case AAMP_TYPE_CURVE2:
		case AAMP_TYPE_CURVE3:
		case AAMP_TYPE_CURVE4:
		{
			u32 count = entry->type - AAMP_TYPE_CURVE1 + 1;
			entry->curve.count = count;
			entry->curve.curves = CALLOC (count, sizeof (aamp_curve_t));
			for (u32 c = 0; c < count; c++)
			{
				size_t coff = payload_off + c * (2 * 4 + 30 * 4);
				if (coff + 128 <= size)
				{
					entry->curve.curves[c].uints[0] = read_u32 (data + coff, le);
					entry->curve.curves[c].uints[1] = read_u32 (data + coff + 4, le);
					for (int f = 0; f < 30; f++)
						entry->curve.curves[c].floats[f] = read_f32 (data + coff + 8 + f * 4, le);
				}
			}
			break;
		}
		default:
			break;
	}
	return ERR_OK;
}

static enumError aamp_scan_v1_object (
	aamp_param_object_t *obj, const u8 *data, size_t size, size_t offset, bool le)
{
	if (offset + 16 > size)
		return ERR_INVALID_DATA;
	u32 obj_size = read_u32 (data + offset, le);
	u32 entry_count = read_u32 (data + offset + 4, le);
	obj->hash = read_u32 (data + offset + 8, le);
	obj->group_hash = read_u32 (data + offset + 12, le);

	if (offset + obj_size > size)
		return ERR_INVALID_DATA;

	obj->entry_count = entry_count;
	obj->entries = CALLOC (entry_count, sizeof (aamp_param_entry_t));
	size_t cur = offset + 16;
	for (u32 i = 0; i < entry_count; i++)
	{
		if (cur >= offset + obj_size)
			break;
		u32 esize = read_u32 (data + cur, le);
		aamp_scan_v1_entry (&obj->entries[i], data, size, cur, le);
		cur += esize;
	}
	return ERR_OK;
}

static enumError aamp_scan_v1_list (
	aamp_param_list_t *list, const u8 *data, size_t size, size_t offset, bool le)
{
	if (offset + 16 > size)
		return ERR_INVALID_DATA;
	u32 list_size = read_u32 (data + offset, le);
	list->hash = read_u32 (data + offset + 4, le);
	u32 child_list_count = read_u32 (data + offset + 8, le);
	u32 object_count = read_u32 (data + offset + 12, le);

	if (offset + list_size > size)
		return ERR_INVALID_DATA;

	list->list_count = child_list_count;
	if (child_list_count)
		list->lists = CALLOC (child_list_count, sizeof (aamp_param_list_t));
	list->object_count = object_count;
	if (object_count)
		list->objects = CALLOC (object_count, sizeof (aamp_param_object_t));

	size_t cur = offset + 16;
	for (u32 i = 0; i < child_list_count; i++)
	{
		if (cur >= offset + list_size)
			break;
		u32 csize = read_u32 (data + cur, le);
		aamp_scan_v1_list (&list->lists[i], data, size, cur, le);
		cur += csize;
	}
	for (u32 i = 0; i < object_count; i++)
	{
		if (cur >= offset + list_size)
			break;
		u32 osize = read_u32 (data + cur, le);
		aamp_scan_v1_object (&list->objects[i], data, size, cur, le);
		cur += osize;
	}
	return ERR_OK;
}

// V2 Scanners
static enumError aamp_scan_v2_entry (
	aamp_param_entry_t *entry, const u8 *data, size_t size, size_t offset, bool le)
{
	if (offset + 8 > size)
		return ERR_INVALID_DATA;
	entry->hash = read_u32 (data + offset, le);
	u32 field4 = read_u32 (data + offset + 4, le);
	u32 data_offset = field4 & 0x00FFFFFF;
	entry->type = (aamp_param_type_t)((field4 >> 24) & 0xFF);

	if (data_offset == 0)
	{
		if (entry->type == AAMP_TYPE_STRING32 || entry->type == AAMP_TYPE_STRING64
			|| entry->type == AAMP_TYPE_STRING256 || entry->type == AAMP_TYPE_STRING_REF)
			entry->str = STRDUP ("");
		return ERR_OK;
	}

	size_t abs_off = offset + (size_t)data_offset * 4;
	if (abs_off >= size)
		return ERR_INVALID_DATA;

	switch (entry->type)
	{
		case AAMP_TYPE_BOOL:
			entry->b = read_u32 (data + abs_off, le) != 0;
			break;
		case AAMP_TYPE_FLOAT:
			entry->f = read_f32 (data + abs_off, le);
			break;
		case AAMP_TYPE_INT:
			entry->i = (s32)read_u32 (data + abs_off, le);
			break;
		case AAMP_TYPE_UINT:
			entry->u = read_u32 (data + abs_off, le);
			break;
		case AAMP_TYPE_VEC2:
			if (abs_off + 8 <= size)
			{
				entry->vec[0] = read_f32 (data + abs_off, le);
				entry->vec[1] = read_f32 (data + abs_off + 4, le);
			}
			break;
		case AAMP_TYPE_VEC3:
			if (abs_off + 12 <= size)
			{
				entry->vec[0] = read_f32 (data + abs_off, le);
				entry->vec[1] = read_f32 (data + abs_off + 4, le);
				entry->vec[2] = read_f32 (data + abs_off + 8, le);
			}
			break;
		case AAMP_TYPE_VEC4:
		case AAMP_TYPE_COLOR:
		case AAMP_TYPE_QUAT:
			if (abs_off + 16 <= size)
			{
				entry->vec[0] = read_f32 (data + abs_off, le);
				entry->vec[1] = read_f32 (data + abs_off + 4, le);
				entry->vec[2] = read_f32 (data + abs_off + 8, le);
				entry->vec[3] = read_f32 (data + abs_off + 12, le);
			}
			break;
		case AAMP_TYPE_STRING32:
		case AAMP_TYPE_STRING64:
		case AAMP_TYPE_STRING256:
		case AAMP_TYPE_STRING_REF:
		{
			size_t max_l = (entry->type == AAMP_TYPE_STRING32) ? 32
				: (entry->type == AAMP_TYPE_STRING64)           ? 64
				: (entry->type == AAMP_TYPE_STRING256)          ? 256
																: (size - abs_off);
			size_t act_l = 0;
			while (abs_off + act_l < size && act_l < max_l && data[abs_off + act_l])
				act_l++;
			entry->str = MALLOC (act_l + 1);
			memcpy (entry->str, data + abs_off, act_l);
			entry->str[act_l] = '\0';
			break;
		}
		case AAMP_TYPE_BUFFER_INT:
		case AAMP_TYPE_BUFFER_UINT:
		case AAMP_TYPE_BUFFER_FLOAT:
		case AAMP_TYPE_BUFFER_BIN:
		{
			if (abs_off < 4)
				return ERR_INVALID_DATA;
			u32 count = read_u32 (data + abs_off - 4, le);
			entry->buf.count = count;
			size_t elem_size = (entry->type == AAMP_TYPE_BUFFER_BIN) ? 1 : 4;
			size_t bsize = (size_t)count * elem_size;
			if (abs_off + bsize > size)
				return ERR_INVALID_DATA;
			entry->buf.data = MALLOC (bsize);
			if (elem_size == 1 || le)
				memcpy (entry->buf.data, data + abs_off, bsize);
			else
			{
				u32 *dst = (u32 *)entry->buf.data;
				for (u32 i = 0; i < count; i++)
					dst[i] = read_u32 (data + abs_off + i * 4, le);
			}
			break;
		}
		case AAMP_TYPE_CURVE1:
		case AAMP_TYPE_CURVE2:
		case AAMP_TYPE_CURVE3:
		case AAMP_TYPE_CURVE4:
		{
			u32 count = entry->type - AAMP_TYPE_CURVE1 + 1;
			entry->curve.count = count;
			entry->curve.curves = CALLOC (count, sizeof (aamp_curve_t));
			for (u32 c = 0; c < count; c++)
			{
				size_t coff = abs_off + c * (2 * 4 + 30 * 4);
				if (coff + 128 <= size)
				{
					entry->curve.curves[c].uints[0] = read_u32 (data + coff, le);
					entry->curve.curves[c].uints[1] = read_u32 (data + coff + 4, le);
					for (int f = 0; f < 30; f++)
						entry->curve.curves[c].floats[f] = read_f32 (data + coff + 8 + f * 4, le);
				}
			}
			break;
		}
		default:
			break;
	}
	return ERR_OK;
}

static enumError aamp_scan_v2_object (
	aamp_param_object_t *obj, const u8 *data, size_t size, size_t offset, bool le)
{
	if (offset + 8 > size)
		return ERR_INVALID_DATA;
	obj->hash = read_u32 (data + offset, le);
	u16 child_offset = read_u16 (data + offset + 4, le);
	u16 child_count = read_u16 (data + offset + 6, le);

	obj->entry_count = child_count;
	if (child_count && child_offset)
	{
		size_t abs_off = offset + (size_t)child_offset * 4;
		obj->entries = CALLOC (child_count, sizeof (aamp_param_entry_t));
		for (u16 i = 0; i < child_count; i++)
			aamp_scan_v2_entry (&obj->entries[i], data, size, abs_off + i * 8, le);
	}
	return ERR_OK;
}

static enumError aamp_scan_v2_list (
	aamp_param_list_t *list, const u8 *data, size_t size, size_t offset, bool le)
{
	if (offset + 12 > size)
		return ERR_INVALID_DATA;
	list->hash = read_u32 (data + offset, le);
	u16 child_list_offset = read_u16 (data + offset + 4, le);
	u16 child_list_count = read_u16 (data + offset + 6, le);
	u16 obj_offset = read_u16 (data + offset + 8, le);
	u16 obj_count = read_u16 (data + offset + 10, le);

	list->list_count = child_list_count;
	if (child_list_count && child_list_offset)
	{
		size_t abs_off = offset + (size_t)child_list_offset * 4;
		list->lists = CALLOC (child_list_count, sizeof (aamp_param_list_t));
		for (u16 i = 0; i < child_list_count; i++)
			aamp_scan_v2_list (&list->lists[i], data, size, abs_off + i * 12, le);
	}

	list->object_count = obj_count;
	if (obj_count && obj_offset)
	{
		size_t abs_off = offset + (size_t)obj_offset * 4;
		list->objects = CALLOC (obj_count, sizeof (aamp_param_object_t));
		for (u16 i = 0; i < obj_count; i++)
			aamp_scan_v2_object (&list->objects[i], data, size, abs_off + i * 8, le);
	}
	return ERR_OK;
}

enumError ScanAAMP (aamp_file_t *aamp, const u8 *data, size_t size)
{
	if (!aamp || !IsAAMP (data, size))
		return ERR_INVALID_DATA;

	InitializeAAMP (aamp);
	u32 ver_le = rd_le32 (data + 4);
	bool le = (ver_le == 1 || ver_le == 2);
	aamp->is_le = le;
	aamp->version = read_u32 (data + 4, le);
	aamp->flags = read_u32 (data + 8, le);

	if (aamp->version == 1)
	{
		if (size < 20)
			return ERR_INVALID_DATA;
		aamp->pio_version = read_u32 (data + 16, le);
		u32 name_len = read_u32 (data + 20, le);
		if (24 + name_len > size)
			return ERR_INVALID_DATA;
		snprintf (aamp->pio_type, sizeof (aamp->pio_type), "%.*s", (int)name_len, (const char *)(data + 24));
		size_t root_off = 24 + name_len;
		return aamp_scan_v1_list (&aamp->root, data, size, root_off, le);
	}
	else if (aamp->version == 2)
	{
		if (size < 0x30)
			return ERR_INVALID_DATA;
		aamp->pio_version = read_u32 (data + 16, le);
		u32 pio_off = read_u32 (data + 20, le);
		const char *type_str = (const char *)(data + 0x30);
		size_t max_tl = (pio_off > 0 && pio_off < 256) ? pio_off : 64;
		snprintf (aamp->pio_type, sizeof (aamp->pio_type), "%.*s", (int)max_tl, type_str);
		size_t root_off = 0x30 + (size_t)pio_off;
		if (root_off >= size)
			return ERR_INVALID_DATA;
		return aamp_scan_v2_list (&aamp->root, data, size, root_off, le);
	}
	return ERR_INVALID_DATA;
}

///////////////////////////////////////////////////////////////////////////////
// Binary Writing (V1 & V2)
///////////////////////////////////////////////////////////////////////////////

typedef struct byte_buf_t
{
	u8 *data;
	size_t size;
	size_t cap;
} byte_buf_t;

static void buf_init (byte_buf_t *b)
{
	b->cap = 4096;
	b->size = 0;
	b->data = MALLOC (b->cap);
}

static void buf_free (byte_buf_t *b)
{
	if (b->data)
		FREE (b->data);
	b->data = NULL;
	b->size = 0;
	b->cap = 0;
}

static void buf_reserve (byte_buf_t *b, size_t needed)
{
	if (b->size + needed > b->cap)
	{
		while (b->size + needed > b->cap)
			b->cap *= 2;
		b->data = REALLOC (b->data, b->cap);
	}
}

static void buf_write (byte_buf_t *b, const void *p, size_t len)
{
	buf_reserve (b, len);
	memcpy (b->data + b->size, p, len);
	b->size += len;
}

static void buf_write_u16 (byte_buf_t *b, u16 val, bool le)
{
	u8 tmp[2];
	if (le)
		wr_le16 (tmp, val);
	else
		wr_be16 (tmp, val);
	buf_write (b, tmp, 2);
}

static void buf_write_u32 (byte_buf_t *b, u32 val, bool le)
{
	u8 tmp[4];
	if (le)
		wr_le32 (tmp, val);
	else
		wr_be32 (tmp, val);
	buf_write (b, tmp, 4);
}

static void buf_patch_u16 (byte_buf_t *b, size_t offset, u16 val, bool le)
{
	if (offset + 2 <= b->size)
	{
		if (le)
			wr_le16 (b->data + offset, val);
		else
			wr_be16 (b->data + offset, val);
	}
}

static void buf_patch_u32 (byte_buf_t *b, size_t offset, u32 val, bool le)
{
	if (offset + 4 <= b->size)
	{
		if (le)
			wr_le32 (b->data + offset, val);
		else
			wr_be32 (b->data + offset, val);
	}
}

static void buf_align (byte_buf_t *b, size_t align)
{
	size_t rem = b->size % align;
	if (rem)
	{
		size_t pad = align - rem;
		buf_reserve (b, pad);
		memset (b->data + b->size, 0, pad);
		b->size += pad;
	}
}

// V1 Writer
static void aamp_write_v1_entry (byte_buf_t *b, const aamp_param_entry_t *entry, bool le)
{
	size_t start = b->size;
	buf_write_u32 (b, 0, le); // size placeholder
	buf_write_u32 (b, (u32)entry->type, le);
	buf_write_u32 (b, entry->hash, le);

	switch (entry->type)
	{
		case AAMP_TYPE_BOOL:
		{
			u8 byte_val = entry->b ? 1 : 0;
			buf_write (b, &byte_val, 1);
			break;
		}
		case AAMP_TYPE_FLOAT:
			buf_write_u32 (b, *(u32 *)&entry->f, le);
			break;
		case AAMP_TYPE_INT:
			buf_write_u32 (b, (u32)entry->i, le);
			break;
		case AAMP_TYPE_UINT:
			buf_write_u32 (b, entry->u, le);
			break;
		case AAMP_TYPE_VEC2:
			buf_write_u32 (b, *(u32 *)&entry->vec[0], le);
			buf_write_u32 (b, *(u32 *)&entry->vec[1], le);
			break;
		case AAMP_TYPE_VEC3:
			buf_write_u32 (b, *(u32 *)&entry->vec[0], le);
			buf_write_u32 (b, *(u32 *)&entry->vec[1], le);
			buf_write_u32 (b, *(u32 *)&entry->vec[2], le);
			break;
		case AAMP_TYPE_VEC4:
		case AAMP_TYPE_COLOR:
		case AAMP_TYPE_QUAT:
			buf_write_u32 (b, *(u32 *)&entry->vec[0], le);
			buf_write_u32 (b, *(u32 *)&entry->vec[1], le);
			buf_write_u32 (b, *(u32 *)&entry->vec[2], le);
			buf_write_u32 (b, *(u32 *)&entry->vec[3], le);
			break;
		case AAMP_TYPE_STRING32:
		case AAMP_TYPE_STRING64:
		case AAMP_TYPE_STRING256:
		case AAMP_TYPE_STRING_REF:
		{
			const char *s = entry->str ? entry->str : "";
			buf_write (b, s, strlen (s) + 1);
			break;
		}
		case AAMP_TYPE_BUFFER_INT:
		case AAMP_TYPE_BUFFER_UINT:
		case AAMP_TYPE_BUFFER_FLOAT:
		{
			if (entry->buf.data && entry->buf.count)
			{
				if (le)
					buf_write (b, entry->buf.data, entry->buf.count * 4);
				else
				{
					const u32 *src = (const u32 *)entry->buf.data;
					for (u32 i = 0; i < entry->buf.count; i++)
						buf_write_u32 (b, src[i], le);
				}
			}
			break;
		}
		case AAMP_TYPE_BUFFER_BIN:
			if (entry->buf.data && entry->buf.count)
				buf_write (b, entry->buf.data, entry->buf.count);
			break;
		case AAMP_TYPE_CURVE1:
		case AAMP_TYPE_CURVE2:
		case AAMP_TYPE_CURVE3:
		case AAMP_TYPE_CURVE4:
		{
			for (u32 c = 0; c < entry->curve.count; c++)
			{
				buf_write_u32 (b, entry->curve.curves[c].uints[0], le);
				buf_write_u32 (b, entry->curve.curves[c].uints[1], le);
				for (int f = 0; f < 30; f++)
					buf_write_u32 (b, *(u32 *)&entry->curve.curves[c].floats[f], le);
			}
			break;
		}
		default:
			break;
	}
	buf_align (b, 4);
	buf_patch_u32 (b, start, (u32)(b->size - start), le);
}

static void aamp_write_v1_object (byte_buf_t *b, const aamp_param_object_t *obj, bool le)
{
	size_t start = b->size;
	buf_write_u32 (b, 0, le); // size placeholder
	buf_write_u32 (b, obj->entry_count, le);
	buf_write_u32 (b, obj->hash, le);
	buf_write_u32 (b, obj->group_hash, le);

	for (u32 i = 0; i < obj->entry_count; i++)
		aamp_write_v1_entry (b, &obj->entries[i], le);

	buf_patch_u32 (b, start, (u32)(b->size - start), le);
}

static void aamp_write_v1_list (byte_buf_t *b, const aamp_param_list_t *list, bool le)
{
	size_t start = b->size;
	buf_write_u32 (b, 0, le); // size placeholder
	buf_write_u32 (b, list->hash, le);
	buf_write_u32 (b, list->list_count, le);
	buf_write_u32 (b, list->object_count, le);

	for (u32 i = 0; i < list->list_count; i++)
		aamp_write_v1_list (b, &list->lists[i], le);

	for (u32 i = 0; i < list->object_count; i++)
		aamp_write_v1_object (b, &list->objects[i], le);

	buf_patch_u32 (b, start, (u32)(b->size - start), le);
}

static enumError aamp_write_v1 (
	const aamp_file_t *aamp, u8 **dest, size_t *dest_size, bool le)
{
	byte_buf_t b;
	buf_init (&b);

	buf_write (&b, "AAMP", 4);
	buf_write_u32 (&b, 1, le);
	buf_write_u32 (&b, 0, le); // endianness
	buf_write_u32 (&b, 0, le); // file size placeholder
	buf_write_u32 (&b, aamp->pio_version, le);

	size_t name_len = strlen (aamp->pio_type);
	size_t padded_name_len = (name_len + 4) & ~3u;
	buf_write_u32 (&b, (u32)padded_name_len, le);
	buf_write (&b, aamp->pio_type, name_len);
	buf_align (&b, 4);

	aamp_write_v1_list (&b, &aamp->root, le);

	buf_patch_u32 (&b, 12, (u32)b.size, le);

	*dest = b.data;
	*dest_size = b.size;
	return ERR_OK;
}

// V2 Writer (BFS list/object layout, data/string pooling, division by 4 relative offsets)
typedef struct v2_list_node_t
{
	const aamp_param_list_t *list;
	size_t header_pos;
} v2_list_node_t;

typedef struct v2_object_node_t
{
	const aamp_param_object_t *obj;
	size_t header_pos;
} v2_object_node_t;

typedef struct v2_param_node_t
{
	const aamp_param_entry_t *entry;
	size_t header_pos;
} v2_param_node_t;

typedef struct v2_pool_entry_t
{
	const void *payload;
	size_t payload_len;
	bool is_buffer;
	u32 buffer_count;
	size_t written_offset; // relative to entry header
	size_t *entry_field4_offsets;
	u32 entry_count;
	u32 entry_alloc;
} v2_pool_entry_t;

static enumError aamp_write_v2 (
	const aamp_file_t *aamp, u8 **dest, size_t *dest_size, bool le)
{
	byte_buf_t b;
	buf_init (&b);

	buf_write (&b, "AAMP", 4);
	buf_write_u32 (&b, 2, le);
	buf_write_u32 (&b, 0, le); // flags / endianness
	buf_write_u32 (&b, 0, le); // file size placeholder (pos 12)
	buf_write_u32 (&b, aamp->pio_version, le); // pos 16

	size_t type_len = strlen (aamp->pio_type) + 1;
	size_t pio_offset = (type_len + 3) & ~3u;
	buf_write_u32 (&b, (u32)pio_offset, le); // pos 20

	size_t counts_pos = b.size;
	buf_write_u32 (&b, 0, le); // list count (pos 24)
	buf_write_u32 (&b, 0, le); // obj count (pos 28)
	buf_write_u32 (&b, 0, le); // param count (pos 32)
	buf_write_u32 (&b, 0, le); // data section size (pos 36)
	buf_write_u32 (&b, 0, le); // string section size (pos 40)
	buf_write_u32 (&b, 0, le); // unk uint count (pos 44)

	buf_write (&b, aamp->pio_type, strlen (aamp->pio_type) + 1);
	buf_align (&b, 4);

	// Queue for BFS traversal
	u32 total_lists = 0;
	u32 total_objs = 0;
	u32 total_params = 0;

	v2_list_node_t *lists = MALLOC (1024 * sizeof (v2_list_node_t));
	u32 list_cnt = 0, list_cap = 1024;

	lists[list_cnt].list = &aamp->root;
	lists[list_cnt].header_pos = b.size;
	list_cnt++;
	total_lists++;

	// Write root list header
	buf_write_u32 (&b, aamp->root.hash, le);
	buf_write_u16 (&b, 0, le); // child list offset placeholder
	buf_write_u16 (&b, (u16)aamp->root.list_count, le);
	buf_write_u16 (&b, 0, le); // obj offset placeholder
	buf_write_u16 (&b, (u16)aamp->root.object_count, le);

	// Write child lists headers BFS
	u32 cur_list = 0;
	while (cur_list < list_cnt)
	{
		const aamp_param_list_t *cur = lists[cur_list].list;
		size_t cur_pos = lists[cur_list].header_pos;
		cur_list++;

		if (cur->list_count > 0)
		{
			size_t child_start = b.size;
			u16 rel_off = (u16)((child_start - cur_pos) / 4);
			buf_patch_u16 (&b, cur_pos + 4, rel_off, le);

			for (u32 i = 0; i < cur->list_count; i++)
			{
				if (list_cnt >= list_cap)
				{
					list_cap *= 2;
					lists = REALLOC (lists, list_cap * sizeof (v2_list_node_t));
				}
				lists[list_cnt].list = &cur->lists[i];
				lists[list_cnt].header_pos = b.size;
				list_cnt++;
				total_lists++;

				buf_write_u32 (&b, cur->lists[i].hash, le);
				buf_write_u16 (&b, 0, le); // child list offset
				buf_write_u16 (&b, (u16)cur->lists[i].list_count, le);
				buf_write_u16 (&b, 0, le); // obj offset
				buf_write_u16 (&b, (u16)cur->lists[i].object_count, le);
			}
		}
	}

	// Write objects headers for all lists
	v2_object_node_t *objs = MALLOC (2048 * sizeof (v2_object_node_t));
	u32 obj_cnt = 0, obj_cap = 2048;

	for (u32 i = 0; i < list_cnt; i++)
	{
		const aamp_param_list_t *l = lists[i].list;
		size_t lpos = lists[i].header_pos;

		if (l->object_count > 0)
		{
			size_t obj_start = b.size;
			u16 rel_off = (u16)((obj_start - lpos) / 4);
			buf_patch_u16 (&b, lpos + 8, rel_off, le);

			for (u32 o = 0; o < l->object_count; o++)
			{
				if (obj_cnt >= obj_cap)
				{
					obj_cap *= 2;
					objs = REALLOC (objs, obj_cap * sizeof (v2_object_node_t));
				}
				objs[obj_cnt].obj = &l->objects[o];
				objs[obj_cnt].header_pos = b.size;
				obj_cnt++;
				total_objs++;

				buf_write_u32 (&b, l->objects[o].hash, le);
				buf_write_u16 (&b, 0, le); // child param offset placeholder
				buf_write_u16 (&b, (u16)l->objects[o].entry_count, le);
			}
		}
	}
	FREE (lists);

	// Write param entry headers for all objects
	v2_param_node_t *params = MALLOC (4096 * sizeof (v2_param_node_t));
	u32 param_cnt = 0, param_cap = 4096;

	for (u32 i = 0; i < obj_cnt; i++)
	{
		const aamp_param_object_t *o = objs[i].obj;
		size_t opos = objs[i].header_pos;

		if (o->entry_count > 0)
		{
			size_t param_start = b.size;
			u16 rel_off = (u16)((param_start - opos) / 4);
			buf_patch_u16 (&b, opos + 4, rel_off, le);

			for (u32 p = 0; p < o->entry_count; p++)
			{
				if (param_cnt >= param_cap)
				{
					param_cap *= 2;
					params = REALLOC (params, param_cap * sizeof (v2_param_node_t));
				}
				params[param_cnt].entry = &o->entries[p];
				params[param_cnt].header_pos = b.size;
				param_cnt++;
				total_params++;

				buf_write_u32 (&b, o->entries[p].hash, le);
				buf_write_u32 (&b, 0, le); // field4 placeholder (pos + 4)
			}
		}
	}
	FREE (objs);

	// Write Data Section
	size_t data_start = b.size;
	for (u32 i = 0; i < param_cnt; i++)
	{
		const aamp_param_entry_t *e = params[i].entry;
		size_t epos = params[i].header_pos;

		bool is_str = (e->type == AAMP_TYPE_STRING32 || e->type == AAMP_TYPE_STRING64
			|| e->type == AAMP_TYPE_STRING256 || e->type == AAMP_TYPE_STRING_REF);
		if (is_str)
			continue; // written in string section below

		bool is_buf = (e->type == AAMP_TYPE_BUFFER_INT || e->type == AAMP_TYPE_BUFFER_FLOAT
			|| e->type == AAMP_TYPE_BUFFER_UINT || e->type == AAMP_TYPE_BUFFER_BIN);

		if (is_buf)
		{
			buf_write_u32 (&b, e->buf.count, le); // buffer length header immediately precedes data
			size_t payload_pos = b.size;
			u32 rel_off = (u32)((payload_pos - epos) / 4);
			u32 field4 = (rel_off & 0x00FFFFFF) | ((u32)e->type << 24);
			buf_patch_u32 (&b, epos + 4, field4, le);

			size_t elem_sz = (e->type == AAMP_TYPE_BUFFER_BIN) ? 1 : 4;
			if (e->buf.data && e->buf.count)
			{
				if (elem_sz == 1 || le)
					buf_write (&b, e->buf.data, e->buf.count * elem_sz);
				else
				{
					const u32 *src = (const u32 *)e->buf.data;
					for (u32 k = 0; k < e->buf.count; k++)
						buf_write_u32 (&b, src[k], le);
				}
			}
			buf_align (&b, 4);
		}
		else
		{
			size_t payload_pos = b.size;
			u32 rel_off = (u32)((payload_pos - epos) / 4);
			u32 field4 = (rel_off & 0x00FFFFFF) | ((u32)e->type << 24);
			buf_patch_u32 (&b, epos + 4, field4, le);

			switch (e->type)
			{
				case AAMP_TYPE_BOOL:
					buf_write_u32 (&b, e->b ? 1 : 0, le);
					break;
				case AAMP_TYPE_FLOAT:
					buf_write_u32 (&b, *(u32 *)&e->f, le);
					break;
				case AAMP_TYPE_INT:
					buf_write_u32 (&b, (u32)e->i, le);
					break;
				case AAMP_TYPE_UINT:
					buf_write_u32 (&b, e->u, le);
					break;
				case AAMP_TYPE_VEC2:
					buf_write_u32 (&b, *(u32 *)&e->vec[0], le);
					buf_write_u32 (&b, *(u32 *)&e->vec[1], le);
					break;
				case AAMP_TYPE_VEC3:
					buf_write_u32 (&b, *(u32 *)&e->vec[0], le);
					buf_write_u32 (&b, *(u32 *)&e->vec[1], le);
					buf_write_u32 (&b, *(u32 *)&e->vec[2], le);
					break;
				case AAMP_TYPE_VEC4:
				case AAMP_TYPE_COLOR:
				case AAMP_TYPE_QUAT:
					buf_write_u32 (&b, *(u32 *)&e->vec[0], le);
					buf_write_u32 (&b, *(u32 *)&e->vec[1], le);
					buf_write_u32 (&b, *(u32 *)&e->vec[2], le);
					buf_write_u32 (&b, *(u32 *)&e->vec[3], le);
					break;
				case AAMP_TYPE_CURVE1:
				case AAMP_TYPE_CURVE2:
				case AAMP_TYPE_CURVE3:
				case AAMP_TYPE_CURVE4:
					for (u32 c = 0; c < e->curve.count; c++)
					{
						buf_write_u32 (&b, e->curve.curves[c].uints[0], le);
						buf_write_u32 (&b, e->curve.curves[c].uints[1], le);
						for (int f = 0; f < 30; f++)
							buf_write_u32 (&b, *(u32 *)&e->curve.curves[c].floats[f], le);
					}
					break;
				default:
					break;
			}
			buf_align (&b, 4);
		}
	}
	size_t data_end = b.size;

	// Write String Section
	size_t string_start = b.size;
	for (u32 i = 0; i < param_cnt; i++)
	{
		const aamp_param_entry_t *e = params[i].entry;
		size_t epos = params[i].header_pos;

		bool is_str = (e->type == AAMP_TYPE_STRING32 || e->type == AAMP_TYPE_STRING64
			|| e->type == AAMP_TYPE_STRING256 || e->type == AAMP_TYPE_STRING_REF);
		if (!is_str)
			continue;

		const char *s = e->str ? e->str : "";
		size_t slen = strlen (s);
		size_t str_pos = b.size;
		u32 rel_off = (u32)((str_pos - epos) / 4);
		u32 field4 = (rel_off & 0x00FFFFFF) | ((u32)e->type << 24);
		buf_patch_u32 (&b, epos + 4, field4, le);

		buf_write (&b, s, slen + 1);
		buf_align (&b, 4);
	}
	size_t string_end = b.size;
	FREE (params);

	// Patch counts and section sizes
	buf_patch_u32 (&b, counts_pos, total_lists, le);
	buf_patch_u32 (&b, counts_pos + 4, total_objs, le);
	buf_patch_u32 (&b, counts_pos + 8, total_params, le);
	buf_patch_u32 (&b, counts_pos + 12, (u32)(data_end - data_start), le);
	buf_patch_u32 (&b, counts_pos + 16, (u32)(string_end - string_start), le);

	// Patch total file size
	buf_patch_u32 (&b, 12, (u32)b.size, le);

	*dest = b.data;
	*dest_size = b.size;
	return ERR_OK;
}

enumError WriteAAMP (
	const aamp_file_t *aamp, u8 **dest, size_t *dest_size, u32 version, bool is_le)
{
	if (!aamp || !dest || !dest_size)
		return ERR_INVALID_DATA;
	if (version == 1)
		return aamp_write_v1 (aamp, dest, dest_size, is_le);
	else
		return aamp_write_v2 (aamp, dest, dest_size, is_le);
}

///////////////////////////////////////////////////////////////////////////////
// YAML Decoding (AAMP Binary -> YAML Text)
///////////////////////////////////////////////////////////////////////////////

static void yaml_indent (FILE *out, int indent)
{
	for (int i = 0; i < indent; i++)
		fputc (' ', out);
}

static void print_yaml_hash (FILE *out, u32 hash)
{
	char buf[32];
	const char *name = AAMP_HashToName (hash, buf, sizeof (buf));
	if (strchr (name, ':') || strchr (name, ' ') || strchr (name, '#'))
		fprintf (out, "\"%s\"", name);
	else
		fprintf (out, "%s", name);
}

static void decode_yaml_entry (FILE *out, const aamp_param_entry_t *entry, int indent)
{
	yaml_indent (out, indent);
	print_yaml_hash (out, entry->hash);
	fprintf (out, ": ");

	switch (entry->type)
	{
		case AAMP_TYPE_BOOL:
			fprintf (out, "%s\n", entry->b ? "true" : "false");
			break;
		case AAMP_TYPE_FLOAT:
			if (fabsf (entry->f - roundf (entry->f)) < 1e-6f && fabsf (entry->f) < 1e9f)
				fprintf (out, "%.1f\n", entry->f);
			else
				fprintf (out, "%.7g\n", entry->f);
			break;
		case AAMP_TYPE_INT:
			fprintf (out, "%d\n", entry->i);
			break;
		case AAMP_TYPE_UINT:
			fprintf (out, "%u\n", entry->u);
			break;
		case AAMP_TYPE_VEC2:
			fprintf (out, "!vec2 [%.7g, %.7g]\n", entry->vec[0], entry->vec[1]);
			break;
		case AAMP_TYPE_VEC3:
			fprintf (out, "!vec3 [%.7g, %.7g, %.7g]\n", entry->vec[0], entry->vec[1], entry->vec[2]);
			break;
		case AAMP_TYPE_VEC4:
			fprintf (out, "!vec4 [%.7g, %.7g, %.7g, %.7g]\n",
				entry->vec[0], entry->vec[1], entry->vec[2], entry->vec[3]);
			break;
		case AAMP_TYPE_COLOR:
			fprintf (out, "!color [%.7g, %.7g, %.7g, %.7g]\n",
				entry->vec[0], entry->vec[1], entry->vec[2], entry->vec[3]);
			break;
		case AAMP_TYPE_QUAT:
			fprintf (out, "!quat [%.7g, %.7g, %.7g, %.7g]\n",
				entry->vec[0], entry->vec[1], entry->vec[2], entry->vec[3]);
			break;
		case AAMP_TYPE_STRING32:
			fprintf (out, "!str32 %s\n", entry->str ? entry->str : "");
			break;
		case AAMP_TYPE_STRING64:
			fprintf (out, "!str64 %s\n", entry->str ? entry->str : "");
			break;
		case AAMP_TYPE_STRING256:
			fprintf (out, "!str256 %s\n", entry->str ? entry->str : "");
			break;
		case AAMP_TYPE_STRING_REF:
			fprintf (out, "!strRef %s\n", entry->str ? entry->str : "");
			break;
		case AAMP_TYPE_BUFFER_INT:
		{
			fprintf (out, "!BufferInt [ ");
			const s32 *vals = (const s32 *)entry->buf.data;
			for (u32 k = 0; k < entry->buf.count; k++)
				fprintf (out, "%s%d", k > 0 ? ", " : "", vals[k]);
			fprintf (out, " ]\n");
			break;
		}
		case AAMP_TYPE_BUFFER_UINT:
		{
			fprintf (out, "!BufferUint [ ");
			const u32 *vals = (const u32 *)entry->buf.data;
			for (u32 k = 0; k < entry->buf.count; k++)
				fprintf (out, "%s%u", k > 0 ? ", " : "", vals[k]);
			fprintf (out, " ]\n");
			break;
		}
		case AAMP_TYPE_BUFFER_FLOAT:
		{
			fprintf (out, "!BufferFloat [ ");
			const float *vals = (const float *)entry->buf.data;
			for (u32 k = 0; k < entry->buf.count; k++)
				fprintf (out, "%s%.7g", k > 0 ? ", " : "", vals[k]);
			fprintf (out, " ]\n");
			break;
		}
		case AAMP_TYPE_BUFFER_BIN:
		{
			fprintf (out, "!BufferBinary [ ");
			const u8 *vals = (const u8 *)entry->buf.data;
			for (u32 k = 0; k < entry->buf.count; k++)
				fprintf (out, "%s%u", k > 0 ? ", " : "", (u32)vals[k]);
			fprintf (out, " ]\n");
			break;
		}
		case AAMP_TYPE_CURVE1:
		case AAMP_TYPE_CURVE2:
		case AAMP_TYPE_CURVE3:
		case AAMP_TYPE_CURVE4:
		{
			fprintf (out, "!curve%u [", entry->curve.count);
			for (u32 c = 0; c < entry->curve.count; c++)
			{
				if (c > 0)
					fprintf (out, ", ");
				fprintf (out, "%u,%u", entry->curve.curves[c].uints[0], entry->curve.curves[c].uints[1]);
				for (int f = 0; f < 30; f++)
					fprintf (out, ",%.7g", entry->curve.curves[c].floats[f]);
			}
			fprintf (out, "]\n");
			break;
		}
		default:
			fprintf (out, "null\n");
			break;
	}
}

static void decode_yaml_object (FILE *out, const aamp_param_object_t *obj, int indent)
{
	yaml_indent (out, indent);
	print_yaml_hash (out, obj->hash);
	fprintf (out, ": !obj\n");

	for (u32 i = 0; i < obj->entry_count; i++)
		decode_yaml_entry (out, &obj->entries[i], indent + 2);
}

static void decode_yaml_list (FILE *out, const aamp_param_list_t *list, int indent)
{
	yaml_indent (out, indent);
	print_yaml_hash (out, list->hash);
	fprintf (out, ": !list\n");

	if (list->object_count == 0)
	{
		yaml_indent (out, indent + 2);
		fprintf (out, "objects: {}\n");
	}
	else
	{
		yaml_indent (out, indent + 2);
		fprintf (out, "objects:\n");
		for (u32 i = 0; i < list->object_count; i++)
			decode_yaml_object (out, &list->objects[i], indent + 4);
	}

	if (list->list_count == 0)
	{
		yaml_indent (out, indent + 2);
		fprintf (out, "lists: {}\n");
	}
	else
	{
		yaml_indent (out, indent + 2);
		fprintf (out, "lists:\n");
		for (u32 i = 0; i < list->list_count; i++)
			decode_yaml_list (out, &list->lists[i], indent + 4);
	}
}

enumError DecodeAAMP_YAML (FILE *out, const u8 *data, size_t size)
{
	if (!out || !data || !size)
		return ERR_INVALID_DATA;

	aamp_file_t aamp;
	enumError err = ScanAAMP (&aamp, data, size);
	if (err)
		return err;

	fprintf (out, "aamp_version: %u\n", aamp.version);
	fprintf (out, "io_version: %u\n", aamp.pio_version);
	fprintf (out, "type: %s\n", aamp.pio_type);
	fprintf (out, "endian: %s\n", aamp.is_le ? "little" : "big");

	decode_yaml_list (out, &aamp.root, 0);

	ResetAAMP (&aamp);
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
// YAML Encoding (YAML Text -> AAMP Binary)
///////////////////////////////////////////////////////////////////////////////

typedef struct text_parser_t
{
	const char *text;
	size_t len;
	size_t pos;
} text_parser_t;

static void parse_skip_ws (text_parser_t *tp)
{
	while (tp->pos < tp->len && (tp->text[tp->pos] == ' ' || tp->text[tp->pos] == '\t'
		|| tp->text[tp->pos] == '\r' || tp->text[tp->pos] == '\n'))
		tp->pos++;
}

static void aamp_add_entry (aamp_param_object_t *obj, const aamp_param_entry_t *entry)
{
	if (obj->entry_count >= obj->entry_alloc)
	{
		obj->entry_alloc = obj->entry_alloc ? obj->entry_alloc * 2 : 16;
		obj->entries = REALLOC (obj->entries, obj->entry_alloc * sizeof (aamp_param_entry_t));
	}
	obj->entries[obj->entry_count++] = *entry;
}

static aamp_param_object_t *aamp_add_object (aamp_param_list_t *list, u32 hash)
{
	if (list->object_count >= list->object_alloc)
	{
		list->object_alloc = list->object_alloc ? list->object_alloc * 2 : 8;
		list->objects = REALLOC (list->objects, list->object_alloc * sizeof (aamp_param_object_t));
	}
	aamp_param_object_t *obj = &list->objects[list->object_count++];
	memset (obj, 0, sizeof (*obj));
	obj->hash = hash;
	return obj;
}

static aamp_param_list_t *aamp_add_list (aamp_param_list_t *list, u32 hash)
{
	if (list->list_count >= list->list_alloc)
	{
		list->list_alloc = list->list_alloc ? list->list_alloc * 2 : 8;
		list->lists = REALLOC (list->lists, list->list_alloc * sizeof (aamp_param_list_t));
	}
	aamp_param_list_t *child = &list->lists[list->list_count++];
	memset (child, 0, sizeof (*child));
	child->hash = hash;
	return child;
}

static int aamp_b64_val (char c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

static u8 *aamp_b64_decode (const char *src, size_t *out_len)
{
	size_t len = strlen (src);
	u8 *out = MALLOC (len + 4);
	size_t o = 0;
	for (size_t i = 0; i < len; )
	{
		while (i < len && (src[i] == ' ' || src[i] == '\t' || src[i] == '\r' || src[i] == '\n'))
			i++;
		if (i >= len) break;
		int a = aamp_b64_val (src[i++]);
		if (i >= len) break;
		int b = aamp_b64_val (src[i++]);
		if (a < 0 || b < 0) break;
		out[o++] = (u8)((a << 2) | (b >> 4));
		if (i < len && src[i] != '=')
		{
			int c = aamp_b64_val (src[i++]);
			if (c < 0) break;
			out[o++] = (u8)(((b & 0xf) << 4) | (c >> 2));
			if (i < len && src[i] != '=')
			{
				int d = aamp_b64_val (src[i++]);
				if (d < 0) break;
				out[o++] = (u8)(((c & 0x3) << 6) | d);
			}
			else if (i < len) i++;
		}
		else if (i < len) i++;
	}
	*out_len = o;
	return out;
}

static void parse_yaml_values_seq (yaml_parser_t *parser, yaml_event_t *ev, aamp_param_entry_t *entry, const char *tag)
{
	float fvals[256];
	u32 uvals[256];
	s32 ivals[256];
	u8 bvals[256];
	u32 count = 0;

	while (1)
	{
		yaml_event_t sub_ev;
		if (!yaml_parser_parse (parser, &sub_ev))
			break;
		if (sub_ev.type == YAML_SEQUENCE_END_EVENT)
		{
			yaml_event_delete (&sub_ev);
			break;
		}
		if (sub_ev.type == YAML_SCALAR_EVENT)
		{
			const char *v = (const char *)sub_ev.data.scalar.value;
			if (count < 256)
			{
				fvals[count] = (float)atof (v);
				uvals[count] = (u32)strtoul (v, NULL, 0);
				ivals[count] = (s32)strtol (v, NULL, 0);
				bvals[count] = (u8)strtoul (v, NULL, 0);
				count++;
			}
		}
		yaml_event_delete (&sub_ev);
	}

	if (!strcmp (tag, "!vec2"))
	{
		entry->type = AAMP_TYPE_VEC2;
		entry->vec[0] = count > 0 ? fvals[0] : 0.0f;
		entry->vec[1] = count > 1 ? fvals[1] : 0.0f;
	}
	else if (!strcmp (tag, "!vec3"))
	{
		entry->type = AAMP_TYPE_VEC3;
		entry->vec[0] = count > 0 ? fvals[0] : 0.0f;
		entry->vec[1] = count > 1 ? fvals[1] : 0.0f;
		entry->vec[2] = count > 2 ? fvals[2] : 0.0f;
	}
	else if (!strcmp (tag, "!vec4"))
	{
		entry->type = AAMP_TYPE_VEC4;
		entry->vec[0] = count > 0 ? fvals[0] : 0.0f;
		entry->vec[1] = count > 1 ? fvals[1] : 0.0f;
		entry->vec[2] = count > 2 ? fvals[2] : 0.0f;
		entry->vec[3] = count > 3 ? fvals[3] : 0.0f;
	}
	else if (!strcmp (tag, "!quat"))
	{
		entry->type = AAMP_TYPE_QUAT;
		entry->vec[0] = count > 0 ? fvals[0] : 0.0f;
		entry->vec[1] = count > 1 ? fvals[1] : 0.0f;
		entry->vec[2] = count > 2 ? fvals[2] : 0.0f;
		entry->vec[3] = count > 3 ? fvals[3] : 0.0f;
	}
	else if (!strcmp (tag, "!color"))
	{
		entry->type = AAMP_TYPE_COLOR;
		entry->vec[0] = count > 0 ? fvals[0] : 0.0f;
		entry->vec[1] = count > 1 ? fvals[1] : 0.0f;
		entry->vec[2] = count > 2 ? fvals[2] : 0.0f;
		entry->vec[3] = count > 3 ? fvals[3] : 0.0f;
	}
	else if (!strcmp (tag, "!BufferInt"))
	{
		entry->type = AAMP_TYPE_BUFFER_INT;
		entry->buf.count = count;
		entry->buf.data = MALLOC (count * 4);
		memcpy (entry->buf.data, ivals, count * 4);
	}
	else if (!strcmp (tag, "!BufferUint"))
	{
		entry->type = AAMP_TYPE_BUFFER_UINT;
		entry->buf.count = count;
		entry->buf.data = MALLOC (count * 4);
		memcpy (entry->buf.data, uvals, count * 4);
	}
	else if (!strcmp (tag, "!BufferFloat"))
	{
		entry->type = AAMP_TYPE_BUFFER_FLOAT;
		entry->buf.count = count;
		entry->buf.data = MALLOC (count * 4);
		memcpy (entry->buf.data, fvals, count * 4);
	}
	else if (!strcmp (tag, "!BufferBinary"))
	{
		entry->type = AAMP_TYPE_BUFFER_BIN;
		entry->buf.count = count;
		entry->buf.data = MALLOC (count);
		memcpy (entry->buf.data, bvals, count);
	}
	else if (!strncmp (tag, "!curve", 6))
	{
		u32 num_curves = (u32)(tag[6] - '0');
		if (num_curves < 1)
			num_curves = 1;
		if (num_curves > 4)
			num_curves = 4;
		entry->type = (aamp_param_type_t)(AAMP_TYPE_CURVE1 + num_curves - 1);
		entry->curve.count = num_curves;
		entry->curve.curves = CALLOC (num_curves, sizeof (aamp_curve_t));
		u32 idx = 0;
		for (u32 c = 0; c < num_curves; c++)
		{
			if (idx < count) entry->curve.curves[c].uints[0] = uvals[idx++];
			if (idx < count) entry->curve.curves[c].uints[1] = uvals[idx++];
			for (int f = 0; f < 30; f++)
			{
				if (idx < count)
					entry->curve.curves[c].floats[f] = fvals[idx++];
			}
		}
	}
}

static void parse_yaml_param (yaml_parser_t *parser, aamp_param_object_t *obj, const char *key, yaml_event_t *val_ev)
{
	aamp_param_entry_t entry;
	memset (&entry, 0, sizeof (entry));
	entry.hash = AAMP_NameToHash (key);

	const char *tag = (const char *)val_ev->data.scalar.tag;
	if (!tag)
		tag = (const char *)val_ev->data.sequence_start.tag;

	if (val_ev->type == YAML_SEQUENCE_START_EVENT)
	{
		parse_yaml_values_seq (parser, val_ev, &entry, tag ? tag : "");
	}
	else if (val_ev->type == YAML_SCALAR_EVENT)
	{
		const char *val = (const char *)val_ev->data.scalar.value;
		if (tag && !strcmp (tag, "!str32"))
		{
			entry.type = AAMP_TYPE_STRING32;
			entry.str = STRDUP (val);
		}
		else if (tag && !strcmp (tag, "!str64"))
		{
			entry.type = AAMP_TYPE_STRING64;
			entry.str = STRDUP (val);
		}
		else if (tag && !strcmp (tag, "!str256"))
		{
			entry.type = AAMP_TYPE_STRING256;
			entry.str = STRDUP (val);
		}
		else if (tag && !strcmp (tag, "!strRef"))
		{
			entry.type = AAMP_TYPE_STRING_REF;
			entry.str = STRDUP (val);
		}
		else if (tag && !strcmp (tag, "!BufferBinary"))
		{
			entry.type = AAMP_TYPE_BUFFER_BIN;
			size_t blen = 0;
			entry.buf.data = aamp_b64_decode (val, &blen);
			entry.buf.count = (u32)blen;
		}
		else if (!strcmp (val, "true") || !strcmp (val, "false"))
		{
			entry.type = AAMP_TYPE_BOOL;
			entry.b = !strcmp (val, "true");
		}
		else if (strchr (val, '.'))
		{
			entry.type = AAMP_TYPE_FLOAT;
			entry.f = (float)atof (val);
		}
		else if (val[0] == '-')
		{
			entry.type = AAMP_TYPE_INT;
			entry.i = (s32)strtol (val, NULL, 0);
		}
		else
		{
			char *end = NULL;
			unsigned long u = strtoul (val, &end, 0);
			if (end && !*end)
			{
				entry.type = AAMP_TYPE_UINT;
				entry.u = (u32)u;
			}
			else
			{
				entry.type = AAMP_TYPE_STRING32;
				entry.str = STRDUP (val);
			}
		}
	}
	aamp_add_entry (obj, &entry);
}

static void parse_yaml_mapping_recursive (yaml_parser_t *parser, aamp_file_t *aamp, aamp_param_list_t *cur_list, aamp_param_object_t *cur_obj)
{
	char key[256];
	key[0] = '\0';
	bool has_key = false;

	while (1)
	{
		yaml_event_t ev;
		if (!yaml_parser_parse (parser, &ev))
			break;

		if (ev.type == YAML_MAPPING_END_EVENT)
		{
			yaml_event_delete (&ev);
			break;
		}

		if (!has_key)
		{
			if (ev.type == YAML_SCALAR_EVENT)
			{
				snprintf (key, sizeof (key), "%s", (const char *)ev.data.scalar.value);
				has_key = true;
			}
			yaml_event_delete (&ev);
			continue;
		}

		has_key = false;

		// We have key and ev is the value
		if (!cur_list && !cur_obj)
		{
			// Top-level
			if (!strcmp (key, "aamp_version") && ev.type == YAML_SCALAR_EVENT)
				aamp->version = (u32)atoi ((const char *)ev.data.scalar.value);
			else if (!strcmp (key, "io_version") && ev.type == YAML_SCALAR_EVENT)
				aamp->pio_version = (u32)atoi ((const char *)ev.data.scalar.value);
			else if (!strcmp (key, "type") && ev.type == YAML_SCALAR_EVENT)
				snprintf (aamp->pio_type, sizeof (aamp->pio_type), "%s", (const char *)ev.data.scalar.value);
			else if ((!strcmp (key, "endian") || !strcmp (key, "byte_order")) && ev.type == YAML_SCALAR_EVENT)
			{
				const char *val = (const char *)ev.data.scalar.value;
				if (!strcasecmp (val, "big") || !strcasecmp (val, "be") || !strcasecmp (val, "BigEndian"))
					aamp->is_le = false;
				else if (!strcasecmp (val, "little") || !strcasecmp (val, "le") || !strcasecmp (val, "LittleEndian"))
					aamp->is_le = true;
			}
			else if (!strcmp (key, "is_le") && ev.type == YAML_SCALAR_EVENT)
			{
				const char *val = (const char *)ev.data.scalar.value;
				aamp->is_le = (!strcasecmp (val, "true") || !strcmp (val, "1"));
			}
			else if (ev.type == YAML_MAPPING_START_EVENT)
			{
				aamp->root.hash = AAMP_NameToHash (key);
				parse_yaml_mapping_recursive (parser, aamp, &aamp->root, NULL);
			}
		}
		else if (cur_list)
		{
			if (!strcmp (key, "objects"))
			{
				if (ev.type == YAML_MAPPING_START_EVENT)
				{
					// Object list mapping
					while (1)
					{
						yaml_event_t oev;
						if (!yaml_parser_parse (parser, &oev))
							break;
						if (oev.type == YAML_MAPPING_END_EVENT)
						{
							yaml_event_delete (&oev);
							break;
						}
						if (oev.type == YAML_SCALAR_EVENT)
						{
							char obj_name[256];
							snprintf (obj_name, sizeof (obj_name), "%s", (const char *)oev.data.scalar.value);
							yaml_event_t sub;
							if (yaml_parser_parse (parser, &sub))
							{
								if (sub.type == YAML_MAPPING_START_EVENT)
								{
									aamp_param_object_t *new_obj = aamp_add_object (cur_list, AAMP_NameToHash (obj_name));
									parse_yaml_mapping_recursive (parser, aamp, NULL, new_obj);
								}
								yaml_event_delete (&sub);
							}
						}
						yaml_event_delete (&oev);
					}
				}
			}
			else if (!strcmp (key, "lists"))
			{
				if (ev.type == YAML_MAPPING_START_EVENT)
				{
					while (1)
					{
						yaml_event_t lev;
						if (!yaml_parser_parse (parser, &lev))
							break;
						if (lev.type == YAML_MAPPING_END_EVENT)
						{
							yaml_event_delete (&lev);
							break;
						}
						if (lev.type == YAML_SCALAR_EVENT)
						{
							char list_name[256];
							snprintf (list_name, sizeof (list_name), "%s", (const char *)lev.data.scalar.value);
							yaml_event_t sub;
							if (yaml_parser_parse (parser, &sub))
							{
								if (sub.type == YAML_MAPPING_START_EVENT)
								{
									aamp_param_list_t *new_list = aamp_add_list (cur_list, AAMP_NameToHash (list_name));
									parse_yaml_mapping_recursive (parser, aamp, new_list, NULL);
								}
								yaml_event_delete (&sub);
							}
						}
						yaml_event_delete (&lev);
					}
				}
			}
			else if (ev.type == YAML_MAPPING_START_EVENT)
			{
				const char *tag = (const char *)ev.data.mapping_start.tag;
				if (tag && !strcmp (tag, "!obj"))
				{
					aamp_param_object_t *new_obj = aamp_add_object (cur_list, AAMP_NameToHash (key));
					parse_yaml_mapping_recursive (parser, aamp, NULL, new_obj);
				}
				else
				{
					aamp_param_list_t *new_list = aamp_add_list (cur_list, AAMP_NameToHash (key));
					parse_yaml_mapping_recursive (parser, aamp, new_list, NULL);
				}
			}
		}
		else if (cur_obj)
		{
			parse_yaml_param (parser, cur_obj, key, &ev);
		}
		yaml_event_delete (&ev);
	}
}

enumError EncodeAAMP_Text (
	u8 **dest, uint *dest_size, const char *text, uint text_len, u32 target_version, bool is_le)
{
	if (!dest || !dest_size || !text)
		return ERR_INVALID_DATA;

	yaml_parser_t parser;
	if (!yaml_parser_initialize (&parser))
		return ERR_OUT_OF_MEMORY;

	yaml_parser_set_input_string (&parser, (const unsigned char *)text, text_len);

	aamp_file_t aamp;
	InitializeAAMP (&aamp);
	aamp.version = target_version ? target_version : 2;
	aamp.is_le = is_le;

	yaml_event_t event;
	while (yaml_parser_parse (&parser, &event))
	{
		if (event.type == YAML_STREAM_END_EVENT)
		{
			yaml_event_delete (&event);
			break;
		}
		if (event.type == YAML_MAPPING_START_EVENT)
		{
			yaml_event_delete (&event);
			parse_yaml_mapping_recursive (&parser, &aamp, NULL, NULL);
			break;
		}
		yaml_event_delete (&event);
	}
	yaml_parser_delete (&parser);

	size_t bin_size = 0;
	u8 *bin_data = NULL;
	enumError err = WriteAAMP (&aamp, &bin_data, &bin_size, aamp.version, aamp.is_le);
	ResetAAMP (&aamp);
	if (err)
		return err;

	*dest = bin_data;
	*dest_size = (uint)bin_size;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
// JSON Decoding
///////////////////////////////////////////////////////////////////////////////

static void json_indent (FILE *out, int indent)
{
	for (int i = 0; i < indent; i++)
		fputc (' ', out);
}

static void print_json_string (FILE *out, const char *s)
{
	fputc ('"', out);
	for (const char *p = s; *p; p++)
	{
		if (*p == '"')
			fputs ("\\\"", out);
		else if (*p == '\\')
			fputs ("\\\\", out);
		else if (*p == '\n')
			fputs ("\\n", out);
		else if (*p == '\r')
			fputs ("\\r", out);
		else if (*p == '\t')
			fputs ("\\t", out);
		else
			fputc (*p, out);
	}
	fputc ('"', out);
}

static void decode_json_entry (FILE *out, const aamp_param_entry_t *entry, int indent)
{
	char name_buf[32];
	const char *name = AAMP_HashToName (entry->hash, name_buf, sizeof (name_buf));

	json_indent (out, indent);
	print_json_string (out, name);
	fputs (": {\n", out);

	json_indent (out, indent + 2);
	fprintf (out, "\"type\": %u,\n", (u32)entry->type);

	json_indent (out, indent + 2);
	fputs ("\"value\": ", out);

	switch (entry->type)
	{
		case AAMP_TYPE_BOOL:
			fprintf (out, "%s\n", entry->b ? "true" : "false");
			break;
		case AAMP_TYPE_FLOAT:
			fprintf (out, "%.7g\n", entry->f);
			break;
		case AAMP_TYPE_INT:
			fprintf (out, "%d\n", entry->i);
			break;
		case AAMP_TYPE_UINT:
			fprintf (out, "%u\n", entry->u);
			break;
		case AAMP_TYPE_VEC2:
			fprintf (out, "[%.7g, %.7g]\n", entry->vec[0], entry->vec[1]);
			break;
		case AAMP_TYPE_VEC3:
			fprintf (out, "[%.7g, %.7g, %.7g]\n", entry->vec[0], entry->vec[1], entry->vec[2]);
			break;
		case AAMP_TYPE_VEC4:
		case AAMP_TYPE_COLOR:
		case AAMP_TYPE_QUAT:
			fprintf (out, "[%.7g, %.7g, %.7g, %.7g]\n",
				entry->vec[0], entry->vec[1], entry->vec[2], entry->vec[3]);
			break;
		case AAMP_TYPE_STRING32:
		case AAMP_TYPE_STRING64:
		case AAMP_TYPE_STRING256:
		case AAMP_TYPE_STRING_REF:
			print_json_string (out, entry->str ? entry->str : "");
			fputc ('\n', out);
			break;
		case AAMP_TYPE_BUFFER_INT:
		{
			fputs ("[", out);
			const s32 *vals = (const s32 *)entry->buf.data;
			for (u32 k = 0; k < entry->buf.count; k++)
				fprintf (out, "%s%d", k > 0 ? ", " : "", vals[k]);
			fputs ("]\n", out);
			break;
		}
		case AAMP_TYPE_BUFFER_UINT:
		{
			fputs ("[", out);
			const u32 *vals = (const u32 *)entry->buf.data;
			for (u32 k = 0; k < entry->buf.count; k++)
				fprintf (out, "%s%u", k > 0 ? ", " : "", vals[k]);
			fputs ("]\n", out);
			break;
		}
		case AAMP_TYPE_BUFFER_FLOAT:
		{
			fputs ("[", out);
			const float *vals = (const float *)entry->buf.data;
			for (u32 k = 0; k < entry->buf.count; k++)
				fprintf (out, "%s%.7g", k > 0 ? ", " : "", vals[k]);
			fputs ("]\n", out);
			break;
		}
		case AAMP_TYPE_BUFFER_BIN:
		{
			fputs ("[", out);
			const u8 *vals = (const u8 *)entry->buf.data;
			for (u32 k = 0; k < entry->buf.count; k++)
				fprintf (out, "%s%u", k > 0 ? ", " : "", (u32)vals[k]);
			fputs ("]\n", out);
			break;
		}
		case AAMP_TYPE_CURVE1:
		case AAMP_TYPE_CURVE2:
		case AAMP_TYPE_CURVE3:
		case AAMP_TYPE_CURVE4:
		{
			fputs ("[", out);
			for (u32 c = 0; c < entry->curve.count; c++)
			{
				if (c > 0)
					fputs (", ", out);
				fprintf (out, "[%u, %u", entry->curve.curves[c].uints[0], entry->curve.curves[c].uints[1]);
				for (int f = 0; f < 30; f++)
					fprintf (out, ", %.7g", entry->curve.curves[c].floats[f]);
				fputs ("]", out);
			}
			fputs ("]\n", out);
			break;
		}
		default:
			fputs ("null\n", out);
			break;
	}

	json_indent (out, indent);
	fputs ("}", out);
}

static void decode_json_object (FILE *out, const aamp_param_object_t *obj, int indent)
{
	char name_buf[32];
	const char *name = AAMP_HashToName (obj->hash, name_buf, sizeof (name_buf));

	json_indent (out, indent);
	print_json_string (out, name);
	fputs (": {\n", out);

	for (u32 i = 0; i < obj->entry_count; i++)
	{
		decode_json_entry (out, &obj->entries[i], indent + 2);
		if (i + 1 < obj->entry_count)
			fputs (",\n", out);
		else
			fputc ('\n', out);
	}

	json_indent (out, indent);
	fputs ("}", out);
}

static void decode_json_list (FILE *out, const aamp_param_list_t *list, int indent)
{
	char name_buf[32];
	const char *name = AAMP_HashToName (list->hash, name_buf, sizeof (name_buf));

	json_indent (out, indent);
	print_json_string (out, name);
	fputs (": {\n", out);

	json_indent (out, indent + 2);
	fputs ("\"objects\": {\n", out);
	for (u32 i = 0; i < list->object_count; i++)
	{
		decode_json_object (out, &list->objects[i], indent + 4);
		if (i + 1 < list->object_count)
			fputs (",\n", out);
		else
			fputc ('\n', out);
	}
	json_indent (out, indent + 2);
	fputs ("},\n", out);

	json_indent (out, indent + 2);
	fputs ("\"lists\": {\n", out);
	for (u32 i = 0; i < list->list_count; i++)
	{
		decode_json_list (out, &list->lists[i], indent + 4);
		if (i + 1 < list->list_count)
			fputs (",\n", out);
		else
			fputc ('\n', out);
	}
	json_indent (out, indent + 2);
	fputs ("}\n", out);

	json_indent (out, indent);
	fputs ("}", out);
}

enumError DecodeAAMP_JSON (FILE *out, const u8 *data, size_t size)
{
	if (!out || !data || !size)
		return ERR_INVALID_DATA;

	aamp_file_t aamp;
	enumError err = ScanAAMP (&aamp, data, size);
	if (err)
		return err;

	fputs ("{\n", out);
	fprintf (out, "  \"aamp_version\": %u,\n", aamp.version);
	fprintf (out, "  \"io_version\": %u,\n", aamp.pio_version);
	fprintf (out, "  \"endian\": \"%s\",\n", aamp.is_le ? "little" : "big");
	fputs ("  \"type\": ", out);
	print_json_string (out, aamp.pio_type);
	fputs (",\n", out);

	decode_json_list (out, &aamp.root, 2);
	fputs ("\n}\n", out);

	ResetAAMP (&aamp);
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
// High-Level File Helpers & Structure Dump
///////////////////////////////////////////////////////////////////////////////

enumError decode_aamp_file (ccp source, ccp dest)
{
	u8 *raw = NULL;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (source, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err || !raw)
		return err ? err : ERR_NOTHING_TO_DO;

	File_t F;
	CreateFileOpt (&F, true, dest, false, source);
	if (!F.f)
	{
		FREE (raw);
		return ERR_CANT_CREATE;
	}

	ccp ext = strrchr (dest, '.');
	if (ext && !strcasecmp (ext, ".json"))
		err = DecodeAAMP_JSON (F.f, raw, raw_size);
	else
		err = DecodeAAMP_YAML (F.f, raw, raw_size);

	ResetFile (&F, opt_preserve);
	FREE (raw);
	return err;
}

enumError encode_aamp_file (ccp source, ccp dest)
{
	u8 *text = NULL;
	size_t text_len = 0;
	enumError err = LoadFileAlloc (source, 0, 0, &text, &text_len, 16 << 20, 0, 0, false);
	if (err || !text)
		return err ? err : ERR_NOTHING_TO_DO;

	u8 *aamp_bin = NULL;
	uint aamp_size = 0;
	bool is_le = true;
	ccp ext = strrchr (dest, '.');
	if (ext && !strcasecmp (ext, ".be"))
		is_le = false;

	err = EncodeAAMP_Text (&aamp_bin, &aamp_size, (const char *)text, (uint)text_len, 2, is_le);
	FREE (text);
	if (err)
		return err;

	if (!testmode)
	{
		File_t F;
		CreateFILE (&F, true, dest, testmode, false, true, false, false);
		if (F.f && fwrite (aamp_bin, 1, aamp_size, F.f) != aamp_size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing AAMP failed: %s\n", dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (aamp_bin);
	return err;
}

enumError decode_aamp_if_possible (ccp arg)
{
	ccp arg_ext = strrchr (arg, '.');
	if (!arg_ext
	    || (strcasecmp (arg_ext, ".aamp") && strcasecmp (arg_ext, ".bparam")
	        && strcasecmp (arg_ext, ".baamp") && strcasecmp (arg_ext, ".bgenv")))
		return ERR_NOTHING_TO_DO;

	u8 head[32];
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOT_EXISTS;
	size_t got = fread (head, 1, sizeof (head), f);
	fclose (f);
	if (got < 24 || memcmp (head, "AAMP", 4) != 0)
		return ERR_NOTHING_TO_DO;

	u8 *data = NULL;
	size_t size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &data, &size, 0, 0, 0, false);
	if (err || !data)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, "\1N.aamp.yml", ".aamp.yml", false);
	else
		snprintf (dest, sizeof (dest), "%s.yml", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE AAMP:%s -> YML:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);
	if (testmode)
	{
		FREE (data);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (!F.f)
	{
		FREE (data);
		return err;
	}
	err = DecodeAAMP_YAML (F.f, data, size);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err;
}

static void dump_list_inner (FILE *out, const aamp_param_list_t *l, int ind)
{
	char name_buf[32];
	const char *name = AAMP_HashToName (l->hash, name_buf, sizeof (name_buf));
	fprintf (out, "%*sList: %s (hash 0x%08X), %u lists, %u objects\n",
		ind, "", name, l->hash, l->list_count, l->object_count);

	for (u32 o = 0; o < l->object_count; o++)
	{
		const aamp_param_object_t *obj = &l->objects[o];
		const char *oname = AAMP_HashToName (obj->hash, name_buf, sizeof (name_buf));
		fprintf (out, "%*sObject: %s (hash 0x%08X), %u params\n",
			ind + 2, "", oname, obj->hash, obj->entry_count);
		for (u32 p = 0; p < obj->entry_count; p++)
		{
			const aamp_param_entry_t *e = &obj->entries[p];
			const char *pname = AAMP_HashToName (e->hash, name_buf, sizeof (name_buf));
			fprintf (out, "%*sParam: %s (type %u)\n", ind + 4, "", pname, (u32)e->type);
		}
	}

	for (u32 c = 0; c < l->list_count; c++)
		dump_list_inner (out, &l->lists[c], ind + 2);
}

void DumpStructureAAMP (FILE *out, const aamp_file_t *aamp, int indent)
{
	if (!out || !aamp)
		return;
	fprintf (out, "%*s[AAMP File] Version %u, PIO Version %u, Type '%s'\n",
		indent, "", aamp->version, aamp->pio_version, aamp->pio_type);

	dump_list_inner (out, &aamp->root, indent + 2);
}

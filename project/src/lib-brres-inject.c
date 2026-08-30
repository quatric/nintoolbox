#include "lib-brres-inject.h"
#include "lib-szs.h"
#include "lib-brres.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dirent.h>
#include <unistd.h>

#pragma pack(push, 1)

typedef struct
{
	char magic[4]; // "bres"
	uint16_t endian; // 0xfeff
	uint16_t version; // 0
	uint32_t fileSize;
	uint16_t headerSize; // 0x10
	uint16_t numSections; // number of root sections (usually 1 or 2)
} BRESHeader;

typedef struct
{
	char tag[4]; // "root"
	uint32_t size;
} BRESSectionHeader;

typedef struct
{
	char tag[4]; // "MDL0"
	uint32_t size;
	uint32_t version; // 8, 9, 10, 11
	int32_t bresOffset; // negative offset to BRES header or 0
	int32_t sectionOffsets[14];
} MDL0Header;

typedef struct
{
	int32_t dataLen;
	int32_t mdl0Offset;
	int32_t dataOffset;
	int32_t stringOffset;
	int32_t index;
	int32_t isXYZ;
	int32_t type;
	uint8_t divisor;
	uint8_t entryStride;
	uint16_t numVertices;
	float extents[6];
	int32_t pad1;
	int32_t pad2;
} MDL0VertexHeader;

typedef struct
{
	int32_t dataLen;
	int32_t mdl0Offset;
	int32_t dataOffset;
	int32_t stringOffset;
	int32_t index;
	int32_t isNBT;
	int32_t type;
	uint8_t divisor;
	uint8_t entryStride;
	uint16_t numVertices;
} MDL0NormalHeader;

typedef struct
{
	int32_t dataLen;
	int32_t mdl0Offset;
	int32_t dataOffset;
	int32_t stringOffset;
	int32_t index;
	int32_t isRGBA;
	int32_t format;
	uint8_t entryStride;
	uint8_t pad;
	uint16_t numEntries;
} MDL0ColorHeader;

typedef struct
{
	int32_t dataLen;
	int32_t mdl0Offset;
	int32_t dataOffset;
	int32_t stringOffset;
	int32_t index;
	int32_t isST;
	int32_t format;
	uint8_t divisor;
	uint8_t entryStride;
	uint16_t numEntries;
	float min[2];
	float max[2];
	int32_t pad[4];
} MDL0UVHeader;

typedef struct
{
	int32_t bufferSize;
	int32_t size;
	int32_t offset;
} PrimGroup;

typedef struct
{
	int32_t totalLength;
	int32_t mdl0Offset;
	int32_t nodeId;
	uint32_t vertexFormatLo;
	uint32_t vertexFormatHi;
	uint32_t vertexSpecs;
	PrimGroup defintions;
	PrimGroup primitives;
	uint32_t arrayFlags;
	int32_t flag;
	int32_t stringOffset;
	int32_t index;
	int32_t numVertices;
	int32_t numFaces;
	int16_t vertexId;
	int16_t normalId;
	int16_t colorIds[2];
	int16_t uvIds[8];
} MDL0ObjHeader;

typedef struct
{
	uint32_t size;
	uint32_t numNodes;
} ResGroup;

typedef struct
{
	uint16_t id;
	uint16_t flag;
	uint16_t leftIndex;
	uint16_t rightIndex;
	int32_t stringOffset;
	int32_t dataOffset;
} ResEntry;

#pragma pack(pop)

static inline uint16_t to_be16 (uint16_t v)
{
	return (v << 8) | (v >> 8);
}
static inline uint32_t to_be32 (uint32_t v)
{
	return ((v << 24) & 0xff000000) | ((v << 8) & 0x00ff0000) | ((v >> 8) & 0x0000ff00)
		| ((v >> 24) & 0x000000ff);
}
static inline float to_bef (float v)
{
	uint32_t t;
	memcpy (&t, &v, 4);
	t = to_be32 (t);
	float out;
	memcpy (&out, &t, 4);
	return out;
}

static size_t align32 (size_t n)
{
	return (n + 31) & ~31;
}
static size_t align4 (size_t n)
{
	return (n + 3) & ~3;
}

// Patricia binary string table entry
typedef struct patricia_node
{
	char name[128];
	int id;
	int index;
	struct patricia_node *left;
	struct patricia_node *right;
} patricia_node_t;

static int compare_bits (int b1, int b2)
{
	for (int i = 8, b = 0x80; i-- != 0; b >>= 1)
	{
		if ((b1 & b) != (b2 & b))
			return i;
	}
	return 0;
}

static bool node_is_right (const patricia_node_t *node, const patricia_node_t *entry)
{
	size_t nlen = strlen (node->name);
	size_t elen = strlen (entry->name);
	if (nlen != elen)
		return false;
	int bit_idx = node->id;
	size_t char_idx = (size_t)(bit_idx >> 3);
	if (char_idx >= elen)
		return false;
	return ((entry->name[char_idx] >> (bit_idx & 7)) & 1) != 0;
}

static int node_generate_id (patricia_node_t *entry, patricia_node_t *comp)
{
	size_t elen = strlen (entry->name);
	size_t clen = strlen (comp->name);
	size_t len = elen < clen ? elen : clen;
	for (int i = (int)len; i-- > 0;)
	{
		if (entry->name[i] != comp->name[i])
		{
			entry->id = (i << 3) | compare_bits (entry->name[i], comp->name[i]);
			if (node_is_right (entry, comp))
			{
				entry->left = entry;
				entry->right = comp;
			}
			else
			{
				entry->left = comp;
				entry->right = entry;
			}
			return entry->id;
		}
	}
	return 0;
}

static void patricia_insert_left (patricia_node_t *prev, patricia_node_t *entry)
{
	if (node_is_right (entry, prev->left))
		entry->right = prev->left;
	else
		entry->left = prev->left;
	prev->left = entry;
}

static void patricia_insert_right (patricia_node_t *prev, patricia_node_t *entry)
{
	if (node_is_right (entry, prev->right))
		entry->right = prev->right;
	else
		entry->left = prev->right;
	prev->right = entry;
}

// Build a BRES ResourceGroup buffer containing names, Patricia binary search entries, and string
// table
static uint8_t *build_resource_group (
	size_t num_items, const char **names, const int32_t *data_offsets, size_t *out_group_size)
{
	size_t total_nodes = num_items + 1;
	patricia_node_t *nodes = CALLOC (total_nodes, sizeof (patricia_node_t));
	if (!nodes)
		return NULL;

	// Root node
	nodes[0].name[0] = 0;
	nodes[0].id = -1;
	nodes[0].index = 0;
	nodes[0].left = &nodes[0];
	nodes[0].right = &nodes[0];

	for (size_t i = 0; i < num_items; i++)
	{
		patricia_node_t *entry = &nodes[i + 1];
		snprintf (entry->name, sizeof (entry->name), "%s", names[i]);
		entry->index = (int)(i + 1);
		entry->left = entry;
		entry->right = entry;
		size_t slen = strlen (entry->name);
		entry->id = slen ? (((int)(slen - 1) << 3) | compare_bits (entry->name[slen - 1], 0)) : -1;

		// Traverse
		patricia_node_t *current = nodes[0].left, *prev = &nodes[0];
		bool is_right = false;
		while (entry->id <= current->id)
		{
			if (entry->id == current->id)
				node_generate_id (entry, current);
			is_right = node_is_right (current, entry);
			prev = current;
			current = is_right ? current->right : current->left;
			if (prev->id <= current->id)
				break;
		}
		if (is_right)
			patricia_insert_right (prev, entry);
		else
			patricia_insert_left (prev, entry);
	}

	// Compute size: ResGroup (8) + total_nodes * ResEntry (16) + strings
	size_t header_and_entries = sizeof (ResGroup) + total_nodes * sizeof (ResEntry);
	size_t strings_size = 0;
	for (size_t i = 0; i < num_items; i++)
	{
		size_t slen = strlen (names[i]);
		strings_size += 4 + align4 (slen + 1); // 4-byte length prefix + null-terminated string
	}

	size_t group_size = align32 (header_and_entries + strings_size);
	uint8_t *buf = CALLOC (1, group_size);
	if (!buf)
	{
		FREE (nodes);
		return NULL;
	}

	ResGroup *grp = (ResGroup *)buf;
	grp->size = to_be32 ((uint32_t)group_size);
	grp->numNodes = to_be32 ((uint32_t)num_items);

	ResEntry *entries = (ResEntry *)(buf + sizeof (ResGroup));
	uint8_t *str_cursor = buf + header_and_entries;

	// Root entry
	entries[0].id = to_be16 (0xffff);
	entries[0].flag = 0;
	entries[0].leftIndex = to_be16 ((uint16_t)nodes[0].left->index);
	entries[0].rightIndex = to_be16 ((uint16_t)nodes[0].right->index);
	entries[0].stringOffset = 0;
	entries[0].dataOffset = 0;

	for (size_t i = 0; i < num_items; i++)
	{
		ResEntry *e = &entries[i + 1];
		patricia_node_t *n = &nodes[i + 1];
		e->id = to_be16 ((uint16_t)n->id);
		e->flag = 0;
		e->leftIndex = to_be16 ((uint16_t)n->left->index);
		e->rightIndex = to_be16 ((uint16_t)n->right->index);

		// Write string: 4-byte big-endian len + string
		size_t slen = strlen (names[i]);
		int32_t str_offset = (int32_t)(str_cursor - buf);
		*(uint32_t *)str_cursor = to_be32 ((uint32_t)slen);
		memcpy (str_cursor + 4, names[i], slen + 1);
		str_cursor += 4 + align4 (slen + 1);

		e->stringOffset = to_be32 ((uint32_t)str_offset);
		e->dataOffset = to_be32 ((uint32_t)data_offsets[i]);
	}

	FREE (nodes);
	*out_group_size = group_size;
	return buf;
}

// Extract pooled string from MDL0
static const char *get_mdl0_string (
	const uint8_t *mdl0, size_t size, int32_t str_offset_field, const uint8_t *base)
{
	if (!str_offset_field)
		return "";
	const uint8_t *ptr = base + str_offset_field;
	if (ptr < mdl0 || ptr + 4 > mdl0 + size)
		return "";
	uint32_t len = to_be32 (*(const uint32_t *)ptr);
	if (ptr + 4 + len > mdl0 + size)
		return "";
	return (const char *)(ptr + 4);
}

int InjectDAEIntoMDL0 (const uint8_t *mdl0_data, size_t mdl0_size, const model_t *dae_model,
	uint8_t **out_data, size_t *out_size)
{
	if (!mdl0_data || mdl0_size < sizeof (MDL0Header) || !dae_model || !out_data || !out_size)
		return 0;

	if (memcmp (mdl0_data, "MDL0", 4))
		return 0;
	const MDL0Header *in_hdr = (const MDL0Header *)mdl0_data;
	uint32_t version = to_be32 (in_hdr->version);
	int32_t in_size = to_be32 (in_hdr->size);
	if (in_size <= 0 || (size_t)in_size > mdl0_size)
		in_size = (int32_t)mdl0_size;

	// Offsets in parent MDL0
	int32_t off_defs = to_be32 (in_hdr->sectionOffsets[0]);
	int32_t off_bones = to_be32 (in_hdr->sectionOffsets[1]);
	int32_t off_mat = to_be32 (in_hdr->sectionOffsets[8]);
	int32_t off_shd = to_be32 (in_hdr->sectionOffsets[9]);
	int32_t off_obj = to_be32 (in_hdr->sectionOffsets[10]);
	int32_t off_tex = to_be32 (in_hdr->sectionOffsets[11]);
	int32_t off_plt = to_be32 (in_hdr->sectionOffsets[12]);
	int32_t off_usr
		= (version >= 10 && in_hdr->sectionOffsets[13]) ? to_be32 (in_hdr->sectionOffsets[13]) : 0;

	// Read parent objects to know how many objects exist
	if (off_obj <= 0 || off_obj + sizeof (ResGroup) > (size_t)in_size)
		return 0;
	const ResGroup *obj_grp = (const ResGroup *)(mdl0_data + off_obj);
	uint32_t num_objs = to_be32 (obj_grp->numNodes);
	if (!num_objs || num_objs > 256)
		return 0;

	const ResEntry *obj_entries = (const ResEntry *)(obj_grp + 1);

	// Buffers for new sections
	// For each object: build new Position, Normal, UV, and Object Primitives buffers
	typedef struct
	{
		char name[128];
		uint8_t *pos_buf;
		size_t pos_size;
		uint8_t *nrm_buf;
		size_t nrm_size;
		uint8_t *uv_buf;
		size_t uv_size;
		uint8_t *obj_buf;
		size_t obj_size;
	} new_obj_data_t;

	new_obj_data_t *new_objs = CALLOC (num_objs, sizeof (new_obj_data_t));
	if (!new_objs)
		return 0;

	float overall_min[3] = { 1e9f, 1e9f, 1e9f }, overall_max[3] = { -1e9f, -1e9f, -1e9f };

	for (uint32_t i = 0; i < num_objs; i++)
	{
		int32_t o_off = to_be32 (obj_entries[i + 1].dataOffset);
		const MDL0ObjHeader *orig_obj = (const MDL0ObjHeader *)((const uint8_t *)obj_grp + o_off);
		const char *obj_name = get_mdl0_string (
			mdl0_data, in_size, orig_obj->stringOffset, (const uint8_t *)orig_obj);
		if (!*obj_name)
			obj_name = get_mdl0_string (
				mdl0_data, in_size, obj_entries[i + 1].stringOffset, (const uint8_t *)obj_grp);
		snprintf (new_objs[i].name, sizeof (new_objs[i].name), "%s", obj_name);

		// Find matching mesh in dae_model
		const mesh_t *match_mesh = NULL;
		for (size_t m = 0; m < dae_model->num_meshes; m++)
		{
			if (!strcmp (dae_model->meshes[m].name, obj_name))
			{
				match_mesh = &dae_model->meshes[m];
				break;
			}
		}
		if (!match_mesh && dae_model->num_meshes == num_objs)
			match_mesh = &dae_model->meshes[i];
		if (!match_mesh && dae_model->num_meshes == 1)
			match_mesh = &dae_model->meshes[0];

		if (match_mesh && match_mesh->num_vertices > 0 && match_mesh->num_positions > 0)
		{
			// 1. Build Position Buffer (MDL0VertexHeader + floats)
			size_t pos_data_len = match_mesh->num_positions * 12;
			size_t pos_total_len = align32 (0x40 + pos_data_len);
			uint8_t *pbuf = CALLOC (1, pos_total_len);
			MDL0VertexHeader *vh = (MDL0VertexHeader *)pbuf;
			vh->dataLen = to_be32 ((uint32_t)pos_total_len);
			vh->dataOffset = to_be32 (0x40);
			vh->index = to_be32 (i);
			vh->isXYZ = to_be32 (1);
			vh->type = to_be32 (4); // GX_F32
			vh->divisor = 0;
			vh->entryStride = 12;
			vh->numVertices = to_be16 ((uint16_t)match_mesh->num_positions);

			float pmin[3] = { 1e9f, 1e9f, 1e9f }, pmax[3] = { -1e9f, -1e9f, -1e9f };
			float *pos_out = (float *)(pbuf + 0x40);
			for (size_t p = 0; p < match_mesh->num_positions; p++)
			{
				float x = match_mesh->positions[p].x;
				float y = match_mesh->positions[p].y;
				float z = match_mesh->positions[p].z;
				if (x < pmin[0])
					pmin[0] = x;
				if (x > pmax[0])
					pmax[0] = x;
				if (y < pmin[1])
					pmin[1] = y;
				if (y > pmax[1])
					pmax[1] = y;
				if (z < pmin[2])
					pmin[2] = z;
				if (z > pmax[2])
					pmax[2] = z;
				pos_out[p * 3 + 0] = to_bef (x);
				pos_out[p * 3 + 1] = to_bef (y);
				pos_out[p * 3 + 2] = to_bef (z);
			}
			for (int c = 0; c < 3; c++)
			{
				vh->extents[c] = to_bef (pmin[c]);
				vh->extents[c + 3] = to_bef (pmax[c]);
				if (pmin[c] < overall_min[c])
					overall_min[c] = pmin[c];
				if (pmax[c] > overall_max[c])
					overall_max[c] = pmax[c];
			}
			new_objs[i].pos_buf = pbuf;
			new_objs[i].pos_size = pos_total_len;

			// 2. Build Normal Buffer (MDL0NormalHeader + floats)
			if (match_mesh->num_normals > 0)
			{
				size_t nrm_data_len = match_mesh->num_normals * 12;
				size_t nrm_total_len = align32 (sizeof (MDL0NormalHeader) + nrm_data_len);
				uint8_t *nbuf = CALLOC (1, nrm_total_len);
				MDL0NormalHeader *nh = (MDL0NormalHeader *)nbuf;
				nh->dataLen = to_be32 ((uint32_t)nrm_total_len);
				nh->dataOffset = to_be32 (sizeof (MDL0NormalHeader));
				nh->index = to_be32 (i);
				nh->isNBT = to_be32 (0);
				nh->type = to_be32 (4); // GX_F32
				nh->divisor = 0;
				nh->entryStride = 12;
				nh->numVertices = to_be16 ((uint16_t)match_mesh->num_normals);

				float *nrm_out = (float *)(nbuf + sizeof (MDL0NormalHeader));
				for (size_t p = 0; p < match_mesh->num_normals; p++)
				{
					nrm_out[p * 3 + 0] = to_bef (match_mesh->normals[p].x);
					nrm_out[p * 3 + 1] = to_bef (match_mesh->normals[p].y);
					nrm_out[p * 3 + 2] = to_bef (match_mesh->normals[p].z);
				}
				new_objs[i].nrm_buf = nbuf;
				new_objs[i].nrm_size = nrm_total_len;
			}

			// 3. Build UV Buffer (MDL0UVHeader + floats)
			if (match_mesh->num_texcoords > 0)
			{
				size_t uv_data_len = match_mesh->num_texcoords * 8;
				size_t uv_total_len = align32 (sizeof (MDL0UVHeader) + uv_data_len);
				uint8_t *ubuf = CALLOC (1, uv_total_len);
				MDL0UVHeader *uh = (MDL0UVHeader *)ubuf;
				uh->dataLen = to_be32 ((uint32_t)uv_total_len);
				uh->dataOffset = to_be32 (sizeof (MDL0UVHeader));
				uh->index = to_be32 (i);
				uh->isST = to_be32 (1);
				uh->format = to_be32 (4); // GX_F32
				uh->divisor = 0;
				uh->entryStride = 8;
				uh->numEntries = to_be16 ((uint16_t)match_mesh->num_texcoords);

				float uvmin[2] = { 1e9f, 1e9f }, uvmax[2] = { -1e9f, -1e9f };
				float *uv_out = (float *)(ubuf + sizeof (MDL0UVHeader));
				for (size_t p = 0; p < match_mesh->num_texcoords; p++)
				{
					float u = match_mesh->texcoords[p].u;
					float v = 1.0f - match_mesh->texcoords[p].v; // GX top-left coordinate system
					if (u < uvmin[0])
						uvmin[0] = u;
					if (u > uvmax[0])
						uvmax[0] = u;
					if (v < uvmin[1])
						uvmin[1] = v;
					if (v > uvmax[1])
						uvmax[1] = v;
					uv_out[p * 2 + 0] = to_bef (u);
					uv_out[p * 2 + 1] = to_bef (v);
				}
				uh->min[0] = to_bef (uvmin[0]);
				uh->min[1] = to_bef (uvmin[1]);
				uh->max[0] = to_bef (uvmax[0]);
				uh->max[1] = to_bef (uvmax[1]);
				new_objs[i].uv_buf = ubuf;
				new_objs[i].uv_size = uv_total_len;
			}

			// 4. Build Display List & Object Header
			bool has_nrm = (match_mesh->num_normals > 0);
			bool has_uv = (match_mesh->num_texcoords > 0);

			// Configure standard 16-bit indices: Pos (idx16), Norm (idx16 if present), UV0 (idx16
			// if present)
			uint32_t new_cp_lo = 0;
			new_cp_lo |= (3 << 9); // Pos = 3 (index16)
			if (has_nrm)
				new_cp_lo |= (3 << 11); // Nrm = 3 (index16)

			uint32_t new_cp_hi = 0;
			if (has_uv)
				new_cp_hi |= 3; // Tex0 = 3 (index16)

			size_t vertex_stride = 2 + (has_nrm ? 2 : 0) + (has_uv ? 2 : 0);
			size_t num_vtx = match_mesh->num_vertices;
			size_t prim_stream_len
				= 1 + 2 + num_vtx * vertex_stride; // 0x90 + uint16 count + vertices
			size_t prim_padded_len = align32 (prim_stream_len);

			size_t obj_total_len = align32 (sizeof (MDL0ObjHeader) + prim_padded_len);
			uint8_t *obuf = CALLOC (1, obj_total_len);
			MDL0ObjHeader *oh = (MDL0ObjHeader *)obuf;

			// Copy base settings from parent object
			*oh = *orig_obj;
			oh->totalLength = to_be32 ((uint32_t)obj_total_len);
			oh->vertexFormatLo = to_be32 (new_cp_lo);
			oh->vertexFormatHi = to_be32 (new_cp_hi);
			oh->vertexSpecs = to_be32 (has_uv ? 1 : 0); // 1 texcoord
			oh->primitives.bufferSize = to_be32 ((uint32_t)prim_padded_len);
			oh->primitives.size = to_be32 ((uint32_t)prim_padded_len);
			oh->primitives.offset = to_be32 (sizeof (MDL0ObjHeader));
			oh->defintions.offset = to_be32 (0);
			oh->defintions.size = to_be32 (0);
			oh->defintions.bufferSize = to_be32 (0);
			oh->numVertices = to_be32 ((int32_t)num_vtx);
			oh->numFaces = to_be32 ((int32_t)(num_vtx / 3));
			oh->vertexId = to_be16 ((int16_t)i);
			oh->normalId = has_nrm ? to_be16 ((int16_t)i) : to_be16 (-1);
			oh->uvIds[0] = has_uv ? to_be16 ((int16_t)i) : to_be16 (-1);
			for (int u = 1; u < 8; u++)
				oh->uvIds[u] = to_be16 (-1);
			oh->colorIds[0] = to_be16 (-1);
			oh->colorIds[1] = to_be16 (-1);

			uint8_t *dl = obuf + sizeof (MDL0ObjHeader);
			dl[0] = 0x90; // GX_DRAW_TRIANGLES
			*(uint16_t *)(dl + 1) = to_be16 ((uint16_t)num_vtx);
			uint8_t *dl_vtx = dl + 3;

			// Triangles with reversed winding for GX hardware
			for (size_t t = 0; t < num_vtx; t += 3)
			{
				// Reverse (t, t+1, t+2) -> (t+2, t+1, t)
				size_t tri_indices[3] = { t + 2, t + 1, t };
				for (int v = 0; v < 3; v++)
				{
					const vertex_t *vtx = &match_mesh->vertices[tri_indices[v]];
					*(uint16_t *)dl_vtx = to_be16 ((uint16_t)vtx->position_idx);
					dl_vtx += 2;
					if (has_nrm)
					{
						*(uint16_t *)dl_vtx = to_be16 ((uint16_t)vtx->normal_idx);
						dl_vtx += 2;
					}
					if (has_uv)
					{
						*(uint16_t *)dl_vtx = to_be16 ((uint16_t)vtx->texcoord_idx);
						dl_vtx += 2;
					}
				}
			}

			new_objs[i].obj_buf = obuf;
			new_objs[i].obj_size = obj_total_len;
		}
		else
		{
			// Keep original object geometry
			// (Clone existing object bytes)
			size_t orig_len = to_be32 (orig_obj->totalLength);
			uint8_t *obuf = MALLOC (orig_len);
			memcpy (obuf, orig_obj, orig_len);
			new_objs[i].obj_buf = obuf;
			new_objs[i].obj_size = orig_len;
		}
	}

	// Now assemble new MDL0 binary
	// Layout:
	// Header (0x80 for v8/v9, 0x88 for v10, 0x8c for v11)
	size_t header_len = (version == 11) ? 0x8c : (version == 10) ? 0x88 : 0x80;

	// Defs & Bones (from parent)
	size_t defs_len = (off_bones > off_defs && off_defs > 0) ? (size_t)(off_bones - off_defs) : 0;
	size_t bones_len = (off_mat > off_bones && off_bones > 0) ? (size_t)(off_mat - off_bones) : 0;

	// Materials and Shaders (from parent, before Objects)
	size_t mat_shd_len = (off_obj > off_mat && off_mat > 0) ? (size_t)(off_obj - off_mat) : 0;

	// Textures, Palettes, UserData, String Pool (from parent, after Objects)
	size_t tex_pool_len
		= (off_tex > 0 && (size_t)in_size > (size_t)off_tex) ? (size_t)(in_size - off_tex) : 0;

	// Build Position ResourceGroup
	const char *pos_names[256];
	int32_t pos_offsets[256];
	size_t pos_group_hdr_size = 0;
	size_t cur_pos_data_off = sizeof (ResGroup) + (num_objs + 1) * sizeof (ResEntry);
	for (uint32_t i = 0; i < num_objs; i++)
	{
		pos_names[i] = new_objs[i].name;
		cur_pos_data_off += 4 + align4 (strlen (new_objs[i].name) + 1);
	}
	cur_pos_data_off = align32 (cur_pos_data_off);
	for (uint32_t i = 0; i < num_objs; i++)
	{
		pos_offsets[i] = (int32_t)cur_pos_data_off;
		cur_pos_data_off += new_objs[i].pos_size;
	}
	uint8_t *pos_grp_buf
		= build_resource_group (num_objs, pos_names, pos_offsets, &pos_group_hdr_size);
	size_t total_pos_section_size = cur_pos_data_off;

	// Build Normal ResourceGroup
	size_t num_nrms = 0;
	const char *nrm_names[256];
	int32_t nrm_offsets[256];
	for (uint32_t i = 0; i < num_objs; i++)
	{
		if (new_objs[i].nrm_size > 0)
		{
			nrm_names[num_nrms++] = new_objs[i].name;
		}
	}
	uint8_t *nrm_grp_buf = NULL;
	size_t nrm_group_hdr_size = 0, total_nrm_section_size = 0;
	if (num_nrms > 0)
	{
		size_t cur_nrm_data_off = sizeof (ResGroup) + (num_nrms + 1) * sizeof (ResEntry);
		for (size_t i = 0; i < num_nrms; i++)
			cur_nrm_data_off += 4 + align4 (strlen (nrm_names[i]) + 1);
		cur_nrm_data_off = align32 (cur_nrm_data_off);
		size_t n_idx = 0;
		for (uint32_t i = 0; i < num_objs; i++)
		{
			if (new_objs[i].nrm_size > 0)
			{
				nrm_offsets[n_idx++] = (int32_t)cur_nrm_data_off;
				cur_nrm_data_off += new_objs[i].nrm_size;
			}
		}
		nrm_grp_buf = build_resource_group (num_nrms, nrm_names, nrm_offsets, &nrm_group_hdr_size);
		total_nrm_section_size = cur_nrm_data_off;
	}

	// Build UV ResourceGroup
	size_t num_uvs = 0;
	const char *uv_names[256];
	int32_t uv_offsets[256];
	for (uint32_t i = 0; i < num_objs; i++)
	{
		if (new_objs[i].uv_size > 0)
		{
			uv_names[num_uvs++] = new_objs[i].name;
		}
	}
	uint8_t *uv_grp_buf = NULL;
	size_t uv_group_hdr_size = 0, total_uv_section_size = 0;
	if (num_uvs > 0)
	{
		size_t cur_uv_data_off = sizeof (ResGroup) + (num_uvs + 1) * sizeof (ResEntry);
		for (size_t i = 0; i < num_uvs; i++)
			cur_uv_data_off += 4 + align4 (strlen (uv_names[i]) + 1);
		cur_uv_data_off = align32 (cur_uv_data_off);
		size_t u_idx = 0;
		for (uint32_t i = 0; i < num_objs; i++)
		{
			if (new_objs[i].uv_size > 0)
			{
				uv_offsets[u_idx++] = (int32_t)cur_uv_data_off;
				cur_uv_data_off += new_objs[i].uv_size;
			}
		}
		uv_grp_buf = build_resource_group (num_uvs, uv_names, uv_offsets, &uv_group_hdr_size);
		total_uv_section_size = cur_uv_data_off;
	}

	// Build Objects ResourceGroup
	const char *obj_names[256];
	int32_t obj_offsets[256];
	size_t obj_group_hdr_size = 0;
	size_t cur_obj_data_off = sizeof (ResGroup) + (num_objs + 1) * sizeof (ResEntry);
	for (uint32_t i = 0; i < num_objs; i++)
	{
		obj_names[i] = new_objs[i].name;
		cur_obj_data_off += 4 + align4 (strlen (new_objs[i].name) + 1);
	}
	cur_obj_data_off = align32 (cur_obj_data_off);
	for (uint32_t i = 0; i < num_objs; i++)
	{
		obj_offsets[i] = (int32_t)cur_obj_data_off;
		cur_obj_data_off += new_objs[i].obj_size;
	}
	uint8_t *obj_grp_buf
		= build_resource_group (num_objs, obj_names, obj_offsets, &obj_group_hdr_size);
	size_t total_obj_section_size = cur_obj_data_off;

	// Compute absolute section offsets in new MDL0
	size_t offset_cursor = header_len;

	int32_t new_off_defs = 0;
	if (defs_len > 0)
	{
		new_off_defs = (int32_t)offset_cursor;
		offset_cursor += align32 (defs_len);
	}

	int32_t new_off_bones = 0;
	if (bones_len > 0)
	{
		new_off_bones = (int32_t)offset_cursor;
		offset_cursor += align32 (bones_len);
	}

	int32_t new_off_pos = (int32_t)offset_cursor;
	offset_cursor += align32 (total_pos_section_size);
	int32_t new_off_nrm = (num_nrms > 0) ? (int32_t)offset_cursor : 0;
	if (num_nrms > 0)
		offset_cursor += align32 (total_nrm_section_size);

	int32_t new_off_clr = 0;
	int32_t new_off_uv = (num_uvs > 0) ? (int32_t)offset_cursor : 0;
	if (num_uvs > 0)
		offset_cursor += align32 (total_uv_section_size);

	int32_t new_off_mat = (mat_shd_len > 0) ? (int32_t)offset_cursor : 0;
	int32_t diff_mat = new_off_mat ? (new_off_mat - off_mat) : 0;
	int32_t new_off_shd = (off_shd > 0) ? (off_shd + diff_mat) : 0;
	if (mat_shd_len > 0)
		offset_cursor += align32 (mat_shd_len);

	int32_t new_off_obj = (int32_t)offset_cursor;
	offset_cursor += align32 (total_obj_section_size);

	int32_t new_off_tex = (tex_pool_len > 0) ? (int32_t)offset_cursor : 0;
	int32_t diff_tex = new_off_tex ? (new_off_tex - off_tex) : 0;
	int32_t new_off_plt = (off_plt > 0) ? (off_plt + diff_tex) : 0;
	int32_t new_off_usr = (off_usr > 0) ? (off_usr + diff_tex) : 0;
	if (tex_pool_len > 0)
		offset_cursor += align32 (tex_pool_len);

	size_t total_mdl0_size = align32 (offset_cursor);
	uint8_t *new_mdl0 = CALLOC (1, total_mdl0_size);
	if (!new_mdl0)
	{
		FREE (pos_grp_buf);
		FREE (nrm_grp_buf);
		FREE (uv_grp_buf);
		FREE (obj_grp_buf);
		for (uint32_t i = 0; i < num_objs; i++)
		{
			FREE (new_objs[i].pos_buf);
			FREE (new_objs[i].nrm_buf);
			FREE (new_objs[i].uv_buf);
			FREE (new_objs[i].obj_buf);
		}
		FREE (new_objs);
		return 0;
	}

	// Write Header
	memcpy (new_mdl0, mdl0_data, header_len);
	MDL0Header *out_hdr = (MDL0Header *)new_mdl0;
	out_hdr->size = to_be32 ((uint32_t)total_mdl0_size);
	out_hdr->sectionOffsets[0] = to_be32 ((uint32_t)new_off_defs);
	out_hdr->sectionOffsets[1] = to_be32 ((uint32_t)new_off_bones);
	out_hdr->sectionOffsets[2] = to_be32 ((uint32_t)new_off_pos);
	out_hdr->sectionOffsets[3] = to_be32 ((uint32_t)new_off_nrm);
	out_hdr->sectionOffsets[4] = to_be32 ((uint32_t)new_off_clr);
	out_hdr->sectionOffsets[5] = to_be32 ((uint32_t)new_off_uv);
	out_hdr->sectionOffsets[6] = 0;
	out_hdr->sectionOffsets[7] = 0;
	out_hdr->sectionOffsets[8] = to_be32 ((uint32_t)new_off_mat);
	out_hdr->sectionOffsets[9] = to_be32 ((uint32_t)new_off_shd);
	out_hdr->sectionOffsets[10] = to_be32 ((uint32_t)new_off_obj);
	out_hdr->sectionOffsets[11] = to_be32 ((uint32_t)new_off_tex);
	out_hdr->sectionOffsets[12] = to_be32 ((uint32_t)new_off_plt);
	if (version >= 10 && header_len >= 0x88)
		*(uint32_t *)(new_mdl0 + 0x84) = to_be32 ((uint32_t)new_off_usr);

	// Update overall extents in header (offset 0x74)
	if (header_len >= 0x80 && overall_min[0] < overall_max[0])
	{
		float *ext_hdr = (float *)(new_mdl0 + 0x74);
		for (int c = 0; c < 3; c++)
		{
			ext_hdr[c] = to_bef (overall_min[c]);
			ext_hdr[c + 3] = to_bef (overall_max[c]);
		}
	}

	// Write Defs & Bones
	if (defs_len > 0)
		memcpy (new_mdl0 + new_off_defs, mdl0_data + off_defs, defs_len);
	if (bones_len > 0)
		memcpy (new_mdl0 + new_off_bones, mdl0_data + off_bones, bones_len);

	// Write Positions Section
	memcpy (new_mdl0 + new_off_pos, pos_grp_buf, pos_group_hdr_size);
	for (uint32_t i = 0; i < num_objs; i++)
	{
		uint8_t *dst = new_mdl0 + new_off_pos + pos_offsets[i];
		memcpy (dst, new_objs[i].pos_buf, new_objs[i].pos_size);
		MDL0VertexHeader *vh = (MDL0VertexHeader *)dst;
		vh->mdl0Offset = to_be32 ((int32_t)-(new_off_pos + pos_offsets[i]));
		vh->stringOffset = to_be32 ((int32_t)-(pos_offsets[i]
			- ((const ResEntry *)(pos_grp_buf + sizeof (ResGroup)))[i + 1].stringOffset));
	}

	// Write Normals Section
	if (num_nrms > 0)
	{
		memcpy (new_mdl0 + new_off_nrm, nrm_grp_buf, nrm_group_hdr_size);
		size_t n_idx = 0;
		for (uint32_t i = 0; i < num_objs; i++)
		{
			if (new_objs[i].nrm_size > 0)
			{
				uint8_t *dst = new_mdl0 + new_off_nrm + nrm_offsets[n_idx];
				memcpy (dst, new_objs[i].nrm_buf, new_objs[i].nrm_size);
				MDL0NormalHeader *nh = (MDL0NormalHeader *)dst;
				nh->mdl0Offset = to_be32 ((int32_t)-(new_off_nrm + nrm_offsets[n_idx]));
				nh->stringOffset = to_be32 ((int32_t)-(nrm_offsets[n_idx]
					- ((const ResEntry *)(nrm_grp_buf + sizeof (ResGroup)))[n_idx + 1]
						.stringOffset));
				n_idx++;
			}
		}
	}

	// Write UVs Section
	if (num_uvs > 0)
	{
		memcpy (new_mdl0 + new_off_uv, uv_grp_buf, uv_group_hdr_size);
		size_t u_idx = 0;
		for (uint32_t i = 0; i < num_objs; i++)
		{
			if (new_objs[i].uv_size > 0)
			{
				uint8_t *dst = new_mdl0 + new_off_uv + uv_offsets[u_idx];
				memcpy (dst, new_objs[i].uv_buf, new_objs[i].uv_size);
				MDL0UVHeader *uh = (MDL0UVHeader *)dst;
				uh->mdl0Offset = to_be32 ((int32_t)-(new_off_uv + uv_offsets[u_idx]));
				uh->stringOffset = to_be32 ((int32_t)-(uv_offsets[u_idx]
					- ((const ResEntry *)(uv_grp_buf + sizeof (ResGroup)))[u_idx + 1]
						.stringOffset));
				u_idx++;
			}
		}
	}

	// Write Materials and Shaders
	if (mat_shd_len > 0)
		memcpy (new_mdl0 + new_off_mat, mdl0_data + off_mat, mat_shd_len);

	// Write Objects Section
	memcpy (new_mdl0 + new_off_obj, obj_grp_buf, obj_group_hdr_size);
	for (uint32_t i = 0; i < num_objs; i++)
	{
		uint8_t *dst = new_mdl0 + new_off_obj + obj_offsets[i];
		memcpy (dst, new_objs[i].obj_buf, new_objs[i].obj_size);
		MDL0ObjHeader *oh = (MDL0ObjHeader *)dst;
		oh->mdl0Offset = to_be32 ((int32_t)-(new_off_obj + obj_offsets[i]));
		oh->stringOffset = to_be32 ((int32_t)-(obj_offsets[i]
			- ((const ResEntry *)(obj_grp_buf + sizeof (ResGroup)))[i + 1].stringOffset));
	}

	// Write Textures, Palettes, UserData, and String Pool
	if (tex_pool_len > 0)
		memcpy (new_mdl0 + new_off_tex, mdl0_data + off_tex, tex_pool_len);

	// Free temporary buffers
	FREE (pos_grp_buf);
	FREE (nrm_grp_buf);
	FREE (uv_grp_buf);
	FREE (obj_grp_buf);
	for (uint32_t i = 0; i < num_objs; i++)
	{
		FREE (new_objs[i].pos_buf);
		FREE (new_objs[i].nrm_buf);
		FREE (new_objs[i].uv_buf);
		FREE (new_objs[i].obj_buf);
	}
	FREE (new_objs);

	*out_data = new_mdl0;
	*out_size = total_mdl0_size;
	return 1;
}

static void get_wszst_cmd (char *buf, size_t buf_sz)
{
	char self_path[PATH_MAX];
	ssize_t len = readlink ("/proc/self/exe", self_path, sizeof (self_path) - 1);
	if (len > 0)
	{
		self_path[len] = 0;
		char *slash = strrchr (self_path, '/');
		if (slash)
		{
			*slash = 0;
			char test_path[PATH_MAX];
			snprintf (test_path, sizeof (test_path), "%s/wszst", self_path);
			if (!access (test_path, X_OK))
			{
				snprintf (buf, buf_sz, "\"%s\"", test_path);
				return;
			}
		}
	}
	snprintf (buf, buf_sz, "wszst");
}

int InjectDAEIntoBRRES (const uint8_t *brres_data, size_t brres_size, const model_t *dae_model,
	uint8_t **out_data, size_t *out_size)
{
	if (!brres_data || brres_size < sizeof (BRESHeader) + sizeof (ResGroup) || !dae_model
		|| !out_data || !out_size)
		return 0;

	// Check if input is raw MDL0 directly
	if (!memcmp (brres_data, "MDL0", 4))
		return InjectDAEIntoMDL0 (brres_data, brres_size, dae_model, out_data, out_size);

	if (memcmp (brres_data, "bres", 4))
		return 0;

	char temp_dir[PATH_MAX];
	snprintf (temp_dir, sizeof (temp_dir), "/tmp/_brres_inj_XXXXXX");
	if (!mkdtemp (temp_dir))
		return 0;

	// Extract brres_data into temp_dir
	szs_file_t ext_szs;
	InitializeSZS (&ext_szs);
	ext_szs.data = (u8 *)brres_data;
	ext_szs.size = brres_size;
	ext_szs.file_size = brres_size;
	ext_szs.fform_file = FF_BRRES;
	ext_szs.fform_arch = FF_BRRES;
	ext_szs.fform_current = FF_BRRES;
	ext_szs.data_alloced = false;

	ccp saved_dest = opt_dest;
	opt_dest = temp_dir;
	ExtractFilesSZS (&ext_szs, 0, false, 0, 0);
	opt_dest = saved_dest;
	ResetSZS (&ext_szs);

	bool injected = false;
	char mdl_dir_path[PATH_MAX];
	snprintf (mdl_dir_path, sizeof (mdl_dir_path), "%s/3DModels(NW4R)", temp_dir);
	DIR *mdl_dir = opendir (mdl_dir_path);
	if (mdl_dir)
	{
		struct dirent *ent;
		while ((ent = readdir (mdl_dir)) != NULL)
		{
			if (ent->d_name[0] == '.')
				continue;
			char mdl_file_path[PATH_MAX];
			snprintf (mdl_file_path, sizeof (mdl_file_path), "%s/%s", mdl_dir_path, ent->d_name);
			u8 *mdl_raw = 0;
			size_t mdl_sz = 0;
			if (!LoadFileAlloc (mdl_file_path, 0, 0, &mdl_raw, &mdl_sz, 0, 0, 0, false))
			{
				u8 *new_mdl0 = NULL;
				size_t new_mdl0_sz = 0;
				if (InjectDAEIntoMDL0 (mdl_raw, mdl_sz, dae_model, &new_mdl0, &new_mdl0_sz))
				{
					FILE *mf = fopen (mdl_file_path, "wb");
					if (mf)
					{
						fwrite (new_mdl0, 1, new_mdl0_sz, mf);
						fclose (mf);
						injected = true;
					}
					FREE (new_mdl0);
				}
				FREE (mdl_raw);
			}
			if (injected)
				break;
		}
		closedir (mdl_dir);
	}

	if (!injected)
	{
		char rm_cmd[PATH_MAX + 16];
		snprintf (rm_cmd, sizeof (rm_cmd), "rm -rf \"%s\"", temp_dir);
		if (system (rm_cmd))
		{
		}
		return 0;
	}

	char wszst_cmd[PATH_MAX];
	get_wszst_cmd (wszst_cmd, sizeof (wszst_cmd));

	char out_path[PATH_MAX];
	snprintf (out_path, sizeof (out_path), "%s/out.brres", temp_dir);
	char cmd[PATH_MAX * 2 + 64];
	snprintf (cmd, sizeof (cmd), "%s create \"%s\" -d \"%s\" --overwrite >/dev/null 2>&1",
		wszst_cmd, temp_dir, out_path);
	int res_code = system (cmd);
	if (res_code == 0 && !access (out_path, F_OK))
	{
		u8 *res_data = 0;
		size_t res_size = 0;
		if (!LoadFileAlloc (out_path, 0, 0, &res_data, &res_size, 0, 0, 0, false))
		{
			*out_data = res_data;
			*out_size = res_size;
		}
	}

	char rm_cmd[PATH_MAX + 16];
	snprintf (rm_cmd, sizeof (rm_cmd), "rm -rf \"%s\"", temp_dir);
	if (system (rm_cmd))
	{
	}

	return (*out_data != NULL) ? 1 : 0;
}

//-----------------------------------------------------------------------------
// Endian & alignment helper macros
//-----------------------------------------------------------------------------

#define ALIGN_4(x) (((size_t)(x) + 3) & ~(size_t)3)
#define SWP16(v) ((((uint16_t)(v) >> 8) & 0xff) | (((uint16_t)(v) << 8) & 0xff00))
#define SWP32(v)                                                                                   \
	((((uint32_t)(v) >> 24) & 0xff) | (((uint32_t)(v) >> 8) & 0xff00)                              \
		| (((uint32_t)(v) << 8) & 0xff0000) | (((uint32_t)(v) << 24) & 0xff000000))
#define RDL16(p) ((uint16_t)(p)[0] | ((uint16_t)(p)[1] << 8))
#define RDL32(p)                                                                                   \
	((uint32_t)(p)[0] | ((uint32_t)(p)[1] << 8) | ((uint32_t)(p)[2] << 16)                         \
		| ((uint32_t)(p)[3] << 24))
#define WRL16(p, v)                                                                                \
	do                                                                                             \
	{                                                                                              \
		(p)[0] = (uint8_t)(v);                                                                     \
		(p)[1] = (uint8_t)((v) >> 8);                                                              \
	} while (0)
#define WRL32(p, v)                                                                                \
	do                                                                                             \
	{                                                                                              \
		(p)[0] = (uint8_t)(v);                                                                     \
		(p)[1] = (uint8_t)((v) >> 8);                                                              \
		(p)[2] = (uint8_t)((v) >> 16);                                                             \
		(p)[3] = (uint8_t)((v) >> 24);                                                             \
	} while (0)

#define RLE16(p) ((uint16_t)(p)[0] | ((uint16_t)(p)[1] << 8))

//-----------------------------------------------------------------------------
// BFRES (Wii U FRES / FMDL) Injection
//-----------------------------------------------------------------------------

#define BFRES_REL(base, addr)                                                                      \
	((size_t)(addr) + (size_t)(int32_t)SWP32 (*(const uint32_t *)((base) + (addr))))

int InjectDAEIntoBFRES (const uint8_t *bfres_data, size_t bfres_size, const model_t *dae_model,
	uint8_t **out_data, size_t *out_size)
{
	if (!bfres_data || bfres_size < 0x60 || !dae_model || !out_data || !out_size
		|| !dae_model->num_meshes)
		return 0;

	if (memcmp (bfres_data, "FRES", 4) != 0 || bfres_data[4] != 3
		|| SWP16 (*(const uint16_t *)(bfres_data + 8)) != 0xfeff)
		return 0;

	// Locate FMDL group at 0x20
	size_t grp = BFRES_REL (bfres_data, 0x20);
	if (grp + 8 > bfres_size)
		return 0;
	uint32_t entries = SWP32 (*(const uint32_t *)(bfres_data + grp + 4));
	if (!entries || grp + 8 + (size_t)(entries + 1) * 16 > bfres_size)
		return 0;

	size_t fmdl_ent = grp + 8 + 16;
	size_t fmdl = BFRES_REL (bfres_data, fmdl_ent + 12);
	if (fmdl + 0x30 > bfres_size || memcmp (bfres_data + fmdl, "FMDL", 4) != 0)
		return 0;

	// FMDL offsets: FSKL (0x0C), FVTX (0x10), FSHP (0x14), FMAT (0x18)
	size_t fshp_grp = BFRES_REL (bfres_data, fmdl + 0x14);
	uint16_t n_fvtx = SWP16 (*(const uint16_t *)(bfres_data + fmdl + 0x20));
	uint16_t n_fshp = SWP16 (*(const uint16_t *)(bfres_data + fmdl + 0x22));
	if (!n_fvtx || !n_fshp)
		return 0;

	const mesh_t *mesh = &dae_model->meshes[0];
	if (!mesh->num_vertices)
		return 0;

	// Create interleaved vertex buffer: pos (12) + norm (12) + uv (8) = 32 bytes
	uint32_t vtx_count = (uint32_t)mesh->num_vertices;
	uint32_t vtx_stride = 32;
	uint32_t vtx_buf_size = vtx_count * vtx_stride;

	uint8_t *vtx_buf = CALLOC (vtx_count, vtx_stride);
	if (!vtx_buf)
		return 0;

	for (uint32_t i = 0; i < vtx_count; i++)
	{
		uint8_t *v = vtx_buf + i * vtx_stride;
		const vertex_t *vx = &mesh->vertices[i];
		vec3_t p = (vx->position_idx >= 0 && (size_t)vx->position_idx < mesh->num_positions)
			? mesh->positions[vx->position_idx]
			: (vec3_t) { 0, 0, 0 };
		vec3_t n = (vx->normal_idx >= 0 && (size_t)vx->normal_idx < mesh->num_normals)
			? mesh->normals[vx->normal_idx]
			: (vec3_t) { 0, 1, 0 };
		vec2_t t = (vx->texcoord_idx >= 0 && (size_t)vx->texcoord_idx < mesh->num_texcoords)
			? mesh->texcoords[vx->texcoord_idx]
			: (vec2_t) { 0, 0 };

		*(uint32_t *)(v + 0) = SWP32 (*(uint32_t *)&p.x);
		*(uint32_t *)(v + 4) = SWP32 (*(uint32_t *)&p.y);
		*(uint32_t *)(v + 8) = SWP32 (*(uint32_t *)&p.z);
		*(uint32_t *)(v + 12) = SWP32 (*(uint32_t *)&n.x);
		*(uint32_t *)(v + 16) = SWP32 (*(uint32_t *)&n.y);
		*(uint32_t *)(v + 20) = SWP32 (*(uint32_t *)&n.z);
		*(uint32_t *)(v + 24) = SWP32 (*(uint32_t *)&t.u);
		*(uint32_t *)(v + 28) = SWP32 (*(uint32_t *)&t.v);
	}

	// Create index buffer (16-bit big-endian unsigned integers)
	uint32_t idx_count = vtx_count;
	uint32_t idx_buf_size = ALIGN_4 (idx_count * 2);
	uint16_t *idx_buf = CALLOC (idx_buf_size / 2, sizeof (uint16_t));
	if (!idx_buf)
	{
		FREE (vtx_buf);
		return 0;
	}
	for (uint32_t i = 0; i < idx_count; i++)
		idx_buf[i] = SWP16 ((uint16_t)i);

	// Build new BFRES buffer
	size_t base_size = ALIGN_4 (bfres_size);
	size_t vtx_offset = ALIGN_4 (base_size);
	size_t idx_offset = ALIGN_4 (vtx_offset + vtx_buf_size);
	size_t total_size = ALIGN_4 (idx_offset + idx_buf_size);

	uint8_t *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (vtx_buf);
		FREE (idx_buf);
		return 0;
	}

	memcpy (out, bfres_data, bfres_size);
	memcpy (out + vtx_offset, vtx_buf, vtx_buf_size);
	memcpy (out + idx_offset, idx_buf, idx_buf_size);
	FREE (vtx_buf);
	FREE (idx_buf);

	// Update FVTX
	size_t fv0 = BFRES_REL (out, fmdl + 0x10);
	*(uint32_t *)(out + fv0 + 8) = SWP32 (vtx_count); // num_vertices
	size_t bufs = BFRES_REL (out, fv0 + 0x18);
	*(uint32_t *)(out + bufs + 4) = SWP32 (vtx_buf_size);
	*(uint16_t *)(out + bufs + 0x0c) = SWP16 ((uint16_t)vtx_stride);
	int32_t rel_vtx = (int32_t)(vtx_offset - (bufs + 0x14));
	*(int32_t *)(out + bufs + 0x14) = (int32_t)SWP32 ((uint32_t)rel_vtx);

	// Update FSHP LOD
	if (fshp_grp + 8 <= bfres_size)
	{
		size_t fshp_ent = fshp_grp + 8 + 16;
		size_t fshp = BFRES_REL (out, fshp_ent + 12);
		if (fshp + 0x20 <= bfres_size && !memcmp (out + fshp, "FSHP", 4))
		{
			size_t lods = BFRES_REL (out, fshp + 0x1c);
			if (lods + 0x18 <= bfres_size)
			{
				*(uint32_t *)(out + lods + 8) = SWP32 (idx_count); // index count
				int32_t rel_idx = (int32_t)(idx_offset - (lods + 0x10));
				*(int32_t *)(out + lods + 0x10) = (int32_t)SWP32 ((uint32_t)rel_idx);
			}
		}
	}

	// Update FRES total file size
	*(uint32_t *)(out + 0x0c) = SWP32 ((uint32_t)total_size);

	*out_data = out;
	*out_size = total_size;
	return 1;
}

//-----------------------------------------------------------------------------
// Switch BFRES Injection (little-endian, BufferInfo pool, non-interleaved FVTX)
//
// Same geometry-replacement strategy as the Wii U injector: append new vertex
// and index buffers at the end of the file, update FVTX buffer descriptors
// and FSHP face-buffer offset to point at the new data, and extend the
// BufferInfo pool size to cover the appended bytes.  The new vertex format
// is a single interleaved buffer per mesh (pos f32 + norm f32 + uv f16x2 =
// 28 bytes, rounded up to 32 with padding) using attribute format codes the
// existing parser already decodes (0x0518, 0x0512).
//-----------------------------------------------------------------------------

// Switch vertex attribute format codes (big-endian stored, byte-swapped).
// Same codes the parser's attr_read_switch() decodes.
#define SWFMT_F32_3 0x0518 // 32_32_32 float (position, normal)
#define SWFMT_F16_2 0x0512 // 16_16 float (UV)
#define SWFMT_F32_4 0x0519 // 32_32_32_32 float (color)

// Write a little-endian u32 (Switch format uses LE throughout).
static inline void wle32 (uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v);
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

// Write a little-endian s64 (for absolute pointers in Switch BFRES).
static inline void wle64 (uint8_t *p, int64_t v)
{
	p[0] = (uint8_t)((uint64_t)v);
	p[1] = (uint8_t)((uint64_t)v >> 8);
	p[2] = (uint8_t)((uint64_t)v >> 16);
	p[3] = (uint8_t)((uint64_t)v >> 24);
	p[4] = (uint8_t)((uint64_t)v >> 32);
	p[5] = (uint8_t)((uint64_t)v >> 40);
	p[6] = (uint8_t)((uint64_t)v >> 48);
	p[7] = (uint8_t)((uint64_t)v >> 56);
}

// Read a little-endian u32 from Switch BFRES.
static inline uint32_t rle32 (const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Read a little-endian s32 from Switch BFRES.
static inline int32_t rles32 (const uint8_t *p)
{
	return (int32_t)rle32 (p);
}

// Read a little-endian u16 from Switch BFRES.
static inline uint16_t rle16 (const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

// Write a big-endian u16 for Switch FVTX attribute format field.
// The format enum is stored byte-swapped relative to the rest of the
// little-endian file (verified against real data and BfresLibrary source).
static inline void wb16 (uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8); // big-endian byte order
	p[1] = (uint8_t)(v);
}

// Compute bfres_switch_hdr_extra inline (same as parser).
static inline uint switch_hdr_extra (uint vmajor)
{
	return vmajor >= 9 ? 4 : 12;
}

int InjectDAEIntoSwitchBFRES (const uint8_t *data, size_t data_size, const model_t *model,
	uint8_t **out_data, size_t *out_size)
{
	if (!data || data_size < 0x100 || !model || !out_data || !out_size || !model->num_meshes)
		return 0;

	// Validate Switch BFRES header: "FRES" magic, BOM at+0x0C = 0xFEFF.
	if (memcmp (data, "FRES", 4) != 0)
		return 0;
	if (rle16 (data + 0x0C) != 0xFEFF)
		return 0;

	const uint32_t version = rle32 (data + 8);
	const uint vmajor = (version >> 16) & 0xFFFF;
	const uint vhdr = switch_hdr_extra (vmajor);
	const uint shdr = vhdr;

	// Locate BufferInfo pointer at header offset 0x90 (verified by parser).
	if (data_size < 0x98)
		return 0;
	const int64_t bufinfo
		= (int64_t)rle32 (data + 0x90) | ((int64_t)(int32_t)rle32 (data + 0x94) << 32);
	if (bufinfo <= 0 || (size_t)bufinfo + 16 > data_size)
		return 0;

	// BufferInfo layout: +0x00 u32 unk, +0x04 u32 size, +0x08 s64 pool_base.
	const int64_t pool_base = (int64_t)rle32 (data + bufinfo + 8)
		| ((int64_t)(int32_t)rle32 (data + bufinfo + 12) << 32);
	if (pool_base <= 0 || (size_t)pool_base >= data_size)
		return 0;

	// Locate FMDL at absolute pointer in header offset 0x28.
	const int64_t fmdl_ptr
		= (int64_t)rle32 (data + 0x28) | ((int64_t)(int32_t)rle32 (data + 0x2C) << 32);
	if (fmdl_ptr <= 0 || (size_t)fmdl_ptr + 0x60 > data_size
		|| memcmp (data + fmdl_ptr, "FMDL", 4) != 0)
		return 0;

	// Find shapes pointer from FMDL header.
	const uint fhdr = switch_hdr_extra (vmajor);
	const int64_t shapes_ptr_field = fmdl_ptr + 4 + fhdr + 32;
	if ((size_t)shapes_ptr_field + 8 > data_size)
		return 0;
	const int64_t shapes_ptr = (int64_t)rle32 (data + shapes_ptr_field)
		| ((int64_t)(int32_t)rle32 (data + shapes_ptr_field + 4) << 32);
	if (shapes_ptr <= 0 || (size_t)shapes_ptr + 0x60 > data_size
		|| memcmp (data + shapes_ptr, "FSHP", 4) != 0)
		return 0;

//--- Phase 1: Scan all FSHP sections from shapes_ptr.
//    Collect per-shape metadata: FVTX ptr, mesh_arr ptr, name.
//    FSHP sections may not be fixed-stride, so scan for "FSHP" magic.
#define MAX_FSHP_SCAN 256
	typedef struct
	{
		size_t fshp_off; // file offset of FSHP section
		int64_t fvtx_ptr; // absolute FVTX pointer
		int64_t mesh_arr; // absolute mesh array pointer
		const char *name; // name from string table (not owned)
		int dai; // matched DAE mesh index (-1 = unmatched)
	} fshp_meta_t;
	fshp_meta_t fshp_meta[MAX_FSHP_SCAN];
	uint n_fshp = 0;

	{
		const uint8_t *s = data + (size_t)shapes_ptr;
		while (s && (size_t)(s - data) < data_size && n_fshp < MAX_FSHP_SCAN)
		{
			if (memcmp (s, "FSHP", 4) != 0)
				break;
			size_t fshp_off = (size_t)(s - data);
			const int64_t fs = (int64_t)fshp_off + 4 + shdr;

			// FSHP fields: +0x00 name(s64), +0x08 FVTX(s64), +0x10 mesh_arr(s64).
			if ((size_t)fs + 0x20 > data_size)
				break;
			const int64_t name_off
				= (int64_t)rle32 (data + fs) | ((int64_t)(int32_t)rle32 (data + fs + 4) << 32);
			const int64_t fvtx_ptr
				= (int64_t)rle32 (data + fs + 8) | ((int64_t)(int32_t)rle32 (data + fs + 12) << 32);
			const int64_t mesh_arr = (int64_t)rle32 (data + fs + 16)
				| ((int64_t)(int32_t)rle32 (data + fs + 20) << 32);
			if (fvtx_ptr <= 0 || mesh_arr <= 0)
				break;

			// Resolve name from string table.
			const char *nm = NULL;
			if (name_off >= 2 && (size_t)name_off + 2 <= data_size)
			{
				const uint nlen = rle16 (data + name_off);
				if (nlen > 0 && (size_t)name_off + 2 + nlen <= data_size)
					nm = (const char *)(data + name_off + 2);
			}

			fshp_meta_t *m = &fshp_meta[n_fshp++];
			m->fshp_off = fshp_off;
			m->fvtx_ptr = fvtx_ptr;
			m->mesh_arr = mesh_arr;
			m->name = nm;
			m->dai = -1;

			// Advance past this FSHP (try contiguous next; fallback to memmem).
			const uint8_t *next = s + 4;
			if ((size_t)(next - data) + 4 <= data_size && !memcmp (next, "FSHP", 4))
			{
				s = next;
			}
			else
			{
				size_t remain = data_size - (size_t)(next - data);
				const uint8_t *found = (const uint8_t *)memmem (next, remain, "FSHP", 4);
				s = found;
			}
		}
	}
	if (!n_fshp)
		return 0;

	//--- Phase 2: Match each FSHP to a DAE mesh by name.
	//    Fallback: positional match (FSHP i -> mesh i) for nameless shapes.
	for (uint i = 0; i < n_fshp; i++)
	{
		fshp_meta_t *fm = &fshp_meta[i];
		if (!fm->name || !fm->name[0])
			continue;
		for (uint j = 0; j < model->num_meshes; j++)
		{
			if (strcmp (model->meshes[j].name, fm->name) == 0)
			{
				fm->dai = (int)j;
				break;
			}
		}
	}
	// Fallback: unmatched FSHPs get positional match.
	for (uint i = 0; i < n_fshp; i++)
	{
		if (fshp_meta[i].dai >= 0)
			continue;
		if (i < model->num_meshes)
			fshp_meta[i].dai = (int)i;
	}

	// Count how many meshes we'll actually inject.
	uint n_inject = 0;
	for (uint i = 0; i < n_fshp; i++)
	{
		if (fshp_meta[i].dai < 0)
			continue;
		const mesh_t *mesh = &model->meshes[fshp_meta[i].dai];
		if (mesh->num_vertices && mesh->num_positions)
			n_inject++;
	}
	if (!n_inject)
		return 0;

	//--- Phase 3: Build per-mesh vertex + index buffers.
	//    Layout: each mesh gets a vtx_buf + idx_buf appended at the end.
	typedef struct
	{
		uint8_t *vtx_buf;
		uint32_t vtx_size;
		uint32_t vtx_count;
		uint32_t vtx_stride;
		uint16_t *idx_buf;
		uint32_t idx_size; // padded byte count
		uint32_t idx_count;
		int dai; // DAE mesh index
		int fshpi; // FSHP index
	} mesh_buf_t;
	mesh_buf_t *mbufs = CALLOC (n_inject, sizeof (mesh_buf_t));
	if (!mbufs)
		return 0;

	uint mb_idx = 0;
	for (uint i = 0; i < n_fshp; i++)
	{
		if (fshp_meta[i].dai < 0)
			continue;
		const mesh_t *mesh = &model->meshes[fshp_meta[i].dai];
		if (!mesh->num_vertices || !mesh->num_positions)
			continue;

		mesh_buf_t *mb = &mbufs[mb_idx++];
		mb->dai = fshp_meta[i].dai;
		mb->fshpi = (int)i;
		mb->vtx_count = (uint32_t)mesh->num_vertices;
		mb->vtx_stride = 32;
		mb->vtx_size = mb->vtx_count * mb->vtx_stride;
		mb->vtx_buf = CALLOC (mb->vtx_count, mb->vtx_stride);
		mb->idx_count = mb->vtx_count;
		mb->idx_size = ALIGN_4 (mb->idx_count * 2);
		mb->idx_buf = CALLOC (mb->idx_size / 2, sizeof (uint16_t));

		if (!mb->vtx_buf || !mb->idx_buf)
			continue; // allocation failure: skip this mesh

		for (uint32_t v = 0; v < mb->vtx_count; v++)
		{
			const vertex_t *vx = &mesh->vertices[v];
			vec3_t p = (vx->position_idx >= 0 && (size_t)vx->position_idx < mesh->num_positions)
				? mesh->positions[vx->position_idx]
				: (vec3_t) { 0, 0, 0 };
			vec3_t n = (vx->normal_idx >= 0 && (size_t)vx->normal_idx < mesh->num_normals)
				? mesh->normals[vx->normal_idx]
				: (vec3_t) { 0, 1, 0 };
			vec2_t t = (vx->texcoord_idx >= 0 && (size_t)vx->texcoord_idx < mesh->num_texcoords)
				? mesh->texcoords[vx->texcoord_idx]
				: (vec2_t) { 0, 0 };

			uint8_t *vv = mb->vtx_buf + v * mb->vtx_stride;
			memcpy (vv + 0, &p.x, 4);
			memcpy (vv + 4, &p.y, 4);
			memcpy (vv + 8, &p.z, 4);
			memcpy (vv + 12, &n.x, 4);
			memcpy (vv + 16, &n.y, 4);
			memcpy (vv + 20, &n.z, 4);
			// UV: f32 -> f16 downcast.
			{
				union
				{
					uint32_t u;
					float f;
				} ux, uy;
				ux.f = t.u;
				uy.f = t.v;
				uint16_t huf = 0, hvf = 0;
				uint32_t su = ux.u, sv = uy.u;
				uint32_t sign_u = (su >> 16) & 0x8000;
				int32_t exp_u = ((su >> 23) & 0xFF) - 127 + 15;
				uint32_t man_u = (su >> 13) & 0x3FF;
				if (exp_u <= 0)
				{
					huf = (uint16_t)(sign_u);
				}
				else if (exp_u >= 31)
				{
					huf = (uint16_t)(sign_u | 0x7C00);
				}
				else
				{
					huf = (uint16_t)(sign_u | ((uint32_t)exp_u << 10) | man_u);
				}
				uint32_t sign_v = (sv >> 16) & 0x8000;
				int32_t exp_v = ((sv >> 23) & 0xFF) - 127 + 15;
				uint32_t man_v = (sv >> 13) & 0x3FF;
				if (exp_v <= 0)
				{
					hvf = (uint16_t)(sign_v);
				}
				else if (exp_v >= 31)
				{
					hvf = (uint16_t)(sign_v | 0x7C00);
				}
				else
				{
					hvf = (uint16_t)(sign_v | ((uint32_t)exp_v << 10) | man_v);
				}
				memcpy (vv + 24, &huf, 2);
				memcpy (vv + 26, &hvf, 2);
			}
		}

		for (uint32_t j = 0; j < mb->idx_count; j++)
			mb->idx_buf[j] = (uint16_t)j;
	}

	//--- Phase 4: Layout all new buffers at end of file.
	size_t cur = ALIGN_4 (data_size);
	// First pass: compute sizes and validate bounds.
	for (uint i = 0; i < n_inject; i++)
	{
		if (!mbufs[i].vtx_buf || !mbufs[i].idx_buf)
			continue;
		mbufs[i].vtx_size = mbufs[i].vtx_count * mbufs[i].vtx_stride;
		mbufs[i].idx_size = ALIGN_4 (mbufs[i].idx_count * 2);
	}

	size_t cur_off = cur;
	for (uint i = 0; i < n_inject; i++)
	{
		if (!mbufs[i].vtx_buf || !mbufs[i].idx_buf)
			continue;
		size_t vtx_off = (cur_off + 7) & ~(size_t)7;
		size_t idx_off = ALIGN_4 (vtx_off + mbufs[i].vtx_size);
		cur_off = ALIGN_4 (idx_off + mbufs[i].idx_size);
	}
	const size_t total_size = cur_off;
	if (total_size < data_size) // overflow guard
		goto fail;

	{
		uint8_t *out = CALLOC (1, total_size);
		if (!out)
			goto fail;
		memcpy (out, data, data_size);

		// Copy buffers into appended region.
		cur_off = cur;
		for (uint i = 0; i < n_inject; i++)
		{
			if (!mbufs[i].vtx_buf || !mbufs[i].idx_buf)
				continue;
			size_t vtx_off = (cur_off + 7) & ~(size_t)7;
			size_t idx_off = ALIGN_4 (vtx_off + mbufs[i].vtx_size);
			memcpy (out + vtx_off, mbufs[i].vtx_buf, mbufs[i].vtx_size);
			memcpy (out + idx_off, mbufs[i].idx_buf, mbufs[i].idx_size);

			// Pool-relative offsets.
			int32_t new_vb_off = (int32_t)(vtx_off - (size_t)pool_base);
			int32_t new_ib_off = (int32_t)(idx_off - (size_t)pool_base);

			// Locate the FSHP + FVTX for this mesh.
			fshp_meta_t *fm = &fshp_meta[mbufs[i].fshpi];
			size_t fvtx_base = (size_t)fm->fvtx_ptr;
			size_t counts_off = fvtx_base + 4 + vhdr + 0x40;
			if (counts_off + 16 > total_size)
				continue;

			// Update FVTX buffer size, stride, offset, count.
			int64_t vtx_bufsize_ptr = (int64_t)rles32 (out + counts_off - 24)
				| ((int64_t)(int32_t)rle32 (out + counts_off - 20) << 32);
			int64_t vtx_stride_ptr = (int64_t)rles32 (out + counts_off - 16)
				| ((int64_t)(int32_t)rle32 (out + counts_off - 12) << 32);
			if (vtx_bufsize_ptr > 0 && (size_t)vtx_bufsize_ptr + 4 <= total_size)
				wle32 (out + vtx_bufsize_ptr, mbufs[i].vtx_size);
			if (vtx_stride_ptr > 0 && (size_t)vtx_stride_ptr + 4 <= total_size)
				wle32 (out + vtx_stride_ptr, mbufs[i].vtx_stride);
			wle32 (out + counts_off, (uint32_t)new_vb_off);
			wle32 (out + counts_off + 8, mbufs[i].vtx_count);

			// Update FVTX attribute formats (_p, _n -> f32x3, _u* -> f16x2).
			int64_t attr_arr_ptr = (int64_t)rles32 (out + fvtx_base + 4 + vhdr)
				| ((int64_t)(int32_t)rle32 (out + fvtx_base + 4 + vhdr + 4) << 32);
			uint8_t n_attr = out[counts_off + 4];
			if (attr_arr_ptr > 0 && (size_t)attr_arr_ptr + (size_t)n_attr * 16 <= total_size)
			{
				for (uint a = 0; a < n_attr; a++)
				{
					size_t ae = (size_t)attr_arr_ptr + a * 16;
					int64_t nm_off = (int64_t)rles32 (out + ae)
						| ((int64_t)(int32_t)rle32 (out + ae + 4) << 32);
					if (nm_off < 2 || (size_t)nm_off + 2 > total_size)
						continue;
					uint nlen = rle16 (out + nm_off);
					if ((size_t)nm_off + 2 + nlen > total_size)
						continue;
					const char *nm = (const char *)(out + nm_off + 2);
					if (nlen >= 2 && nm[0] == '_' && nm[1] == 'p')
						wb16 (out + ae + 8, SWFMT_F32_3);
					else if (nlen >= 2 && nm[0] == '_' && nm[1] == 'n')
						wb16 (out + ae + 8, SWFMT_F32_3);
					else if (nlen >= 2 && nm[0] == '_' && nm[1] == 'u')
						wb16 (out + ae + 8, SWFMT_F16_2);
				}
			}

			// Update FSHP mesh entry: face_buffer_offset + index_count.
			int64_t mesh_arr = fm->mesh_arr;
			if (mesh_arr > 0 && (size_t)mesh_arr + 48 <= total_size)
			{
				wle32 (out + mesh_arr + 32, (uint32_t)new_ib_off);
				wle32 (out + mesh_arr + 44, mbufs[i].idx_count);
			}

			cur_off = ALIGN_4 (idx_off + mbufs[i].idx_size);
		}

		// Update BufferInfo pool size and FRES file size.
		wle32 (out + bufinfo + 4, (uint32_t)(total_size - (size_t)pool_base));
		wle32 (out + 0x04, (uint32_t)total_size);

		// Cleanup temporary buffers.
		for (uint i = 0; i < n_inject; i++)
		{
			FREE (mbufs[i].vtx_buf);
			FREE (mbufs[i].idx_buf);
		}
		FREE (mbufs);

		*out_data = out;
		*out_size = total_size;
		return 1;
	}

fail:
	for (uint i = 0; i < n_inject; i++)
	{
		FREE (mbufs[i].vtx_buf);
		FREE (mbufs[i].idx_buf);
	}
	FREE (mbufs);
	return 0;
}

//-----------------------------------------------------------------------------
// CreateSwitchBFRES -- full Switch BFRES encoder from model_t.
//
// Builds a complete little-endian Switch BFRES v8 file from scratch:
//   FRES header → BufferInfo → FMDL (FSKL + FVTX + FSHP + FMAT) →
//   string table → buffer pool (vertex + index data).
// All internal pointers are absolute s64. The output is a standalone file
// that the existing Switch BFRES parser can validate.
//-----------------------------------------------------------------------------

// Switch BFRES attribute format codes (big-endian stored, byte-swapped).
// Same codes the parser's attr_read_switch() decodes.
#ifndef SWFMT_F32_3
#define SWFMT_F32_3 0x0518 // 32_32_32 float
#define SWFMT_F16_2 0x0512 // 16_16 float
#define SWFMT_8_8_8_UNORM 0x010b // 8_8_8_8 unorm (color, uses first 3)
#endif

// Write a little-endian u32 (Switch format uses LE throughout).
static inline void sw_le32 (uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v);
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

// Write a little-endian u16.
static inline void sw_le16 (uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v);
	p[1] = (uint8_t)(v >> 8);
}

// Write a little-endian s64.
static inline void sw_le64 (uint8_t *p, int64_t v)
{
	p[0] = (uint8_t)((uint64_t)v);
	p[1] = (uint8_t)((uint64_t)v >> 8);
	p[2] = (uint8_t)((uint64_t)v >> 16);
	p[3] = (uint8_t)((uint64_t)v >> 24);
	p[4] = (uint8_t)((uint64_t)v >> 32);
	p[5] = (uint8_t)((uint64_t)v >> 40);
	p[6] = (uint8_t)((uint64_t)v >> 48);
	p[7] = (uint8_t)((uint64_t)v >> 56);
}

// Write a big-endian u16 (attribute format field -- stored BE, read via swz16).
static inline void sw_be16 (uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)(v);
}

// Collect unique strings for the Switch BFRES string table.
// Returns a 0-terminated array of string pointers. Caller frees.
static const char **collect_switch_strings (
	const model_t *model, const char *model_name, uint *out_count)
{
	uint cap = 64, count = 0;
	const char **strs = CALLOC (cap, sizeof (char *));
	if (!strs)
		return NULL;

#define ADD_STR(s)                                                                                 \
	do                                                                                             \
	{                                                                                              \
		if (s && *(s))                                                                             \
		{                                                                                          \
			int dup = 0;                                                                           \
			for (uint _i = 0; _i < count; _i++)                                                    \
				if (!strcmp (strs[_i], (s)))                                                       \
				{                                                                                  \
					dup = 1;                                                                       \
					break;                                                                         \
				}                                                                                  \
			if (!dup)                                                                              \
			{                                                                                      \
				if (count >= cap)                                                                  \
				{                                                                                  \
					cap *= 2;                                                                      \
					strs = REALLOC (strs, cap * sizeof (char *));                                  \
				}                                                                                  \
				strs[count++] = (s);                                                               \
			}                                                                                      \
		}                                                                                          \
	} while (0)

	ADD_STR (model_name);
	ADD_STR ("model");
	ADD_STR ("_p");
	ADD_STR ("_n");
	ADD_STR ("_u0");
	for (size_t i = 0; i < model->num_materials; i++)
		ADD_STR (model->materials[i].name);
	for (size_t i = 0; i < model->num_joints; i++)
		ADD_STR (model->joints[i].name);
	for (size_t i = 0; i < model->num_meshes; i++)
		ADD_STR (model->meshes[i].name);
	for (size_t i = 0; i < model->num_materials; i++)
		for (int t = 0; t < model->materials[i].num_textures; t++)
			ADD_STR (model->materials[i].textures[t]);

#undef ADD_STR
	strs[count] = NULL;
	*out_count = count;
	return strs;
}

int CreateSwitchBFRES (const model_t *model, uint8_t **out_data, size_t *out_size)
{
	if (!model || !out_data || !out_size || !model->num_meshes)
		return 0;

	const uint vmajor = 8; // target v8 for broadest compatibility
	const uint vhdr = switch_hdr_extra (vmajor);

	const char *model_name = "model"; // model_t has no name field; use default

	// Collect unique strings for the string table.
	uint n_str = 0;
	const char **strs = collect_switch_strings (model, model_name, &n_str);
	if (!strs)
		return 0;

	//--- Build string table (LE u16 length-prefixed, 0-terminator at end).
	size_t strtab_size = 0;
	for (uint i = 0; i < n_str; i++)
		strtab_size += 2 + strlen (strs[i]) + 1;
	strtab_size += 2; // 0x0000 terminator
	strtab_size = ALIGN_4 (strtab_size);

	uint8_t *strtab = CALLOC (1, strtab_size);
	if (!strtab)
	{
		FREE (strs);
		return 0;
	}
	size_t so = 0;
	for (uint i = 0; i < n_str; i++)
	{
		const size_t len = strlen (strs[i]);
		sw_le16 (strtab + so, (uint16_t)len);
		memcpy (strtab + so + 2, strs[i], len);
		strtab[so + 2 + len] = '\0';
		so += 2 + len + 1;
	}
	// Terminator.
	sw_le16 (strtab + so, 0);

	//--- Compute file layout.
	const size_t fmdl_off = ALIGN_4 (0xF0); // FRES header = 0xF0 bytes
	const uint n_fshp = (uint)model->num_meshes;
	const uint n_fmat = (uint)model->num_materials;
	const uint n_bones = (uint)model->num_joints;
	const uint n_buf = 1; // all attributes in one interleaved buffer

	// FSKL section size: FSKL(4) + vhdr + fields + bone array + matrix array.
	const size_t fskl_size
		= ALIGN_4 (4 + vhdr + 0x38 + (size_t)n_bones * 0x60 + (size_t)n_bones * 48);
	const size_t fskl_off = fmdl_off + 0x60;

	// FVTX sections (one per mesh): each = FVTX(4)+vhdr + attr_arr(s64) +
	//   stride_off(s64) + bufsize_off(s64) + pad(8) + counts(16) = 0x60,
	//   plus attribute array = n_attr*16.
	const uint n_attr = 3; // _p, _n, _u0
	const size_t fvtx_sec_size
		= ALIGN_4 (0x60 + n_attr * 16 + n_buf * 16 * 2); // header + attrs + buf_size + buf_stride
	const size_t fvtx_off = fskl_off + fskl_size;
	const size_t fvtxs_total = fvtx_sec_size * n_fshp;

	// FSHP sections (one per mesh): each = FSHP(4)+vhdr + name(s64) +
	//   fvtx_ptr(s64) + mesh_arr(s64) + skin_bone_arr(s64) + ... ≈0x60
	//   + mesh entries.
	const size_t fshp_sec_size = ALIGN_4 (0x60 + 56); // 56-byte mesh entry
	const size_t fshp_off = fvtx_off + fvtxs_total;
	const size_t fshps_total = fshp_sec_size * n_fshp;

	// FMAT sections (one per material): FMAT(4)+vhdr + name + texture refs.
	const size_t fmat_sec_size = ALIGN_4 (0xB0); // conservative fixed size
	const size_t fmat_off = fshp_off + fshps_total;
	const size_t fmats_total = fmat_sec_size * n_fmat;

	// FMDL total size.
	const size_t fmdl_size = ALIGN_4 (0x60 + fskl_size + fvtxs_total + fshps_total + fmats_total);

	// String table and buffer pool come after FMDL.
	// Buffer pool must be 8-byte aligned because read_fvtx_switch() applies
	// cur = (cur + 7) & ~7 to each buffer start within the pool.
	const size_t strtab_off = fmdl_off + fmdl_size;
	const size_t pool_off = (ALIGN_4 (strtab_off + strtab_size) + 7) & ~(size_t)7;

	// Compute vertex/index buffer sizes and pool offsets for each mesh.
	uint32_t *vtx_sizes = CALLOC (n_fshp, sizeof (uint32_t));
	uint32_t *idx_sizes = CALLOC (n_fshp, sizeof (uint32_t));
	uint32_t *vtx_pool_offs = CALLOC (n_fshp, sizeof (uint32_t));
	uint32_t *idx_pool_offs = CALLOC (n_fshp, sizeof (uint32_t));
	if (!vtx_sizes || !idx_sizes || !vtx_pool_offs || !idx_pool_offs)
	{
		FREE (strtab);
		FREE (strs);
		FREE (vtx_sizes);
		FREE (idx_sizes);
		FREE (vtx_pool_offs);
		FREE (idx_pool_offs);
		return 0;
	}
	for (uint i = 0; i < n_fshp; i++)
	{
		const mesh_t *m = &model->meshes[i];
		vtx_sizes[i] = (uint32_t)m->num_vertices * 32; // interleaved: pos12+norm12+uv4+pad4
		idx_sizes[i] = (uint32_t)m->num_vertices * 2; // u16 index list
	}

	size_t pool_cur = pool_off;
	for (uint i = 0; i < n_fshp; i++)
	{
		pool_cur = (pool_cur + 7) & ~(size_t)7;
		vtx_pool_offs[i] = (uint32_t)(pool_cur - pool_off);
		pool_cur += vtx_sizes[i];
	}
	for (uint i = 0; i < n_fshp; i++)
	{
		pool_cur = (pool_cur + 7) & ~(size_t)7;
		idx_pool_offs[i] = (uint32_t)(pool_cur - pool_off);
		pool_cur += idx_sizes[i];
	}
	const uint32_t pool_size = (uint32_t)ALIGN_4 (pool_cur - pool_off);
	const size_t total_size = pool_off + pool_size;

	uint8_t *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (strtab);
		FREE (strs);
		FREE (vtx_sizes);
		FREE (idx_sizes);
		FREE (vtx_pool_offs);
		FREE (idx_pool_offs);
		return 0;
	}

// Helper: look up a string's offset in the string table.
// The string table uses LE u16 length-prefixed entries, so the pointer
// must point to the start of the length field (2 bytes before the string).
#define STR_OFF(s)                                                                                 \
	((s) && *(s) ? (int64_t)((const uint8_t *)memmem (strtab, strtab_size, (s), strlen (s) + 1)    \
					   - strtab - 2)                                                               \
				 : (int64_t)0)
#define STR_OFF_ABS(s) ((s) && *(s) ? (int64_t)strtab_off + STR_OFF (s) : (int64_t)0)

	//--- FRES Header (0xF0 bytes).
	memcpy (out, "FRES", 4);
	sw_le32 (out + 0x04, (uint32_t)total_size);
	sw_le32 (out + 0x08, vmajor << 16); // version: major=8, minor=0
	sw_le16 (out + 0x0C, 0xFEFF); // BOM (LE)
	sw_le16 (out + 0x0E, 0x00D4); // header size = 212 bytes
	sw_le16 (out + 0x10, 1); // num sections
	sw_le32 (out + 0x14, 4); // alignment
	sw_le32 (out + 0x18, 0); // name offset (relative to string pool)
	// +0x28: FMDL array pointer (absolute).
	sw_le64 (out + 0x28, (int64_t)fmdl_off);
	// +0x90: BufferInfo pointer (absolute).
	sw_le64 (out + 0x90, (int64_t)0x98); // BufferInfo at 0x98 (right after FRES header)
	// +0xBC (v8): numModel = 1.
	sw_le16 (out + 0xBC, 1);

	//--- BufferInfo at 0x98.
	sw_le32 (out + 0x98, 34); // unk field (BfresLibrary default)
	sw_le32 (out + 0x9C, (uint32_t)pool_size); // pool size
	sw_le64 (out + 0xA0, (int64_t)pool_off); // pool_base (absolute)

	//--- FMDL Section (starts at fmdl_off).
	memcpy (out + fmdl_off, "FMDL", 4);
	// +0x04: vhdr (4 bytes for v>=9, 12 for v<9). For v8, write 12-byte legacy block.
	if (vmajor < 9)
	{
		sw_le32 (out + fmdl_off + 4, 0); // offset (unused)
		sw_le64 (out + fmdl_off + 8, 0); // size (unused)
	}
	const size_t fm = fmdl_off + 4 + vhdr; // first field after prologue
	sw_le64 (out + fm + 0, STR_OFF_ABS (model_name)); // name
	sw_le64 (out + fm + 8, STR_OFF_ABS ("model")); // path
	sw_le64 (out + fm + 16, (int64_t)fskl_off); // skeleton
	sw_le64 (out + fm + 24, (int64_t)(fvtx_off)); // vertex buffer array (first FVTX)
	sw_le64 (out + fm + 32, (int64_t)(fshp_off)); // shapes array (first FSHP)
	sw_le64 (out + fm + 40, 0); // shapes dict (unused)
	sw_le64 (out + fm + 48, (int64_t)(fmat_off)); // materials array (first FMAT)
	sw_le64 (out + fm + 56, 0); // materials dict (unused)
	if (vmajor >= 10)
		sw_le64 (out + fm + 64, 0); // shader-assign (v10+)
	sw_le64 (out + fm + (vmajor >= 10 ? 72 : 64), 0); // userdata_val
	sw_le64 (out + fm + (vmajor >= 10 ? 80 : 72), 0); // userdata_dict
	sw_le64 (out + fm + (vmajor >= 10 ? 88 : 80), 0); // userptr
	// numVertexBuffer u16, numShape u16, numMaterial u16.
	const size_t ncounts_off = fm + (vmajor >= 10 ? 88 : 80) + 8;
	sw_le16 (out + ncounts_off, (uint16_t)n_fshp); // numVertexBuffer = num shapes
	sw_le16 (out + ncounts_off + 2, (uint16_t)n_fshp);
	sw_le16 (out + ncounts_off + 4, (uint16_t)n_fmat);

	//--- FSKL Section.
	memcpy (out + fskl_off, "FSKL", 4);
	const size_t sk = fskl_off + 4 + vhdr;
	// +0x10: bone array pointer.
	sw_le64 (out + sk + 0x10, (int64_t)(sk + 0x40));
	// +0x20: inverse bind matrix pointer (3x4 float, 48 bytes per bone).
	sw_le64 (out + sk + 0x20, (int64_t)(sk + 0x40 + (size_t)n_bones * 0x60));
	// v8: bone count at sk+0x4C.
	sw_le16 (out + sk + 0x4C, (uint16_t)n_bones);

	// Per-bone data.
	for (uint b = 0; b < n_bones; b++)
	{
		const joint_t *j = &model->joints[b];
		const size_t boff = sk + 0x40 + b * 0x60;
		sw_le64 (out + boff, STR_OFF_ABS (j->name));
		sw_le16 (out + boff + 0x2A, (uint16_t)(j->parent_idx >= 0 ? j->parent_idx : 0xFFFF));
		// TRS at +0x38.
		sw_le32 (out + boff + 0x38, *(uint32_t *)&j->scale.x);
		sw_le32 (out + boff + 0x3C, *(uint32_t *)&j->scale.y);
		sw_le32 (out + boff + 0x40, *(uint32_t *)&j->scale.z);
		sw_le32 (out + boff + 0x44, *(uint32_t *)&j->rotate.x);
		sw_le32 (out + boff + 0x48, *(uint32_t *)&j->rotate.y);
		sw_le32 (out + boff + 0x4C, *(uint32_t *)&j->rotate.z);
		sw_le32 (out + boff + 0x54, *(uint32_t *)&j->translate.x);
		sw_le32 (out + boff + 0x58, *(uint32_t *)&j->translate.y);
		sw_le32 (out + boff + 0x5C, *(uint32_t *)&j->translate.z);
	}

	// Inverse bind matrices (3x4 row-major float, stored as raw bytes).
	for (uint b = 0; b < n_bones; b++)
	{
		const joint_t *j = &model->joints[b];
		const size_t moff = sk + 0x40 + n_bones * 0x60 + b * 48;
		if (j->has_inverse_bind)
			memcpy (out + moff, j->inverse_bind, 48);
		else if (j->has_inverse_bind == 0)
			memcpy (out + moff, j->bind, 48); // fallback to bind matrix
	}

	//--- FVTX Sections + Buffer Descriptors.
	// FVTX layout (Switch v8):
	//   +0:   "FVTX" (4)
	//   +4:   vhdr (12 for v<9, 0 for v>=9)
	//   +16:  attr_arr s64 pointer (absolute, to attribute entry array)
	//   +24..+48: unused s64 fields (zero)
	//   +56:  vtx_bufsize_off s64 pointer (absolute, to buffer size descriptors)
	//   +64:  vtx_stride_off  s64 pointer (absolute, to buffer stride descriptors)
	//   +72:  8-byte padding
	//   +80:  counts area: vb_local_off(4), n_attr(1), n_buf(1), pad(2), vtx_count(4), pad(4)
	// Write vertex data.
	for (uint i = 0; i < n_fshp; i++)
	{
		const mesh_t *m = &model->meshes[i];
		uint8_t *vp = out + pool_off + vtx_pool_offs[i];
		for (size_t v = 0; v < m->num_vertices; v++)
		{
			const vertex_t *vx = &m->vertices[v];
			vec3_t p = (vx->position_idx >= 0 && (size_t)vx->position_idx < m->num_positions)
				? m->positions[vx->position_idx]
				: (vec3_t) { 0, 0, 0 };
			vec3_t n = (vx->normal_idx >= 0 && (size_t)vx->normal_idx < m->num_normals)
				? m->normals[vx->normal_idx]
				: (vec3_t) { 0, 1, 0 };
			vec2_t t = (vx->texcoord_idx >= 0 && (size_t)vx->texcoord_idx < m->num_texcoords)
				? m->texcoords[vx->texcoord_idx]
				: (vec2_t) { 0, 0 };
			memcpy (vp + 0, &p.x, 4);
			memcpy (vp + 4, &p.y, 4);
			memcpy (vp + 8, &p.z, 4);
			memcpy (vp + 12, &n.x, 4);
			memcpy (vp + 16, &n.y, 4);
			memcpy (vp + 20, &n.z, 4);
			// f16 UV
			union
			{
				uint32_t u;
				float f;
			} fu, fv_uv;
			fu.f = t.u;
			fv_uv.f = t.v;
			uint16_t huf = 0, hvf = 0;
			{
				uint32_t s = (fu.u >> 16) & 0x8000;
				int32_t e = ((fu.u >> 23) & 0xFF) - 127 + 15;
				uint32_t mt = (fu.u >> 13) & 0x3FF;
				huf = (uint16_t)(e <= 0 ? s : e >= 31 ? s | 0x7C00 : s | ((uint32_t)e << 10) | mt);
			}
			{
				uint32_t s = (fv_uv.u >> 16) & 0x8000;
				int32_t e = ((fv_uv.u >> 23) & 0xFF) - 127 + 15;
				uint32_t mt = (fv_uv.u >> 13) & 0x3FF;
				hvf = (uint16_t)(e <= 0 ? s : e >= 31 ? s | 0x7C00 : s | ((uint32_t)e << 10) | mt);
			}
			memcpy (vp + 24, &huf, 2);
			memcpy (vp + 26, &hvf, 2);
			vp += 32;
		}
	}

	// Write index data.
	for (uint i = 0; i < n_fshp; i++)
	{
		const mesh_t *m = &model->meshes[i];
		uint8_t *ip = out + pool_off + idx_pool_offs[i];
		for (size_t v = 0; v < m->num_vertices; v++)
			sw_le16 (ip + v * 2, (uint16_t)v);
	}

	//--- FVTX Section Headers.
	for (uint i = 0; i < n_fshp; i++)
	{
		const mesh_t *m = &model->meshes[i];
		const size_t fvtx_base = fvtx_off + i * fvtx_sec_size;
		memcpy (out + fvtx_base, "FVTX", 4);
		if (vmajor < 9)
		{
			sw_le32 (out + fvtx_base + 4, 0);
			sw_le64 (out + fvtx_base + 8, 0);
		}
		const size_t fv = fvtx_base + 4 + vhdr; // = fvtx_base + 16

		// Attribute array, buffer size/stride descriptors follow after the FVTX header.
		// The header ends at FVTX+0x60, so descriptors start there.
		const size_t attr_arr = fvtx_base + 0x60;
		const size_t buf_size_arr = attr_arr + n_attr * 16;
		const size_t buf_stride_arr = buf_size_arr + n_buf * 16;

		// Pointer at FVTX+16 (fv+0): attr_arr (absolute).
		sw_le64 (out + fv + 0, (int64_t)attr_arr);
		// Pointer at FVTX+56 (fv+40): vtx_bufsize_off (absolute).
		sw_le64 (out + fv + 40, (int64_t)buf_size_arr);
		// Pointer at FVTX+64 (fv+48): vtx_stride_off (absolute).
		sw_le64 (out + fv + 48, (int64_t)buf_stride_arr);
		// FVTX+72 (fv+56): 8-byte padding (already zero from CALLOC).

		// Counts area at FVTX+80 (fv+0x40).
		const size_t counts = fv + 0x40;
		sw_le32 (out + counts, vtx_pool_offs[i]); // vb_local_off
		out[counts + 4] = (uint8_t)n_attr; // n_attr
		out[counts + 5] = (uint8_t)n_buf; // n_buf
		sw_le32 (out + counts + 8, (uint32_t)m->num_vertices); // vertex_count

		// Write buffer size descriptors (each 16 bytes: u32 size + 12 pad).
		for (uint b = 0; b < n_buf; b++)
			sw_le32 (out + buf_size_arr + b * 16, vtx_sizes[i]);

		// Write buffer stride descriptors (each 16 bytes: u32 stride + 12 pad).
		for (uint b = 0; b < n_buf; b++)
			sw_le32 (out + buf_stride_arr + b * 16, 32);

		// Write attribute array entries (16 bytes each).
		// All attributes use buffer index 0 (interleaved in one buffer).
		const char *attr_names[] = { "_p", "_n", "_u0" };
		const uint16_t attr_fmts[] = { SWFMT_F32_3, SWFMT_F32_3, SWFMT_F16_2 };
		const uint attr_offsets[] = { 0, 12, 24 }; // byte offset within the interleaved buffer
		for (uint a = 0; a < n_attr; a++)
		{
			const size_t ae = attr_arr + a * 16;
			sw_le64 (out + ae, STR_OFF_ABS (attr_names[a]));
			sw_be16 (out + ae + 8, attr_fmts[a]); // format (big-endian)
			sw_le16 (out + ae + 12, (uint16_t)attr_offsets[a]); // buffer offset
			out[ae + 14] = 0; // buffer index (all in buffer 0)
			out[ae + 15] = 0;
		}
	}

	//--- FSHP Section Headers.
	for (uint i = 0; i < n_fshp; i++)
	{
		const mesh_t *m = &model->meshes[i];
		const size_t fshp_base = fshp_off + i * fshp_sec_size;
		memcpy (out + fshp_base, "FSHP", 4);
		if (vmajor < 9)
		{
			sw_le32 (out + fshp_base + 4, 0);
			sw_le64 (out + fshp_base + 8, 0);
		}
		const size_t fs = fshp_base + 4 + vhdr;

		sw_le64 (out + fs, STR_OFF_ABS (m->name)); // name
		sw_le64 (out + fs + 8, (int64_t)(fvtx_off + i * fvtx_sec_size)); // FVTX pointer
		// mesh array pointer (right after FSHP fixed fields).
		sw_le64 (out + fs + 16, (int64_t)(fs + 0x40));
		// skin bone array: point to empty for now (no skinning support in encoder).
		sw_le64 (out + fs + 24, 0);

		// Mesh entry at fs+0x40.
		const size_t me = fs + 0x40;
		sw_le32 (out + me + 0, 0); // vertex_offset (unused)
		sw_le32 (out + me + 4, 0); // vertex_count2
		sw_le32 (out + me + 8, 0); // bone_index_offset
		sw_le32 (out + me + 12, 0); // bone_count
		sw_le16 (out + me + 16, 0); // material_index
		out[me + 18] = 0;
		out[me + 19] = 0; // flags
		sw_le32 (out + me + 20, 0); // surface_min
		sw_le32 (out + me + 24, 0); // surface_max
		sw_le32 (out + me + 28, 0); // unknown
		sw_le32 (out + me + 32, (uint32_t)idx_pool_offs[i]); // face_buffer_offset
		sw_le32 (out + me + 36, 3); // primitive_type (3=triangles)
		sw_le32 (out + me + 40, 1); // index_format (1=u16)
		sw_le32 (out + me + 44, (uint32_t)m->num_vertices); // index_count

		// FMAT index: v>=9 at sname_off+0x4A, v<9 at sname_off+0x4E.
		// (sname_off == fs; both offsets are from the sname_off base.)
		sw_le16 (out + fs + 0x4E, (uint16_t)i); // fmat_index (use shape index as mat index)
		// Num LOD meshes: v>=9 at sname_off+0x53, v<9 at sname_off+0x57.
		out[fs + 0x57] = 1; // one LOD mesh
	}

	//--- FMAT Section Headers.
	for (uint i = 0; i < n_fmat; i++)
	{
		const material_t *mat = &model->materials[i];
		const size_t fmat_base = fmat_off + i * fmat_sec_size;
		memcpy (out + fmat_base, "FMAT", 4);
		if (vmajor < 9)
		{
			sw_le32 (out + fmat_base + 4, 0);
			sw_le64 (out + fmat_base + 8, 0);
		}
		const size_t fa = fmat_base + 4 + vhdr;
		sw_le64 (out + fa, STR_OFF_ABS (mat->name));

		// Write texture references if present.
		if (mat->num_textures > 0)
		{
			// Texture name array pointer at FMAT+0x38 (v<9).
			const size_t tex_arr_off = fmat_base + 0x38;
			const size_t tex_arr = fa + 0x34;
			sw_le64 (out + tex_arr_off, (int64_t)tex_arr);
			for (int t = 0; t < mat->num_textures && t < 8; t++)
			{
				sw_le64 (out + tex_arr + t * 8, STR_OFF_ABS (mat->textures[t]));
			}
			// num textures at FMAT+0xAD (v<9).
			out[fmat_base + 0xAD] = (uint8_t)mat->num_textures;
		}
	}

	// Copy string table.
	memcpy (out + strtab_off, strtab, strtab_size);

	// Cleanup.
	FREE (vtx_sizes);
	FREE (idx_sizes);
	FREE (strtab);
	FREE (strs);
	FREE (vtx_pool_offs);
	FREE (idx_pool_offs);

#undef STR_OFF
#undef STR_OFF_ABS

	*out_data = out;
	*out_size = total_size;
	return 1;
}

//-----------------------------------------------------------------------------
// BCH (3DS H3D) Injection
//-----------------------------------------------------------------------------

int InjectDAEIntoBCH (const uint8_t *bch_data, size_t bch_size, const model_t *dae_model,
	uint8_t **out_data, size_t *out_size)
{
	if (!bch_data || bch_size < 0x44 || !dae_model || !out_data || !out_size
		|| !dae_model->num_meshes)
		return 0;

	if (memcmp (bch_data, "BCH", 3) != 0 || bch_data[3] != 0)
		return 0;

	const mesh_t *mesh = &dae_model->meshes[0];
	if (!mesh->num_vertices)
		return 0;

	uint32_t vtx_count = (uint32_t)mesh->num_vertices;
	uint32_t vtx_stride = 32; // pos(12) + norm(12) + uv(8)
	uint32_t vtx_buf_size = vtx_count * vtx_stride;

	uint8_t *vtx_buf = CALLOC (vtx_count, vtx_stride);
	if (!vtx_buf)
		return 0;

	for (uint32_t i = 0; i < vtx_count; i++)
	{
		uint8_t *v = vtx_buf + i * vtx_stride;
		const vertex_t *vx = &mesh->vertices[i];
		vec3_t p = (vx->position_idx >= 0 && (size_t)vx->position_idx < mesh->num_positions)
			? mesh->positions[vx->position_idx]
			: (vec3_t) { 0, 0, 0 };
		vec3_t n = (vx->normal_idx >= 0 && (size_t)vx->normal_idx < mesh->num_normals)
			? mesh->normals[vx->normal_idx]
			: (vec3_t) { 0, 1, 0 };
		vec2_t t = (vx->texcoord_idx >= 0 && (size_t)vx->texcoord_idx < mesh->num_texcoords)
			? mesh->texcoords[vx->texcoord_idx]
			: (vec2_t) { 0, 0 };

		memcpy (v + 0, &p.x, 4);
		memcpy (v + 4, &p.y, 4);
		memcpy (v + 8, &p.z, 4);
		memcpy (v + 12, &n.x, 4);
		memcpy (v + 16, &n.y, 4);
		memcpy (v + 20, &n.z, 4);
		memcpy (v + 24, &t.u, 4);
		memcpy (v + 28, &t.v, 4);
	}

	uint32_t idx_count = vtx_count;
	uint32_t idx_buf_size = ALIGN_4 (idx_count * 2);
	uint16_t *idx_buf = CALLOC (idx_buf_size / 2, sizeof (uint16_t));
	if (!idx_buf)
	{
		FREE (vtx_buf);
		return 0;
	}
	for (uint32_t i = 0; i < idx_count; i++)
		idx_buf[i] = (uint16_t)i;

	size_t base_size = ALIGN_4 (bch_size);
	size_t vtx_offset = ALIGN_4 (base_size);
	size_t idx_offset = ALIGN_4 (vtx_offset + vtx_buf_size);
	size_t total_size = ALIGN_4 (idx_offset + idx_buf_size);

	uint8_t *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (vtx_buf);
		FREE (idx_buf);
		return 0;
	}

	memcpy (out, bch_data, bch_size);
	memcpy (out + vtx_offset, vtx_buf, vtx_buf_size);
	memcpy (out + idx_offset, idx_buf, idx_buf_size);
	FREE (vtx_buf);
	FREE (idx_buf);

	// Expand raw_data_len to cover the appended buffers
	uint8_t bc = bch_data[4];
	const bool has_ext = bc >= 0x21;
	uint o_raw = has_ext ? 0x2c : 0x28;
	if (o_raw + 4 <= bch_size)
	{
		uint32_t old_raw_len = RDL32 (out + o_raw);
		WRL32 (out + o_raw, old_raw_len + (uint32_t)(total_size - base_size));
	}

	*out_data = out;
	*out_size = total_size;
	return 1;
}

//-----------------------------------------------------------------------------
// BCRES / CGFX (3DS CGFX) Injection
//-----------------------------------------------------------------------------

int InjectDAEIntoBCRES (const uint8_t *bcres_data, size_t bcres_size, const model_t *dae_model,
	uint8_t **out_data, size_t *out_size)
{
	if (!bcres_data || bcres_size < 0x20 || !dae_model || !out_data || !out_size
		|| !dae_model->num_meshes)
		return 0;

	if (memcmp (bcres_data, "CGFX", 4) != 0)
		return 0;

	const mesh_t *mesh = &dae_model->meshes[0];
	if (!mesh->num_vertices)
		return 0;

	uint32_t vtx_count = (uint32_t)mesh->num_vertices;
	uint32_t vtx_stride = 32;
	uint32_t vtx_buf_size = vtx_count * vtx_stride;

	uint8_t *vtx_buf = CALLOC (vtx_count, vtx_stride);
	if (!vtx_buf)
		return 0;

	for (uint32_t i = 0; i < vtx_count; i++)
	{
		uint8_t *v = vtx_buf + i * vtx_stride;
		const vertex_t *vx = &mesh->vertices[i];
		vec3_t p = (vx->position_idx >= 0 && (size_t)vx->position_idx < mesh->num_positions)
			? mesh->positions[vx->position_idx]
			: (vec3_t) { 0, 0, 0 };
		vec3_t n = (vx->normal_idx >= 0 && (size_t)vx->normal_idx < mesh->num_normals)
			? mesh->normals[vx->normal_idx]
			: (vec3_t) { 0, 1, 0 };
		vec2_t t = (vx->texcoord_idx >= 0 && (size_t)vx->texcoord_idx < mesh->num_texcoords)
			? mesh->texcoords[vx->texcoord_idx]
			: (vec2_t) { 0, 0 };

		memcpy (v + 0, &p.x, 4);
		memcpy (v + 4, &p.y, 4);
		memcpy (v + 8, &p.z, 4);
		memcpy (v + 12, &n.x, 4);
		memcpy (v + 16, &n.y, 4);
		memcpy (v + 20, &n.z, 4);
		memcpy (v + 24, &t.u, 4);
		memcpy (v + 28, &t.v, 4);
	}

	uint32_t idx_count = vtx_count;
	uint32_t idx_buf_size = ALIGN_4 (idx_count * 2);
	uint16_t *idx_buf = CALLOC (idx_buf_size / 2, sizeof (uint16_t));
	if (!idx_buf)
	{
		FREE (vtx_buf);
		return 0;
	}
	for (uint32_t i = 0; i < idx_count; i++)
		idx_buf[i] = (uint16_t)i;

	size_t base_size = ALIGN_4 (bcres_size);
	size_t vtx_offset = ALIGN_4 (base_size);
	size_t idx_offset = ALIGN_4 (vtx_offset + vtx_buf_size);
	size_t total_size = ALIGN_4 (idx_offset + idx_buf_size);

	uint8_t *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (vtx_buf);
		FREE (idx_buf);
		return 0;
	}

	memcpy (out, bcres_data, bcres_size);
	memcpy (out + vtx_offset, vtx_buf, vtx_buf_size);
	memcpy (out + idx_offset, idx_buf, idx_buf_size);
	FREE (vtx_buf);
	FREE (idx_buf);

	// Update CGFX header total size at 0x0c
	WRL32 (out + 0x0c, (uint32_t)total_size);

	*out_data = out;
	*out_size = total_size;
	return 1;
}

//-----------------------------------------------------------------------------
// NSBMD (Nintendo DS BMD0) Injection
//-----------------------------------------------------------------------------

// Encode float (-8..+7.999) to 1:3:12 fixed-point s16
static inline int16_t fx12_enc (float v)
{
	int val = (int)roundf (v * 4096.0f);
	if (val > 32767)
		val = 32767;
	if (val < -32768)
		val = -32768;
	return (int16_t)val;
}

// Encode float (-1..+0.999) to 1:0:9 fixed-point 10-bit signed
static inline uint32_t fx9_enc (float v)
{
	int val = (int)roundf (v * 511.0f);
	if (val > 511)
		val = 511;
	if (val < -512)
		val = -512;
	return (uint32_t)(val & 0x3FF);
}

// Encode float (-2048..+2047.9) to 1:11:4 fixed-point s16
static inline int16_t fx4_enc (float v)
{
	int val = (int)roundf (v * 16.0f);
	if (val > 32767)
		val = 32767;
	if (val < -32768)
		val = -32768;
	return (int16_t)val;
}

int InjectDAEIntoNSBMD (const uint8_t *nsbmd_data, size_t nsbmd_size, const model_t *dae_model,
	uint8_t **out_data, size_t *out_size)
{
	if (!nsbmd_data || nsbmd_size < 0x20 || !dae_model || !out_data || !out_size
		|| !dae_model->num_meshes)
		return 0;

	if (memcmp (nsbmd_data, "BMD0", 4) != 0)
		return 0;

	const mesh_t *mesh = &dae_model->meshes[0];
	if (!mesh->num_vertices)
		return 0;

	// Encode DS Geometry display list
	// Capacity: 4 command bytes per 4-byte command word + param words
	size_t max_dl_words = (mesh->num_vertices + 10) * 8;
	uint32_t *dl_words = CALLOC (max_dl_words, sizeof (uint32_t));
	if (!dl_words)
		return 0;

	size_t word_idx = 0;

	// Command 1: BEGIN_VTXS(0 = GL_TRIANGLES)
	// Opcode 0x40 with param = 0
	dl_words[word_idx++] = 0x00000040; // 0x40, 0x00, 0x00, 0x00
	dl_words[word_idx++] = 0; // GL_TRIANGLES

	// Vertices: each has NORMAL (0x21), TEXCOORD (0x22), VTX_16 (0x20)
	for (size_t i = 0; i < mesh->num_vertices; i++)
	{
		const vertex_t *vx = &mesh->vertices[i];
		vec3_t p = (vx->position_idx >= 0 && (size_t)vx->position_idx < mesh->num_positions)
			? mesh->positions[vx->position_idx]
			: (vec3_t) { 0, 0, 0 };
		vec3_t n = (vx->normal_idx >= 0 && (size_t)vx->normal_idx < mesh->num_normals)
			? mesh->normals[vx->normal_idx]
			: (vec3_t) { 0, 1, 0 };
		vec2_t t = (vx->texcoord_idx >= 0 && (size_t)vx->texcoord_idx < mesh->num_texcoords)
			? mesh->texcoords[vx->texcoord_idx]
			: (vec2_t) { 0, 0 };

		uint32_t norm_packed = fx9_enc (n.x) | (fx9_enc (n.y) << 10) | (fx9_enc (n.z) << 20);
		uint32_t tex_packed = (uint16_t)fx4_enc (t.u) | ((uint32_t)(uint16_t)fx4_enc (t.v) << 16);
		uint32_t vtx_xy = (uint16_t)fx12_enc (p.x) | ((uint32_t)(uint16_t)fx12_enc (p.y) << 16);
		uint32_t vtx_z = (uint16_t)fx12_enc (p.z);

		// Pack commands: 0x21 (NORMAL), 0x22 (TEXCOORD), 0x23 (VTX_16), 0x00 (NOP)
		dl_words[word_idx++] = 0x00232221;
		dl_words[word_idx++] = norm_packed;
		dl_words[word_idx++] = tex_packed;
		dl_words[word_idx++] = vtx_xy;
		dl_words[word_idx++] = vtx_z;
	}

	// Command: END_VTXS (0x41)
	dl_words[word_idx++] = 0x00000041;

	size_t dl_bytes = word_idx * sizeof (uint32_t);

	// Build new NSBMD buffer
	size_t base_size = ALIGN_4 (nsbmd_size);
	size_t total_size = ALIGN_4 (base_size + dl_bytes);

	uint8_t *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (dl_words);
		return 0;
	}

	memcpy (out, nsbmd_data, nsbmd_size);
	memcpy (out + base_size, dl_words, dl_bytes);
	FREE (dl_words);

	// Update BMD0 total file size at offset 0x08
	WRL32 (out + 0x08, (uint32_t)total_size);

	*out_data = out;
	*out_size = total_size;
	return 1;
}

//-----------------------------------------------------------------------------
int InjectDAEIntoEarlyDSBMD (const uint8_t *bmd_data, size_t bmd_size, const model_t *dae_model,
	uint8_t **out_data, size_t *out_size)
{
	if (!bmd_data || bmd_size < 60 || !dae_model || !out_data || !out_size
		|| !dae_model->num_meshes)
		return 0;

	uint32_t shapes_base = RDL32 (bmd_data + 16);
	if (shapes_base != 0x3c)
		return 0;

	const mesh_t *mesh = &dae_model->meshes[0];
	if (!mesh->num_vertices)
		return 0;

	size_t max_dl_words = (mesh->num_vertices + 10) * 8;
	uint32_t *dl_words = CALLOC (max_dl_words, sizeof (uint32_t));
	if (!dl_words)
		return 0;

	size_t word_idx = 0;
	dl_words[word_idx++] = 0x00000040; // BEGIN_VTXS
	dl_words[word_idx++] = 0; // GL_TRIANGLES

	for (size_t i = 0; i < mesh->num_vertices; i++)
	{
		const vertex_t *vx = &mesh->vertices[i];
		vec3_t p = (vx->position_idx >= 0 && (size_t)vx->position_idx < mesh->num_positions)
			? mesh->positions[vx->position_idx]
			: (vec3_t) { 0, 0, 0 };
		vec3_t n = (vx->normal_idx >= 0 && (size_t)vx->normal_idx < mesh->num_normals)
			? mesh->normals[vx->normal_idx]
			: (vec3_t) { 0, 1, 0 };
		vec2_t t = (vx->texcoord_idx >= 0 && (size_t)vx->texcoord_idx < mesh->num_texcoords)
			? mesh->texcoords[vx->texcoord_idx]
			: (vec2_t) { 0, 0 };

		uint32_t norm_packed = fx9_enc (n.x) | (fx9_enc (n.y) << 10) | (fx9_enc (n.z) << 20);
		uint32_t tex_packed = (uint16_t)fx4_enc (t.u) | ((uint32_t)(uint16_t)fx4_enc (t.v) << 16);
		uint32_t vtx_xy = (uint16_t)fx12_enc (p.x) | ((uint32_t)(uint16_t)fx12_enc (p.y) << 16);
		uint32_t vtx_z = (uint16_t)fx12_enc (p.z);

		dl_words[word_idx++] = 0x00232221;
		dl_words[word_idx++] = norm_packed;
		dl_words[word_idx++] = tex_packed;
		dl_words[word_idx++] = vtx_xy;
		dl_words[word_idx++] = vtx_z;
	}

	dl_words[word_idx++] = 0x00000041; // END_VTXS
	size_t dl_bytes = word_idx * sizeof (uint32_t);

	size_t base_size = ALIGN_4 (bmd_size);
	size_t total_size = ALIGN_4 (base_size + dl_bytes);

	uint8_t *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (dl_words);
		return 0;
	}

	memcpy (out, bmd_data, bmd_size);
	memcpy (out + base_size, dl_words, dl_bytes);
	FREE (dl_words);

	// Update shapes table entry at offset 0x3c
	// Entry: [0]=count1, [1]=chunk1_off, [2]=count2, [3]=chunk2_off, [4]=dl_len, [5]=dl_off, [6]=0
	WRL32 (out + 0x3c + 16, (uint32_t)dl_bytes);
	WRL32 (out + 0x3c + 20, (uint32_t)base_size);

	*out_data = out;
	*out_size = total_size;
	return 1;
}

//-----------------------------------------------------------------------------
// Universal Dispatcher
//-----------------------------------------------------------------------------

int InjectDAEIntoModel (const uint8_t *parent_data, size_t parent_size, const model_t *dae_model,
	uint8_t **out_data, size_t *out_size)
{
	if (!parent_data || parent_size < 4 || !dae_model || !out_data || !out_size)
		return 0;

	if (parent_size >= 4 && !memcmp (parent_data, "bres", 4))
		return InjectDAEIntoBRRES (parent_data, parent_size, dae_model, out_data, out_size);
	if (parent_size >= 4 && !memcmp (parent_data, "MDL0", 4))
		return InjectDAEIntoMDL0 (parent_data, parent_size, dae_model, out_data, out_size);
	if (parent_size >= 4 && !memcmp (parent_data, "FRES", 4))
	{
		// Distinguish Wii U (BOM at+0x08) vs Switch (BOM at+0x0C).
		if (parent_size >= 10 && SWP16 (*(const uint16_t *)(parent_data + 8)) == 0xFEFF)
			return InjectDAEIntoBFRES (parent_data, parent_size, dae_model, out_data, out_size);
		if (parent_size >= 14 && RLE16 (parent_data + 0x0C) == 0xFEFF)
			return InjectDAEIntoSwitchBFRES (
				parent_data, parent_size, dae_model, out_data, out_size);
		return 0;
	}
	if (parent_size >= 4 && !memcmp (parent_data, "BCH", 3) && parent_data[3] == 0)
		return InjectDAEIntoBCH (parent_data, parent_size, dae_model, out_data, out_size);
	if (parent_size >= 4 && !memcmp (parent_data, "CGFX", 4))
		return InjectDAEIntoBCRES (parent_data, parent_size, dae_model, out_data, out_size);
	if (parent_size >= 4 && !memcmp (parent_data, "BMD0", 4))
		return InjectDAEIntoNSBMD (parent_data, parent_size, dae_model, out_data, out_size);
	if (parent_size >= 60 && RDL32 (parent_data + 16) == 0x3c)
		return InjectDAEIntoEarlyDSBMD (parent_data, parent_size, dae_model, out_data, out_size);

	return 0;
}

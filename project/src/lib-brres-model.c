#include "lib-brres-model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#pragma pack(push, 1)

typedef struct
{
	char tag[4];
	uint32_t size;
	uint32_t version;
	int32_t bresOffset;

	int32_t defsOffset;
	int32_t bonesOffset;
	int32_t positionsOffset;
	int32_t normalsOffset;
	int32_t colorsOffset;
	int32_t uvsOffset;
	int32_t furVecsOffset;
	int32_t furPosOffset;
	int32_t materialsOffset;
	int32_t texSRTOffset;
	int32_t shadersOffset;
	int32_t meshesOffset;
	int32_t texLinksOffset;
	int32_t palettesOffset;
	int32_t userDataOffset;
	int32_t stringOffset;
} MDL0Header;

typedef struct
{
	int32_t headerLen;
	int32_t mdl0Offset;
	int32_t stringOffset;
	int32_t index;

	int32_t nodeId;
	uint32_t flags;
	uint32_t bbFlags;
	uint32_t bbIndex;

	float scale[3];
	float rotation[3];
	float translation[3];
	float extents[6];

	int32_t parentOffset;
	int32_t firstChildOffset;
	int32_t nextOffset;
	int32_t prevOffset;
	int32_t userDataOffset;

	float transform[12];
	float transformInv[12];
} MDL0Bone;

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
} MDL0VertexData;

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
} MDL0NormalData;

typedef struct
{
	int32_t bufferSize;
	int32_t size;
	int32_t offset;
} PrimDataGroup;

typedef struct
{
	int32_t totalLength;
	int32_t mdl0Offset;
	int32_t nodeId;

	uint32_t vertexFormatLo;
	uint32_t vertexFormatHi;
	uint32_t vertexSpecs;

	PrimDataGroup defintions;
	PrimDataGroup primitives;

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
} MDL0Object;

typedef struct
{
	uint32_t size;
	uint32_t numNodes;
} ResourceGroup;

typedef struct
{
	uint16_t id;
	uint16_t unknown;
	uint16_t leftIndex;
	uint16_t rightIndex;
	int32_t stringOffset;
	int32_t dataOffset;
} ResourceEntry;

#pragma pack(pop)

typedef struct
{
	float matrix[12];
	float inverse[12];
	uint8_t valid;
	uint8_t has_inverse;
	// Bone node-ids and weights this matrix-node mixes, straight from the
	// bone table (one entry, weight 1) or a NodeMix influence (many). Kept so
	// the COLLADA exporter can emit a real skin controller instead of only
	// the baked bind pose.
	uint16_t *bones;
	float *weights;
	size_t num_weights;
} node_transform_t;

static void free_node_transforms (node_transform_t *table, size_t count)
{
	for (size_t i = 0; i < count; i++)
	{
		free (table[i].bones);
		free (table[i].weights);
	}
	free (table);
}

static int set_node_weights (
	node_transform_t *node, const uint16_t *bones, const float *weights, size_t count)
{
	uint16_t *b = malloc (count * sizeof (*b));
	float *w = malloc (count * sizeof (*w));
	if (!b || !w)
	{
		free (b);
		free (w);
		return 0;
	}
	memcpy (b, bones, count * sizeof (*b));
	memcpy (w, weights, count * sizeof (*w));
	free (node->bones);
	free (node->weights);
	node->bones = b;
	node->weights = w;
	node->num_weights = count;
	return 1;
}

static uint16_t swap16 (uint16_t val)
{
	return (val << 8) | (val >> 8);
}

static uint32_t swap32 (uint32_t val)
{
	return ((val << 24) & 0xff000000) | ((val << 8) & 0x00ff0000) | ((val >> 8) & 0x0000ff00)
		| ((val >> 24) & 0x000000ff);
}

static float swapf (float val)
{
	uint32_t temp;
	memcpy (&temp, &val, sizeof (float));
	temp = swap32 (temp);
	memcpy (&val, &temp, sizeof (float));
	return val;
}

static uint16_t read_be16 (const uint8_t *p)
{
	return (uint16_t)p[0] << 8 | p[1];
}

static int ensure_node_transform (node_transform_t **table, size_t *count, unsigned node_id)
{
	// GX matrix-node IDs are 16-bit. Refuse unreasonable corrupt IDs instead
	// of allowing a malformed bone to request an unbounded allocation.
	if (node_id > 0xffff)
		return 0;
	if (node_id < *count)
		return 1;
	size_t next = *count ? *count : 16;
	while (next <= node_id)
		next *= 2;
	node_transform_t *resized = realloc (*table, next * sizeof (*resized));
	if (!resized)
		return 0;
	memset (resized + *count, 0, (next - *count) * sizeof (*resized));
	*table = resized;
	*count = next;
	return 1;
}

static void multiply_affine43 (float out[12], const float a[12], const float b[12])
{
	// MDL0 bMatrix43 values are three conventional rows: the fourth value
	// in each row is translation. This is the same conversion BrawlCrate's
	// bMatrix43 -> Matrix operator performs before weighting vertices.
	for (unsigned r = 0; r < 3; r++)
	{
		for (unsigned c = 0; c < 3; c++)
			out[r * 4 + c]
				= a[r * 4 + 0] * b[c + 0] + a[r * 4 + 1] * b[c + 4] + a[r * 4 + 2] * b[c + 8];
		out[r * 4 + 3]
			= a[r * 4 + 0] * b[3] + a[r * 4 + 1] * b[7] + a[r * 4 + 2] * b[11] + a[r * 4 + 3];
	}
}

static unsigned attr_size (unsigned format)
{
	// GX_VA_*: 0=absent, 1=direct, 2=index8, 3=index16. Direct attributes
	// need the VAT component description and are deliberately rejected by
	// this decoder instead of guessing a byte layout and corrupting the DAE.
	return format == 2 ? 1 : format == 3 ? 2 : 0;
}

static int append_triangle (mesh_t *mesh, size_t *capacity, vertex_t a, vertex_t b, vertex_t c)
{
	if (mesh->num_vertices + 3 > *capacity)
	{
		size_t next = *capacity ? *capacity * 2 : 1024;
		while (next < mesh->num_vertices + 3)
			next *= 2;
		vertex_t *resized = realloc (mesh->vertices, next * sizeof (*resized));
		if (!resized)
			return 0;
		mesh->vertices = resized;
		*capacity = next;
	}
	mesh->vertices[mesh->num_vertices++] = a;
	mesh->vertices[mesh->num_vertices++] = b;
	mesh->vertices[mesh->num_vertices++] = c;
	return 1;
}

static int decode_gx_primitives (
	const uint8_t *prim_data, int32_t prim_size, uint32_t cp_lo, uint32_t cp_hi, mesh_t *mesh)
{
	const unsigned pos_fmt = cp_lo >> 9 & 3;
	const unsigned nrm_fmt = cp_lo >> 11 & 3;
	const unsigned uv0_fmt = cp_hi & 3;
	if (!pos_fmt || pos_fmt == 1 || nrm_fmt == 1 || uv0_fmt == 1)
		return 0;

	unsigned stride = cp_lo & 1;
	for (unsigned i = 0; i < 8; i++)
		stride += cp_lo >> (i + 1) & 1;
	stride += attr_size (pos_fmt) + attr_size (nrm_fmt);
	stride += attr_size (cp_lo >> 13 & 3) + attr_size (cp_lo >> 15 & 3);
	for (unsigned i = 0; i < 8; i++)
		stride += attr_size (cp_hi >> (i * 2) & 3);
	if (!stride)
		return 0;

	uint16_t matrix_nodes[16];
	for (unsigned i = 0; i < 16; i++)
		matrix_nodes[i] = 0xffff;
	size_t capacity = 0, off = 0;
	while (off < (size_t)prim_size)
	{
		const uint8_t command = prim_data[off++];
		if (!command)
			break;
		if (command >= 0x20 && command <= 0x38 && !(command & 7))
		{
			if (off + 4 > (size_t)prim_size)
				return 0;
			if (command == 0x20)
			{
				const unsigned slot = (read_be16 (prim_data + off + 2) & 0xfff) / 12;
				if (slot < 16)
					matrix_nodes[slot] = read_be16 (prim_data + off);
			}
			off += 4;
			continue;
		}

		const uint8_t primitive = command & 0xf8;
		if (primitive != 0x80 && primitive != 0x90 && primitive != 0x98 && primitive != 0xa0
			&& primitive != 0xa8 && primitive != 0xb0 && primitive != 0xb8)
			return 0;
		if (off + 2 > (size_t)prim_size)
			return 0;
		const unsigned count = read_be16 (prim_data + off);
		off += 2;
		if (count > ((size_t)prim_size - off) / stride)
			return 0;

		vertex_t *points = calloc ((size_t)count, sizeof (*points));
		if (!points)
			return 0;
		for (unsigned p = 0; p < count; p++)
		{
			memset (points + p, 0, sizeof (*points));
			size_t cursor = off + (size_t)p * stride;
			points[p].matrix_idx = -1;
			points[p].tangent_idx = -1;
			if (cp_lo & 1)
			{
				const unsigned slot = prim_data[cursor] / 3;
				if (slot < 16 && matrix_nodes[slot] != 0xffff)
					points[p].matrix_idx = matrix_nodes[slot];
				cursor++;
			}
			for (unsigned i = 0; i < 8; i++)
				cursor += cp_lo >> (i + 1) & 1;
			const unsigned ps = attr_size (pos_fmt);
			points[p].position_idx = ps == 1 ? prim_data[cursor] : read_be16 (prim_data + cursor);
			cursor += ps;
			const unsigned ns = attr_size (nrm_fmt);
			points[p].normal_idx
				= ns ? ns == 1 ? prim_data[cursor] : read_be16 (prim_data + cursor) : 0;
			cursor += ns;
			for (unsigned c = 0; c < 2; c++)
			{
				const unsigned cs = attr_size (cp_lo >> (13 + c * 2) & 3);
				points[p].color_idx[c]
					= cs ? cs == 1 ? prim_data[cursor] : read_be16 (prim_data + cursor) : 0;
				cursor += cs;
			}
			for (unsigned t = 0; t < 8; t++)
			{
				const unsigned ts = attr_size (cp_hi >> (t * 2) & 3);
				const int index
					= ts ? ts == 1 ? prim_data[cursor] : read_be16 (prim_data + cursor) : 0;
				if (!t)
					points[p].texcoord_idx = index;
				else
					points[p].extra_texcoord_idx[t - 1] = index;
				cursor += ts;
			}
		}
		off += (size_t)count * stride;

		// GX display lists store each primitive's vertices in the order the
		// hardware consumes them, which is the REVERSE of the front-facing
		// (right-hand-rule) winding COLLADA and every modern renderer expect.
		// Emitting them in file order produced a model whose triangles all
		// face inward: it looks correct in a viewer that does not cull
		// backfaces, and inside-out in one that does (Preview/SceneKit,
		// Unity, Unreal). Verified two ways -- against BrawlLib's
		// PrimitiveManager.ExtractPrimitives(), which reverses every
		// primitive type identically, and against the models' own authored
		// vertex normals: before this change the geometric normal disagreed
		// with the shading normal on 98.3% of triangles across a 400-model
		// retail corpus, after it they agree.
		int ok = 1;
		if (primitive == 0x90)
			for (unsigned p = 0; ok && p + 2 < count; p += 3)
				ok = append_triangle (mesh, &capacity, points[p + 2], points[p + 1], points[p]);
		else if (primitive == 0x80)
			for (unsigned p = 0; ok && p + 3 < count; p += 4)
			{
				ok = append_triangle (mesh, &capacity, points[p], points[p + 2], points[p + 1]);
				if (ok)
					ok = append_triangle (mesh, &capacity, points[p], points[p + 3], points[p + 2]);
			}
		else if (primitive == 0x98)
			for (unsigned p = 2; ok && p < count; p++)
				ok = p & 1
					? append_triangle (mesh, &capacity, points[p], points[p - 2], points[p - 1])
					: append_triangle (mesh, &capacity, points[p], points[p - 1], points[p - 2]);
		else if (primitive == 0xa0)
			for (unsigned p = 2; ok && p < count; p++)
				ok = append_triangle (mesh, &capacity, points[0], points[p], points[p - 1]);
		free (points);
		if (!ok)
			return 0;
	}
	return mesh->num_vertices > 0;
}

// Every offset below comes straight from untrusted file bytes and is used
// as raw pointer arithmetic into `data`/mmap'd storage. Bounds-check each
// one before dereferencing -- an out-of-range offset previously walked off
// the mapped region and raised SIGBUS (bus error) instead of failing
// cleanly, e.g. on malformed/unusual MDL0 bones or mesh groups.
static int in_bounds (const uint8_t *base, size_t size, const void *ptr, size_t len)
{
	if ((const uint8_t *)ptr < base)
		return 0;
	size_t off = (const uint8_t *)ptr - base;
	return off <= size && len <= size - off;
}

enum mdl_group_type
{
	MDL_DEFINITIONS,
	MDL_BONES,
	MDL_POSITIONS,
	MDL_NORMALS,
	MDL_COLORS,
	MDL_UVS,
	MDL_MATERIALS,
	MDL_OBJECTS,
	MDL_TEXTURES
};

static ResourceGroup *get_mdl_group (
	const uint8_t *data, size_t size, uint32_t version, enum mdl_group_type type)
{
	unsigned index;
	switch (type)
	{
		case MDL_DEFINITIONS:
			index = 0;
			break;
		case MDL_BONES:
			index = 1;
			break;
		case MDL_POSITIONS:
			index = 2;
			break;
		case MDL_NORMALS:
			index = 3;
			break;
		case MDL_COLORS:
			index = 4;
			break;
		case MDL_UVS:
			index = 5;
			break;
		case MDL_MATERIALS:
			index = version >= 10 ? 8 : 6;
			break;
		case MDL_OBJECTS:
			index = version >= 10 ? 10 : 8;
			break;
		case MDL_TEXTURES:
			index = version >= 10 ? 11 : 9;
			break;
		default:
			return NULL;
	}
	const size_t field = 0x10 + index * 4;
	if (field + 4 > size)
		return NULL;
	const int32_t offset = (int32_t)swap32 (*(const uint32_t *)(data + field));
	ResourceGroup *group = (ResourceGroup *)(data + offset);
	return offset > 0 && in_bounds (data, size, group, sizeof (*group)) ? group : NULL;
}

static const uint8_t *get_group_resource (
	const uint8_t *data, size_t size, ResourceGroup *group, int id, size_t need)
{
	if (!group || id < 0)
		return NULL;
	const uint32_t count = swap32 (group->numNodes);
	if ((uint32_t)id >= count
		|| !in_bounds (data, size, group + 1, (size_t)(count + 1) * sizeof (ResourceEntry)))
		return NULL;
	ResourceEntry *entries = (ResourceEntry *)(group + 1);
	const int32_t offset = (int32_t)swap32 (entries[id + 1].dataOffset);
	const uint8_t *resource = (const uint8_t *)group + offset;
	return offset >= 0 && in_bounds (data, size, resource, need) ? resource : NULL;
}

static float read_component (const uint8_t *p, uint32_t type, uint8_t divisor)
{
	const float scale = 1.0f / (float)(1u << (divisor < 31 ? divisor : 30));
	switch (type)
	{
		case 0:
			return p[0] * scale;
		case 1:
			return (int8_t)p[0] * scale;
		case 2:
			return read_be16 (p) * scale;
		case 3:
			return (int16_t)read_be16 (p) * scale;
		case 4:
		{
			uint32_t bits
				= (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
			float value;
			memcpy (&value, &bits, sizeof (value));
			return value;
		}
		default:
			return 0.0f;
	}
}

static unsigned component_size (uint32_t type)
{
	return type < 2 ? 1 : type < 4 ? 2 : type == 4 ? 4 : 0;
}

static int load_vec3_array (const uint8_t *data, size_t size, const uint8_t *node,
	size_t header_size, vec3_t **dest, size_t *count)
{
	if (!node || !in_bounds (data, size, node, header_size))
		return 0;
	const int32_t data_offset = (int32_t)swap32 (*(const uint32_t *)(node + 8));
	const uint32_t dimensions = swap32 (*(const uint32_t *)(node + 20));
	const uint32_t type = swap32 (*(const uint32_t *)(node + 24));
	const uint8_t divisor = node[28], stride = node[29];
	const uint16_t num = read_be16 (node + 30);
	const unsigned elem = component_size (type);
	// Position headers use isXYZ (XY vs XYZ). Normal headers use isNBT;
	// ordinary XYZ normals have that field clear but still contain 3 values.
	const unsigned components = header_size == 0x20 ? 3 : dimensions ? 3 : 2;
	const uint8_t *src = node + data_offset;
	if (data_offset <= 0 || !num || !elem || stride < elem * components
		|| !in_bounds (data, size, src, (size_t)stride * num))
		return 0;
	vec3_t *array = calloc (num, sizeof (*array));
	if (!array)
		return 0;
	for (unsigned i = 0; i < num; i++)
	{
		const uint8_t *p = src + (size_t)i * stride;
		array[i].x = read_component (p, type, divisor);
		array[i].y = read_component (p + elem, type, divisor);
		array[i].z = components == 3 ? read_component (p + 2 * elem, type, divisor) : 0.0f;
	}
	*dest = array;
	*count = num;
	return 1;
}

// Load normals and tangents from an MDL0 NBT (Normal-Binormal-Tangent) buffer.
// Each vertex stores 3 × 3 components: [Nx Ny Nz] [Bx By Bz] [Tx Ty Tz].
// The isNBT flag is at node+0x04 (big-endian int32).
static int load_normal_nbt_array (const uint8_t *data, size_t size, const uint8_t *node,
	vec3_t **normals, size_t *num_normals, vec3_t **tangents, size_t *num_tangents)
{
	if (!node || !in_bounds (data, size, node, 0x20))
		return 0;
	const int32_t is_nbt = (int32_t)swap32 (*(const uint32_t *)(node + 4));
	if (!is_nbt)
	{
		// Plain normals: load as usual, no tangents.
		*tangents = NULL;
		*num_tangents = 0;
		return load_vec3_array (data, size, node, 0x20, normals, num_normals);
	}
	// NBT mode: each vertex has 9 components (3 × vec3).
	const int32_t data_offset = (int32_t)swap32 (*(const uint32_t *)(node + 8));
	const uint32_t type = swap32 (*(const uint32_t *)(node + 24));
	const uint8_t divisor = node[28], stride = node[29];
	const uint16_t num = read_be16 (node + 30);
	const unsigned elem = component_size (type);
	// For NBT, each component group is 3 values; 3 groups = 9 values total.
	// The actual stride should be 3× the normal entry stride.
	const uint8_t nbt_stride = (uint8_t)(3 * elem * 3);
	const uint8_t effective_stride = stride >= nbt_stride ? stride : (uint8_t)(elem * 9);
	const uint8_t *src = node + data_offset;
	if (data_offset <= 0 || !num || !elem || effective_stride < elem * 9
		|| !in_bounds (data, size, src, (size_t)effective_stride * num))
		return 0;
	vec3_t *nrm = calloc (num, sizeof (*nrm));
	vec3_t *tan = calloc (num, sizeof (*tan));
	if (!nrm || !tan)
	{
		free (nrm);
		free (tan);
		return 0;
	}
	for (unsigned i = 0; i < num; i++)
	{
		const uint8_t *p = src + (size_t)i * effective_stride;
		// Normal: components 0..2
		nrm[i].x = read_component (p, type, divisor);
		nrm[i].y = read_component (p + elem, type, divisor);
		nrm[i].z = read_component (p + 2 * elem, type, divisor);
		// Binormal: components 3..5 (skipped)
		// Tangent: components 6..8
		tan[i].x = read_component (p + 6 * elem, type, divisor);
		tan[i].y = read_component (p + 7 * elem, type, divisor);
		tan[i].z = read_component (p + 8 * elem, type, divisor);
	}
	*normals = nrm;
	*num_normals = num;
	*tangents = tan;
	*num_tangents = num;
	return 1;
}

static int load_vec2_array (
	const uint8_t *data, size_t size, const uint8_t *node, vec2_t **dest, size_t *count)
{
	if (!node || !in_bounds (data, size, node, 0x20))
		return 0;
	const int32_t data_offset = (int32_t)swap32 (*(const uint32_t *)(node + 8));
	const uint32_t dimensions = swap32 (*(const uint32_t *)(node + 20));
	const uint32_t type = swap32 (*(const uint32_t *)(node + 24));
	const uint8_t divisor = node[28], stride = node[29];
	const uint16_t num = read_be16 (node + 30);
	const unsigned elem = component_size (type);
	const unsigned components = dimensions ? 2 : 1;
	const uint8_t *src = node + data_offset;
	if (data_offset <= 0 || !num || !elem || stride < elem * components
		|| !in_bounds (data, size, src, (size_t)stride * num))
		return 0;
	vec2_t *array = calloc (num, sizeof (*array));
	if (!array)
		return 0;
	for (unsigned i = 0; i < num; i++)
	{
		const uint8_t *p = src + (size_t)i * stride;
		array[i].u = read_component (p, type, divisor);
		array[i].v = components == 2 ? read_component (p + elem, type, divisor) : 0.0f;
	}
	*dest = array;
	*count = num;
	return 1;
}

static int load_color_array (
	const uint8_t *data, size_t size, const uint8_t *node, color4_t **dest, size_t *count)
{
	if (!node || !in_bounds (data, size, node, 0x20))
		return 0;
	const int32_t data_offset = (int32_t)swap32 (*(const uint32_t *)(node + 8));
	const uint32_t format = swap32 (*(const uint32_t *)(node + 24));
	const uint8_t stride = node[28];
	const uint16_t num = read_be16 (node + 30);
	static const uint8_t format_size[] = { 2, 3, 4, 2, 3, 4 };
	if (format >= sizeof (format_size) || data_offset <= 0 || !num || stride < format_size[format])
		return 0;
	const uint8_t *src = node + data_offset;
	if (!in_bounds (data, size, src, (size_t)stride * num))
		return 0;
	color4_t *array = calloc (num, sizeof (*array));
	if (!array)
		return 0;
	for (unsigned i = 0; i < num; i++)
	{
		const uint8_t *p = src + (size_t)i * stride;
		unsigned r = 0, g = 0, b = 0, a = 255;
		switch (format)
		{
			case 0: // RGB565
			{
				const unsigned v = read_be16 (p);
				r = (v >> 11 & 31) * 255 / 31;
				g = (v >> 5 & 63) * 255 / 63;
				b = (v & 31) * 255 / 31;
				break;
			}
			case 1:
				r = p[0];
				g = p[1];
				b = p[2];
				break; // RGB8
			case 2:
				r = p[0];
				g = p[1];
				b = p[2];
				break; // RGBX8
			case 3: // RGBA4
			{
				const unsigned v = read_be16 (p);
				r = (v >> 12 & 15) * 17;
				g = (v >> 8 & 15) * 17;
				b = (v >> 4 & 15) * 17;
				a = (v & 15) * 17;
				break;
			}
			case 4: // RGBA6, packed into 24 bits
			{
				const unsigned v = (unsigned)p[0] << 16 | (unsigned)p[1] << 8 | p[2];
				r = (v >> 18 & 63) * 255 / 63;
				g = (v >> 12 & 63) * 255 / 63;
				b = (v >> 6 & 63) * 255 / 63;
				a = (v & 63) * 255 / 63;
				break;
			}
			case 5:
				r = p[0];
				g = p[1];
				b = p[2];
				a = p[3];
				break; // RGBA8
		}
		array[i] = (color4_t) { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
	}
	*dest = array;
	*count = num;
	return 1;
}

// Resolves a length-prefixed pooled string from a 4-byte big-endian offset
// field. `resolve_base` is what the offset is added to: the outer
// ResourceHeader convention (name/index) is struct-relative (resolve_base =
// start of the owning struct), but layer/texture-name fields deeper inside
// a Material use a different, self-relative convention (resolve_base =
// address of the offset field itself) -- verified against a real retail
// BRRES (SpinningRoom_01.brres / MT_L_map material). A zero offset means
// "absent". Returns NULL if the field, length prefix, or string body would
// read outside [base,base+size), or the bytes aren't printable ASCII.
static const char *read_pooled_string (const uint8_t *base, size_t size, const uint8_t *field_ptr,
	const uint8_t *resolve_base, uint32_t *out_len)
{
	if (!in_bounds (base, size, field_ptr, 4))
		return NULL;
	int32_t rel = (int32_t)swap32 (*(const uint32_t *)field_ptr);
	if (!rel)
		return NULL;
	const uint8_t *target = resolve_base + rel;
	if (!in_bounds (base, size, target - 4, 4))
		return NULL;
	uint32_t len = swap32 (*(const uint32_t *)(target - 4));
	if (!len || len > 255 || !in_bounds (base, size, target, len))
		return NULL;
	for (uint32_t i = 0; i < len; i++)
	{
		uint8_t c = target[i];
		if (c < 0x20 || c > 0x7e)
			return NULL;
	}
	if (out_len)
		*out_len = len;
	return (const char *)target;
}

static void copy_pooled_string (char *dst, size_t dst_size, const char *src, uint32_t len)
{
	if (len >= dst_size)
		len = dst_size - 1;
	memcpy (dst, src, len);
	dst[len] = 0;
}

// Read the declared MDL0TextureRef array in layer order. This is deliberately
// structural: scanning the material body also finds shader and user-data
// strings, which produced bogus image names in exported COLLADA files.
static void read_material_textures (const uint8_t *data, size_t size, const uint8_t *mat_base,
	int32_t mat_len, uint32_t version, material_t *mat)
{
	// MDL0Material::_numTextures and ::_matRefOffset are at 0x2c/0x30.
	// Each referenced MDL0TextureRef is exactly 0x34 bytes and its first
	// dword is a self-relative texture-name offset.  Do not scan arbitrary
	// material bytes: shader/user-data strings are not texture layers.
	if (mat_len < 0x34)
		return;
	uint32_t count = swap32 (*(const uint32_t *)(mat_base + 0x2c));
	int32_t refs_offset = (int32_t)swap32 (*(const uint32_t *)(mat_base + 0x30));
	const unsigned max_layers = sizeof (mat->textures) / sizeof (mat->textures[0]);
	if (refs_offset <= 0 || count > max_layers)
		return;
	const uint8_t *ref = mat_base + refs_offset;
	if (!in_bounds (data, size, ref, (size_t)count * 0x34))
		return;
	// Keep the declared layer indices even if one pooled string is outside a
	// detached MDL0 chunk. Compacting the valid names changes sampler/UV-set
	// association and is exactly the wrong recovery for these files.
	mat->num_textures = count;
	for (uint32_t i = 0; i < count; i++, ref += 0x34)
	{
		// MDL0TextureRef sampler state: uWrap/vWrap at 0x18/0x1c and
		// min/mag filter at 0x20/0x24. Confirmed on a retail corpus, where
		// these fields only ever hold their legal enum values (wrap 0..2,
		// filter 0..1) -- a misread layout would show arbitrary dwords.
		// Without them every CLAMP or MIRROR layer was exported as the
		// importer's default (usually REPEAT) and mapped visibly wrong.
		const uint32_t u = swap32 (*(const uint32_t *)(ref + 0x18));
		const uint32_t v = swap32 (*(const uint32_t *)(ref + 0x1c));
		const uint32_t mn = swap32 (*(const uint32_t *)(ref + 0x20));
		const uint32_t mg = swap32 (*(const uint32_t *)(ref + 0x24));
		mat->wrap_s[i] = u <= 2 ? (uint8_t)u : 1;
		mat->wrap_t[i] = v <= 2 ? (uint8_t)v : 1;
		mat->min_filter[i] = mn <= 5 ? (uint8_t)mn : 1;
		mat->mag_filter[i] = mg <= 1 ? (uint8_t)mg : 1;

		uint32_t len;
		const char *name = read_pooled_string (data, size, ref, ref, &len);
		if (!name)
			continue;
		copy_pooled_string (mat->textures[i], sizeof (mat->textures[i]), name, len);
	}

	// The material display list stores one XF texgen command followed by one
	// dual-texture command per layer. SourceRow bits 7..11 select Geometry,
	// Normal, Color, or TexCoord0..7; COLLADA needs the latter as an input set.
	const size_t display_field = version >= 10 ? 0x3c : 0x38;
	if (display_field + 4 > (size_t)mat_len)
		return;
	const int32_t display_offset = (int32_t)swap32 (*(const uint32_t *)(mat_base + display_field));
	const uint8_t *xf = mat_base + display_offset + 0xe0;
	if (display_offset <= 0 || !in_bounds (data, size, xf, 1))
		return;
	unsigned command = 0;
	while (
		command < (unsigned)mat->num_textures * 2 && in_bounds (data, size, xf, 5) && xf[0] == 0x10)
	{
		const unsigned values = read_be16 (xf + 1) + 1;
		const size_t command_size = 5 + (size_t)values * 4;
		if (!in_bounds (data, size, xf, command_size))
			break;
		if (!(command & 1) && values)
		{
			const uint32_t info = swap32 (*(const uint32_t *)(xf + 5));
			const unsigned source_row = info >> 7 & 31;
			mat->texture_coord[command / 2]
				= source_row >= 5 && source_row <= 12 ? (int)source_row - 5 : 0;
		}
		command++;
		xf += command_size;
	}
}

// BRRES uses one shared string pool after all of its resources. Extraction
// normally rewrites pooled offsets and appends the strings to each detached
// BRSUB, but older MDL0 layouts contain material-ref string fields that the
// generic BRSUB string walker does not visit. The resource-group names *are*
// retained, and the MDL0 Textures group provides an authoritative mapping
// from each texture name to the exact MDL0TextureRef records that use it.
// This is also how BrawlCrate links MDL0TextureNode resources back to material
// refs, and recovers v8/v9 names without scanning arbitrary ASCII bytes.
static void read_linked_material_textures (
	const uint8_t *data, size_t size, uint32_t version, material_t *materials, size_t num_materials)
{
	ResourceGroup *mat_group = get_mdl_group (data, size, version, MDL_MATERIALS);
	ResourceGroup *tex_group = get_mdl_group (data, size, version, MDL_TEXTURES);
	if (!mat_group || !tex_group || !materials)
		return;

	const uint32_t n_mat = swap32 (mat_group->numNodes);
	const uint32_t n_tex = swap32 (tex_group->numNodes);
	if (n_mat > num_materials
		|| !in_bounds (data, size, mat_group + 1, (size_t)(n_mat + 1) * sizeof (ResourceEntry))
		|| !in_bounds (data, size, tex_group + 1, (size_t)(n_tex + 1) * sizeof (ResourceEntry)))
		return;

	ResourceEntry *mat_entries = (ResourceEntry *)(mat_group + 1);
	ResourceEntry *tex_entries = (ResourceEntry *)(tex_group + 1);
	for (uint32_t ti = 1; ti <= n_tex; ti++)
	{
		uint32_t name_len;
		const char *name = read_pooled_string (data, size,
			(const uint8_t *)&tex_entries[ti].stringOffset, (const uint8_t *)tex_group, &name_len);
		const uint8_t *links = get_group_resource (data, size, tex_group, ti - 1, 4);
		if (!name || !links)
			continue;
		const uint32_t n_links = swap32 (*(const uint32_t *)links);
		if (n_links > (size - (size_t)(links - data) - 4) / 8
			|| !in_bounds (data, size, links + 4, (size_t)n_links * 8))
			continue;

		for (uint32_t li = 0; li < n_links; li++)
		{
			const int32_t ref_rel
				= (int32_t)swap32 (*(const uint32_t *)(links + 8 + (size_t)li * 8));
			if (ref_rel < 0)
				continue;
			const uint8_t *linked_ref = links + ref_rel;
			if (!in_bounds (data, size, linked_ref, 0x34))
				continue;

			for (uint32_t mi = 0; mi < n_mat; mi++)
			{
				const int32_t mat_rel = (int32_t)swap32 (mat_entries[mi + 1].dataOffset);
				if (mat_rel < 0)
					continue;
				const uint8_t *mat = (const uint8_t *)mat_group + mat_rel;
				if (!in_bounds (data, size, mat, 0x34))
					continue;
				const uint32_t count = swap32 (*(const uint32_t *)(mat + 0x2c));
				const int32_t refs_rel = (int32_t)swap32 (*(const uint32_t *)(mat + 0x30));
				if (refs_rel <= 0 || count > 8)
					continue;
				const uint8_t *refs = mat + refs_rel;
				if (!in_bounds (data, size, refs, (size_t)count * 0x34))
					continue;
				for (uint32_t layer = 0; layer < count; layer++)
					if (refs + (size_t)layer * 0x34 == linked_ref
						&& !materials[mi].textures[layer][0])
						copy_pooled_string (materials[mi].textures[layer],
							sizeof (materials[mi].textures[layer]), name, name_len);
			}
		}
	}
}

static void read_draw_materials (const uint8_t *data, size_t size, uint32_t version,
	int *object_material, size_t num_objects, size_t num_materials)
{
	ResourceGroup *defs = get_mdl_group (data, size, version, MDL_DEFINITIONS);
	if (!defs)
		return;
	uint32_t count = swap32 (defs->numNodes);
	if (!in_bounds (data, size, defs + 1, (size_t)(count + 1) * sizeof (ResourceEntry)))
		return;
	ResourceEntry *entries = (ResourceEntry *)(defs + 1);
	for (uint32_t i = 1; i <= count; i++)
	{
		int32_t offset = (int32_t)swap32 (entries[i].dataOffset);
		const uint8_t *p = (const uint8_t *)defs + offset;
		if (offset < 0 || !in_bounds (data, size, p, 1) || *p != 4)
			continue;
		while (in_bounds (data, size, p, 1) && *p == 4)
		{
			// DrawOpa/DrawXlu command: tag, material, object, bone, priority.
			if (!in_bounds (data, size, p, 8))
				break;
			unsigned material = read_be16 (p + 1);
			unsigned object = read_be16 (p + 3);
			if (object < num_objects && material < num_materials && object_material[object] < 0)
				object_material[object] = material;
			p += 8;
		}
	}
}

static void read_node_mix_transforms (
	const uint8_t *data, size_t size, uint32_t version, node_transform_t **nodes, size_t *num_nodes)
{
	ResourceGroup *defs = get_mdl_group (data, size, version, MDL_DEFINITIONS);
	if (!defs)
		return;
	const uint32_t count = swap32 (defs->numNodes);
	if (!in_bounds (data, size, defs + 1, (size_t)(count + 1) * sizeof (ResourceEntry)))
		return;
	ResourceEntry *entries = (ResourceEntry *)(defs + 1);
	const uint8_t *p = NULL;
	for (uint32_t i = 1; i <= count; i++)
	{
		uint32_t name_len;
		const char *name = read_pooled_string (data, size,
			(const uint8_t *)&entries[i].stringOffset, (const uint8_t *)defs, &name_len);
		if (name && name_len == 7 && !memcmp (name, "NodeMix", 7))
		{
			const int32_t offset = (int32_t)swap32 (entries[i].dataOffset);
			const uint8_t *candidate = (const uint8_t *)defs + offset;
			if (offset >= 0 && in_bounds (data, size, candidate, 1))
				p = candidate;
			break;
		}
	}
	if (!p)
		return;

	// NodeMix consists of adjacent type-3 weighted influences and type-5
	// environment-matrix links. It has no length field or terminator; the
	// first byte that isn't one of those entry types starts the next resource.
	while (in_bounds (data, size, p, 1))
	{
		if (*p == 5)
		{
			if (!in_bounds (data, size, p, 5))
				break;
			p += 5;
			continue;
		}
		if (*p != 3 || !in_bounds (data, size, p, 4))
			break;

		const unsigned node_id = read_be16 (p + 1);
		const unsigned weights = p[3];
		const uint8_t *entry = p + 4;
		if (!weights || !in_bounds (data, size, entry, (size_t)weights * 6))
			break;
		if (!ensure_node_transform (nodes, num_nodes, node_id))
			break;

		node_transform_t result;
		memset (&result, 0, sizeof (result));
		uint16_t weight_bones[256];
		float weight_values[256];
		size_t num_weight_values = 0;
		if (weights == 1)
		{
			// BrawlCrate treats a one-weight Influence as the primary bone,
			// so its bind matrix (not an identity skinning matrix) is used.
			const unsigned bone_id = read_be16 (entry);
			if (bone_id < *num_nodes && (*nodes)[bone_id].valid)
			{
				memcpy (result.matrix, (*nodes)[bone_id].matrix, sizeof (result.matrix));
				memcpy (result.inverse, (*nodes)[bone_id].inverse, sizeof (result.inverse));
				result.valid = (*nodes)[bone_id].valid;
				result.has_inverse = (*nodes)[bone_id].has_inverse;
				weight_bones[0] = (uint16_t)bone_id;
				weight_values[0] = 1.0f;
				num_weight_values = 1;
			}
		}
		else
		{
			// At bind pose a real influence is the weighted sum of
			// bone.bind * bone.inverseBind. Usually that is exactly identity,
			// but use the stored matrices so unusual files match BrawlCrate.
			int valid = 1;
			for (unsigned w = 0; w < weights; w++, entry += 6)
			{
				const unsigned bone_id = read_be16 (entry);
				uint32_t bits = (uint32_t)entry[2] << 24 | (uint32_t)entry[3] << 16
					| (uint32_t)entry[4] << 8 | entry[5];
				float weight;
				memcpy (&weight, &bits, sizeof (weight));
				if (bone_id >= *num_nodes || !(*nodes)[bone_id].valid
					|| !(*nodes)[bone_id].has_inverse)
				{
					valid = 0;
					break;
				}
				float product[12];
				multiply_affine43 (product, (*nodes)[bone_id].matrix, (*nodes)[bone_id].inverse);
				for (unsigned n = 0; n < 12; n++)
					result.matrix[n] += product[n] * weight;
				if (num_weight_values < sizeof (weight_bones) / sizeof (*weight_bones))
				{
					weight_bones[num_weight_values] = (uint16_t)bone_id;
					weight_values[num_weight_values++] = weight;
				}
			}
			result.valid = valid;
			if (!valid)
				num_weight_values = 0;
		}
		if (result.valid)
		{
			node_transform_t *target = (*nodes) + node_id;
			free (target->bones);
			free (target->weights);
			memcpy (target, &result, sizeof (*target));
			target->bones = NULL;
			target->weights = NULL;
			target->num_weights = 0;
			if (num_weight_values)
				set_node_weights (target, weight_bones, weight_values, num_weight_values);
		}
		p += 4 + (size_t)weights * 6;
	}
}

static uint64_t bind_key_hash (uint64_t key)
{
	key ^= key >> 30;
	key *= UINT64_C (0xbf58476d1ce4e5b9);
	key ^= key >> 27;
	key *= UINT64_C (0x94d049bb133111eb);
	return key ^ (key >> 31);
}

static int remap_bind_array (
	mesh_t *mesh, int object_node, const node_transform_t *nodes, size_t num_nodes, int normals)
{
	vec3_t *source = normals ? mesh->normals : mesh->positions;
	const size_t source_count = normals ? mesh->num_normals : mesh->num_positions;
	if (!source || !source_count || !mesh->num_vertices)
		return 1;

	int needs_transform = 0;
	for (size_t i = 0; i < mesh->num_vertices; i++)
	{
		const int node
			= mesh->vertices[i].matrix_idx >= 0 ? mesh->vertices[i].matrix_idx : object_node;
		if (node >= 0 && (size_t)node < num_nodes && nodes[node].valid)
		{
			needs_transform = 1;
			break;
		}
	}
	if (!needs_transform)
		return 1;

	size_t hash_count = 16;
	while (hash_count < mesh->num_vertices * 2)
		hash_count *= 2;
	uint64_t *keys = malloc (hash_count * sizeof (*keys));
	size_t *values = malloc (hash_count * sizeof (*values));
	uint8_t *used = calloc (hash_count, 1);
	vec3_t *result = malloc (mesh->num_vertices * sizeof (*result));
	if (!keys || !values || !used || !result)
	{
		free (keys);
		free (values);
		free (used);
		free (result);
		return 0;
	}

	int *result_node = NULL;
	if (!normals)
	{
		result_node = malloc (mesh->num_vertices * sizeof (*result_node));
		if (!result_node)
		{
			free (keys);
			free (values);
			free (used);
			free (result);
			return 0;
		}
	}

	size_t result_count = 0;
	for (size_t i = 0; i < mesh->num_vertices; i++)
	{
		vertex_t *vertex = mesh->vertices + i;
		const unsigned source_idx = normals ? vertex->normal_idx : vertex->position_idx;
		const int node = vertex->matrix_idx >= 0 ? vertex->matrix_idx : object_node;
		const uint64_t key = (uint64_t)(uint32_t)node << 32 | source_idx;
		size_t slot = bind_key_hash (key) & (hash_count - 1);
		while (used[slot] && keys[slot] != key)
			slot = (slot + 1) & (hash_count - 1);
		if (!used[slot])
		{
			used[slot] = 1;
			keys[slot] = key;
			values[slot] = result_count;
			vec3_t value = source[source_idx];
			if (node >= 0 && (size_t)node < num_nodes && nodes[node].valid)
			{
				const float *m = nodes[node].matrix;
				const float x = value.x, y = value.y, z = value.z;
				value.x = m[0] * x + m[1] * y + m[2] * z + (normals ? 0.0f : m[3]);
				value.y = m[4] * x + m[5] * y + m[6] * z + (normals ? 0.0f : m[7]);
				value.z = m[8] * x + m[9] * y + m[10] * z + (normals ? 0.0f : m[11]);
			}
			if (result_node)
				result_node[result_count] = node;
			result[result_count++] = value;
		}
		if (normals)
			vertex->normal_idx = (int)values[slot];
		else
			vertex->position_idx = (int)values[slot];
	}

	free (keys);
	free (values);
	free (used);
	free (source);
	vec3_t *shrunk = realloc (result, result_count * sizeof (*result));
	if (shrunk)
		result = shrunk;
	if (normals)
	{
		mesh->normals = result;
		mesh->num_normals = result_count;
	}
	else
	{
		mesh->positions = result;
		mesh->num_positions = result_count;
		int *shrunk_nodes = realloc (result_node, result_count * sizeof (*result_node));
		free (mesh->position_node);
		mesh->position_node = shrunk_nodes ? shrunk_nodes : result_node;
	}
	return 1;
}

static int apply_bind_pose_transforms (
	mesh_t *mesh, int object_node, const node_transform_t *nodes, size_t num_nodes)
{
	return remap_bind_array (mesh, object_node, nodes, num_nodes, 0)
		&& remap_bind_array (mesh, object_node, nodes, num_nodes, 1);
}

model_t *ParseMDL0 (const uint8_t *data, size_t size)
{
	if (!data || size < sizeof (MDL0Header))
		return NULL;

	MDL0Header *hdr = (MDL0Header *)data;
	if (strncmp (hdr->tag, "MDL0", 4) != 0)
		return NULL;

	const uint32_t version = swap32 (hdr->version);
	if (version < 8 || version > 11)
		return NULL;
	model_t *model = calloc (1, sizeof (model_t));
	if (!model)
		return NULL;
	node_transform_t *node_transforms = NULL;
	size_t num_node_transforms = 0;

	// Parse Bones
	{
		ResourceGroup *grp = get_mdl_group (data, size, version, MDL_BONES);
		if (!grp)
			goto skip_bones;
		int32_t numBones = swap32 (grp->numNodes);
		if (numBones < 0
			|| !in_bounds (data, size, grp + 1, (size_t)(numBones + 1) * sizeof (ResourceEntry)))
			goto skip_bones;
		model->num_joints = numBones;
		model->joints = calloc (numBones, sizeof (joint_t));
		MDL0Bone **bone_nodes = calloc (numBones, sizeof (*bone_nodes));

		ResourceEntry *entries = (ResourceEntry *)(grp + 1);
		for (int i = 1; i <= numBones; i++)
		{
			int32_t dOffset = swap32 (entries[i].dataOffset);
			MDL0Bone *bNode = (MDL0Bone *)((uint8_t *)grp + dOffset);
			if (dOffset < 0 || !in_bounds (data, size, bNode, sizeof (MDL0Bone)))
				continue;
			if (bone_nodes)
				bone_nodes[i - 1] = bNode;

			model->joints[i - 1].parent_idx = -1;
			uint32_t name_len;
			const char *name = read_pooled_string (data, size,
				(const uint8_t *)&entries[i].stringOffset, (const uint8_t *)grp, &name_len);
			if (name)
				copy_pooled_string (
					model->joints[i - 1].name, sizeof (model->joints[i - 1].name), name, name_len);

			model->joints[i - 1].scale.x = swapf (bNode->scale[0]);
			model->joints[i - 1].scale.y = swapf (bNode->scale[1]);
			model->joints[i - 1].scale.z = swapf (bNode->scale[2]);

			model->joints[i - 1].rotate.x = swapf (bNode->rotation[0]);
			model->joints[i - 1].rotate.y = swapf (bNode->rotation[1]);
			model->joints[i - 1].rotate.z = swapf (bNode->rotation[2]);

			model->joints[i - 1].translate.x = swapf (bNode->translation[0]);
			model->joints[i - 1].translate.y = swapf (bNode->translation[1]);
			model->joints[i - 1].translate.z = swapf (bNode->translation[2]);

			const int32_t node_id = (int32_t)swap32 ((uint32_t)bNode->nodeId);
			if (node_id >= 0
				&& ensure_node_transform (
					&node_transforms, &num_node_transforms, (unsigned)node_id))
			{
				node_transform_t *transform = node_transforms + node_id;
				for (unsigned n = 0; n < 12; n++)
				{
					transform->matrix[n] = swapf (bNode->transform[n]);
					transform->inverse[n] = swapf (bNode->transformInv[n]);
				}
				transform->valid = transform->has_inverse = 1;
				const uint16_t self = (uint16_t)node_id;
				const float one = 1.0f;
				set_node_weights (transform, &self, &one, 1);
			}
			model->joints[i - 1].has_inverse_bind = 1;
			for (unsigned n = 0; n < 12; n++)
			{
				model->joints[i - 1].bind[n] = swapf (bNode->transform[n]);
				model->joints[i - 1].inverse_bind[n] = swapf (bNode->transformInv[n]);
			}
		}

		if (bone_nodes)
			for (int i = 0; i < numBones; i++)
			{
				MDL0Bone *bone = bone_nodes[i];
				if (!bone)
					continue;
				const int32_t parent_offset = (int32_t)swap32 ((uint32_t)bone->parentOffset);
				if (!parent_offset)
					continue;
				const uint8_t *parent = (const uint8_t *)bone + parent_offset;
				if (!in_bounds (data, size, parent, sizeof (MDL0Bone)))
					continue;
				for (int j = 0; j < numBones; j++)
					if ((const uint8_t *)bone_nodes[j] == parent)
					{
						model->joints[i].parent_idx = j;
						break;
					}
			}
		free (bone_nodes);
	}
skip_bones:

	read_node_mix_transforms (data, size, version, &node_transforms, &num_node_transforms);

	// Parse Materials (name + referenced texture layer names, so mesh
	// exporters can actually emit/bind the textures a model needs).
	{
		ResourceGroup *grp = get_mdl_group (data, size, version, MDL_MATERIALS);
		if (!grp)
			goto skip_materials;
		int32_t numMats = swap32 (grp->numNodes);
		if (numMats < 0
			|| !in_bounds (data, size, grp + 1, (size_t)(numMats + 1) * sizeof (ResourceEntry)))
			goto skip_materials;
		model->num_materials = numMats;
		model->materials = calloc (numMats, sizeof (material_t));

		ResourceEntry *entries = (ResourceEntry *)(grp + 1);
		for (int i = 1; i <= numMats; i++)
		{
			int32_t dOffset = swap32 (entries[i].dataOffset);
			const uint8_t *matBase = (uint8_t *)grp + dOffset;
			// Material headers start with { length, mdl0Offset, stringOffset, index, ... }.
			if (dOffset < 0 || !in_bounds (data, size, matBase, 16))
				continue;

			int32_t matLen = (int32_t)swap32 (*(const uint32_t *)matBase);
			if (matLen < 16 || !in_bounds (data, size, matBase, (size_t)matLen))
				matLen = 16;

			material_t *mat = &model->materials[i - 1];
			uint32_t nlen;
			const char *nm = read_pooled_string (data, size, matBase + 8, matBase, &nlen);
			if (!nm)
				nm = read_pooled_string (data, size, (const uint8_t *)&entries[i].stringOffset,
					(const uint8_t *)grp, &nlen);
			if (nm)
				copy_pooled_string (mat->name, sizeof (mat->name), nm, nlen);

			read_material_textures (data, size, matBase, matLen, version, mat);
		}
		read_linked_material_textures (data, size, version, model->materials, model->num_materials);
	}
skip_materials:

	// Parse Meshes
	{
		ResourceGroup *grp = get_mdl_group (data, size, version, MDL_OBJECTS);
		ResourceGroup *positions = get_mdl_group (data, size, version, MDL_POSITIONS);
		ResourceGroup *normals = get_mdl_group (data, size, version, MDL_NORMALS);
		ResourceGroup *colors = get_mdl_group (data, size, version, MDL_COLORS);
		ResourceGroup *uvs = get_mdl_group (data, size, version, MDL_UVS);
		if (!grp)
			goto skip_meshes;
		int32_t numMeshes = swap32 (grp->numNodes);
		if (numMeshes < 0
			|| !in_bounds (data, size, grp + 1, (size_t)(numMeshes + 1) * sizeof (ResourceEntry)))
			goto skip_meshes;
		model->meshes = calloc (numMeshes, sizeof (mesh_t));
		model->num_meshes = 0;
		int *object_material = malloc ((size_t)numMeshes * sizeof (*object_material));
		if (!object_material)
			goto skip_meshes;
		for (int i = 0; i < numMeshes; i++)
			object_material[i] = -1;
		read_draw_materials (data, size, version, object_material, numMeshes, model->num_materials);

		ResourceEntry *entries = (ResourceEntry *)(grp + 1);
		for (int i = 1; i <= numMeshes; i++)
		{
			int32_t dOffset = swap32 (entries[i].dataOffset);
			MDL0Object *oNode = (MDL0Object *)((uint8_t *)grp + dOffset);
			if (dOffset < 0 || !in_bounds (data, size, oNode, sizeof (MDL0Object)))
				continue;

			uint32_t cpLo = swap32 (oNode->vertexFormatLo);
			uint32_t cpHi = swap32 (oNode->vertexFormatHi);
			const int32_t object_node = (int32_t)swap32 ((uint32_t)oNode->nodeId);
			int32_t primOffset = swap32 (oNode->primitives.offset);
			int32_t primSize = swap32 (oNode->primitives.size);

			const uint8_t *primData = (const uint8_t *)&oNode->primitives + primOffset;
			if (primOffset < 0 || primSize < 0
				|| !in_bounds (data, size, primData, (size_t)primSize))
				continue;
			mesh_t *mesh = &model->meshes[model->num_meshes];
			const int vertex_id = (int16_t)swap16 ((uint16_t)oNode->vertexId);
			const int normal_id = (int16_t)swap16 ((uint16_t)oNode->normalId);
			load_vec3_array (data, size,
				get_group_resource (data, size, positions, vertex_id, 0x40), 0x40, &mesh->positions,
				&mesh->num_positions);
			load_vec3_array (data, size, get_group_resource (data, size, normals, normal_id, 0x20),
				0x20, &mesh->normals, &mesh->num_normals);
			for (unsigned c = 0; c < 2; c++)
			{
				const int id = (int16_t)swap16 ((uint16_t)oNode->colorIds[c]);
				load_color_array (data, size, get_group_resource (data, size, colors, id, 0x20),
					&mesh->colors[c], &mesh->num_colors[c]);
			}
			for (unsigned t = 0; t < 8; t++)
			{
				const int id = (int16_t)swap16 ((uint16_t)oNode->uvIds[t]);
				if (!t)
					load_vec2_array (data, size, get_group_resource (data, size, uvs, id, 0x20),
						&mesh->texcoords, &mesh->num_texcoords);
				else
					load_vec2_array (data, size, get_group_resource (data, size, uvs, id, 0x20),
						&mesh->extra_texcoords[t - 1], &mesh->num_extra_texcoords[t - 1]);
			}
			if (!mesh->num_positions
				|| !decode_gx_primitives (primData, primSize, cpLo, cpHi, mesh))
			{
				free (mesh->positions);
				mesh->positions = NULL;
				mesh->num_positions = 0;
				free (mesh->normals);
				mesh->normals = NULL;
				mesh->num_normals = 0;
				free (mesh->tangents);
				mesh->tangents = NULL;
				mesh->num_tangents = 0;
				free (mesh->texcoords);
				mesh->texcoords = NULL;
				mesh->num_texcoords = 0;
				for (unsigned c = 0; c < 2; c++)
				{
					free (mesh->colors[c]);
					mesh->colors[c] = NULL;
					mesh->num_colors[c] = 0;
				}
				for (unsigned t = 0; t < 7; t++)
				{
					free (mesh->extra_texcoords[t]);
					mesh->extra_texcoords[t] = NULL;
					mesh->num_extra_texcoords[t] = 0;
				}
				free (mesh->vertices);
				mesh->vertices = NULL;
				mesh->num_vertices = 0;
				continue;
			}

			// Reject corrupt indices at the parser boundary. The previous
			// exporter wrote them into COLLADA and left importers to fail.
			for (size_t p = 0; p < mesh->num_vertices; p++)
				if ((size_t)mesh->vertices[p].position_idx >= mesh->num_positions
					|| mesh->num_normals
						&& (size_t)mesh->vertices[p].normal_idx >= mesh->num_normals
					|| mesh->num_texcoords
						&& (size_t)mesh->vertices[p].texcoord_idx >= mesh->num_texcoords
					|| mesh->num_colors[0]
						&& (size_t)mesh->vertices[p].color_idx[0] >= mesh->num_colors[0]
					|| mesh->num_colors[1]
						&& (size_t)mesh->vertices[p].color_idx[1] >= mesh->num_colors[1]
					|| mesh->num_extra_texcoords[0]
						&& (size_t)mesh->vertices[p].extra_texcoord_idx[0]
							>= mesh->num_extra_texcoords[0]
					|| mesh->num_extra_texcoords[1]
						&& (size_t)mesh->vertices[p].extra_texcoord_idx[1]
							>= mesh->num_extra_texcoords[1]
					|| mesh->num_extra_texcoords[2]
						&& (size_t)mesh->vertices[p].extra_texcoord_idx[2]
							>= mesh->num_extra_texcoords[2]
					|| mesh->num_extra_texcoords[3]
						&& (size_t)mesh->vertices[p].extra_texcoord_idx[3]
							>= mesh->num_extra_texcoords[3]
					|| mesh->num_extra_texcoords[4]
						&& (size_t)mesh->vertices[p].extra_texcoord_idx[4]
							>= mesh->num_extra_texcoords[4]
					|| mesh->num_extra_texcoords[5]
						&& (size_t)mesh->vertices[p].extra_texcoord_idx[5]
							>= mesh->num_extra_texcoords[5]
					|| mesh->num_extra_texcoords[6]
						&& (size_t)mesh->vertices[p].extra_texcoord_idx[6]
							>= mesh->num_extra_texcoords[6])
				{
					free (mesh->vertices);
					mesh->vertices = NULL;
					mesh->num_vertices = 0;
					break;
				}
			if (!mesh->num_vertices)
			{
				free (mesh->positions);
				mesh->positions = NULL;
				mesh->num_positions = 0;
				free (mesh->normals);
				mesh->normals = NULL;
				mesh->num_normals = 0;
				free (mesh->texcoords);
				mesh->texcoords = NULL;
				mesh->num_texcoords = 0;
				for (unsigned c = 0; c < 2; c++)
				{
					free (mesh->colors[c]);
					mesh->colors[c] = NULL;
					mesh->num_colors[c] = 0;
				}
				for (unsigned t = 0; t < 7; t++)
				{
					free (mesh->extra_texcoords[t]);
					mesh->extra_texcoords[t] = NULL;
					mesh->num_extra_texcoords[t] = 0;
				}
				continue;
			}

			if (!apply_bind_pose_transforms (
					mesh, object_node, node_transforms, num_node_transforms))
			{
				free (mesh->position_node);
				mesh->position_node = NULL;
				free (mesh->positions);
				mesh->positions = NULL;
				mesh->num_positions = 0;
				free (mesh->normals);
				mesh->normals = NULL;
				mesh->num_normals = 0;
				free (mesh->tangents);
				mesh->tangents = NULL;
				mesh->num_tangents = 0;
				free (mesh->texcoords);
				mesh->texcoords = NULL;
				mesh->num_texcoords = 0;
				for (unsigned c = 0; c < 2; c++)
				{
					free (mesh->colors[c]);
					mesh->colors[c] = NULL;
					mesh->num_colors[c] = 0;
				}
				for (unsigned t = 0; t < 7; t++)
				{
					free (mesh->extra_texcoords[t]);
					mesh->extra_texcoords[t] = NULL;
					mesh->num_extra_texcoords[t] = 0;
				}
				free (mesh->vertices);
				mesh->vertices = NULL;
				mesh->num_vertices = 0;
				continue;
			}

			mesh->material_idx = object_material[i - 1];
			if (mesh->material_idx < 0 && model->num_materials == 1)
				mesh->material_idx = 0;
			// In NBT mode tangents share the same index space as normals.
			if (mesh->num_tangents)
				for (size_t p = 0; p < mesh->num_vertices; p++)
					mesh->vertices[p].tangent_idx = mesh->vertices[p].normal_idx;
			// Keep the object's real name (polygon0, mune_M, ...). BrawlCrate
			// uses it for the geometry/controller/node ids, so a DAE that
			// renames every mesh to mesh_N cannot be matched back to the MDL0.
			uint32_t mesh_name_len;
			const char *mesh_name = read_pooled_string (data, size,
				(const uint8_t *)&oNode->stringOffset, (const uint8_t *)oNode, &mesh_name_len);
			if (!mesh_name)
				mesh_name
					= read_pooled_string (data, size, (const uint8_t *)&entries[i].stringOffset,
						(const uint8_t *)grp, &mesh_name_len);
			if (mesh_name)
				copy_pooled_string (mesh->name, sizeof (mesh->name), mesh_name, mesh_name_len);
			else
				snprintf (mesh->name, sizeof (mesh->name), "mesh_%zu", model->num_meshes);
			model->num_meshes++;
		}
		free (object_material);
	}
skip_meshes:

	// Publish the matrix-node -> bone weight table the meshes were bound
	// with, so the exporter can emit a skin controller. Bone node-ids are
	// translated to joint indices here; ids without a joint are dropped.
	if (num_node_transforms && model->num_joints)
	{
		int *node_to_joint = malloc (num_node_transforms * sizeof (*node_to_joint));
		node_influence_t *table = calloc (num_node_transforms, sizeof (*table));
		if (node_to_joint && table)
		{
			for (size_t n = 0; n < num_node_transforms; n++)
				node_to_joint[n] = -1;
			{
				ResourceGroup *bone_group = get_mdl_group (data, size, version, MDL_BONES);
				ResourceEntry *bone_entries = bone_group ? (ResourceEntry *)(bone_group + 1) : NULL;
				for (size_t j = 0; bone_entries && j < model->num_joints; j++)
				{
					const int32_t off = (int32_t)swap32 (bone_entries[j + 1].dataOffset);
					const MDL0Bone *bone = (const MDL0Bone *)((const uint8_t *)bone_group + off);
					if (off < 0 || !in_bounds (data, size, bone, sizeof (*bone)))
						continue;
					const int32_t id = (int32_t)swap32 ((uint32_t)bone->nodeId);
					if (id >= 0 && (size_t)id < num_node_transforms)
						node_to_joint[id] = (int)j;
				}
			}
			for (size_t n = 0; n < num_node_transforms; n++)
			{
				const node_transform_t *src = node_transforms + n;
				if (!src->num_weights)
					continue;
				influence_t *w = calloc (src->num_weights, sizeof (*w));
				if (!w)
					continue;
				size_t used = 0;
				for (size_t k = 0; k < src->num_weights; k++)
				{
					const unsigned bone_node = src->bones[k];
					if (bone_node >= num_node_transforms || node_to_joint[bone_node] < 0)
						continue;
					w[used].bone_idx = node_to_joint[bone_node];
					w[used++].weight = src->weights[k];
				}
				if (used)
				{
					table[n].weights = w;
					table[n].num_weights = used;
				}
				else
					free (w);
			}
			model->node_influences = table;
			model->num_node_influences = num_node_transforms;
			table = NULL;
		}
		free (node_to_joint);
		free (table);
	}

	// A handful of retail MDL0 resources are intentionally bone-only scene
	// placeholders. BrawlCrate still exports their joint scene as COLLADA;
	// returning NULL here made XX silently omit those DAEs altogether.
	if (!model->num_meshes && !model->num_joints)
	{
		free_node_transforms (node_transforms, num_node_transforms);
		FreeModel (model);
		return NULL;
	}
	free_node_transforms (node_transforms, num_node_transforms);
	return model;
}

void FreeModel (model_t *model)
{
	if (!model)
		return;
	if (model->joints)
		free (model->joints);

	if (model->meshes)
	{
		for (size_t i = 0; i < model->num_meshes; i++)
		{
			if (model->meshes[i].positions)
				free (model->meshes[i].positions);
			if (model->meshes[i].normals)
				free (model->meshes[i].normals);
			if (model->meshes[i].tangents)
				free (model->meshes[i].tangents);
			if (model->meshes[i].texcoords)
				free (model->meshes[i].texcoords);
			for (unsigned c = 0; c < 2; c++)
				free (model->meshes[i].colors[c]);
			for (unsigned t = 0; t < 7; t++)
				free (model->meshes[i].extra_texcoords[t]);
			free (model->meshes[i].position_node);
			for (size_t t = 0; t < model->meshes[i].num_morph_targets; t++)
				free (model->meshes[i].morph_targets[t].position_deltas);
			free (model->meshes[i].morph_targets);
			free (model->meshes[i].morph_weights);
			if (model->meshes[i].vertices)
			{
				free (model->meshes[i].vertices);
			}
		}
		free (model->meshes);
	}
	if (model->materials)
		free (model->materials);
	free (model->instances);
	free (model->cameras);
	free (model->lights);
	for (size_t a = 0; a < model->num_animations; a++)
	{
		for (size_t c = 0; c < model->animations[a].num_channels; c++)
		{
			free (model->animations[a].channels[c].times);
			free (model->animations[a].channels[c].values);
		}
		free (model->animations[a].channels);
	}
	free (model->animations);
	for (size_t i = 0; i < model->num_node_influences; i++)
		free (model->node_influences[i].weights);
	free (model->node_influences);
	free (model);
}

// ---------------------------------------------------------------------
// CHR0 (bone/skeletal) animation.
//
// Oracle: BrawlCrate's BrawlLib (github.com/soopercool101/BrawlCrate),
// BrawlLib/SSBB/Types/Animations/CHR0.cs (header/entry layout),
// BrawlLib/Wii/Animations/{AnimationConverter,EncodingTypes}.cs (keyframe
// decode + the I4/I6/I12/L1/L2/L4 block formats) and
// BrawlLib/Wii/Models/AnimationCode.cs (the per-entry bitfield). CHR0's
// group/entry table is the same ResourceGroup/ResourceEntry format already
// used above for MDL0 bones, so the sentinel-at-index-0 iteration mirrors
// that code exactly.

typedef struct
{
	int frame;
	float value;
	float tangent;
} br_key_t;

static uint32_t be32p (const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint16_t be16p (const uint8_t *p)
{
	return (uint16_t)p[0] << 8 | p[1];
}
static float bef32p (const uint8_t *p)
{
	uint32_t u = be32p (p);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

// BrawlLib's KeyframeEntry.Interpolate() Hermite curve, evaluated at every
// integer frame in [0,numFrames) from a sparse (frame,value,tangent) key
// list. Non-looped edge clamp only -- looped wraparound isn't reproduced.
static void bake_hermite (float *dense, int numFrames, const br_key_t *keys, int numKeys)
{
	if (numKeys <= 0)
	{
		for (int f = 0; f < numFrames; f++)
			dense[f] = 0.0f;
		return;
	}
	for (int f = 0; f < numFrames; f++)
	{
		if (f <= keys[0].frame)
		{
			dense[f] = keys[0].value;
			continue;
		}
		if (f >= keys[numKeys - 1].frame)
		{
			dense[f] = keys[numKeys - 1].value;
			continue;
		}
		int k = 0;
		while (k + 1 < numKeys && keys[k + 1].frame <= f)
			k++;
		const br_key_t *a = &keys[k], *b = &keys[k + 1];
		const float span = (float)(b->frame - a->frame);
		const float offset = (float)(f - a->frame);
		if (offset <= 0 || span <= 0)
		{
			dense[f] = a->value;
			continue;
		}
		const float time = offset / span, diff = b->value - a->value, inv = time - 1.0f;
		dense[f] = a->value + offset * inv * (inv * a->tangent + time * b->tangent)
			+ time * time * (3.0f - 2.0f * time) * diff;
	}
}

// Decode one AnimDataFormat block (I4=1,I6=2,I12=3,L1=4,L2=5,L4=6) into a
// dense per-frame array. `entry` is the CHR0Entry's own start address --
// I4/I6/I12/L1/L2/L4 offsets are relative to the entry, not the file or the
// containing group (see AnimationConverter.DecodeCHR0Keyframes).
static int decode_anim_format (float *dense, int numFrames, const uint8_t *fbase, size_t fsize,
	const uint8_t *entry, uint32_t rel_offset, int format)
{
	const uint8_t *p = entry + rel_offset;
	if (numFrames <= 0 || !in_bounds (fbase, fsize, p, 8))
		return 0;
	switch (format)
	{
		case 6: // L4: straight float array, no header
			if (!in_bounds (fbase, fsize, p, (size_t)numFrames * 4))
				return 0;
			for (int f = 0; f < numFrames; f++)
				dense[f] = bef32p (p + f * 4);
			return 1;
		case 5:
		{ // L2: step/base header + u16 array
			const float step = bef32p (p), base = bef32p (p + 4);
			const uint8_t *d = p + 8;
			if (!in_bounds (fbase, fsize, d, (size_t)numFrames * 2))
				return 0;
			for (int f = 0; f < numFrames; f++)
				dense[f] = base + be16p (d + f * 2) * step;
			return 1;
		}
		case 4:
		{ // L1: step/base header + u8 array
			const float step = bef32p (p), base = bef32p (p + 4);
			const uint8_t *d = p + 8;
			if (!in_bounds (fbase, fsize, d, (size_t)numFrames))
				return 0;
			for (int f = 0; f < numFrames; f++)
				dense[f] = base + d[f] * step;
			return 1;
		}
		case 3:
		{ // I12: numFrames(u16)+pad(u16), then {index,value,tangent} floats
			const int fCount = be16p (p);
			const uint8_t *d = p + 8;
			if (fCount <= 0 || !in_bounds (fbase, fsize, d, (size_t)fCount * 12))
				return 0;
			br_key_t *keys = malloc (sizeof (br_key_t) * (size_t)fCount);
			if (!keys)
				return 0;
			for (int i = 0; i < fCount; i++)
			{
				keys[i].frame = (int)bef32p (d + i * 12);
				keys[i].value = bef32p (d + i * 12 + 4);
				keys[i].tangent = bef32p (d + i * 12 + 8);
			}
			bake_hermite (dense, numFrames, keys, fCount);
			free (keys);
			return 1;
		}
		case 2:
		{ // I6: numFrames(u16)+unk(u16)+frameScale(f32,unused)+step(f32)+base(f32), then 6-byte
		  // entries
			const int fCount = be16p (p);
			const float step = bef32p (p + 8), base = bef32p (p + 12);
			const uint8_t *d = p + 16;
			if (fCount <= 0 || !in_bounds (fbase, fsize, d, (size_t)fCount * 6))
				return 0;
			br_key_t *keys = malloc (sizeof (br_key_t) * (size_t)fCount);
			if (!keys)
				return 0;
			for (int i = 0; i < fCount; i++)
			{
				const uint16_t data = be16p (d + i * 6), rawstep = be16p (d + i * 6 + 2);
				const int16_t exp = (int16_t)be16p (d + i * 6 + 4);
				keys[i].frame = data >> 5;
				keys[i].value = base + rawstep * step;
				keys[i].tangent = exp / 256.0f;
			}
			bake_hermite (dense, numFrames, keys, fCount);
			free (keys);
			return 1;
		}
		case 1:
		{ // I4: entries(u16)+unk(u16)+frameScale(f32,unused)+step(f32)+base(f32), then packed
		  // 4-byte entries
			const int fCount = be16p (p);
			const float step = bef32p (p + 8), base = bef32p (p + 12);
			const uint8_t *d = p + 16;
			if (fCount <= 0 || !in_bounds (fbase, fsize, d, (size_t)fCount * 4))
				return 0;
			br_key_t *keys = malloc (sizeof (br_key_t) * (size_t)fCount);
			if (!keys)
				return 0;
			for (int i = 0; i < fCount; i++)
			{
				const uint32_t raw = be32p (d + i * 4);
				const int step12 = (raw >> 12) & 0xFFF;
				const int32_t tan12 = (int32_t)((raw & 0xFFF) << 20) >> 20;
				keys[i].frame = (int)((raw >> 24) & 0xFF);
				keys[i].value = base + step12 * step;
				keys[i].tangent = tan12 / 32.0f;
			}
			bake_hermite (dense, numFrames, keys, fCount);
			free (keys);
			return 1;
		}
	}
	return 0;
}

// One decoded CHR0 bone entry: up to 9 logical per-frame tracks
// (0-2 = scale XYZ, 3-5 = rotation XYZ degrees, 6-8 = translation XYZ).
// A NULL slot means that component wasn't animated (leave the joint at its
// MDL0 bind-pose value for that channel).
typedef struct
{
	float *dense[9];
} chr0_track_t;

static void free_chr0_track (chr0_track_t *t)
{
	for (int i = 0; i < 9; i++)
		free (t->dense[i]);
}

#define CHR0_ALLOCFILL(idx, val)                                                                   \
	do                                                                                             \
	{                                                                                              \
		track->dense[idx] = malloc (sizeof (float) * (size_t)numFrames);                           \
		if (track->dense[idx])                                                                     \
			for (int _f = 0; _f < numFrames; _f++)                                                 \
				track->dense[idx][_f] = (val);                                                     \
	} while (0)
#define CHR0_ALLOCTRACK(idx, off, fmt)                                                             \
	do                                                                                             \
	{                                                                                              \
		track->dense[idx] = malloc (sizeof (float) * (size_t)numFrames);                           \
		if (track->dense[idx]                                                                      \
			&& !decode_anim_format (                                                               \
				track->dense[idx], numFrames, fbase, fsize, entry, (off), (fmt)))                  \
		{                                                                                          \
			free (track->dense[idx]);                                                              \
			track->dense[idx] = NULL;                                                              \
		}                                                                                          \
	} while (0)

// Transliteration of AnimationConverter.DecodeCHR0Keyframes: walk the
// entry's data sequentially, either a fixed float or a 4-byte block offset
// per present, non-isotropic axis (or one shared value/offset when
// isotropic). `code` is the entry's already-endian-swapped 32-bit flags.
static void decode_chr0_entry (chr0_track_t *track, const uint8_t *fbase, size_t fsize,
	const uint8_t *entry, uint32_t code, int numFrames)
{
	memset (track, 0, sizeof (*track));
	const uint8_t *sp = entry + 8; // CHR0Entry::Data

	const int hasScale = (code >> 22) & 1, hasRot = (code >> 23) & 1, hasTrans = (code >> 24) & 1;
	const int scaleIso = (code >> 4) & 1, rotIso = (code >> 5) & 1, transIso = (code >> 6) & 1;
	const int sxFix = (code >> 13) & 1, syFix = (code >> 14) & 1, szFix = (code >> 15) & 1;
	const int rxFix = (code >> 16) & 1, ryFix = (code >> 17) & 1, rzFix = (code >> 18) & 1;
	const int txFix = (code >> 19) & 1, tyFix = (code >> 20) & 1, tzFix = (code >> 21) & 1;
	const int scaleFmt = (code >> 25) & 3, rotFmt = (code >> 27) & 7, transFmt = (code >> 30) & 3;

	if (hasScale)
	{
		if (scaleIso)
		{
			if (!in_bounds (fbase, fsize, sp, 4))
				return;
			if (szFix)
			{
				const float v = bef32p (sp);
				sp += 4;
				CHR0_ALLOCFILL (0, v);
				CHR0_ALLOCFILL (1, v);
				CHR0_ALLOCFILL (2, v);
			}
			else
			{
				const uint32_t off = be32p (sp);
				sp += 4;
				CHR0_ALLOCTRACK (0, off, scaleFmt);
				CHR0_ALLOCTRACK (1, off, scaleFmt);
				CHR0_ALLOCTRACK (2, off, scaleFmt);
			}
		}
		else
		{
			if (!in_bounds (fbase, fsize, sp, 4))
				return;
			if (sxFix)
			{
				const float v = bef32p (sp);
				sp += 4;
				CHR0_ALLOCFILL (0, v);
			}
			else
			{
				const uint32_t off = be32p (sp);
				sp += 4;
				CHR0_ALLOCTRACK (0, off, scaleFmt);
			}
			if (!in_bounds (fbase, fsize, sp, 4))
				return;
			if (syFix)
			{
				const float v = bef32p (sp);
				sp += 4;
				CHR0_ALLOCFILL (1, v);
			}
			else
			{
				const uint32_t off = be32p (sp);
				sp += 4;
				CHR0_ALLOCTRACK (1, off, scaleFmt);
			}
			if (!in_bounds (fbase, fsize, sp, 4))
				return;
			if (szFix)
			{
				const float v = bef32p (sp);
				sp += 4;
				CHR0_ALLOCFILL (2, v);
			}
			else
			{
				const uint32_t off = be32p (sp);
				sp += 4;
				CHR0_ALLOCTRACK (2, off, scaleFmt);
			}
		}
	}

	if (hasRot)
	{
		if (rotIso)
		{
			if (!in_bounds (fbase, fsize, sp, 4))
				return;
			if (rzFix)
			{
				const float v = bef32p (sp);
				sp += 4;
				CHR0_ALLOCFILL (3, v);
				CHR0_ALLOCFILL (4, v);
				CHR0_ALLOCFILL (5, v);
			}
			else
			{
				const uint32_t off = be32p (sp);
				sp += 4;
				CHR0_ALLOCTRACK (3, off, rotFmt);
				CHR0_ALLOCTRACK (4, off, rotFmt);
				CHR0_ALLOCTRACK (5, off, rotFmt);
			}
		}
		else
		{
			if (!in_bounds (fbase, fsize, sp, 4))
				return;
			if (rxFix)
			{
				const float v = bef32p (sp);
				sp += 4;
				CHR0_ALLOCFILL (3, v);
			}
			else
			{
				const uint32_t off = be32p (sp);
				sp += 4;
				CHR0_ALLOCTRACK (3, off, rotFmt);
			}
			if (!in_bounds (fbase, fsize, sp, 4))
				return;
			if (ryFix)
			{
				const float v = bef32p (sp);
				sp += 4;
				CHR0_ALLOCFILL (4, v);
			}
			else
			{
				const uint32_t off = be32p (sp);
				sp += 4;
				CHR0_ALLOCTRACK (4, off, rotFmt);
			}
			if (!in_bounds (fbase, fsize, sp, 4))
				return;
			if (rzFix)
			{
				const float v = bef32p (sp);
				sp += 4;
				CHR0_ALLOCFILL (5, v);
			}
			else
			{
				const uint32_t off = be32p (sp);
				sp += 4;
				CHR0_ALLOCTRACK (5, off, rotFmt);
			}
		}
	}

	if (hasTrans)
	{
		if (transIso)
		{
			if (!in_bounds (fbase, fsize, sp, 4))
				return;
			if (tzFix)
			{
				const float v = bef32p (sp);
				sp += 4;
				CHR0_ALLOCFILL (6, v);
				CHR0_ALLOCFILL (7, v);
				CHR0_ALLOCFILL (8, v);
			}
			else
			{
				const uint32_t off = be32p (sp);
				sp += 4;
				CHR0_ALLOCTRACK (6, off, transFmt);
				CHR0_ALLOCTRACK (7, off, transFmt);
				CHR0_ALLOCTRACK (8, off, transFmt);
			}
		}
		else
		{
			if (!in_bounds (fbase, fsize, sp, 4))
				return;
			if (txFix)
			{
				const float v = bef32p (sp);
				sp += 4;
				CHR0_ALLOCFILL (6, v);
			}
			else
			{
				const uint32_t off = be32p (sp);
				sp += 4;
				CHR0_ALLOCTRACK (6, off, transFmt);
			}
			if (!in_bounds (fbase, fsize, sp, 4))
				return;
			if (tyFix)
			{
				const float v = bef32p (sp);
				sp += 4;
				CHR0_ALLOCFILL (7, v);
			}
			else
			{
				const uint32_t off = be32p (sp);
				sp += 4;
				CHR0_ALLOCTRACK (7, off, transFmt);
			}
			if (!in_bounds (fbase, fsize, sp, 4))
				return;
			if (tzFix)
			{
				const float v = bef32p (sp);
				sp += 4;
				CHR0_ALLOCFILL (8, v);
			}
			else
			{
				const uint32_t off = be32p (sp);
				sp += 4;
				CHR0_ALLOCTRACK (8, off, transFmt);
			}
		}
	}
}
#undef CHR0_ALLOCFILL
#undef CHR0_ALLOCTRACK

static int find_joint_by_name (const model_t *model, const char *name)
{
	for (size_t i = 0; i < model->num_joints; i++)
		if (!strcmp (model->joints[i].name, name))
			return (int)i;
	return -1;
}

// Half-angle XYZ Euler (degrees) -> quaternion, matching the exact formula
// ExportModelToGLB() already uses for a joint's static bind-pose rotation
// (lib-model-dae.c, write_joint_node's inline node emission) so animated
// and bind-pose rotations use one convention.
static void chr0_euler_to_quat (float rx, float ry, float rz, float q[4])
{
	const double hx = rx * M_PI / 360.0, hy = ry * M_PI / 360.0, hz = rz * M_PI / 360.0;
	const double cx = cos (hx), sx = sin (hx), cy = cos (hy), sy = sin (hy), cz = cos (hz),
				 sz = sin (hz);
	q[0] = (float)(sx * cy * cz - cx * sy * sz);
	q[1] = (float)(cx * sy * cz + sx * cy * sz);
	q[2] = (float)(cx * cy * sz - sx * sy * cz);
	q[3] = (float)(cx * cy * cz + sx * sy * sz);
}

static void add_anim_channel (model_animation_t *anim, int node_idx, model_anim_path_t path,
	float *times, float *values, size_t count, size_t components)
{
	model_anim_channel_t *ch = realloc (anim->channels, sizeof (*ch) * (anim->num_channels + 1));
	if (!ch)
	{
		free (times);
		free (values);
		return;
	}
	anim->channels = ch;
	model_anim_channel_t *c = &anim->channels[anim->num_channels++];
	c->node_idx = node_idx;
	c->path = path;
	c->times = times;
	c->values = values;
	c->count = count;
	c->components = components;
}

// Parse a CHR0 sub-file's raw bytes (as extracted to .../AnmChr(NW4R)/*.chr0)
// and append one model_animation_t of joint TRS channels, matching each
// CHR0 bone entry by name against model->joints[]. Bones the CHR0 doesn't
// reference, or channels a bone entry doesn't animate, are simply left at
// their MDL0 bind-pose value (glTF leaves untargeted TRS components alone).
int ParseCHR0IntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name)
{
	if (!model || !data || size < 0x28 || strncmp ((const char *)data, "CHR0", 4))
		return 0;
	const uint32_t version = be32p (data + 8);
	if (version < 3 || version > 5)
		return 0;

	const uint32_t dataOffset = be32p (data + 0x10);
	if (!in_bounds (data, size, data + dataOffset, 8))
		return 0;
	const uint8_t *group = data + dataOffset;
	const uint32_t numEntries = be32p (group + 4);
	if (!in_bounds (data, size, group + 8, (size_t)(numEntries + 1) * 16))
		return 0;

	const int numFramesHdr = version == 5 ? be16p (data + 0x20) : be16p (data + 0x1C);
	const int loop = version == 5 ? (be32p (data + 0x24) != 0) : (be32p (data + 0x20) != 0);
	const int numFrames = numFramesHdr + (loop ? 1 : 0);
	if (numFrames <= 0 || numFrames > 100000)
		return 0;

	model_animation_t anim = { 0 };
	copy_pooled_string (anim.name, sizeof (anim.name), clip_name ? clip_name : "chr0",
		clip_name ? (uint32_t)strlen (clip_name) : 4);

	// Times shared by every channel in this clip -- one allocation, many owners
	// would double-free, so give every channel its own copy.
	const float fps = 60.0f;
	int any = 0;
	for (uint32_t i = 1; i <= numEntries; i++)
	{
		const uint8_t *entryRec = group + 8 + (size_t)i * 16;
		const int32_t entryOff = (int32_t)be32p (entryRec + 12);
		const uint8_t *entry = group + entryOff;
		if (entryOff < 0 || !in_bounds (data, size, entry, 8))
			continue;

		uint32_t name_len;
		const char *name = read_pooled_string (data, size, entryRec + 8, group, &name_len);
		char namebuf[64] = { 0 };
		if (name)
			copy_pooled_string (namebuf, sizeof (namebuf), name, name_len);
		const int joint_idx = namebuf[0] ? find_joint_by_name (model, namebuf) : -1;
		if (joint_idx < 0)
			continue;

		const uint32_t code = be32p (entry + 4);
		chr0_track_t track;
		decode_chr0_entry (&track, data, size, entry, code, numFrames);

		if (track.dense[0] || track.dense[1] || track.dense[2])
		{
			float *times = malloc (sizeof (float) * numFrames);
			float *values = malloc (sizeof (float) * numFrames * 3);
			if (times && values)
			{
				for (int f = 0; f < numFrames; f++)
				{
					times[f] = f / fps;
					values[f * 3 + 0]
						= track.dense[0] ? track.dense[0][f] : model->joints[joint_idx].scale.x;
					values[f * 3 + 1]
						= track.dense[1] ? track.dense[1][f] : model->joints[joint_idx].scale.y;
					values[f * 3 + 2]
						= track.dense[2] ? track.dense[2][f] : model->joints[joint_idx].scale.z;
				}
				add_anim_channel (&anim, joint_idx, MODEL_ANIM_SCALE, times, values, numFrames, 3);
				any = 1;
			}
			else
			{
				free (times);
				free (values);
			}
		}
		if (track.dense[3] || track.dense[4] || track.dense[5])
		{
			float *times = malloc (sizeof (float) * numFrames);
			float *values = malloc (sizeof (float) * numFrames * 4);
			if (times && values)
			{
				for (int f = 0; f < numFrames; f++)
				{
					times[f] = f / fps;
					const float rx
						= track.dense[3] ? track.dense[3][f] : model->joints[joint_idx].rotate.x;
					const float ry
						= track.dense[4] ? track.dense[4][f] : model->joints[joint_idx].rotate.y;
					const float rz
						= track.dense[5] ? track.dense[5][f] : model->joints[joint_idx].rotate.z;
					chr0_euler_to_quat (rx, ry, rz, &values[f * 4]);
				}
				add_anim_channel (
					&anim, joint_idx, MODEL_ANIM_ROTATION, times, values, numFrames, 4);
				any = 1;
			}
			else
			{
				free (times);
				free (values);
			}
		}
		if (track.dense[6] || track.dense[7] || track.dense[8])
		{
			float *times = malloc (sizeof (float) * numFrames);
			float *values = malloc (sizeof (float) * numFrames * 3);
			if (times && values)
			{
				for (int f = 0; f < numFrames; f++)
				{
					times[f] = f / fps;
					values[f * 3 + 0]
						= track.dense[6] ? track.dense[6][f] : model->joints[joint_idx].translate.x;
					values[f * 3 + 1]
						= track.dense[7] ? track.dense[7][f] : model->joints[joint_idx].translate.y;
					values[f * 3 + 2]
						= track.dense[8] ? track.dense[8][f] : model->joints[joint_idx].translate.z;
				}
				add_anim_channel (
					&anim, joint_idx, MODEL_ANIM_TRANSLATION, times, values, numFrames, 3);
				any = 1;
			}
			else
			{
				free (times);
				free (values);
			}
		}
		free_chr0_track (&track);
	}

	if (!any)
	{
		free (anim.channels);
		return 0;
	}

	model_animation_t *na = realloc (model->animations, sizeof (*na) * (model->num_animations + 1));
	if (!na)
	{
		for (size_t c = 0; c < anim.num_channels; c++)
		{
			free (anim.channels[c].times);
			free (anim.channels[c].values);
		}
		free (anim.channels);
		return 0;
	}
	model->animations = na;
	model->animations[model->num_animations++] = anim;
	return 1;
}

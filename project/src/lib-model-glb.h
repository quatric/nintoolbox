#ifndef LIB_MODEL_GLB_H
#define LIB_MODEL_GLB_H

#include <stdint.h>
#include <stddef.h>

typedef struct
{
	float x, y, z;
} vec3_t;

typedef struct
{
	float u, v;
} vec2_t;

typedef struct
{
	float r, g, b, a;
} color4_t;

typedef struct
{
	int position_idx;
	int normal_idx;
	int tangent_idx; // -1 when tangent is unavailable
	int texcoord_idx;
	int matrix_idx;
	int color_idx[2];
	int extra_texcoord_idx[7];
} vertex_t;

typedef struct
{
	char name[64];
	uint8_t source_kind; // 0=generic, 1=HSF shape, 2=HSF cluster
	// Position deltas indexed like mesh_t::positions. glTF expands these to
	// the primitive's unified vertex stream through vertex.position_idx.
	vec3_t *position_deltas;
	size_t num_positions;
} morph_target_t;

typedef struct
{
	int bone_idx; // index into model_t::joints
	float weight;
} influence_t;

// Skin influences for one GX matrix-node id. A single-bone node carries one
// entry with weight 1; a NodeMix influence carries its full weight list.
typedef struct
{
	influence_t *weights;
	size_t num_weights;
} node_influence_t;

typedef struct
{
	char name[64];

	vec3_t *positions;
	size_t num_positions;

	vec3_t *normals;
	size_t num_normals;

	vec3_t *tangents;
	size_t num_tangents;

	vec2_t *texcoords;
	size_t num_texcoords;

	color4_t *colors[2];
	size_t num_colors[2];

	vec2_t *extra_texcoords[7];
	size_t num_extra_texcoords[7];

	// For each entry of `positions`, the GX matrix-node id its source vertex
	// was bound to (-1 when the mesh is unbound). Deduplication in the bind
	// pass is keyed on (node,index), so this is well defined per position and
	// is what the COLLADA skin controller weights each vertex with.
	int *position_node;

	vertex_t *vertices;
	size_t num_vertices;
	int *triangle_materials; // optional, one entry per num_vertices/3

	morph_target_t *morph_targets;
	size_t num_morph_targets;
	float *morph_weights;

	int material_idx;
} mesh_t;

typedef struct
{
	char name[64];
	int parent_idx;
	vec3_t translate;
	vec3_t rotate;
	vec3_t scale;
	// MDL0 stores each bone's absolute bind matrix and its inverse as 3x4
	// row-major affine matrices. COLLADA skinning needs the inverse; the
	// forward matrix is authoritative for bones whose scale/rotation cannot
	// be expressed by a plain inherited TRS chain (segment scale compensate).
	float bind[12];
	float inverse_bind[12];
	uint8_t has_inverse_bind;
} joint_t;

typedef struct
{
	char name[64];
	char textures[8][64]; // texture layer names, in declaration order
	int texture_coord[8]; // GX texgen UV source for each texture layer
	// GX sampler state per layer, straight from MDL0TextureRef:
	// wrap 0=clamp 1=repeat 2=mirror, filter 0=nearest 1=linear.
	uint8_t wrap_s[8], wrap_t[8];
	uint8_t min_filter[8], mag_filter[8];
	int num_textures;
	// Texture transforms per layer (HSD TOBJ SRT / KHR_texture_transform).
	// rotate is in radians; scale and translate are per-U/V.
	// has_tex_transform is nonzero when any transform deviates from identity.
	float tex_rotate[8];
	float tex_scale_s[8], tex_scale_t[8];
	float tex_translate_s[8], tex_translate_t[8];
	uint8_t has_tex_transform[8];
	// Diffuse material colour (linear RGBA). Set by HSD via HSD_Material,
	// HSF via HsfMaterial_s.color[3], or MDL0. GLB exporter falls back to
	// [0.8,0.8,0.8,1.0] when all four components are zero.
	float diffuse[4];
	float specular[3];
	float ambient[3];
	float shininess;
	uint8_t has_alpha;
} material_t;

// An additional scene node which reuses an existing mesh.  HSF replica
// objects map directly to this instead of duplicating geometry.
typedef struct
{
	char name[64];
	int mesh_idx;
	int parent_idx; // joint index, or -1 for a scene root
	vec3_t translate;
	vec3_t rotate;
	vec3_t scale;
	float matrix[16]; // optional column-major local matrix
	uint8_t has_matrix;
} model_instance_t;

typedef struct
{
	char name[64];
	float matrix[16];
	float yfov; // radians
	float znear, zfar;
} model_camera_t;

typedef enum
{
	MODEL_LIGHT_DIRECTIONAL,
	MODEL_LIGHT_POINT,
	MODEL_LIGHT_SPOT
} model_light_kind_t;
typedef struct
{
	char name[64];
	float matrix[16];
	model_light_kind_t kind;
	float color[3];
	float intensity;
	float range;
	float inner_cone, outer_cone; // radians, spot lights only
} model_light_t;

typedef enum
{
	MODEL_ANIM_TRANSLATION,
	MODEL_ANIM_ROTATION,
	MODEL_ANIM_SCALE,
	MODEL_ANIM_WEIGHTS
} model_anim_path_t;
typedef struct
{
	int node_idx; // glTF node index
	model_anim_path_t path;
	float *times;
	float *values;
	size_t count;
	size_t components;
} model_anim_channel_t;
typedef struct
{
	char name[64];
	model_anim_channel_t *channels;
	size_t num_channels;
} model_animation_t;

// A texture carried inside the model file, byte for byte as it was embedded.
// The GLB exporter copies each material's staged PNG into the glTF binary
// chunk, so a model edited elsewhere comes back with its textures attached;
// keeping the bytes lets an encoder compare them against what the parent
// archive already holds and rewrite only the ones that actually changed.
typedef struct
{
	char name[64]; // matches material_t::textures[] entries
	uint8_t *data;
	size_t size;
} model_image_t;

typedef struct
{
	mesh_t *meshes;
	size_t num_meshes;

	joint_t *joints;
	size_t num_joints;

	material_t *materials;
	size_t num_materials;

	model_instance_t *instances;
	size_t num_instances;

	model_camera_t *cameras;
	size_t num_cameras;
	model_light_t *lights;
	size_t num_lights;

	model_animation_t *animations;
	size_t num_animations;

	// Indexed by GX matrix-node id; empty entries mean "not a skinned node".
	node_influence_t *node_influences;
	size_t num_node_influences;

	// Textures embedded in the source file, if it carried any.
	model_image_t *images;
	size_t num_images;
} model_t;

#ifdef __cplusplus
extern "C"
{
#endif

	int ExportModelToGLB (const model_t *model, const char *out_glb_file);

	// Parse a COLLADA DAE (.dae) or GLB (.glb) file or in-memory data into a model_t.
	model_t *ParseGLB (const uint8_t *data, size_t size);
	model_t *ParseGLBFile (const char *filename);
	void FreeModel (model_t *model);

	// Populate bind/inverse-bind matrices from the joint parent tree and local
	// TRS fields. Returns false for a cyclic hierarchy or singular transform.
	int ComputeModelTRSBinds (model_t *model);

	// Configure an optional tree-wide PNG lookup used by recursive archive
	// extraction. Pass NULL to release the index and restore standalone lookup.
	void SetDAETextureSearchRoot (const char *root);

#ifdef __cplusplus
}
#endif

#endif // LIB_MODEL_GLB_H

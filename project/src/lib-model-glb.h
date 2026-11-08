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

// Raw NSB* animation resources preserved byte-for-byte from their source
// file. The DS's NSBCA/BTA0/BTP0/BVA0/BMA0 parse into model channels but
// BCA0 is the only one with a glTF representation; keeping the original
// bytes lets the EncodeNSB* encoders reproduce an unchanged animation
// byte-exactly (pass-through) instead of re-quantizing it.
typedef enum
{
	NSB_RAW_BCA0,
	NSB_RAW_BTA0,
	NSB_RAW_BTP0,
	NSB_RAW_BVA0,
	NSB_RAW_BMA0,
	NSB_RAW_BCK0
} model_nsb_kind_t;

typedef struct
{
	model_nsb_kind_t kind;
	char name[64]; // clip name from the source path
	uint8_t *data;
	size_t size;
} model_nsb_raw_t;

// BRRES MDL0 metadata BrawlCrate surfaces on a decoded model (header
// properties, bone flags/extents, material render state and UserData). It is
// kept apart from joint_t/material_t and indexed the same way as
// model_t::joints / ::materials; model_t::mdl0_meta is NULL for other formats.
typedef struct
{
	char name[64];
	uint8_t type; // 0=int, 1=float, 2=string (BrawlCrate UserValueType)
	size_t count;
	double *numbers; // int/float values
	char *text;      // string value
} user_data_entry_t;

typedef struct
{
	user_data_entry_t *entries;
	size_t num_entries;
} user_data_t;

typedef struct
{
	int32_t node_id;
	uint32_t flags;    // BrawlCrate BoneFlags
	uint32_t bb_flags; // BillboardFlags: 0 off .. 6 YPerspective
	uint32_t bb_index; // billboard reference bone
	float extents[6];  // min xyz, max xyz
	user_data_t user_data;
} mdl0_joint_meta_t;

typedef struct
{
	uint32_t usage_flags; // MatUsageFlags (bit 31 = XLU)
	uint8_t num_tex_gens, num_light_chans, num_tev_stages, num_ind_tex_stages;
	int32_t cull; // 0 none, 1 outside, 2 inside, 3 all
	uint8_t depth_test_before_texture;
	int8_t light_set, fog_set;
	uint8_t has_mode_block;
	// GX pixel state from the material's mode block.
	uint8_t alpha_comp0, alpha_comp1, alpha_logic, alpha_ref0, alpha_ref1;
	uint8_t z_enable, z_func, z_update;
	uint16_t blend_raw; // BP blend mode register (enable, logic, factors, ...)
	uint8_t const_alpha_enable, const_alpha;
	user_data_t user_data;
} mdl0_material_meta_t;

typedef struct
{
	uint32_t version;
	int32_t scaling_rule;    // 0 standard, 1 SoftImage, 2 Maya
	int32_t tex_matrix_mode; // 0 Maya, 1 XSI, 2 3ds Max
	int32_t num_vertices, num_triangles, num_nodes;
	uint8_t need_nrm_mtx_array, need_tex_mtx_array, enable_extents;
	uint8_t env_mtx_mode; // 0 normal, 1 approximate, 2 exact
	float extents[6];
	char orig_path[256];
	int has_props;
	user_data_t user_data;
	mdl0_joint_meta_t *joints;
	size_t num_joints;
	mdl0_material_meta_t *materials;
	size_t num_materials;
} mdl0_meta_t;

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

	// NSB* animation resources preserved from the source file.
	model_nsb_raw_t *nsb_raw;
	size_t num_nsb_raw;

	// Original CGFX/BCRES container retained while it passes through GLB.
	// This is deliberately separate from model geometry: a retail CGFX has
	// resources which the generic model representation does not describe.
	uint8_t *bcres_raw;
	size_t bcres_raw_size;

	// BRRES MDL0 metadata (see mdl0_meta_t); NULL for other formats.
	mdl0_meta_t *mdl0_meta;
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
	void FreeMDL0Meta (mdl0_meta_t *meta);

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

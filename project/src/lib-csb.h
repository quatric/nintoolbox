// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Paper Mario collision scene (.csb) + collision search table (.ctb).
//
// Covers Paper Mario: The Thousand-Year Door (Switch, little-endian),
// Paper Mario: The Origami King (Switch, little-endian) and Paper Mario:
// Color Splash (Wii U, big-endian).
//
// Re-implemented in C from KillzXGaming/CollisionSceneBinary (MIT, 2024):
// https://github.com/KillzXGaming/CollisionSceneBinary
// attribute definitions, layout and the XZ-quadtree generator below follow
// CsbFile.cs / CtbFile.cs / CsbImporter.cs / CsbExporter.cs /
// OctreeGenerator.cs / TriangleHelper.cs. No upstream code is copied.
//
// Layout (both files have NO magic; endianness is detected structurally):
//
// .csb: u32 n_spheres + n_spheres * (f32 unk + vec3 p1 + vec3 p2 + f32 radius)
//       u32 n_boxes + n_boxes * (f32 unk + vec3 p1 + vec3 p2 + vec3 size
//         + vec3 rotation + f32[9] box_extra)
//       u32 0 + u32 1 + u8[16] 0
//       u32 name_off[n_obj] + colflag[n_obj] (u32 BE / u64 LE)
//         + u16 node[n_obj] + u32 obj_strtab_len + bytes
//       u16 unk3 (1 = Color Splash, 2 = else) + u16 n_models + u32 n_meshes
//       u32 mesh_name_off[n] + u32 tri_start[n] + u32 vtx_start[n]
//         + colflag[n] (u32 BE / u64 LE) + u32 mat_attr[n] + u32 model_id[n]
//         + u16 mesh_node[n] + u32 mesh_strtab_len + bytes
//       u16 n_nodes + n_nodes * (u16 id + u8 flags + u8 n_children)
//       per model (first = combined "DEADBEEF" buffer):
//         u32 unk0(=3) + u32 0 + [LE: u64 0] + u32 0 + u32 0 + char[64] name
//         + u32 unk5(=1) + u32 n_vtx + u32 n_tri + vec3 zero/translate/rotation
//         bbox + vec3 positions[n_vtx] + (u32 a,b,c + vec3 normal)[n_tri]
//       u32 0 + u8 0xFE + u8 0x07 + u16 0
//       u32 n_split + bbox sub_bbox + n_split split models (per-mesh buffers
//         with u16 node + u16 0 + colflag + mat_attr + [LE: u32 0] + ... same)
// .ctb: u32 0,0,0 + u32 1 + f32 root_size + f32 1 + vec3 root_pos
//       u32 n_nodes + u32 n_root_tris
//       per node: vec3 pos + f32 size + u32 node_id + u8 child_bits
//         + u8 root_flag + u16 pad + u32 n_tris
//       per node: u32 tri_index[n_tris] (into Models[0].Triangles)
// The .ctb "octree" is really an XZ-quadtree (4 children, Y ignored with an
// infinite-height overlap test); generation mirrors OctreeGenerator with
// root_scale = max(dx,dz)*0.68, root at ((min+max)/2.x, 0, (min+max)/2.z),
// max 10 triangles per node and max depth 6.
//
// wmdlt converts .csb to/from GLB through model_t: one mesh per CSB
// mesh/split-model with materials named "MAT{attr}_FLAG{flag}" (the upstream
// DAE convention). Sphere/box trigger volumes have no model_t-native shape,
// so each becomes an instance named "MAPOBJ_SPHERE_{name}" /
// "MAPOBJ_BOX_{name}" (plus "#FLAG{flag}" when nonzero) referencing a shared
// degenerate carrier mesh; the instance translation/scale (or matrix) carries
// center and size (or radius). Retail .csb files are usually stored
// Zstandard-compressed as .csb.zst / .ctb.zst.
//-----------------------------------------------------------------------------

#ifndef SZS_LIB_CSB_H
#define SZS_LIB_CSB_H 1

#include "types.h"
#include "lib-model-glb.h"

//--- fixed names -------------------------------------------------------------

#define CSB_COMBINED_MODEL "DEADBEEF" // combined triangle buffer model
#define CSB_MAPOBJ_SPHERE_PREFIX "MAPOBJ_SPHERE_"
#define CSB_MAPOBJ_BOX_PREFIX "MAPOBJ_BOX_"
#define CSB_MATERIAL_FMT "MAT%u_FLAG%llu" // material name encoding
#define CSB_TRIGGER_MESH "_CSB_TRIGGER_VOLUME" // carrier mesh for trigger instances

//--- in-memory representation ------------------------------------------------

typedef struct csb_object_t
{
	bool is_sphere;
	float unknown; // always 0
	vec3_t p1; // sphere center (= p2) / box corner
	vec3_t p2; // sphere center (= p1) / box corner
	vec3_t size; // box scale (sphere: unused)
	vec3_t rotation; // box euler rotation (sphere: unused)
	float radius; // sphere radius
	float box_extra[9]; // always {0,0,1,0,0,0,0,1,0}
	u64 colflag;
	u32 node_index;
	char *name; // owned, never NULL
} csb_object_t;

typedef struct csb_node_t
{
	u16 id;
	u8 flags;
	u8 num_children;
} csb_node_t;

typedef struct csb_tri_t
{
	u32 a, b, c; // indices into the parent model position buffer
	vec3_t normal; // face normal
} csb_tri_t;

typedef struct csb_mesh_t
{
	char *name; // owned, never NULL
	u32 mat_attr; // material attribute id
	u64 colflag; // collision flags
	int node_index; // owning CSB node id
	u32 tri_start; // start triangle in the parent model buffer
	u32 vtx_start; // start position in the parent model buffer
	u32 n_tris; // triangle count (slice length)
	u32 n_vtx; // position count (slice length)
} csb_mesh_t;

typedef struct csb_bbox_t
{
	vec3_t min;
	vec3_t max;
} csb_bbox_t;

typedef struct csb_model_t
{
	char name[64];
	u32 unknown0; // 3 (preserved as read)
	u64 colflag;
	u32 mat_attr;
	u32 unknown4; // 0 (split models, LE only on disk)
	u32 unknown5; // 1
	vec3_t zero;
	vec3_t translate;
	vec3_t rotation;
	u32 node_index;
	csb_bbox_t bbox;
	vec3_t *positions; // owned global buffer
	u32 n_positions;
	csb_tri_t *triangles; // owned global buffer
	u32 n_triangles;
	csb_mesh_t *meshes; // owned slices into the buffers above (models[0] only)
	u32 n_meshes;
} csb_model_t;

typedef struct csb_t
{
	csb_object_t *objects;
	u32 n_objects;
	csb_node_t *nodes;
	u32 n_nodes;
	csb_model_t *models; // models[0] = combined buffer when present
	u32 n_models;
	csb_bbox_t sub_bbox; // bounding over the split models
	bool big_endian; // detected on read (Color Splash = big-endian)
	u32 unknown; // 1
	u16 unknown3; // 1 (Color Splash) or 2
	u8 tail_b0; // 0xFE
	u8 tail_b1; // 0x07
} csb_t;

typedef struct ctb_node_t
{
	vec3_t position;
	float size;
	u32 node_id;
	u8 child_bits; // bit j set => next flat node (depth-first) is child j
	u8 root_flag; // 1 for the root, 0x7F otherwise
	u16 padding;
	u32 *triangles; // owned indices into csb Models[0].Triangles
	u32 n_triangles;
} ctb_node_t;

typedef struct ctb_t
{
	u32 num_model_groups; // 1
	float root_size;
	float unk; // 1
	vec3_t root_position;
	ctb_node_t *nodes; // owned, depth-first order, nodes[0] = root
	u32 n_nodes;
	bool big_endian;
} ctb_t;

//--- validation --------------------------------------------------------------

// Structural validators (no magic exists, so the whole layout is walked).
// Return true only if the buffer parses end to end with no trailing bytes.
bool IsCSB (const u8 *data, u32 size);
bool IsCTB (const u8 *data, u32 size);

// 0 = little-endian, 1 = big-endian, -1 = neither parses.
int DetectCSBEndian (const u8 *data, u32 size);
int DetectCTBEndian (const u8 *data, u32 size);

//--- csb container -----------------------------------------------------------

// Parse DATA (auto-detects endianness, stored in csb->big_endian).
// Always call FreeCSB() afterwards, even on error.
enumError ScanCSB (csb_t *csb, const u8 *data, u32 size);
void FreeCSB (csb_t *csb);

// Serialize (LE unless BIG_ENDIAN). Allocates *DEST (free with FREE()).
enumError SaveCSB (u8 **dest, u32 *dest_size, const csb_t *csb, bool big_endian);

//--- ctb container -----------------------------------------------------------

enumError ScanCTB (ctb_t *ctb, const u8 *data, u32 size);
void FreeCTB (ctb_t *ctb);
enumError SaveCTB (u8 **dest, u32 *dest_size, const ctb_t *ctb, bool big_endian);

// Build the XZ-quadtree search table over csb Models[0] (mirrors
// CtbFile.Generate + OctreeGenerator). No-op (empty table) without
// combined-model triangles.
enumError GenerateCTB (ctb_t *ctb, const csb_t *csb);

//--- model conversion (wmdlt) ------------------------------------------------

// Decode a .csb buffer to GLB at OUT_PATH via model_t.
// Returns ERR_NOTHING_TO_DO when DATA is no CSB.
enumError DecodeCSB (const u8 *data, u32 size, ccp out_path);

// Encode MODEL (parsed from GLB/DAE) to .csb at OUT_CSB_PATH and write the
// generated search table beside it (same stem, .ctb extension, same ZSTD
// compression as the .csb destination). BIG_ENDIAN selects the Color Splash
// layout (u32 flags, no compression); MAP_OBJECT writes one split model per
// mesh and no .ctb, like upstream -mobj. Outputs ending in .zst/.zs/.zstd
// are Zstandard-compressed.
enumError EncodeCSB (const model_t *model, ccp out_csb_path, bool big_endian, bool map_object);

//--- text dumps (wmdlt CAT) --------------------------------------------------

enumError DumpCSB (FILE *f, const csb_t *csb);
enumError DumpCTB (FILE *f, const ctb_t *ctb);

#endif // SZS_LIB_CSB_H

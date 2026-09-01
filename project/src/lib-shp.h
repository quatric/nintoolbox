#ifndef SZS_LIB_SHP0_H
#define SZS_LIB_SHP0_H 1

#include "lib-std.h"
#include "lib-brres.h"

///////////////////////////////////////////////////////////////////////////////

#define SHP0_MIN_VERSION 3
#define SHP0_MAX_VERSION 4
#define SHP0_DEFAULT_VERSION 4

// [[shp0_keyframe_t]]

typedef struct shp0_keyframe_t
{
	float index;
	float value;
	float tangent;
} shp0_keyframe_t;

// [[shp0_vertex_set_t]]

typedef struct shp0_vertex_set_t
{
	ccp name; // alloced entry name (morph target name)
	bool is_fixed; // true: no per-frame data, just 'fixed_value'
	float fixed_value; // only relevant if 'is_fixed'
	shp0_keyframe_t *keyframes; // NULL if fixed, else alloced array of keyframes
	uint n_keyframes; // size of 'keyframes'
} shp0_vertex_set_t;

// [[shp0_entry_t]]

typedef struct shp0_entry_t
{
	ccp name; // alloced entry (base shape name)
	uint flags; // entry flags
	
	shp0_vertex_set_t *vset; // list of vertex sets, alloced
	uint n_vset; // number of used elements of 'vset'
	uint n_vset_alloced; // number of alloced elements of 'vset'

} shp0_entry_t;

///////////////////////////////////////////////////////////////////////////////
// [[shp0_t]]

typedef struct shp0_t
{
	ccp fname; // alloced filename of loaded file
	FileAttrib_t fatt; // file attribute

	uint version; // 3 or 4
	ccp name; // alloced resource name of the animation, or NULL
	ccp orig_path; // alloced original source path, or NULL
	uint n_frames; // number of frames
	bool loop; // true: animation loops

	shp0_entry_t *entry; // list of entries, alloced
	uint n_entry; // number of used elements of 'entry'
	uint n_entry_alloced; // number of alloced elements of 'entry'
	
	ccp *strings; // list of unique strings, alloced
	uint n_strings; // number of used elements of 'strings'
	uint n_strings_alloced; // number of alloced elements of 'strings'

} shp0_t;

///////////////////////////////////////////////////////////////////////////////

void InitializeSHP0 (shp0_t *shp);
void ResetSHP0 (shp0_t *shp);

shp0_entry_t *AppendEntrySHP0 (shp0_t *shp, ccp name);
shp0_vertex_set_t *AppendVertexSetSHP0 (shp0_entry_t *entry, ccp name);

//-----------------------------------------------------------------------------

enumError ScanRawSHP0 (shp0_t *shp, // SHP0 data structure
	bool init_shp, // true: initialize 'shp' first
	const void *data, // data to scan
	uint data_size // size of 'data'
);

//-----------------------------------------------------------------------------

enumError ScanTextSHP0 (shp0_t *shp, // SHP0 data structure
	bool init_shp, // true: initialize 'shp' first
	ccp src_fname // name of the text source to load
);

///////////////////////////////////////////////////////////////////////////////

enumError SaveRawSHP0 (shp0_t *shp, // pointer to valid SHP0
	ccp fname, // filename of destination
	bool set_time // true: set time stamps
);

//-----------------------------------------------------------------------------

enumError SaveTextSHP0 (shp0_t *shp, // pointer to valid SHP0
	ccp fname, // filename of destination
	bool set_time // true: set time stamps
);

///////////////////////////////////////////////////////////////////////////////

#endif // SZS_LIB_SHP0_H

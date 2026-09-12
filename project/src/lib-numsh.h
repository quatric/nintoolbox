#ifndef LIB_NUMSH_H
#define LIB_NUMSH_H

#include "lib-model-glb.h"
#include <stddef.h>
#include <stdint.h>

bool IsSSBH (const uint8_t *data, size_t size);
model_t *ParseNUMSHB (const uint8_t *data, size_t size);

// Same, plus the sibling .nusktb skeleton: bone hierarchy becomes joints and
// the mesh's rigging groups become per-vertex skin weights. Pass NULL for
// SKEL to get the unskinned model ParseNUMSHB() returns.
model_t *ParseNUMSHBSkinned (
	const uint8_t *data, size_t size, const uint8_t *skel, size_t skel_size);

//-----------------------------------------------------------------------------
// Byte-exact round trip. ParseNUMSHB() only keeps positions/normals/UVs and
// skinning, so rebuilding a NUMSHB from a model_t can't reproduce the source
// bytes -- but the vertex and index buffers are plain binary blobs the parser
// never reinterprets, so splicing the *original* buffer bytes back into the
// rest of the file (also copied verbatim) reproduces it exactly.

#define NSH_MAX_VBUF 16

typedef struct nshb_buffer_loc_t
{
	uint n_vtx;
	size_t vtx_off[NSH_MAX_VBUF], vtx_size[NSH_MAX_VBUF];
	size_t idx_off, idx_size;
} nshb_buffer_loc_t;

// Locates the vertex and index buffer byte ranges of a NUMSHB file, using
// the same header fields ParseNUMSHB() reads. Returns false if 'data' isn't
// a recognised SSBH MESH.
bool LocateNUMSHBBuffers (const uint8_t *data, size_t size, nshb_buffer_loc_t *loc);

// Returns a newly allocated copy of 'data' (size bytes) with every range
// 'loc' reports zeroed out. Splicing those ranges back in with
// RebuildNUMSHB() reproduces 'data' exactly; the caller is free to replace a
// range's payload with different bytes of the same length instead, to patch
// geometry while leaving every offset, string and unparsed field untouched.
uint8_t *ExtractNUMSHBPrefix (const uint8_t *data, size_t size);

// Splices the vertex/index buffer contents named by 'loc' into a copy of
// 'prefix' (prefix_size bytes, as returned by ExtractNUMSHBPrefix()) and
// returns the newly allocated result. Each vtx[i] must be exactly
// loc->vtx_size[i] bytes and idx exactly loc->idx_size bytes; a NULL entry
// leaves that range zeroed. Returns NULL on a size mismatch.
uint8_t *RebuildNUMSHB (const uint8_t *prefix, size_t prefix_size, const nshb_buffer_loc_t *loc,
	const uint8_t *const vtx[], const uint8_t *idx);

#endif

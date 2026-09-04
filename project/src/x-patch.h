#ifndef WIT_X_PATCH_H
#define WIT_X_PATCH_H 1

#include "lib-std.h"
#include "lib-sf.h"

//-----------------------------------------------------------------------------
// wit PATCH: apply known, documented anti-piracy neutralization patches
// (Gecko-code style memory pokes) to a Wii disc image or extracted FST.
//
// This reuses the existing Riivolution memory-patch/build pipeline
// (x-riivolution.c/.h) by synthesizing a minimal in-memory Riivolution
// document from a small internal patch database, rather than inventing
// a second patch-application mechanism.
//-----------------------------------------------------------------------------

typedef struct PatchOptions_t
{
    ccp source_image;        // Input game image or extracted FST directory
    ccp dest_path;           // Destination output path (image or FST directory)

    StringField_t select;    // Explicit patch keys to apply (e.g. "metafortress").
                              // Empty = apply all patches available for the detected game.
    bool list_only;           // List available/known patches and exit (no build)
    bool overwrite;
    int  testmode;
    int  verbose;

    ccp custom_id;
    ccp custom_name;
    enumOFT output_oft;
} PatchOptions_t;

void InitPatchOptions ( PatchOptions_t *opt );
enumError PatchCommand ( PatchOptions_t *opt );

extern PatchOptions_t patch_options;

#endif // WIT_X_PATCH_H

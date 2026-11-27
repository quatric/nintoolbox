// Re-injection of an edited PNG into raw GX texture wrappers (GCT0, WIT, CTX, GAFTX, S3T):
// the wrapper header is kept, only the tiled pixel data (and the mip chain behind it) is rewritten.
#ifndef GXWRAP_H
#define GXWRAP_H 1

#include "lib-std.h"
#include "lib-image.h"

// true if path is a wrapper this module can re-encode (by extension and header)
void GXWrapSetIndex (uint idx);
void GXWrapRepackLZSS (ccp root);
void GXWrapRepackSet (ccp root);
void GXWrapRepackOpoona (ccp root);
void GXWrapRepackXtc (ccp root);
void GXWrapRepackNcfam (ccp root);
void GXWrapSetName (ccp name);
bool GXWrapIsFile (ccp path);

// Encode img (any PNG-loaded image) over the pixels of the wrapper file dest, in place.
// The image size must match the wrapper; an unsupported file returns ERR_NOTHING_TO_DO.
enumError GXWrapReinject (Image_t *img, ccp dest);

// After a "<name>.<ext>.tpl" was re-encoded, copy its pixels back into "<name>.<ext>" (S3T).
// Returns ERR_NOTHING_TO_DO when there is no such wrapper.
enumError GXWrapSyncFromTPL (ccp tpl_path);

#endif

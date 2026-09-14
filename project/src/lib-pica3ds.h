#ifndef SZS_LIB_PICA3DS_H
#define SZS_LIB_PICA3DS_H 1

#include "lib-std.h"
#include "lib-image.h"

// Four small 3DS texture wrapper formats sharing the PICA200 codec:
// BTGA (aka LGA), STEX, DMPBM, and a CMB model's embedded texture chunk.
// See lib-pica3ds.c.
bool Pica3DSFilenameExt (ccp fname, ccp ext);
enumError DecodeBTGA_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, uint size);
enumError DecodeSTEX_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, uint size);
enumError DecodeDMPBM_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, uint size);
enumError DecodeCMBTexture_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, uint size);
enumError SavePica3DSTexture (Image_t *img, file_format_t fform, FILE *fo, ccp path, bool overwrite);

#endif

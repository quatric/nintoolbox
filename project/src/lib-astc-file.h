#ifndef SZS_LIB_ASTC_FILE_H
#define SZS_LIB_ASTC_FILE_H 1

#include "lib-std.h"
#include "lib-image.h"

#ifdef __cplusplus
extern "C" {
#endif

bool IsASTCFile (const u8 *data, uint size);
enumError DecodeASTCFile_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, uint size);
enumError EncodeASTCFile_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, uint block_x, uint block_y);
enumError SaveASTC (Image_t *img, ccp dest, ccp source);
enumError SaveASTCFile (Image_t *img, FILE *fo, ccp path, bool overwrite);

#ifdef __cplusplus
}
#endif

#endif // SZS_LIB_ASTC_FILE_H

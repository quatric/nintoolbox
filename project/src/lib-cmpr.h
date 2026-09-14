#ifndef SZS_LIB_CMPR_H
#define SZS_LIB_CMPR_H 1

#include "lib-std.h"
#include "lib-image.h"

// Nintendo GameCube/Wii CMPR block-compressed texture format. See lib-cmpr.c.
enumError conv_from_CMPR (Image_t *dest_img, const Image_t *src_img, image_format_t iform);
enumError conv_to_CMPR (Image_t *dest_img, const Image_t *src_img, palette_format_t pform);

#endif

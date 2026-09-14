#ifndef SZS_LIB_AJPG_H
#define SZS_LIB_AJPG_H 1

#include "lib-std.h"
#include "lib-image.h"

// ActImagine AJPG still-image codec (GBA / Wii Message Board). See lib-ajpg.c.
enumError SaveAJPG (Image_t *img, FILE *fo, ccp path1, ccp path2, bool overwrite);

#endif

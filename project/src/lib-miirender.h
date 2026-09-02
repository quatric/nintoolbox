#ifndef LIB_MIIRENDER_H
#define LIB_MIIRENDER_H

#include "lib-nintendo.h"

// Check if buffer or filename represents Mii character data (FFSD, RSD, RCD, CFL, AFL, etc.)
bool IsMiiData (const u8 *data, uint size, ccp filename);

// Render Mii data to PNG buffer via mii-unsecure.ariankordi.net API
enumError RenderMiiPNG (u8 **dest_png, uint *dest_size, const u8 *data, uint size, uint width);

// Render Mii data to GLB 3D model buffer via mii-unsecure.ariankordi.net API
enumError RenderMiiGLB (u8 **dest_glb, uint *dest_size, const u8 *data, uint size);

#endif

#ifndef SZS_LIB_GVR_H
#define SZS_LIB_GVR_H 1

#include "lib-std.h"

// Sega GameCube/Wii GVR texture wrapper. See lib-gvr.c.
enumError DecodeGVR_RGBA (
	u8 **rgba_ptr, uint *width_ptr, uint *height_ptr, const u8 *data, uint data_size);

#endif

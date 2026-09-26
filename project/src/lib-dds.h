#ifndef SZS_LIB_DDS_H
#define SZS_LIB_DDS_H 1

#include "lib-std.h"
#include "lib-image.h"

#ifdef __cplusplus
extern "C"
{
#endif

	bool IsDDS (const u8 *data, uint size);
	enumError DecodeDDS_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, uint size);
	enumError EncodeDDS_RGBA (
		u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, uint dxgi_fmt);
	enumError SaveDDS (Image_t *img, ccp dest, ccp source);
	enumError SaveDDSFile (Image_t *img, FILE *fo, ccp path, bool overwrite);

#ifdef __cplusplus
}
#endif

#endif // SZS_LIB_DDS_H

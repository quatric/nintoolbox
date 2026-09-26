#ifndef SZS_LIB_IMAGE_INTERNAL_H
#define SZS_LIB_IMAGE_INTERNAL_H 1

// Declarations shared between the image subsystem's own translation units
// (lib-image-*.c and the per-format lib-<fmt>.c files it dispatches to), but
// not meant as part of the project's general public API. Keep this list
// small: anything a caller outside the image subsystem needs belongs in
// lib-image.h instead.

#include "lib-std.h"
#include "lib-image.h"

// lib-image-transform.c: how many --transform terms are queued. A raw
// TPL/cup-icon save checks the count to see whether a forced transform is
// pending before taking its own no-transform fast path.
extern uint n_transform;

// lib-image-convert.c: pixel-buffer helpers shared with the block-codec
// formats (CMPR) that convert to/from a generic image_format_t.
u8 *AllocDataIMG (const Image_t *src, uint bytes_per_pix, uint *data_size);
void AssignData (Image_t *dest, const Image_t *src, u8 *data, uint data_size, image_format_t iform);
enumError CalcImageBlock (const Image_t *img, uint bits_per_pixel, uint block_width,
	uint block_height, uint *h_blocks, uint *v_blocks, uint *img_size, bool calc_only);

// lib-image-load.c: fill IMG from a decoded RGBA8 buffer (endian only
// matters for img->endian bookkeeping, the buffer itself is native order).
void AssignDecodedRGBA (
	Image_t *img, u8 *rgba, uint width, uint height, const endian_func_t *endian, ccp fname);

// lib-image-mipmap.c: shared mip-chain assembly, used by every per-format
// Save*() that writes a full mipmap chain rather than a single image.
// [[mipmap_info_t]]
typedef struct mipmap_info_t
{
	MipmapOptions_t mmo; // mipmap options
	uint n_mipmap; // number of mipmaps to write
	uint image_size; // calculated size of all images
	Image_t img; // transformed image
	const Image_t *src_img; // pointer to source image

	// tpl helpers

	uint img_head_off; // offset of image header
	uint img_data_off; // offset of image data
	uint img_data_size; // size of image data

	uint pal_head_off; // offset of palette header
	uint pal_data_off; // offset of palette header
	uint pal_data_size; // size of palette data

} mipmap_info_t;

void ResetMMI (mipmap_info_t *mmi);
enumError PrepareImages (mipmap_info_t *mmi, const Image_t *src_img, const MipmapOptions_t *mmo);
enumError PrepareImagesTPL (mipmap_info_t *mmi, const Image_t *src_img);
enumError WriteImageData (mipmap_info_t *mmi, u8 *data, u8 **p_dest);

// lib-image-save.c: write a finished buffer verbatim (formats that don't go
// through the mipmap-chain machinery above).
enumError SaveImageBuffer (
	Image_t *img, FILE *fo, ccp path, bool overwrite, const u8 *data, uint size, ccp format);

#endif

#ifndef AJPG_AJPG_H
#define AJPG_AJPG_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

	// ODH / "AJPG": ActImagine/Nintendo's baseline-JPEG-derived still image
	// format (GBA originally, later the Wii Message Board's photo attachments).
	//
	// This is the validated codec core from our mobipeg FFmpeg fork
	// (libavcodec/odh.c), which fixes three big-endian traps and two
	// out-of-bounds bugs present in the original cdbackup reference, and was
	// verified bit-exact against an oracle built from the unmodified reference
	// decoder. Only the FFmpeg wrappers were replaced; the compression core is
	// untouched. See odh_core.c.

	// Reads the 16-byte AJPG header. Returns 1 and fills width/height on
	// success, 0 if the data is too short or the magic is wrong.
	int AjpgGetInfo (const void *src, size_t src_size, int *width, int *height);

	// Decodes an AJPG image to a tightly packed width*height RGBA8 buffer,
	// allocated with malloc() (caller frees). Returns 1 on success.
	int AjpgDecodeRGBA (
		const void *src, size_t src_size, uint8_t **out_rgba, int *out_width, int *out_height);

	// Encodes a tightly packed width*height RGBA8 buffer to an AJPG image,
	// allocated with malloc() (caller frees). 'quality' is JPEG-style 1..100.
	// Dimensions must be even (the reference encoder rejects odd sizes).
	// Returns 1 on success.
	int AjpgEncodeRGBA (const uint8_t *rgba, int width, int height, int quality, uint8_t **out_data,
		size_t *out_size);

	// Releases a buffer returned by AjpgDecodeRGBA/AjpgEncodeRGBA. Use this
	// rather than free() directly: this translation unit is built outside
	// dclib, which redefines free() in the rest of the project.
	void AjpgFree (void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* AJPG_AJPG_H */

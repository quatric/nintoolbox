#ifndef SZS_BCN_WRAPPER_H
#define SZS_BCN_WRAPPER_H 1

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

	int szs_decode_bc6 (
		const uint8_t *src, uint32_t width, uint32_t height, int is_signed, uint8_t *rgba);
	// BC6H as half floats: width*height*3 (r, g, b) values.
	int szs_decode_bc6_half (
		const uint8_t *src, uint32_t width, uint32_t height, int is_signed, uint16_t *out);
	int szs_decode_bc7 (const uint8_t *src, uint32_t width, uint32_t height, uint8_t *rgba);

#ifdef __cplusplus
}
#endif
#endif

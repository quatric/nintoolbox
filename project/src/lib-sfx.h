// Monster Games .sfx audio (Excite Truck / ExciteBots, Wii).
#ifndef SZS_LIB_SFX_H
#define SZS_LIB_SFX_H 1

#include "types.h"

// Fields of a .sfx header that matter for decoding. Sizes and the sample rate
// are little-endian; the nibble count and the coefficients are the standard
// big-endian Nintendo DSP fields, kept as they appear on disc.
typedef struct sfx_info_t
{
	u32 data_size; // payload bytes after the 0x80 header
	u32 sample_rate;
	u32 byte_rate; // sample_rate * 2: the decoded form is 16-bit mono
	u32 num_nibbles;
	s16 coef[16];

} sfx_info_t;

// Read and validate a .sfx header. Returns false when DATA is not one.
bool GetSFXInfo (const u8 *data, uint size, sfx_info_t *info);

// Wrap a .sfx payload in a GENH header so an external decoder can read it.
// GENH is a generic container understood by ffmpeg and mobipeg alike; it
// carries the sample rate and the DSP coefficients, which is everything
// adpcm_thp needs and everything a .sfx header holds.
enumError BuildGENHFromSFX (const u8 *data, uint size, u8 **dest, uint *dest_size);

// Decoding lives in the wszst command layer rather than here: this module is
// linked into tools that have no pass-through, so calling it from here would
// drag an external-tool dependency into every one of them.

#endif // SZS_LIB_SFX_H

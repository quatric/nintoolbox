#include "bcn_wrapper.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

static bool is_opaque_black (const uint8_t *rgba)
{
	for (unsigned i = 0; i < 16; i++)
		if (rgba[i * 4] || rgba[i * 4 + 1] || rgba[i * 4 + 2] || rgba[i * 4 + 3] != 255)
			return false;
	return true;
}

static bool is_transparent_black (const uint8_t *rgba)
{
	for (unsigned i = 0; i < 4 * 4 * 4; i++)
		if (rgba[i])
			return false;
	return true;
}

int main ()
{
	const uint8_t zero_block[16] = { };
	uint8_t rgba[4 * 4 * 4];

	std::memset (rgba, 0xcc, sizeof (rgba));
	if (!szs_decode_bc6 (zero_block, 4, 4, 0, rgba) || !is_opaque_black (rgba))
	{
		std::fprintf (stderr, "BC6H unsigned zero-block decode failed\n");
		return 1;
	}

	std::memset (rgba, 0xcc, sizeof (rgba));
	if (!szs_decode_bc6 (zero_block, 4, 4, 1, rgba) || !is_opaque_black (rgba))
	{
		std::fprintf (stderr, "BC6H signed zero-block decode failed\n");
		return 1;
	}

	std::memset (rgba, 0xcc, sizeof (rgba));
	if (!szs_decode_bc7 (zero_block, 4, 4, rgba) || !is_transparent_black (rgba))
	{
		std::fprintf (stderr, "BC7 invalid-mode fallback failed: %u,%u,%u,%u\n", rgba[0], rgba[1],
			rgba[2], rgba[3]);
		return 1;
	}
	return 0;
}

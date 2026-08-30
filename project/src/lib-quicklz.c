#include "lib-quicklz.h"
#include "dclib/dclib-basics.h"

// The two namespaced vendor codecs (see qlz120.c / qlz140.c).
size_t qlz140_size_decompressed (const char *source);
size_t qlz140_size_compressed (const char *source);
size_t qlz140_decompress (const char *source, void *destination, char *scratch);
size_t qlz140_compress (const void *source, char *destination, size_t size, char *scratch);
int qlz140_get_setting (int setting);

unsigned int qlz120_size_decompressed (const char *source);
unsigned int qlz120_size_compressed (const char *source);
unsigned int qlz120_decompress (const char *source, void *destination);

///////////////////////////////////////////////////////////////////////////////

// Which stream version a buffer holds is decided by the header byte, using the
// rule each vendor decoder enforces on itself:
//   1.4.0 always sets bit 6 when compressing  (*destination |= (1 << 6))
//   1.20  refuses to decode unless (*source & 0xfc) == 0
// So the two are cleanly separable and neither can be mistaken for the other.
enum
{
	QLZ_NONE,
	QLZ_V120,
	QLZ_V140
};

static int qlz_variant (const u8 *src, uint src_size)
{
	// The header is 3 bytes for a short stream and 9 for a long one; bit 1
	// selects. Nothing may be read before that much is present.
	if (src_size < 3)
		return QLZ_NONE;
	const uint n = (src[0] & 2) == 2 ? 4 : 1;
	if (src_size < 1 + 2 * n)
		return QLZ_NONE;

	// A QuickLZ header records its own compressed length. Requiring that to be
	// exactly the buffer we were handed is what turns a one-byte flag test
	// into something safe to act on.
	if (src[0] & 0x40)
	{
		if (qlz140_size_compressed ((ccp)src) == src_size
			&& qlz140_size_decompressed ((ccp)src) > 0)
			return QLZ_V140;
	}
	else if (!(src[0] & 0xfc))
	{
		if (qlz120_size_compressed ((ccp)src) == src_size
			&& qlz120_size_decompressed ((ccp)src) > 0)
			return QLZ_V120;
	}
	return QLZ_NONE;
}

bool IsQuickLZ (const u8 *src, uint src_size)
{
	return src && qlz_variant (src, src_size) != QLZ_NONE;
}

enumError DecodeQuickLZ (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	DASSERT (dest);
	DASSERT (dest_size);
	DASSERT (src);
	*dest = 0;
	*dest_size = 0;

	const int variant = qlz_variant (src, src_size);
	if (variant == QLZ_NONE)
		return ERROR0 (ERR_INVALID_DATA, "Not a QuickLZ stream.\n");

	const size_t size = variant == QLZ_V140 ? qlz140_size_decompressed ((ccp)src)
											: qlz120_size_decompressed ((ccp)src);
	if (size > 0x40000000)
		return ERROR0 (ERR_INVALID_DATA, "QuickLZ: implausible decompressed size: %zu\n", size);

	// Both vendor decoders write whole machine words and can run a few bytes
	// past the logical end, so the buffer is padded rather than exact.
	u8 *buf = MALLOC (size + 400);
	size_t written;
	if (variant == QLZ_V140)
	{
		char *scratch = MALLOC (qlz140_get_setting (2));
		written = qlz140_decompress ((ccp)src, buf, scratch);
		FREE (scratch);
	}
	else
		written = qlz120_decompress ((ccp)src, buf);

	if (written != size)
	{
		FREE (buf);
		return ERROR0 (
			ERR_INVALID_DATA, "QuickLZ: decompressed %zu of %zu bytes.\n", written, size);
	}

	*dest = buf;
	*dest_size = (uint)size;
	return ERR_OK;
}

enumError EncodeQuickLZ (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	DASSERT (dest);
	DASSERT (dest_size);
	DASSERT (src);
	*dest = 0;
	*dest_size = 0;

	// Vendor requirement: the destination must have "uncompressed size" + 400
	// bytes available, because an incompressible input is stored verbatim
	// behind a header.
	u8 *buf = MALLOC (src_size + 400);
	char *scratch = MALLOC (qlz140_get_setting (1));
	const size_t written = qlz140_compress (src, (char *)buf, src_size, scratch);
	FREE (scratch);

	if (!written)
	{
		FREE (buf);
		return ERROR0 (ERR_INVALID_DATA, "QuickLZ: compression failed.\n");
	}

	*dest = buf;
	*dest_size = (uint)written;
	return ERR_OK;
}

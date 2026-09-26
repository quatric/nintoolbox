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

// Little-endian header field of 'n' bytes (1 or 4) at src+off. The vendor
// readers always load a whole 32-bit word, which can run past a short
// buffer, so detection parses the header here instead.
static u32 qlz_header_field (const u8 *src, uint off, uint n)
{
	return n == 4 ? le32 (src + off) : src[off];
}

// Header facts shared by detection and decoding; returns false for anything
// that is not a self-consistent QuickLZ stream of exactly src_size bytes.
typedef struct
{
	int variant;
	uint header_len; // bytes before the payload
	u32 decomp_size;
	bool compressed;
} qlz_header_t;

static bool qlz_parse_header (const u8 *src, uint src_size, qlz_header_t *h)
{
	memset (h, 0, sizeof (*h));
	// The header is 3 bytes for a short stream and 9 for a long one; bit 1
	// selects. Nothing may be read before that much is present.
	if (src_size < 3)
		return false;
	const uint n = (src[0] & 2) == 2 ? 4 : 1;
	if (src_size < 1 + 2 * n)
		return false;

	u32 comp_size;
	if (src[0] & 0x40)
		h->variant = QLZ_V140;
	else if (!(src[0] & 0xfc))
		h->variant = QLZ_V120;
	else
		return false;

	// (1.20's 32-byte "QCLZ" packet form starts with 'Q' = 0x51, which the
	// flag test above never lets through, so only the plain header exists.)
	comp_size = qlz_header_field (src, 1, n);
	h->decomp_size = qlz_header_field (src, 1 + n, n);
	h->compressed = src[0] & 1;
	h->header_len = 1 + 2 * n;

	// A QuickLZ header records its own compressed length. Requiring that to be
	// exactly the buffer we were handed is what turns a one-byte flag test
	// into something safe to act on.
	if (comp_size != src_size || !h->decomp_size)
		return false;
	// A stored block is exactly its payload (the vendor decoders memcpy
	// decomp_size bytes from it unchecked).
	return h->compressed || h->decomp_size == src_size - h->header_len;
}

static int qlz_variant (const u8 *src, uint src_size)
{
	qlz_header_t h;
	return qlz_parse_header (src, src_size, &h) ? h.variant : QLZ_NONE;
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

	qlz_header_t h;
	if (!qlz_parse_header (src, src_size, &h))
		return ERROR0 (ERR_INVALID_DATA, "Not a QuickLZ stream.\n");

	const size_t size = h.decomp_size;
	if (size > 0x40000000)
		return ERROR0 (
			ERR_INVALID_DATA, "QuickLZ: implausible decompressed size: %llu\n", (u64)size);

	// Even in their memory-safe modes the vendor decoders read whole words
	// past the header/stream end, and 1.20 copies literals without checking
	// the source. Decode from a zero-padded copy long enough for any overrun
	// a stream of this output size can cause.
	const size_t padded = (size_t)src_size + size + 64;
	u8 *copy = CALLOC (1, padded);
	memcpy (copy, src, src_size);

	// Both vendor decoders write whole machine words and can run a few bytes
	// past the logical end, so the buffer is padded rather than exact.
	u8 *buf = MALLOC (size + 400);
	size_t written;
	if (h.variant == QLZ_V140)
	{
		char *scratch = MALLOC (qlz140_get_setting (2));
		written = qlz140_decompress ((ccp)copy, buf, scratch);
		FREE (scratch);
	}
	else
		written = qlz120_decompress ((ccp)copy, buf);
	FREE (copy);

	if (written != size)
	{
		FREE (buf);
		return ERROR0 (ERR_INVALID_DATA, "QuickLZ: decompressed %llu of %llu bytes.\n",
			(u64)written, (u64)size);
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

/*
  TrueYZ - Byte-exact Yaz0 compression library

  TrueYZ reproduces, byte for byte, the output of the in-house Yaz0 (SZS)
  encoder used by Nintendo EAD/EPD from the Nintendo 3DS era onwards. It is
  a reconstruction of a zlib-derived lazy-matching deflate front end driving
  a Yaz0 token writer.

  TrueYZ is an encoder only. For decompression, or for a much faster encoder
  where bit-exactness does not matter, see FastYZ.

  This software is released under the MIT License.
  See LICENSE file for details.
*/

#ifndef TRUEYZ_H
#define TRUEYZ_H

#define TRUEYZ_VERSION 0x010000

#define TRUEYZ_VERSION_MAJOR 1
#define TRUEYZ_VERSION_MINOR 0
#define TRUEYZ_VERSION_REVISION 0

#define TRUEYZ_VERSION_STRING "1.0.0"

#include <stddef.h>
#include <stdint.h>

/*
 * Workaround for DJGPP (DOS GCC) to find fixed-width integer types.
 */
#if defined(__MSDOS__) && defined(__GNUC__)
#include <stdint-gcc.h>
#endif

#if defined(__cplusplus)
extern "C"
{
#endif

/**
 * Yaz0 Header Structure (16 bytes)
 *
 * Offset  Size  Description
 * 0x00    4     Magic "Yaz0"
 * 0x04    4     Decompressed size (big-endian)
 * 0x08    4     Reserved (alignment hint, usually 0)
 * 0x0C    4     Reserved (usually 0)
 */
#define YAZ0_HEADER_SIZE 16

/**
 * Number of bits in a flag byte for Yaz0 format.
 * Each flag byte contains 8 bits, each indicating whether the corresponding
 * token is a literal (1) or a match reference (0).
 */
#define YAZ0_FLAG_BYTE_NUM_BITS 8

/**
 * Minimum match length for Yaz0 format.
 * Matches shorter than 3 bytes are encoded as literals.
 */
#define YAZ0_MIN_MATCH_LENGTH 3

/**
 * Minimum length for long-form matches in Yaz0 format.
 * Long-form matches use a 3-byte encoding for lengths of 18 bytes (3 + 15)
 * or more, where 15 is the maximum encodable length in the short form.
 */
#define YAZ0_MIN_LONG_MATCH_LENGTH (YAZ0_MIN_MATCH_LENGTH + 15)

/**
 * Maximum match length for Yaz0 format.
 * Matches can be up to 273 bytes long (18 + 255), where 255 is the maximum
 * encodable length in the long form.
 */
#define YAZ0_MAX_MATCH_LENGTH (YAZ0_MIN_LONG_MATCH_LENGTH + 255)

/**
 * Maximum back-reference distance for Yaz0 format.
 * Yaz0 uses 12-bit distance encoding, allowing back-references up to 4096 bytes.
 */
#define YAZ0_MAX_MATCH_DISTANCE (1 << 12)

/**
 * Calculate the maximum possible size of compressed output.
 *
 * In the worst case (incompressible data), Yaz0 output is slightly larger
 * than the input due to the flag bytes overhead. This macro provides a
 * safe upper bound for output buffer allocation.
 *
 * @param length  Size of the input data in bytes
 * @return        Maximum possible size of compressed output
 */
#define TRUEYZ_BOUND(length) (YAZ0_HEADER_SIZE + (length) + ((length) / 8) + 1)

/**
 * Match-finder state placement policy.
 *
 *   TRUEYZ_STATE_STATIC (default)
 *       State lives in a single static array. NOT reentrant: concurrent
 *       compressions share one set of tables. Single-threaded only.
 *
 *   TRUEYZ_STATE_SCRATCH
 *       State lives in caller-supplied scratch memory. Thread-safe.
 */
#define TRUEYZ_STATE_STATIC 0
#define TRUEYZ_STATE_SCRATCH 1

#ifndef TRUEYZ_STATE
#define TRUEYZ_STATE TRUEYZ_STATE_STATIC
#endif

/*
 * Match-finder geometry.
 *
 * These values are the observed property of the encoder being reproduced.
 * Hash bits is equal to zlib's default at MEM_LEVEL 8.
 */
#define TRUEYZ_HASH_LOG 15u
#define TRUEYZ_WINDOW_SIZE 4096u

#if TRUEYZ_STATE == TRUEYZ_STATE_SCRATCH
/**
 * Number of bytes of scratch memory trueyz_compress_scratch() requires.
 */
#define TRUEYZ_SCRATCH_SIZE                                                                        \
	((size_t)((1u << TRUEYZ_HASH_LOG) + TRUEYZ_WINDOW_SIZE + 1) * sizeof (uint32_t))
#endif

#if TRUEYZ_STATE == TRUEYZ_STATE_SCRATCH
	/**
	 * Compress a block of data using Yaz0 compression.
	 *
	 * Produces a valid Yaz0 stream complete with the standard 16-byte header,
	 * identical byte for byte to the output of Nintendo's in-house encoder for
	 * the same input.
	 *
	 * Thread-safe.
	 *
	 * @param input   Pointer to the input data to compress
	 * @param length  Size of the input data in bytes
	 * @param output  Pointer to the output buffer for compressed data
	 *                Must be at least TRUEYZ_BOUND(length) bytes
	 * @param scratch Scratch buffer, at least TRUEYZ_SCRATCH_SIZE bytes,
	 *                aligned to at least alignof(uint32_t).
	 *                It does not need to be initialized and may be reused
	 *                across calls on the same thread.
	 *
	 * @return        Size of the compressed data in bytes,
	 *                or 0 if compression failed
	 *
	 * @note The input and output buffers must not overlap.
	 * @note The output includes the 16-byte Yaz0 header.
	 */
	int trueyz_compress_scratch (const void *input, int length, void *output, void *scratch);
	/**
	 * Alternative to trueyz_compress_scratch where the alignment hint can be provided.
	 *
	 * @param alignment Alignment hint to store in the Yaz0 header
	 *                  Only relatively new games specify it
	 *                  Fails if not greater than zero
	 */
	int trueyz_compress_align_scratch (
		const void *input, int length, void *output, int alignment, void *scratch);
#else
/**
 * Compress a block of data using Yaz0 compression.
 *
 * Produces a valid Yaz0 stream complete with the standard 16-byte header,
 * identical byte for byte to the output of Nintendo's in-house encoder for
 * the same input.
 *
 * NOT thread-safe!
 *
 * @param input   Pointer to the input data to compress
 * @param length  Size of the input data in bytes
 * @param output  Pointer to the output buffer for compressed data
 *                Must be at least TRUEYZ_BOUND(length) bytes
 *
 * @return        Size of the compressed data in bytes,
 *                or 0 if compression failed
 *
 * @note The input and output buffers must not overlap.
 * @note The output includes the 16-byte Yaz0 header.
 */
int trueyz_compress (const void *input, int length, void *output);
/**
 * Alternative to trueyz_compress where the alignment hint can be provided.
 *
 * @param alignment Alignment hint to store in the Yaz0 header
 *                  Only relatively new games specify it
 *                  Fails if not greater than zero
 */
int trueyz_compress_align (const void *input, int length, void *output, int alignment);
#endif

#if defined(__cplusplus)
}
#endif

#endif /* TRUEYZ_H */

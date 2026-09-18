/*
  FastYZ - Fast Yaz0 compression library
  Based on FastLZ by Ariya Hidayat

  This file contains both the compressor (adapted from FastLZ's LZ77 strategy)
  and a standard Yaz0 decompressor.

  The compression algorithm uses a hash-based approach to find matching
  sequences, similar to FastLZ, but outputs in the Yaz0 format which uses
  a flag-byte scheme instead of FastLZ's opcode-based encoding.

  This software is released under the MIT License.
  See LICENSE file for details.
*/

#include "fastyz.h"

#include <stdbool.h>
#include <string.h>

/*
 * Branch prediction hints for the compiler.
 * These help optimize the hot paths in compression/decompression loops.
 */
#if defined(__clang__) || (defined(__GNUC__) && (__GNUC__ > 2))
#define FASTYZ_LIKELY(c)   (__builtin_expect(!!(c), 1))
#define FASTYZ_UNLIKELY(c) (__builtin_expect(!!(c), 0))
#else
#define FASTYZ_LIKELY(c)   (c)
#define FASTYZ_UNLIKELY(c) (c)
#endif

/*
 * Count-trailing-zeros & count-leading-zeros shims.
 */
#if defined(__clang__) || defined(__GNUC__)
#define FASTYZ_CTZ32(x) ((uint32_t)__builtin_ctz(x))
#define FASTYZ_CLZ32(x) ((uint32_t)__builtin_clz(x))
#define FASTYZ_CTZ64(x) ((uint32_t)__builtin_ctzll(x))
#define FASTYZ_CLZ64(x) ((uint32_t)__builtin_clzll(x))
#elif defined(_MSC_VER)
#include <intrin.h>
static uint32_t FASTYZ_CTZ32(uint32_t x) { unsigned long i; _BitScanForward(&i, x); return (uint32_t)i; }
static uint32_t FASTYZ_CLZ32(uint32_t x) { unsigned long i; _BitScanReverse(&i, x); return 31u - (uint32_t)i; }
#if defined(_M_X64) || defined(_M_ARM64)
static uint32_t FASTYZ_CTZ64(uint64_t x) { unsigned long i; _BitScanForward64(&i, x); return (uint32_t)i; }
static uint32_t FASTYZ_CLZ64(uint64_t x) { unsigned long i; _BitScanReverse64(&i, x); return 63u - (uint32_t)i; }
#endif
#else
static uint32_t FASTYZ_CTZ32(uint32_t x) { uint32_t n = 0; while (!(x & 1u)) { x >>= 1; ++n; } return n; }
static uint32_t FASTYZ_CLZ32(uint32_t x) { uint32_t n = 0; while (!(x & 0x80000000u)) { x <<= 1; ++n; } return n; }
static uint32_t FASTYZ_CTZ64(uint64_t x) { uint32_t n = 0; while (!(x & 1u)) { x >>= 1; ++n; } return n; }
static uint32_t FASTYZ_CLZ64(uint64_t x) { uint32_t n = 0; while (!(x & 0x8000000000000000ull)) { x <<= 1; ++n; } return n; }
#endif

/*
 * Force-inline attribute, portably.
 */
#if defined(__clang__) || defined(__GNUC__)
#define FASTYZ_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define FASTYZ_ALWAYS_INLINE __forceinline
#else
#define FASTYZ_ALWAYS_INLINE inline
#endif

/*
 * Little-endian detection.
 */
#ifdef FASTYZ_LITTLE_ENDIAN
    #if FASTYZ_LITTLE_ENDIAN != 0 && FASTYZ_LITTLE_ENDIAN != 1
        #error "FastYZ: FASTYZ_LITTLE_ENDIAN must be 0 or 1"
    #endif
#else
    #if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
        #define FASTYZ_LITTLE_ENDIAN (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    #elif defined(_WIN32) || defined(_M_IX86) || defined(_M_X64) || defined(_M_ARM) || defined(_M_ARM64)
        #define FASTYZ_LITTLE_ENDIAN 1
    #elif defined(__i386__) || defined(__x86_64__) || defined(__ARMEL__) || defined(__MIPSEL__)
        #define FASTYZ_LITTLE_ENDIAN 1
    #elif defined(__BIG_ENDIAN__) || defined(__ARMEB__) || defined(__MIPSEB__) || defined(_M_PPC)
        #define FASTYZ_LITTLE_ENDIAN 0
    #else
        #error "FastYZ: cannot determine endianness; define FASTYZ_LITTLE_ENDIAN to 0 or 1"
    #endif
#endif

/*
 * Index of the first byte, in memory order, where an XOR is non-zero.
 * On little-endian that byte is the least significant (ctz); on big-endian
 * it is the most significant (clz).
 */
#if FASTYZ_LITTLE_ENDIAN
    #define FASTYZ_FIRST_DIFF32(x) (FASTYZ_CTZ32(x) >> 3)
    #define FASTYZ_FIRST_DIFF64(x) (FASTYZ_CTZ64(x) >> 3)
#else
    #define FASTYZ_FIRST_DIFF32(x) (FASTYZ_CLZ32(x) >> 3)
    #define FASTYZ_FIRST_DIFF64(x) (FASTYZ_CLZ64(x) >> 3)
#endif

/*
 * Enable 64-bit optimizations on supported architectures.
 * This allows reading/comparing 8 bytes at a time using native instructions.
 */
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__)
#define FASTYZ_ARCH64
#endif

/* ========================================================================
 * Internal Constants
 * ======================================================================== */

/* Maximum literals that can be emitted in one flag byte */
#define MAX_COPY YAZ0_FLAG_BYTE_NUM_BITS

/* Short-form minimum match length */
#define SHORT_FORM_MIN YAZ0_MIN_MATCH_LENGTH

/* Long-form minimum match length */
#define LONG_FORM_MIN YAZ0_MIN_LONG_MATCH_LENGTH

/* Maximum match length: 273 bytes */
#define MAX_LEN YAZ0_MAX_MATCH_LENGTH

/* Maximum back-reference distance */
#define MAX_MATCH_DISTANCE YAZ0_MAX_MATCH_DISTANCE

#if FASTYZ_HTAB != FASTYZ_HTAB_SCRATCH
    #define HASH_SIZE (1 << FASTYZ_HASH_LOG)
#endif

/* ========================================================================
 * Memory Access Utilities
 * ======================================================================== */

/*
 * Read 4 bytes as a native-endian u32.
 */
static uint32_t read_u32(const void* ptr)
{
    uint32_t v;
    memcpy(&v, ptr, 4);
    return v;
}

/*
 * Read 4 bytes as a little-endian u32, regardless of host endianness.
 */
static uint32_t read_u32_le(const void* ptr)
{
#if FASTYZ_LITTLE_ENDIAN
    return read_u32(ptr);
#else
    const uint8_t* p = (const uint8_t*)ptr;
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[1] <<  8) | ((uint32_t)p[0]);
#endif
}

/*
 * Compute a hash value for match finding.
 * Uses a multiplicative hash with a prime constant for good distribution.
 */
static uint32_t compute_hash(uint32_t v, uint32_t shift, uint32_t mask)
{
    return ((v * 2654435761u) >> shift) & mask;
}

/*
 * Compare two memory regions and return the length of the matching prefix.
 * The comparison stops at the boundary 'limit' to prevent buffer overruns.
 */
#if defined(FASTYZ_ARCH64)
static uint64_t read_u64(const void* ptr)
{
    uint64_t v;
    memcpy(&v, ptr, 8);
    return v;
}

static uint32_t compare_match(const uint8_t* p, const uint8_t* q, const uint8_t* limit)
{
    const uint8_t* start = p;

    /* 8 bytes per iteration; ctz/clz locates the first differing byte */
    while (q + 8 <= limit)
    {
        uint64_t x = read_u64(p) ^ read_u64(q);
        if (x)
            return (uint32_t)(p - start) + FASTYZ_FIRST_DIFF64(x);
        p += 8;
        q += 8;
    }
    
    /* Byte-by-byte comparison for remaining bytes */
    while (q < limit && *p == *q)
    {
        ++p;
        ++q;
    }
    
    return (uint32_t)(p - start);
}
#else
static uint32_t compare_match(const uint8_t* p, const uint8_t* q, const uint8_t* limit)
{
    const uint8_t* start = p;

    /* 4 bytes per iteration; ctz/clz locates the first differing byte */
    while (q + 4 <= limit)
    {
        uint32_t x = read_u32(p) ^ read_u32(q);
        if (x)
            return (uint32_t)(p - start) + FASTYZ_FIRST_DIFF32(x);
        p += 4;
        q += 4;
    }
    
    /* Byte-by-byte comparison for remaining bytes */
    while (q < limit && *p == *q)
    {
        ++p;
        ++q;
    }
    
    return (uint32_t)(p - start);
}
#endif

/* ========================================================================
 * Yaz0 Writer State
 * ======================================================================== */

/*
 * Writer state for Yaz0 encoding.
 * 
 * Yaz0 uses a flag-byte scheme where each bit indicates:
 *   1 = literal byte follows
 *   0 = match reference follows
 *
 * The flag byte is written first, then up to 8 data items follow
 * (either literal bytes or 2-3 byte match encodings).
 */
typedef struct
{
    uint8_t* op;      /* Current write position in output buffer */
    uint8_t* flagp;   /* Pointer to the current flag byte */
    uint8_t  mask;    /* Current bit mask (starts at 0x80, shifts right) */
} yaz0_writer_t;

/*
 * Start a new flag group.
 * Reserves a byte for the flags and resets the bit mask.
 */
static inline void writer_new_group(yaz0_writer_t* w)
{
    w->flagp = w->op;
    *w->op++ = 0;
    w->mask = 0x80;
}

/*
 * Emit N literal bytes to the output.
 * Each literal byte sets the corresponding flag bit to 1.
 */
static inline void writer_emit_literals(yaz0_writer_t* w, uint32_t count, const uint8_t* src)
{
    if (FASTYZ_UNLIKELY(count == 0))
        return;

    /* Fill the current flag group if there's remaining space */
    if (w->mask != 0x80)
    {
        uint32_t room = FASTYZ_CTZ32(w->mask) + 1;
        const bool fits_in_room = count < room;
        uint32_t n = fits_in_room ? count : room;

        /* Set n flag bits starting at the current position, in one shot */
        *w->flagp |= (uint8_t)((0xFFu >> (8 - n)) << (room - n));
        w->mask = (uint8_t)(w->mask >> n);

        memcpy(w->op, src, n);
        w->op += n;
        src += n;
        count -= n;

        if (fits_in_room)
            return;
        else
            writer_new_group(w);
    }

    /* Emit full groups of 8 literals */
    while (count >= MAX_COPY)
    {
        *w->flagp = 0xFF;  /* All 8 bits set = all literals */
        memcpy(w->op, src, MAX_COPY);
        w->op += MAX_COPY;
        src += MAX_COPY;
        count -= MAX_COPY;
        writer_new_group(w);
    }

    /* Emit remaining literals */
    if (count)
    {
        *w->flagp |= 0xFFu << (8 - count);
        w->mask = 0x80u >> count;

        memcpy(w->op, src, count);
        w->op += count;
    }
}

/*
 * Emit an LZ match reference.
 * 
 * Yaz0 Match Encoding:
 *   Flag bit = 0 (match reference follows)
 *
 *   Short form (2 bytes, len 3-17):
 *     Byte 0: [NNNN RRRR] - N = (length - 2), R = high 4 bits of distance-1
 *     Byte 1: [RRRR RRRR] - R = low 8 bits of distance-1
 *
 *   Long form (3 bytes, len 18-273):
 *     Byte 0: [0000 RRRR] - R = high 4 bits of distance-1
 *     Byte 1: [RRRR RRRR] - R = low 8 bits of distance-1
 *     Byte 2: [NNNN NNNN] - N = length - 18
 *
 * Distance is stored as (distance - 1), allowing distances 1-4096.
 */
static inline void writer_emit_match(yaz0_writer_t* w, uint32_t len, uint32_t distance)
{
    distance--;
    
    /* Handle matches longer than MAX_LEN by splitting into chunks */
    if (FASTYZ_UNLIKELY(len > MAX_LEN))
    {
        while (len > MAX_LEN)
        {
            /*
             * If a full 273-byte chunk would leave a tail of only 1-2 bytes,
             * emit 271 bytes instead so the remaining tail will be >= 3 bytes
             * (the minimum match length).
             */
            uint32_t chunk = FASTYZ_UNLIKELY(len - MAX_LEN < SHORT_FORM_MIN) ? (MAX_LEN - (SHORT_FORM_MIN - 1)) : MAX_LEN;

            /* Emit long form (3 bytes) for this chunk */
            uint8_t dist_high = (distance >> 8) & 0x0F;
            *w->op++ = dist_high;
            *w->op++ = distance & 0xFF;
            *w->op++ = (uint8_t)(chunk - LONG_FORM_MIN);

            /* Consume one flag bit (0 = match) */
            w->mask >>= 1;
            if (FASTYZ_UNLIKELY(w->mask == 0))
                writer_new_group(w);

            len -= chunk;
        }
    }

    /* Emit the final (or only) chunk */
    if (len < LONG_FORM_MIN)
    {
        /* Short form: 2 bytes for lengths 3-17 */
        uint16_t code = ((len - (SHORT_FORM_MIN - 1)) << 12) | distance;
        *w->op++ = code >> 8;
        *w->op++ = code & 0xFF;
    }
    else
    {
        /* Long form: 3 bytes for lengths 18-273 */
        uint8_t dist_high = (distance >> 8) & 0x0F;
        *w->op++ = dist_high;
        *w->op++ = distance & 0xFF;
        *w->op++ = (uint8_t)(len - LONG_FORM_MIN);
    }

    /* Consume the flag bit for this match (0 = match) */
    w->mask >>= 1;
    if (FASTYZ_UNLIKELY(w->mask == 0))
        writer_new_group(w);
}

/* ========================================================================
 * Public API: Compression
 * ======================================================================== */

static FASTYZ_ALWAYS_INLINE
int compress_core(
    const void* input, int length, void* output,
#if FASTYZ_HTAB == FASTYZ_HTAB_SCRATCH
    void* scratch,
#endif
    const uint32_t hlog
)
{
    const uint32_t hsize = 1 << hlog;
    const uint32_t hmask = hsize - 1;
    const uint32_t hshift = 32 - hlog;

    const uint8_t* ip = (const uint8_t*)input;
    const uint8_t* ip_start = ip;
    const uint8_t* ip_bound = ip + length - 4;  /* Leave room for read_u32_le */
    const uint8_t* ip_limit = ip + length - 12 - 1;
    uint8_t* op = (uint8_t*)output;

    /* Write the Yaz0 header */
    op[0] = 'Y';
    op[1] = 'a';
    op[2] = 'z';
    op[3] = '0';
    op[4] = (length >> 24) & 0xFF;
    op[5] = (length >> 16) & 0xFF;
    op[6] = (length >>  8) & 0xFF;
    op[7] = (length      ) & 0xFF;
    
    /* Reserved fields (alignment hint and padding) */
    for (int i = 8; i < YAZ0_HEADER_SIZE; ++i)
        op[i] = 0;

    /* Initialize writer state after the 16-byte header */
    yaz0_writer_t w;
    w.op = op + YAZ0_HEADER_SIZE;
    writer_new_group(&w);

    /* Initialize hash table for match finding */
#if FASTYZ_HTAB == FASTYZ_HTAB_SCRATCH
    uint32_t* restrict htab = (uint32_t*)scratch;
#else
    static uint32_t htab[HASH_SIZE];
#endif
    memset(htab, 0, hsize * sizeof(uint32_t));

    /* Start with literal copy (first 2 bytes can't have back-references) */
    const uint8_t* anchor = ip;
    ip += (SHORT_FORM_MIN - 1);

    /* Main compression loop */
    while (FASTYZ_LIKELY(ip < ip_limit))
    {
        const uint8_t* ref;
        uint32_t distance, cmp, seq, hash;

        /* Find a potential match using the hash table */
        do
        {
            seq = read_u32_le(ip) & 0xffffff;  /* Use 3 bytes for hashing, the minimum match length */
            hash = compute_hash(seq, hshift, hmask);
            ref = ip_start + htab[hash];
            htab[hash] = ip - ip_start;
            distance = ip - ref;
            
            /* Check if the match is valid (within distance and matching) */
            cmp = FASTYZ_LIKELY(distance <= MAX_MATCH_DISTANCE)
                  ? read_u32_le(ref) & 0xffffff
                  : 0x1000000;
            
            if (FASTYZ_UNLIKELY(ip >= ip_limit))
                break;
            ++ip;
        } while (seq != cmp);

        if (FASTYZ_UNLIKELY(ip >= ip_limit))
            break;
        --ip;

        /* Emit any pending literals before this match */
        if (FASTYZ_LIKELY(anchor < ip))
            writer_emit_literals(&w, (uint32_t)(ip - anchor), anchor);

        /* Extend the match as far as possible */
        uint32_t len = compare_match(ref + SHORT_FORM_MIN, ip + SHORT_FORM_MIN, ip_bound) + SHORT_FORM_MIN;
        writer_emit_match(&w, len, distance);

        /* Advance past the matched region */
        ip += len;
        anchor = ip;

        /* Update hash table at the match boundary for future matches */
        seq = read_u32_le(ip);
        hash = compute_hash(seq & 0xFFFFFF, hshift, hmask);
        htab[hash] = ip++ - ip_start;
        seq >>= 8;
        hash = compute_hash(seq, hshift, hmask);
        htab[hash] = ip++ - ip_start;
    }

    /* Emit any remaining literals at the end of input */
    uint32_t remaining = ip_start + length - anchor;
    writer_emit_literals(&w, remaining, anchor);

    return (int)(w.op - (uint8_t*)output);
}

#if FASTYZ_HTAB == FASTYZ_HTAB_SCRATCH
int yaz0_compress_scratch(const void* input, int length, void* output, void* scratch)
{
    if (scratch == NULL)
        return 0;
#else
int yaz0_compress(const void* input, int length, void* output)
{
#endif
    if (length < 0 || input == NULL || output == NULL)
        return 0;
    if (FASTYZ_HASH_LOG > 10 && length < (1 << 12))
        return compress_core(
            input, length, output,
#if FASTYZ_HTAB == FASTYZ_HTAB_SCRATCH
            scratch,
#endif
            10
        );
    if (FASTYZ_HASH_LOG > 12 && length < (1 << 15))
        return compress_core(
            input, length, output,
#if FASTYZ_HTAB == FASTYZ_HTAB_SCRATCH
            scratch,
#endif
            12
        );
    return compress_core(
        input, length, output,
#if FASTYZ_HTAB == FASTYZ_HTAB_SCRATCH
        scratch,
#endif
        FASTYZ_HASH_LOG
    );
}

/* ========================================================================
 * Public API: Decompression
 * ======================================================================== */

int yaz0_decompress(const void* input, int length, void* output, int maxout)
{
    if (input == NULL || output == NULL)
        return 0;

    /* Validate header magic */
    if (length < YAZ0_HEADER_SIZE)
        return 0;

    /* Read decompressed size */
    uint32_t decompressed_size = yaz0_get_decompressed_size(input);
    if (decompressed_size == 0)
        return 0;

    /* Check output buffer is large enough */
    if (maxout < 0 || decompressed_size > (uint32_t)maxout)
        return 0;

    const uint8_t* src = (const uint8_t*)input;
    const uint8_t* src_end = src + length;
    uint8_t* dst = (uint8_t*)output;
    uint8_t* dst_end = dst + decompressed_size;

    /* Skip header */
    src += YAZ0_HEADER_SIZE;

    /* The unchecked path needs slack for 8-byte overwrites and 3-byte tokens */
    uint8_t* dst_fast = (decompressed_size > 32) ? dst_end - 32 : dst;
    const uint8_t* src_fast = (length > 32) ? src_end - 32 : (const uint8_t*)input;

    /* Decompression loop */
    while (dst < dst_end)
    {
        /* Read new flag byte */
        if (src >= src_end)
            return 0;
        uint32_t flag = *src++;

        if (FASTYZ_LIKELY(dst < dst_fast && src < src_fast))
        {
            /* Hot path: 8 tokens with no per-token bounds tests */
            for (int i = 0; i < YAZ0_FLAG_BYTE_NUM_BITS; ++i)
            {
                if (flag & 0x80)
                {
                    /* Flag bit = 1: literal byte */
                    *dst++ = *src++;
                }
                else
                {
                    /* Flag bit = 0: match reference */
                    uint8_t byte1 = *src++;
                    uint8_t byte2 = *src++;

                    /* Extract distance (always present in first 2 bytes) */
                    uint32_t distance = ((byte1 & 0x0F) << 8) | byte2;
                    distance++;  /* Stored as distance-1 */

                    uint32_t len = byte1 >> 4;
                    if (len == 0)
                    {
                        /* Long form: length in third byte */
                        len = *src++;
                        len += LONG_FORM_MIN;
                    }
                    else
                    {
                        /* Short form: length in high nibble */
                        len += (SHORT_FORM_MIN - 1);
                    }

                    /* Validate back-reference and copy from it (byte-by-byte for overlapping copies) */
                    const uint8_t* ref = dst - distance;
                    if (FASTYZ_UNLIKELY(ref < (uint8_t*)output))
                        return 0;

                    if (FASTYZ_UNLIKELY(dst + len > dst_fast))
                    {
                        if (dst + len > dst_end)
                            return 0;

                        for (uint32_t j = 0; j < len; ++j)
                            *dst++ = *ref++;
                    }
                    else if (distance >= 8)
                    {
                        uint8_t* local_dst = dst;
                        uint8_t* local_dst_end = dst + len;
                        do
                        {
                            memcpy(local_dst, ref, 8);
                            local_dst += 8;
                            ref += 8;
                        }
                        while (local_dst < local_dst_end);
                        dst += len;
                    }
                    else
                    {
                        /* overlapping run:
                           materialize 8 bytes, then move the source so the remainder has an effective distance of at least 8
                           (standard offset-table trick) */
                        static const uint32_t inc32[8] = { 0, 1, 2,  1,  0, 4, 4, 4 };
                        static const  int32_t dec64[8] = { 0, 0, 0, -1, -4, 1, 2, 3 };

                        const uint8_t* local_ref = ref;
                        /* Need explicit sequential forward stores since they propagate the pattern */
                        dst[0] = local_ref[0]; dst[1] = local_ref[1]; dst[2] = local_ref[2]; dst[3] = local_ref[3];

                        local_ref += inc32[distance];
                        memcpy(dst + 4, local_ref, 4);

                        /* We copied 8 bytes already, check if there's more to copy */
                        if (len > 8)
                        {
                            local_ref -= dec64[distance];
                            uint8_t* local_dst = dst + 8;
                            uint8_t* local_dst_end = dst + len;
                            do
                            {
                                memcpy(local_dst, local_ref, 8);
                                local_dst += 8;
                                local_ref += 8;
                            }
                            while (local_dst < local_dst_end);
                        }

                        dst += len;
                    }
                }
                flag <<= 1;
                if (dst >= dst_end)
                    break;
            }
        }
        else
        {
            /* Careful path near either buffer end */
            for (int i = 0; i < YAZ0_FLAG_BYTE_NUM_BITS && dst < dst_end; ++i)
            {
                if (flag & 0x80)
                {
                    /* Flag bit = 1: literal byte */
                    if (src >= src_end)
                        return 0;
                    *dst++ = *src++;
                }
                else
                {
                    /* Flag bit = 0: match reference */
                    if (src + 2 > src_end)
                        return 0;

                    uint8_t byte1 = *src++;
                    uint8_t byte2 = *src++;

                    /* Extract distance (always present in first 2 bytes) */
                    uint32_t distance = ((byte1 & 0x0F) << 8) | byte2;
                    distance++;  /* Stored as distance-1 */

                    uint32_t len = byte1 >> 4;
                    if (len == 0)
                    {
                        /* Long form: length in third byte */
                        if (src >= src_end)
                            return 0;
                        len = *src++;
                        len += LONG_FORM_MIN;
                    }
                    else
                    {
                        /* Short form: length in high nibble */
                        len += (SHORT_FORM_MIN - 1);
                    }

                    /* Validate back-reference */
                    const uint8_t* ref = dst - distance;
                    if (ref < (uint8_t*)output)
                        return 0;
                    if (dst + len > dst_end)
                        return 0;

                    /* Copy from back-reference (byte-by-byte for overlapping copies) */
                    for (uint32_t j = 0; j < len; ++j)
                        *dst++ = *ref++;
                }
                flag <<= 1;
            }
        }
    }

    return (int)(dst - (uint8_t*)output);
}

/* ========================================================================
 * Public API: Utility Functions
 * ======================================================================== */

uint32_t yaz0_get_decompressed_size(const void* input)
{
    /* Validate magic */
    if (!yaz0_is_valid(input))
        return 0;

    /* Read big-endian size */
    const uint8_t* src = (const uint8_t*)input;
    return ((uint32_t)src[4] << 24) |
           ((uint32_t)src[5] << 16) |
           ((uint32_t)src[6] << 8) |
           ((uint32_t)src[7]);
}

int yaz0_is_valid(const void* input)
{
    const uint8_t* src = (const uint8_t*)input;
    return (src[0] == 'Y' && src[1] == 'a' && src[2] == 'z' && src[3] == '0');
}

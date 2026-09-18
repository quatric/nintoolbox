/*
  TrueYZ - Byte-exact Yaz0 compression library

  This file reconstructs the Yaz0 (SZS) encoder used by Nintendo EAD/EPD
  from the Nintendo 3DS era onwards. It is a zlib-derived lazy-matching
  deflate front end (hash chains, longest_match, one-token lookahead)
  driving a Yaz0 token writer instead of a Huffman coder.

  Because the entire purpose of this file is to reproduce another encoder's
  output exactly, the match-finder constants and the shape of the search are
  not free parameters. Several of them differ from stock zlib, and each such
  divergence is called out where it appears. Changing any of them silently
  produces valid Yaz0 that no longer matches the reference bytes.

  This software is released under the MIT License.
  See LICENSE file for details.
*/

#include "trueyz.h"

#include <string.h>

/*
 * Count-trailing-zeros & count-leading-zeros shims.
 */
#if defined(__clang__) || defined(__GNUC__)
#define TRUEYZ_CTZ32(x) ((uint32_t)__builtin_ctz(x))
#define TRUEYZ_CLZ32(x) ((uint32_t)__builtin_clz(x))
#define TRUEYZ_CTZ64(x) ((uint32_t)__builtin_ctzll(x))
#define TRUEYZ_CLZ64(x) ((uint32_t)__builtin_clzll(x))
#elif defined(_MSC_VER)
#include <intrin.h>
static uint32_t TRUEYZ_CTZ32(uint32_t x) { unsigned long i; _BitScanForward(&i, x); return (uint32_t)i; }
static uint32_t TRUEYZ_CLZ32(uint32_t x) { unsigned long i; _BitScanReverse(&i, x); return 31u - (uint32_t)i; }
#if defined(_M_X64) || defined(_M_ARM64)
static uint32_t TRUEYZ_CTZ64(uint64_t x) { unsigned long i; _BitScanForward64(&i, x); return (uint32_t)i; }
static uint32_t TRUEYZ_CLZ64(uint64_t x) { unsigned long i; _BitScanReverse64(&i, x); return 63u - (uint32_t)i; }
#endif
#else
static uint32_t TRUEYZ_CTZ32(uint32_t x) { uint32_t n = 0; while (!(x & 1u)) { x >>= 1; ++n; } return n; }
static uint32_t TRUEYZ_CLZ32(uint32_t x) { uint32_t n = 0; while (!(x & 0x80000000u)) { x <<= 1; ++n; } return n; }
static uint32_t TRUEYZ_CTZ64(uint64_t x) { uint32_t n = 0; while (!(x & 1u)) { x >>= 1; ++n; } return n; }
static uint32_t TRUEYZ_CLZ64(uint64_t x) { uint32_t n = 0; while (!(x & 0x8000000000000000ull)) { x <<= 1; ++n; } return n; }
#endif

/*
 * Force-inline attribute, portably.
 */
#if defined(__clang__) || defined(__GNUC__)
#define TRUEYZ_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define TRUEYZ_ALWAYS_INLINE __forceinline
#else
#define TRUEYZ_ALWAYS_INLINE inline
#endif

/*
 * Little-endian detection.
 */
#ifdef TRUEYZ_LITTLE_ENDIAN
    #if TRUEYZ_LITTLE_ENDIAN != 0 && TRUEYZ_LITTLE_ENDIAN != 1
        #error "TrueYZ: TRUEYZ_LITTLE_ENDIAN must be 0 or 1"
    #endif
#else
    #if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
        #define TRUEYZ_LITTLE_ENDIAN (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    #elif defined(_WIN32) || defined(_M_IX86) || defined(_M_X64) || defined(_M_ARM) || defined(_M_ARM64)
        #define TRUEYZ_LITTLE_ENDIAN 1
    #elif defined(__i386__) || defined(__x86_64__) || defined(__ARMEL__) || defined(__MIPSEL__)
        #define TRUEYZ_LITTLE_ENDIAN 1
    #elif defined(__BIG_ENDIAN__) || defined(__ARMEB__) || defined(__MIPSEB__) || defined(_M_PPC)
        #define TRUEYZ_LITTLE_ENDIAN 0
    #else
        #error "TrueYZ: cannot determine endianness; define TRUEYZ_LITTLE_ENDIAN to 0 or 1"
    #endif
#endif

/*
 * Index of the first byte, in memory order, where an XOR is non-zero.
 * On little-endian that byte is the least significant (ctz); on big-endian
 * it is the most significant (clz).
 */
#if TRUEYZ_LITTLE_ENDIAN
    #define TRUEYZ_FIRST_DIFF32(x) (TRUEYZ_CTZ32(x) >> 3)
    #define TRUEYZ_FIRST_DIFF64(x) (TRUEYZ_CTZ64(x) >> 3)
#else
    #define TRUEYZ_FIRST_DIFF32(x) (TRUEYZ_CLZ32(x) >> 3)
    #define TRUEYZ_FIRST_DIFF64(x) (TRUEYZ_CLZ64(x) >> 3)
#endif

/*
 * Enable 64-bit optimizations on supported architectures.
 * This allows reading/comparing 8 bytes at a time using native instructions.
 */
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__)
#define TRUEYZ_ARCH64
#endif

/* ========================================================================
 * Internal Constants: Yaz0 token encoding
 * ======================================================================== */

/* Short-form minimum match length */
#define SHORT_FORM_MIN YAZ0_MIN_MATCH_LENGTH

/* Long-form minimum match length */
#define LONG_FORM_MIN YAZ0_MIN_LONG_MATCH_LENGTH

/* ========================================================================
 * Internal Constants: Match finder
 * ======================================================================== */

/* Hash width, and the shortest sequence worth encoding as a match */
#define MIN_MATCH YAZ0_MIN_MATCH_LENGTH

/* Maximum match length: Yaz0's 273, not deflate's 258. */
#define MAX_MATCH YAZ0_MAX_MATCH_LENGTH

/* Sliding window geometry */
#define WSIZE TRUEYZ_WINDOW_SIZE
#define WMASK (WSIZE - 1u)

/*
 * Lookahead the window guarantees ahead of the scan position.
 *
 * Stock zlib defines this as MAX_MATCH + MIN_MATCH + 1. The reference
 * encoder omits the +1, which shifts the point at which the window slides
 * and therefore shifts every match distance computed after the first slide.
 */
#define MIN_LOOKAHEAD (MAX_MATCH + MIN_MATCH)

/*
 * Furthest back-reference the search will consider.
 *
 * Stock zlib uses WSIZE - MIN_LOOKAHEAD to keep the match end inside
 * the window. The reference encoder uses the full 4096 supported by
 * Yaz0's 12-bit distance field.
 */
#define MAX_DIST YAZ0_MAX_MATCH_DISTANCE

/*
 * Hash table geometry.
 */
#define HASH_SIZE  (1u << TRUEYZ_HASH_LOG)
#define HASH_MASK  (HASH_SIZE - 1u)
#define HASH_SHIFT ((uint32_t)((TRUEYZ_HASH_LOG + MIN_MATCH - 1) / MIN_MATCH))

/*
 * Search effort.
 
 * GOOD_MATCH and MAX_CHAIN are zlib's level 9 settings.
 * NICE_MATCH is also the maximum possible value like zlib's level 9.
 * MAX_LAZY matches zlib's level 7 setting.
 */
#define GOOD_MATCH 32u
#define MAX_LAZY   32u
#define NICE_MATCH MAX_MATCH
#define MAX_CHAIN  4096u

/* Empty hash bucket / end of chain */
#define NIL 0xFFFFFFFFu

/* ========================================================================
 * Memory Access Utilities
 * ======================================================================== */

/*
 * Compare two memory regions and return the length of the matching prefix.
 * The comparison stops at the boundary 'limit', which bounds 'q' only;
 * 'p' always trails 'q' and so cannot run past it.
 */
#if defined(TRUEYZ_ARCH64)
/*
 * Read 8 bytes as a native-endian u64.
 */
static uint64_t read_u64(const void* ptr)
{
    uint64_t v;
    memcpy(&v, ptr, 8);
    return v;
}

static uint32_t compare_match(const uint8_t* p, const uint8_t* q, const uint8_t* limit)
{
    const uint8_t* start = q;

    /* 8 bytes per iteration; ctz/clz locates the first differing byte */
    while (q + 8 <= limit)
    {
        uint64_t x = read_u64(p) ^ read_u64(q);
        if (x)
            return (uint32_t)(q - start) + TRUEYZ_FIRST_DIFF64(x);
        p += 8;
        q += 8;
    }

    /* Byte-by-byte comparison for remaining bytes */
    while (q < limit && *p == *q)
    {
        ++p;
        ++q;
    }

    return (uint32_t)(q - start);
}
#else
/*
 * Read 4 bytes as a native-endian u32.
 */
static uint32_t read_u32(const void* ptr)
{
    uint32_t v;
    memcpy(&v, ptr, 4);
    return v;
}

static uint32_t compare_match(const uint8_t* p, const uint8_t* q, const uint8_t* limit)
{
    const uint8_t* start = q;

    /* 4 bytes per iteration; ctz/clz locates the first differing byte */
    while (q + 4 <= limit)
    {
        uint32_t x = read_u32(p) ^ read_u32(q);
        if (x)
            return (uint32_t)(q - start) + TRUEYZ_FIRST_DIFF32(x);
        p += 4;
        q += 4;
    }

    /* Byte-by-byte comparison for remaining bytes */
    while (q < limit && *p == *q)
    {
        ++p;
        ++q;
    }

    return (uint32_t)(q - start);
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
 *
 * Unlike FastYZ's writer, this one emits a single token at a time, because
 * the lazy matcher only knows one token ahead. A flag group is opened on
 * demand rather than up front, so that a zero-length input produces a bare
 * 16-byte header with no trailing flag byte.
 */
typedef struct
{
    uint8_t* op;      /* Current write position in output buffer */
    uint8_t* flagp;   /* Pointer to the current flag byte */
    uint8_t  mask;    /* Bit for the next token; 0 means no group is open */
} yaz0_writer_t;

/*
 * Start a new flag group.
 * Reserves a byte for the flags and resets the bit mask.
 */
static TRUEYZ_ALWAYS_INLINE void writer_new_group(yaz0_writer_t* w)
{
    w->flagp = w->op;
    *w->op++ = 0;
    w->mask = 0x80;
}

/*
 * Open a flag group if the previous one is full (or none exists yet).
 * Reserves a byte for the flags and resets the bit mask.
 */
static TRUEYZ_ALWAYS_INLINE void writer_ensure_group(yaz0_writer_t* w)
{
    if (w->mask == 0)
        writer_new_group(w);
}

/*
 * Emit one literal byte, setting its flag bit to 1.
 */
static TRUEYZ_ALWAYS_INLINE void writer_emit_literal(yaz0_writer_t* w, uint8_t value)
{
    writer_ensure_group(w);

    *w->flagp |= w->mask;
    *w->op++ = value;
    w->mask >>= 1;
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
 *
 * The match finder never produces a length above MAX_MATCH, so unlike
 * FastYZ there is no chunk-splitting path here.
 */
static TRUEYZ_ALWAYS_INLINE void writer_emit_match(yaz0_writer_t* w, uint32_t len, uint32_t distance)
{
    writer_ensure_group(w);

    distance--;

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

    /* Consume the flag bit for this match
     * The flag bit for a match is 0, which writer_ensure_group already wrote
     */
    w->mask >>= 1;
}

/* ========================================================================
 * Match Finder
 * ======================================================================== */

/*
 * Hash chains over the sliding window.
 *
 *   head[h] holds the most recent position whose 3-byte hash is h.
 *   prev[p & WMASK] holds the previous position sharing that position's hash.
 *
 * Both store positions tagged with 'epoch' rather than bare positions: a
 * slot holds epoch + position, and any value below the current epoch is an
 * entry from an earlier call and reads as empty. See matcher_reset().
 */
typedef struct
{
    uint32_t head[HASH_SIZE];
    uint32_t prev[WSIZE];
    uint32_t epoch;
} trueyz_state_t;

#if TRUEYZ_STATE == TRUEYZ_STATE_SCRATCH
typedef char trueyz_scratch_size_check[
    (sizeof(trueyz_state_t) == TRUEYZ_SCRATCH_SIZE) ? 1 : -1];
#endif

typedef struct
{
    trueyz_state_t* restrict state;
    const uint8_t*  restrict data;  /* Input being compressed */
    uint32_t        length;         /* Size of the input in bytes */
    uint32_t        hash;           /* Rolling hash, updated by matcher_insert() */
    uint32_t        epoch;          /* Tag distinguishing this call's entries */
} trueyz_matcher_t;

/*
 * Empty the hash table for a new call.
 *
 * Clearing 128 KiB of head[] outright costs more than compressing a few KiB
 * of input, so entries are tagged with a monotonically increasing epoch
 * instead: a slot counts as empty when its tag is below the current epoch,
 * and "clearing" the table is an addition. A real clear happens only when
 * the epoch would wrap, once per 4 GiB of cumulative input.
 *
 * Under TRUEYZ_STATE_STATIC the table lives in static storage, which C
 * guarantees is zero-initialized, so epoch == 0 reliably means "never used"
 * and the scheme bootstraps with no cost and no contract on the caller.
 *
 * Under TRUEYZ_STATE_SCRATCH the buffer belongs to the caller and starts
 * uninitialized, so there is no zero state to build on and head[] is cleared
 * on every call. Skipping it would require the caller to promise the buffer
 * had been zeroed once, and a forgotten promise would silently produce
 * valid-but-non-matching output, which is the one failure this library
 * exists to rule out.
 *
 * prev[] is never cleared in either mode. A slot is only ever read for a
 * position that a chain reached, every position on a chain was inserted
 * earlier in the same call, and inserting a position writes its slot. So no
 * read of prev[] can observe a value from before this call.
 */
static TRUEYZ_ALWAYS_INLINE void matcher_reset(trueyz_state_t* restrict state, uint32_t length)
{
#if TRUEYZ_STATE == TRUEYZ_STATE_SCRATCH
    (void)length;
    memset(state->head, 0, sizeof(state->head));
    state->epoch = 1;
#else
    if (state->epoch == 0 || state->epoch > UINT32_MAX - length)
    {
        memset(state->head, 0, sizeof(state->head));
        state->epoch = 1;
    }
#endif
}

/*
 * Map an absolute input position to zlib's window offset for that position.
 *
 * zlib does not track absolute positions. It keeps a 2*WSIZE buffer and
 * slides it down by WSIZE whenever strstart reaches 2*WSIZE - MIN_LOOKAHEAD,
 * so strstart runs 0..7915 once and then cycles through
 * [WSIZE - MIN_LOOKAHEAD, 2*WSIZE - MIN_LOOKAHEAD) forever.
 *
 * That offset is what decides how far back the search is allowed to reach,
 * so reproducing the reference output means reproducing the slide schedule.
 * Note that this is only needed for the search bounds: prev[] can still be
 * indexed with absolute positions, because window offset and absolute
 * position are congruent modulo WSIZE by construction.
 */
static TRUEYZ_ALWAYS_INLINE uint32_t window_position(uint32_t position)
{
    const uint32_t first_slide = 2u * WSIZE - MIN_LOOKAHEAD;

    if (position < first_slide)
        return position;

    return (WSIZE - MIN_LOOKAHEAD) + ((position - first_slide) % WSIZE);
}

/*
 * Roll the hash forward onto 'position' and link that position into its
 * chain, returning the position that previously headed the chain (or NIL
 * if there are fewer than MIN_MATCH bytes left to hash).
 */
static TRUEYZ_ALWAYS_INLINE uint32_t matcher_insert(trueyz_matcher_t* m, uint32_t position)
{
    if (position + MIN_MATCH > m->length)
        return NIL;

    m->hash = ((m->hash << HASH_SHIFT) ^ m->data[position + MIN_MATCH - 1]) & HASH_MASK;

    uint32_t tag = m->state->head[m->hash];
    m->state->prev[position & WMASK] = tag;
    m->state->head[m->hash] = m->epoch + position;

    return (tag >= m->epoch) ? (tag - m->epoch) : NIL;
}

/*
 * Walk the hash chain from 'head' and return the longest match found at
 * 'position', or 'previous_length' if nothing beats it.
 *
 * '*match_position' is written only when a strictly longer match is found.
 * Leaving it stale otherwise is deliberate: the caller only reads it when
 * it also decided to emit, and this is the behaviour of the reference.
 */
static TRUEYZ_ALWAYS_INLINE uint32_t matcher_longest_match(
    const trueyz_matcher_t* m,
    uint32_t position,
    uint32_t head,
    uint32_t previous_length,
    uint32_t chain_limit,
    uint32_t* match_position
)
{
    uint32_t lookahead    = m->length - position;
    /* No match can be longer than the bytes remaining */
    if (previous_length >= lookahead)
        return lookahead;

    uint32_t chain_length = MAX_CHAIN;
    uint32_t best_length  = previous_length;
    uint32_t nice_length  = ((uint32_t)NICE_MATCH < lookahead) ? (uint32_t)NICE_MATCH : lookahead;

    const uint8_t* scan  = m->data + position;
    const uint8_t* limit = (position + MAX_MATCH < m->length)
                           ? scan + MAX_MATCH
                           : m->data + m->length;

    uint32_t current = head;

    /* Already sitting on a decent match; spend a quarter of the budget */
    if (previous_length >= GOOD_MATCH)
        chain_length >>= 2;

    do
    {
        const uint8_t* candidate = m->data + current;
        if (candidate[best_length] == scan[best_length])
        {
            uint32_t length = compare_match(candidate, scan, limit);
            if (length > best_length)
            {
                best_length = length;
                *match_position = current;

                if (best_length >= nice_length)
                    break;
            }
        }

        /*
         * Chains run strictly backwards. A link that does not move backwards
         * is a stale entry from before the window wrapped, and following it
         * would loop forever.
         */
        uint32_t tag = m->state->prev[current & WMASK];
        uint32_t next = (tag >= m->epoch) ? (tag - m->epoch) : NIL;
        if (next == NIL || next >= current)
            break;

        current = next;
    }
    while (current > chain_limit && --chain_length != 0);

    return (best_length > lookahead) ? lookahead : best_length;
}

/* ========================================================================
 * Compression Core
 * ======================================================================== */

static TRUEYZ_ALWAYS_INLINE int compress_core(
    const void* input, uint32_t length, void* output,
    uint32_t alignment,
    trueyz_state_t* restrict state
)
{
    const uint8_t* data = (const uint8_t*)input;
    uint8_t* op = (uint8_t*)output;

    /* Write the Yaz0 header */
    op[ 0] = 'Y';
    op[ 1] = 'a';
    op[ 2] = 'z';
    op[ 3] = '0';
    op[ 4] = (uint8_t)(length >> 24);
    op[ 5] = (uint8_t)(length >> 16);
    op[ 6] = (uint8_t)(length >>  8);
    op[ 7] = (uint8_t)(length      );
    op[ 8] = (uint8_t)(alignment >> 24);
    op[ 9] = (uint8_t)(alignment >> 16);
    op[10] = (uint8_t)(alignment >>  8);
    op[11] = (uint8_t)(alignment      );

    /* Reserved fields (padding) */
    memset(op + 12, 0, YAZ0_HEADER_SIZE - 12);

    /* Initialize writer state after the 16-byte header */
    yaz0_writer_t w = { op + YAZ0_HEADER_SIZE, NULL, 0 };

    /* Initialize hash chains state for match finding */
    matcher_reset(state, length);

    trueyz_matcher_t matcher;
    matcher.state = state;
    matcher.data = data;
    matcher.length = length;
    matcher.hash = 0;
    matcher.epoch = state->epoch;

    /*
     * Prime the rolling hash with the first MIN_MATCH - 1 bytes.
     * matcher_insert() supplies the third byte for each position.
     */
    if (length >= 1)
        matcher.hash = ((matcher.hash << HASH_SHIFT) ^ data[0]) & HASH_MASK;
    if (length >= 2)
        matcher.hash = ((matcher.hash << HASH_SHIFT) ^ data[1]) & HASH_MASK;

    uint32_t position = 0;
    uint32_t match_length = MIN_MATCH - 1;
    uint32_t match_position = 0;

    /*
     * Lazy matching: a candidate token found at 'position' is not emitted
     * until the next position has been examined, so that a longer match
     * starting one byte later can displace it.
     */
    int match_pending = 0;

    while (position < length)
    {
        uint32_t head = matcher_insert(&matcher, position);

        uint32_t previous_length = match_length;
        uint32_t previous_position = match_position;

        match_length = MIN_MATCH - 1;

        /*
         * Translate the search bounds out of zlib's sliding window and back
         * into absolute positions. window_base is the oldest position still
         * resident in the window; chain_limit is the oldest position within
         * reach of a Yaz0 back-reference.
         */
        uint32_t offset = window_position(position);
        uint32_t window_base = position - offset;
        uint32_t chain_limit = (offset > MAX_DIST) ? (position - MAX_DIST) : window_base;

        if (head != NIL && previous_length < MAX_LAZY &&
            head >= chain_limit && head > window_base)
        {
            match_length = matcher_longest_match(&matcher, position, head,
                                                 previous_length, chain_limit,
                                                 &match_position);
        }

        if (previous_length >= MIN_MATCH && match_length <= previous_length)
        {
            /* The pending match wins; it started one byte back */
            writer_emit_match(&w, previous_length, (position - 1) - previous_position);

            /*
             * Insert the interior of the match into the hash chains. The
             * first byte was inserted when the match was found and the last
             * is inserted by the next iteration, hence the two fewer.
             */
            uint32_t remaining = previous_length - 2;
            while (remaining--)
            {
                position++;
                matcher_insert(&matcher, position);
            }

            match_pending = 0;
            match_length = MIN_MATCH - 1;
            position++;
        }
        else if (match_pending)
        {
            /* The pending token lost to this one; flush it as a literal */
            writer_emit_literal(&w, data[position - 1]);
            position++;
        }
        else
        {
            match_pending = 1;
            position++;
        }
    }

    /* Flush a token still held back at end of input */
    if (match_pending)
        writer_emit_literal(&w, data[position - 1]);

    /* Retire this call's tags; every one of them is now below the epoch */
    state->epoch += length;

    return (int)(w.op - (uint8_t*)output);
}

/* ========================================================================
 * Public API: Compression
 * ======================================================================== */

#if TRUEYZ_STATE == TRUEYZ_STATE_SCRATCH
int trueyz_compress_scratch(const void* input, int length, void* output, void* scratch)
{
    if (length < 0 || input == NULL || output == NULL || scratch == NULL)
        return 0;

    return compress_core(input, (uint32_t)length, output, 0u, (trueyz_state_t*)scratch);
}
int trueyz_compress_align_scratch(const void* input, int length, void* output, int alignment, void* scratch)
{
    if (length < 0 || alignment < 0 || input == NULL || output == NULL || scratch == NULL)
        return 0;

    return compress_core(input, (uint32_t)length, output, (uint32_t)alignment, (trueyz_state_t*)scratch);
}
#else
static int TRUEYZ_ALWAYS_INLINE compress_static(const void* input, uint32_t length, void* output, uint32_t alignment)
{
    static trueyz_state_t state;
    return compress_core(input, length, output, alignment, &state);
}
int trueyz_compress(const void* input, int length, void* output)
{
    if (length < 0 || input == NULL || output == NULL)
        return 0;

    return compress_static(input, (uint32_t)length, output, 0u);
}
int trueyz_compress_align(const void* input, int length, void* output, int alignment)
{
    if (length < 0 || alignment < 0 || input == NULL || output == NULL)
        return 0;

    return compress_static(input, (uint32_t)length, output, (uint32_t)alignment);
}
#endif

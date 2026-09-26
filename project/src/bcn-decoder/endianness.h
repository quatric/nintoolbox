#ifndef SZS_BCN_ENDIANNESS_H
#define SZS_BCN_ENDIANNESS_H 1

#include <stdint.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define __BIG_ENDIAN__ 1
#define lton32(x) __builtin_bswap32 ((uint32_t)(x))
#define lton64(x) __builtin_bswap64 ((uint64_t)(x))
#else
#define __LITTLE_ENDIAN__ 1
#define lton32(x) (x)
#define lton64(x) (x)
#endif

#endif

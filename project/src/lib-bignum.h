#ifndef SZS_LIB_BIGNUM_H
#define SZS_LIB_BIGNUM_H 1

#include <stdint.h>
#include <stddef.h>

// Minimal arbitrary-precision unsigned integer arithmetic, just enough for
// RSA sign/verify (PKCS#1 v1.5) at up to 4096 bits. No external bignum
// library (e.g. GMP/OpenSSL) is used -- schoolbook algorithms throughout,
// which is plenty fast enough for one-off signing, not a hot path.

#define BIGNUM_LIMBS 128 // 128 * 32 bits = 4096 bits

typedef struct bignum_t
{
	uint32_t d[BIGNUM_LIMBS]; // little-endian limbs (d[0] = least significant)
	int n; // number of significant limbs (0 for the value 0)
} bignum_t;

void BN_Zero (bignum_t *r);
void BN_FromBytesBE (bignum_t *r, const uint8_t *data, int len);
// Writes exactly 'len' big-endian bytes, zero-padded on the left.
// Returns false if the value doesn't fit in 'len' bytes.
int BN_ToBytesBE (const bignum_t *a, uint8_t *out, int len);
int BN_Cmp (const bignum_t *a, const bignum_t *b);
int BN_IsZero (const bignum_t *a);

// r = a mod m  (r may alias a)
void BN_Mod (bignum_t *r, const bignum_t *a, const bignum_t *m);
// r = (a * b) mod m
void BN_MulMod (bignum_t *r, const bignum_t *a, const bignum_t *b, const bignum_t *m);
// r = (base ^ exp) mod m
void BN_ModExp (bignum_t *r, const bignum_t *base, const bignum_t *exp, const bignum_t *m);

#endif

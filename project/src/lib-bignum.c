// Minimal arbitrary-precision unsigned integer arithmetic (schoolbook
// algorithms). See lib-bignum.h for scope/rationale.

#include "lib-bignum.h"
#include <string.h>

#define WIDE_LIMBS (2 * BIGNUM_LIMBS)

static void wide_zero (uint32_t *d, int n)
{
	memset (d, 0, n * sizeof (uint32_t));
}

static int wide_top (const uint32_t *d, int n)
{
	while (n > 0 && d[n - 1] == 0)
		n--;
	return n;
}

// r = a + b (both length n, result may carry into r[n]; returns carry 0/1)
static uint32_t wide_add (uint32_t *r, const uint32_t *a, const uint32_t *b, int n)
{
	uint64_t carry = 0;
	for (int i = 0; i < n; i++)
	{
		uint64_t s = (uint64_t)a[i] + b[i] + carry;
		r[i] = (uint32_t)s;
		carry = s >> 32;
	}
	return (uint32_t)carry;
}

// r = a - b (both length n, assumes a >= b); returns borrow (should be 0)
static uint32_t wide_sub (uint32_t *r, const uint32_t *a, const uint32_t *b, int n)
{
	int64_t borrow = 0;
	for (int i = 0; i < n; i++)
	{
		int64_t d = (int64_t)a[i] - b[i] - borrow;
		if (d < 0)
		{
			d += (int64_t)1 << 32;
			borrow = 1;
		}
		else
			borrow = 0;
		r[i] = (uint32_t)d;
	}
	return (uint32_t)borrow;
}

static int wide_cmp (const uint32_t *a, int an, const uint32_t *b, int bn)
{
	an = wide_top (a, an);
	bn = wide_top (b, bn);
	if (an != bn)
		return an < bn ? -1 : 1;
	for (int i = an - 1; i >= 0; i--)
		if (a[i] != b[i])
			return a[i] < b[i] ? -1 : 1;
	return 0;
}

// shift left by 1 bit within a length-n limb array; returns bit shifted out
static uint32_t wide_shl1 (uint32_t *d, int n)
{
	uint32_t carry = 0;
	for (int i = 0; i < n; i++)
	{
		uint32_t nc = d[i] >> 31;
		d[i] = (d[i] << 1) | carry;
		carry = nc;
	}
	return carry;
}

static int wide_bitlen (const uint32_t *d, int n)
{
	n = wide_top (d, n);
	if (!n)
		return 0;
	uint32_t top = d[n - 1];
	int bits = (n - 1) * 32;
	while (top)
	{
		bits++;
		top >>= 1;
	}
	return bits;
}

static int wide_getbit (const uint32_t *d, int n, int bit)
{
	int limb = bit / 32, off = bit % 32;
	if (limb >= n)
		return 0;
	return (d[limb] >> off) & 1;
}

//-----------------------------------------------------------------------------

void BN_Zero (bignum_t *r)
{
	memset (r->d, 0, sizeof (r->d));
	r->n = 0;
}

void BN_FromBytesBE (bignum_t *r, const uint8_t *data, int len)
{
	BN_Zero (r);
	int limb = 0, shift = 0;
	for (int i = len - 1; i >= 0 && limb < BIGNUM_LIMBS; i--)
	{
		r->d[limb] |= (uint32_t)data[i] << shift;
		shift += 8;
		if (shift == 32)
		{
			shift = 0;
			limb++;
		}
	}
	r->n = wide_top (r->d, BIGNUM_LIMBS);
}

int BN_ToBytesBE (const bignum_t *a, uint8_t *out, int len)
{
	if (wide_bitlen (a->d, BIGNUM_LIMBS) > len * 8)
		return 0;
	for (int i = 0; i < len; i++)
	{
		int byte_index = len - 1 - i; // position from LSB
		int limb = byte_index / 4, shift = (byte_index % 4) * 8;
		out[i] = limb < BIGNUM_LIMBS ? (uint8_t)(a->d[limb] >> shift) : 0;
	}
	return 1;
}

int BN_Cmp (const bignum_t *a, const bignum_t *b)
{
	return wide_cmp (a->d, BIGNUM_LIMBS, b->d, BIGNUM_LIMBS);
}

int BN_IsZero (const bignum_t *a)
{
	return wide_top (a->d, BIGNUM_LIMBS) == 0;
}

// Generic: reduce a wide buffer (length an limbs) modulo m, result into r.
static void mod_reduce (uint32_t *r_out, const uint32_t *a, int an, const bignum_t *m)
{
	uint32_t r[WIDE_LIMBS];
	wide_zero (r, WIDE_LIMBS);
	uint32_t mw[WIDE_LIMBS]; // m, zero-extended to WIDE_LIMBS -- m->d is only
	wide_zero (mw, WIDE_LIMBS); // BIGNUM_LIMBS long, must not read past it
	memcpy (mw, m->d, BIGNUM_LIMBS * sizeof (uint32_t));
	int bits = wide_bitlen (a, an);
	for (int bit = bits - 1; bit >= 0; bit--)
	{
		wide_shl1 (r, WIDE_LIMBS);
		if (wide_getbit (a, an, bit))
			r[0] |= 1;
		if (wide_cmp (r, WIDE_LIMBS, mw, WIDE_LIMBS) >= 0)
			wide_sub (r, r, mw, WIDE_LIMBS);
	}
	memcpy (r_out, r, BIGNUM_LIMBS * sizeof (uint32_t));
}

void BN_Mod (bignum_t *r, const bignum_t *a, const bignum_t *m)
{
	uint32_t tmp[BIGNUM_LIMBS];
	mod_reduce (tmp, a->d, BIGNUM_LIMBS, m);
	memcpy (r->d, tmp, sizeof (tmp));
	r->n = wide_top (r->d, BIGNUM_LIMBS);
}

void BN_MulMod (bignum_t *r, const bignum_t *a, const bignum_t *b, const bignum_t *m)
{
	uint32_t prod[WIDE_LIMBS];
	wide_zero (prod, WIDE_LIMBS);
	int an = wide_top (a->d, BIGNUM_LIMBS), bn = wide_top (b->d, BIGNUM_LIMBS);
	for (int i = 0; i < an; i++)
	{
		if (!a->d[i])
			continue;
		uint64_t carry = 0;
		for (int j = 0; j < bn; j++)
		{
			uint64_t p = (uint64_t)a->d[i] * b->d[j] + prod[i + j] + carry;
			prod[i + j] = (uint32_t)p;
			carry = p >> 32;
		}
		int k = i + bn;
		while (carry)
		{
			uint64_t p = (uint64_t)prod[k] + carry;
			prod[k] = (uint32_t)p;
			carry = p >> 32;
			k++;
		}
	}
	uint32_t tmp[BIGNUM_LIMBS];
	mod_reduce (tmp, prod, WIDE_LIMBS, m);
	memcpy (r->d, tmp, sizeof (tmp));
	r->n = wide_top (r->d, BIGNUM_LIMBS);
}

void BN_ModExp (bignum_t *r, const bignum_t *base, const bignum_t *exp, const bignum_t *m)
{
	bignum_t result, b;
	BN_Zero (&result);
	result.d[0] = 1;
	result.n = 1;
	BN_Mod (&b, base, m);

	int bits = wide_bitlen (exp->d, BIGNUM_LIMBS);
	for (int i = 0; i < bits; i++)
	{
		if (wide_getbit (exp->d, BIGNUM_LIMBS, i))
			BN_MulMod (&result, &result, &b, m);
		BN_MulMod (&b, &b, &b, m);
	}
	*r = result;
}

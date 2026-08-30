// Minimal RSA (PKCS#1 v1.5 SHA-1) sign/verify + PKCS#1 PEM parsing.
// See lib-rsa.h for scope/rationale. The bignum modexp this relies on
// (lib-bignum.c) was verified against `openssl` output for a real 1024-bit
// keypair before this file was written on top of it.

#include "lib-rsa.h"
#include "crypt.h"
#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
///////////////			base64				///////////////
//-----------------------------------------------------------------------------

static int b64_val (char c)
{
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if (c >= '0' && c <= '9')
		return c - '0' + 52;
	if (c == '+')
		return 62;
	if (c == '/')
		return 63;
	return -1;
}

// Decodes base64 text (ignoring whitespace/newlines) into 'out'. Returns
// the number of bytes written, or -1 on malformed input. 'out' must have
// room for at least 3*len/4 bytes.
static int base64_decode (const char *in, size_t in_len, uint8_t *out)
{
	int val = 0, bits = 0, out_n = 0;
	for (size_t i = 0; i < in_len; i++)
	{
		char c = in[i];
		if (c == '\n' || c == '\r' || c == ' ' || c == '\t')
			continue;
		if (c == '=')
			break;
		int v = b64_val (c);
		if (v < 0)
			return -1;
		val = (val << 6) | v;
		bits += 6;
		if (bits >= 8)
		{
			bits -= 8;
			out[out_n++] = (uint8_t)((val >> bits) & 0xFF);
		}
	}
	return out_n;
}

//-----------------------------------------------------------------------------
///////////////		minimal DER (ASN.1) parser		///////////////
//-----------------------------------------------------------------------------

typedef struct der_t
{
	const uint8_t *p, *end;
} der_t;

// Reads a TLV header at *d->p; on success *tag/*content/*content_len are
// set and d->p is NOT yet advanced past the content (caller does that).
static int der_read_tlv (der_t *d, uint8_t *tag, const uint8_t **content, size_t *content_len)
{
	if (d->p >= d->end)
		return 0;
	*tag = *d->p++;
	if (d->p >= d->end)
		return 0;
	size_t len;
	uint8_t b0 = *d->p++;
	if (b0 & 0x80)
	{
		int nbytes = b0 & 0x7F;
		if (nbytes == 0 || nbytes > 4 || d->p + nbytes > d->end)
			return 0;
		len = 0;
		for (int i = 0; i < nbytes; i++)
			len = (len << 8) | *d->p++;
	}
	else
		len = b0;
	if (d->p + len > d->end)
		return 0;
	*content = d->p;
	*content_len = len;
	return 1;
}

static int der_read_integer (der_t *d, bignum_t *out)
{
	uint8_t tag;
	const uint8_t *content;
	size_t len;
	if (!der_read_tlv (d, &tag, &content, &len) || tag != 0x02)
		return 0;
	d->p = content + len;
	// Strip a leading 0x00 sign byte (ASN.1 INTEGER is signed, RSA values
	// are always non-negative but padded with 0x00 when the MSB is set).
	while (len > 1 && content[0] == 0)
	{
		content++;
		len--;
	}
	if (out)
		BN_FromBytesBE (out, content, (int)len);
	return 1;
}

//-----------------------------------------------------------------------------
///////////////			PEM loading			///////////////
//-----------------------------------------------------------------------------

// Smallest byte count that BN_ToBytesBE can represent 'n' in, up to the
// bignum's max size. Returns 0 if 'n' somehow doesn't fit at all.
static int rsa_modulus_size (const bignum_t *n)
{
	uint8_t tmp[BIGNUM_LIMBS * 4];
	for (int size = 1; size <= (int)sizeof (tmp); size++)
		if (BN_ToBytesBE (n, tmp, size))
			return size;
	return 0;
}

static const char *find_pem_body (const char *text, size_t len, const char *begin_marker,
	const char *end_marker, size_t *body_len)
{
	const char *b = strstr (text, begin_marker);
	if (!b)
		return 0;
	b += strlen (begin_marker);
	const char *e = strstr (b, end_marker);
	if (!e)
		return 0;
	*body_len = (size_t)(e - b);
	return b;
}

int RSA_LoadPrivateKeyPEM (rsa_key_t *key, const uint8_t *pem, size_t pem_len)
{
	size_t body_len = 0;
	const char *body = find_pem_body ((const char *)pem, pem_len, "-----BEGIN RSA PRIVATE KEY-----",
		"-----END RSA PRIVATE KEY-----", &body_len);
	if (!body)
		return 0;

	uint8_t *der = (uint8_t *)malloc (body_len);
	if (!der)
		return 0;
	int der_len = base64_decode (body, body_len, der);
	if (der_len <= 0)
	{
		free (der);
		return 0;
	}

	der_t d = { der, der + der_len };
	uint8_t tag;
	const uint8_t *content;
	size_t len;
	if (!der_read_tlv (&d, &tag, &content, &len) || tag != 0x30)
	{
		free (der);
		return 0;
	}
	der_t seq = { content, content + len };

	bignum_t version;
	if (!der_read_integer (&seq, &version))
	{
		free (der);
		return 0;
	}
	if (!der_read_integer (&seq, &key->n))
	{
		free (der);
		return 0;
	}
	if (!der_read_integer (&seq, &key->e))
	{
		free (der);
		return 0;
	}
	if (!der_read_integer (&seq, &key->d))
	{
		free (der);
		return 0;
	}
	// (prime1/prime2/exponent1/exponent2/coefficient follow; not needed
	// for a plain modexp signature, so not parsed.)

	key->size = rsa_modulus_size (&key->n);
	free (der);
	return key->size > 0;
}

int RSA_LoadPublicKeyPEM (rsa_key_t *key, const uint8_t *pem, size_t pem_len)
{
	size_t body_len = 0;
	const char *body = find_pem_body ((const char *)pem, pem_len, "-----BEGIN RSA PUBLIC KEY-----",
		"-----END RSA PUBLIC KEY-----", &body_len);
	if (!body)
		return 0;

	uint8_t *der = (uint8_t *)malloc (body_len);
	if (!der)
		return 0;
	int der_len = base64_decode (body, body_len, der);
	if (der_len <= 0)
	{
		free (der);
		return 0;
	}

	der_t d = { der, der + der_len };
	uint8_t tag;
	const uint8_t *content;
	size_t len;
	if (!der_read_tlv (&d, &tag, &content, &len) || tag != 0x30)
	{
		free (der);
		return 0;
	}
	der_t seq = { content, content + len };

	memset (&key->d, 0, sizeof (key->d));
	if (!der_read_integer (&seq, &key->n))
	{
		free (der);
		return 0;
	}
	if (!der_read_integer (&seq, &key->e))
	{
		free (der);
		return 0;
	}

	key->size = rsa_modulus_size (&key->n);
	free (der);
	return key->size > 0;
}

void RSA_SetPublicKey (rsa_key_t *key, const uint8_t *n, int n_len, uint32_t e)
{
	BN_FromBytesBE (&key->n, n, n_len);
	BN_Zero (&key->e);
	key->e.d[0] = e;
	key->e.n = e ? 1 : 0;
	memset (&key->d, 0, sizeof (key->d));
	key->size = n_len;
}

//-----------------------------------------------------------------------------
///////////////		PKCS#1 v1.5 SHA-1 sign/verify		///////////////
//-----------------------------------------------------------------------------

// DER prefix for "DigestInfo" wrapping a SHA-1 OID, per RFC 3447 / PKCS#1.
static const uint8_t sha1_digestinfo_prefix[15]
	= { 0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x1a, 0x05, 0x00, 0x04, 0x14 };

static int build_pkcs1_v15_block (int k, const uint8_t digest[20], uint8_t *em)
{
	// EM = 0x00 || 0x01 || PS(0xFF...) || 0x00 || DigestInfo
	const int digestinfo_len = 15 + 20;
	const int ps_len = k - 3 - digestinfo_len;
	if (ps_len < 8)
		return 0; // key too small for SHA-1 PKCS#1 v1.5
	em[0] = 0x00;
	em[1] = 0x01;
	memset (em + 2, 0xFF, ps_len);
	em[2 + ps_len] = 0x00;
	memcpy (em + 3 + ps_len, sha1_digestinfo_prefix, 15);
	memcpy (em + 3 + ps_len + 15, digest, 20);
	return 1;
}

int RSA_SignSHA1 (const rsa_key_t *key, const uint8_t *data, size_t data_len, uint8_t *sig)
{
	if (BN_IsZero (&key->d))
		return 0; // no private exponent loaded
	uint8_t digest[20];
	SHA1 (data, data_len, digest);

	uint8_t *em = (uint8_t *)malloc (key->size);
	if (!em || !build_pkcs1_v15_block (key->size, digest, em))
	{
		free (em);
		return 0;
	}

	bignum_t m, s;
	BN_FromBytesBE (&m, em, key->size);
	free (em);
	BN_ModExp (&s, &m, &key->d, &key->n);
	int ok = BN_ToBytesBE (&s, sig, key->size);
	return ok;
}

int RSA_VerifySHA1 (const rsa_key_t *key, const uint8_t *data, size_t data_len, const uint8_t *sig)
{
	uint8_t digest[20];
	SHA1 (data, data_len, digest);

	uint8_t *expect = (uint8_t *)malloc (key->size);
	if (!expect || !build_pkcs1_v15_block (key->size, digest, expect))
	{
		free (expect);
		return 0;
	}

	bignum_t s, m;
	BN_FromBytesBE (&s, sig, key->size);
	BN_ModExp (&m, &s, &key->e, &key->n);

	uint8_t *got = (uint8_t *)malloc (key->size);
	int ok = got && BN_ToBytesBE (&m, got, key->size) && !memcmp (got, expect, key->size);
	free (got);
	free (expect);
	return ok ? 1 : 0;
}

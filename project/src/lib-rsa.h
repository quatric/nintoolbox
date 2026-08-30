#ifndef SZS_LIB_RSA_H
#define SZS_LIB_RSA_H 1

#include "lib-bignum.h"
#include <stdint.h>
#include <stddef.h>

// Minimal RSA (PKCS#1 v1.5, SHA-1) sign/verify, native, no external crypto
// library. Enough to reproduce what RiiConnect24/wc24-tools' wc24encrypt.py
// does with Python's `rsa` module: sign with a PKCS#1 "RSA PRIVATE KEY" PEM
// using rsa.sign(data, key, "SHA-1"), producing a signature the same size
// as the modulus (128 bytes for a 1024-bit key).

typedef struct rsa_key_t
{
	bignum_t n; // modulus
	bignum_t e; // public exponent
	bignum_t d; // private exponent (zero/unused for a public-only key)
	int size; // modulus size in bytes (e.g. 128 for RSA-1024)
} rsa_key_t;

// Parses a PKCS#1 "-----BEGIN RSA PRIVATE KEY-----" PEM (as produced by
// `openssl genrsa` / `openssl rsa -traditional`). Returns 1 on success.
int RSA_LoadPrivateKeyPEM (rsa_key_t *key, const uint8_t *pem, size_t pem_len);

// Parses a PKCS#1 "-----BEGIN RSA PUBLIC KEY-----" PEM, or a raw
// (modulus,exponent) pair already split out (e.g. from a Wii wc24pubk.mod
// style blob) via RSA_SetPublicKey.
int RSA_LoadPublicKeyPEM (rsa_key_t *key, const uint8_t *pem, size_t pem_len);
void RSA_SetPublicKey (rsa_key_t *key, const uint8_t *n, int n_len, uint32_t e);

// PKCS#1 v1.5 signature over the SHA-1 digest of 'data'. 'sig' must have
// room for key->size bytes.
int RSA_SignSHA1 (const rsa_key_t *key, const uint8_t *data, size_t data_len, uint8_t *sig);
// Verifies 'sig' (key->size bytes) against the SHA-1 digest of 'data'.
// Returns 1 if valid.
int RSA_VerifySHA1 (const rsa_key_t *key, const uint8_t *data, size_t data_len, const uint8_t *sig);

#endif

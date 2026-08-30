#ifndef SZS_LIB_AES_H
#define SZS_LIB_AES_H 1

#include <stdint.h>
#include <stddef.h>

// Minimal, self-contained AES-128 (FIPS-197) implementation, ECB block
// primitive plus CBC mode helpers. Used for WC24-style payload
// encryption/decryption. No external crypto library is used.

typedef struct aes128_ctx_t
{
	uint8_t round_key[176]; // 11 round keys of 16 bytes each
} aes128_ctx_t;

void AES128_Init (aes128_ctx_t *ctx, const uint8_t key[16]);
void AES128_EncryptBlock (const aes128_ctx_t *ctx, uint8_t block[16]);
void AES128_DecryptBlock (const aes128_ctx_t *ctx, uint8_t block[16]);

// CBC mode over 'size' bytes (must be a multiple of 16). 'iv' is 16 bytes
// and is consumed (not modified) -- pass a copy if you need it again.
void AES128_CBC_Encrypt (const uint8_t key[16], const uint8_t iv[16], uint8_t *data, size_t size);
void AES128_CBC_Decrypt (const uint8_t key[16], const uint8_t iv[16], uint8_t *data, size_t size);

// OFB mode over 'size' bytes (any size, not just multiples of 16 -- it's a
// stream cipher, no padding). This is what real WC24 files use (NOT CBC).
// Encrypt and decrypt are the same operation for OFB.
void AES128_OFB_Crypt (const uint8_t key[16], const uint8_t iv[16], uint8_t *data, size_t size);

#endif

// WC24 (Wii Connect24) file encrypt/decrypt -- shared implementation used
// by both `wszst WC24DECRYPT/WC24ENCRYPT` and the wwc24crypt tool.
//
// Matches RiiConnect24/wc24-tools' wc24decrypt.py / wc24encrypt.py exactly:
// AES-128 in OFB mode (NOT CBC, and NOT HMAC-anything -- real WC24 files
// carry an RSA-SHA1 signature, not an HMAC). No external crypto library or
// subprocess is used; AES (lib-aes.c) and RSA (lib-bignum.c/lib-rsa.c) are
// both native to this project and verified against openssl-produced
// vectors during development (see selftest).
//
// wc24decrypt.py layout (what we read for `wc24-decrypt`):
//   offset 48:  16-byte IV
//   offset 320: encrypted data to EOF
//   key: a 16-byte file, or bytes [512:528) of a 544-byte file (a Wii
//        wc24pubk.mod-style blob with the AES key embedded), or a 32
//        hex-char string
//
// wc24encrypt.py layout (what we write for `wc24-encrypt`):
//   magic(4)="WC24" version(4)=1 filler(4)=0 crypt_type(1)=1 pad(3)=0
//   reserved(32)=0 iv(16) signature(RSA-SHA1 PKCS#1v1.5 of the plaintext,
//   size = RSA modulus size) data(AES-128-OFB ciphertext)
//
// NOTE: wc24decrypt.py and wc24encrypt.py disagree with each other on
// where the encrypted data starts (offset 320 vs. offset 64+sig_size,
// i.e. 192 for a 1024-bit key) -- that's a real inconsistency in the
// upstream RiiConnect24/wc24-tools project (decrypt.py targets real
// Nintendo-format WC24 mail dumps, which apparently have a longer header
// than what encrypt.py's own header assembly produces), not something
// introduced by this port. `wc24-encrypt` output will therefore NOT
// round-trip through `wc24-decrypt` directly -- decrypt real files with
// `wc24-decrypt`, and read data back out of files this tool produces at
// offset 64+signature_size. Both were verified independently against
// openssl (see selftest, and the AES-OFB/RSA-SHA1 layers each match
// openssl-produced output byte-for-byte).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "lib-aes.h"
#include "lib-rsa.h"
#include "crypto/wiimm-sha.h"
#include "lib-std.h"
#include "lib-wc24.h"

static int hex_nibble (char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int hex_decode (const char *hex, uint8_t *out, int out_len)
{
	if ((int)strlen (hex) != out_len * 2)
		return 0;
	for (int i = 0; i < out_len; i++)
	{
		int hi = hex_nibble (hex[i * 2]), lo = hex_nibble (hex[i * 2 + 1]);
		if (hi < 0 || lo < 0)
			return 0;
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return 1;
}

static uint8_t *read_file (const char *fname, long *out_size)
{
	FILE *f = fopen (fname, "rb");
	if (!f)
		return 0;
	fseek (f, 0, SEEK_END);
	long size = ftell (f);
	fseek (f, 0, SEEK_SET);
	uint8_t *buf = (uint8_t *)MALLOC (size > 0 ? size : 1);
	if (!buf)
	{
		fclose (f);
		return 0;
	}
	if (size > 0 && fread (buf, 1, size, f) != (size_t)size)
	{
		fclose (f);
		FREE (buf);
		return 0;
	}
	fclose (f);
	*out_size = size;
	return buf;
}

// Resolves a wc24decrypt.py-style key argument: an existing 16-byte file
// (used directly), an existing 544-byte file (AES key embedded at [512:528),
// a wc24pubk.mod-style blob), or a 32-char hex string.
static int resolve_aes_key (const char *arg, uint8_t key[16])
{
	FILE *f = fopen (arg, "rb");
	if (f)
	{
		fseek (f, 0, SEEK_END);
		long size = ftell (f);
		fseek (f, 0, SEEK_SET);
		int ok = 0;
		if (size == 16)
			ok = fread (key, 1, 16, f) == 16;
		else if (size == 544)
		{
			fseek (f, 512, SEEK_SET);
			ok = fread (key, 1, 16, f) == 16;
		}
		fclose (f);
		if (!ok)
			fprintf (stderr,
				"WC24: %s exists but is neither a 16-byte key "
				"nor a 544-byte wc24pubk.mod-style blob\n",
				arg);
		return ok;
	}
	return hex_decode (arg, key, 16);
}

//-----------------------------------------------------------------------------

enumError WC24DecryptFile (ccp infile, ccp outfile, ccp keyarg)
{
	uint8_t key[16];
	if (!resolve_aes_key (keyarg, key))
	{
		fprintf (stderr, "WC24: can't resolve AES key from '%s'\n", keyarg);
		return ERR_INVALID_DATA;
	}

	long size = 0;
	uint8_t *file = read_file (infile, &size);
	if (!file)
	{
		fprintf (stderr, "WC24: can't read %s\n", infile);
		return ERR_INVALID_DATA;
	}
	if (size < 320)
	{
		fprintf (stderr, "WC24: %s is too small to be a WC24 file (%ld bytes, need >= 320)\n",
			infile, size);
		FREE (file);
		return ERR_INVALID_DATA;
	}

	uint8_t iv[16];
	memcpy (iv, file + 48, 16);
	uint8_t *data = file + 320;
	size_t data_size = (size_t)(size - 320);

	AES128_OFB_Crypt (key, iv, data, data_size);

	FILE *out = fopen (outfile, "wb");
	if (!out)
	{
		fprintf (stderr, "WC24: can't write %s\n", outfile);
		FREE (file);
		return ERR_INVALID_DATA;
	}
	fwrite (data, 1, data_size, out);
	fclose (out);
	FREE (file);
	printf ("wszst: decrypted %s -> %s (%zu bytes)\n", infile, outfile, data_size);
	return ERR_OK;
}

enumError WC24EncryptFile (ccp infile, ccp outfile, ccp aes_keyarg, ccp rsa_pem_path, ccp ivarg)
{
	uint8_t aes_key[16];
	if (!resolve_aes_key (aes_keyarg, aes_key))
	{
		fprintf (stderr, "WC24: can't resolve AES key from '%s'\n", aes_keyarg);
		return ERR_INVALID_DATA;
	}

	uint8_t iv[16];
	if (ivarg)
	{
		if (!resolve_aes_key (ivarg, iv))
		{
			fprintf (stderr, "WC24: can't resolve IV from '%s'\n", ivarg);
			return ERR_INVALID_DATA;
		}
	}
	else
	{
		FILE *rf = fopen ("/dev/urandom", "rb");
		if (!rf || fread (iv, 1, 16, rf) != 16)
		{
			fprintf (stderr, "WC24: can't read random IV from /dev/urandom\n");
			if (rf)
				fclose (rf);
			return ERR_INVALID_DATA;
		}
		fclose (rf);
	}

	long pem_size = 0;
	uint8_t *pem = read_file (rsa_pem_path, &pem_size);
	if (!pem)
	{
		fprintf (stderr, "WC24: can't read RSA private key %s\n", rsa_pem_path);
		return ERR_INVALID_DATA;
	}
	rsa_key_t rsakey;
	int rsa_ok = RSA_LoadPrivateKeyPEM (&rsakey, pem, (size_t)pem_size);
	FREE (pem);
	if (!rsa_ok)
	{
		fprintf (stderr,
			"WC24: %s is not a PKCS#1 'RSA PRIVATE KEY' PEM "
			"(if it's PKCS#8 'PRIVATE KEY', convert with: "
			"openssl rsa -in %s -traditional -out privkey.pem)\n",
			rsa_pem_path, rsa_pem_path);
		return ERR_INVALID_DATA;
	}

	long data_size = 0;
	uint8_t *data = read_file (infile, &data_size);
	if (!data)
	{
		fprintf (stderr, "WC24: can't read %s\n", infile);
		return ERR_INVALID_DATA;
	}

	uint8_t *sig = (uint8_t *)MALLOC (rsakey.size);
	if (!RSA_SignSHA1 (&rsakey, data, (size_t)data_size, sig))
	{
		fprintf (stderr, "WC24: RSA signing failed\n");
		FREE (sig);
		FREE (data);
		return ERR_INVALID_DATA;
	}

	// AES-128-OFB encrypt a working copy (leave the signed plaintext intact).
	uint8_t *enc = (uint8_t *)MALLOC (data_size > 0 ? data_size : 1);
	memcpy (enc, data, data_size);
	AES128_OFB_Crypt (aes_key, iv, enc, (size_t)data_size);
	FREE (data);

	FILE *out = fopen (outfile, "wb");
	if (!out)
	{
		fprintf (stderr, "WC24: can't write %s\n", outfile);
		FREE (sig);
		FREE (enc);
		return ERR_INVALID_DATA;
	}
	uint8_t hdr_magic[4] = { 'W', 'C', '2', '4' };
	uint8_t u32_1[4] = { 0, 0, 0, 1 };
	uint8_t u32_0[4] = { 0, 0, 0, 0 };
	uint8_t crypt_type = 1;
	uint8_t pad[3] = { 0, 0, 0 };
	uint8_t reserved[32];
	memset (reserved, 0, sizeof (reserved));
	fwrite (hdr_magic, 1, 4, out);
	fwrite (u32_1, 1, 4, out);
	fwrite (u32_0, 1, 4, out); // filler
	fwrite (&crypt_type, 1, 1, out);
	fwrite (pad, 1, 3, out);
	fwrite (reserved, 1, 32, out);
	fwrite (iv, 1, 16, out);
	fwrite (sig, 1, rsakey.size, out);
	fwrite (enc, 1, (size_t)data_size, out);
	fclose (out);

	FREE (sig);
	FREE (enc);
	printf ("wszst: encrypted+signed %s -> %s (%ld byte payload, %d byte signature)\n", infile,
		outfile, data_size, rsakey.size);
	return ERR_OK;
}

//-----------------------------------------------------------------------------

// A throwaway 512-bit RSA test key -- zero security value, used only to
// give the selftest a fixed, reproducible sign+verify known-answer check.
static const char kat_rsa_pem[]
	= "-----BEGIN RSA PRIVATE KEY-----\n"
	  "MIIBPAIBAAJBAJzsPMGtaMNOm07ZReQwqPPL8i6aVTpTIrIbt14vGL4g8Mb9aT2J\n"
	  "FAnfFZRMgGybl/44IbL3aBKz56PlunLPELsCAwEAAQJAQJAh3z3NoK2y0JosW1p5\n"
	  "6PS8S9hLwJd76vPkWefesjqOTxkgIlNOysakU9AUoEWmBSMFKb7fN39O7rye05ja\n"
	  "oQIhAM92ap2/5/YYfo9anDq1D+BBwPAd6MGPJ6tRRWenCZwHAiEAwaLWBUvY7pD6\n"
	  "mo621o5lQ4BgdjySTlkHLkCEBBtCYK0CIQDPcl1RuB8+WWfT+IrXuU1StO00LPQc\n"
	  "+AR2riF0b/aP6QIhAIQdgR8N+A4V1xabJv7PGyJqNeaWP1C7h520IR7YJnrlAiEA\n"
	  "hQqIhQGFfPzOeon1xJZpiZ5185GYHHdpTxnYV3rdCaQ=\n"
	  "-----END RSA PRIVATE KEY-----\n";

// The signature `openssl dgst -sha1 -sign` produces for the message
// "AES-RSA-KAT" with the private key above (64 bytes, 512-bit modulus).
static const uint8_t kat_rsa_sig[64] = { 0x96, 0xb1, 0x49, 0xa4, 0x5e, 0xb6, 0x01, 0x1c, 0x10, 0x5b,
	0xb5, 0x7c, 0xd6, 0x50, 0x69, 0x39, 0x60, 0x95, 0x5c, 0x7e, 0x5b, 0x28, 0x90, 0xbc, 0x60, 0x77,
	0x5c, 0xca, 0x16, 0xe6, 0x69, 0xa8, 0x35, 0xe7, 0x13, 0x61, 0x43, 0x25, 0xb6, 0x1a, 0xe9, 0x8d,
	0x66, 0x35, 0xb7, 0xf6, 0x8b, 0xdf, 0x87, 0xeb, 0x86, 0x5c, 0x2a, 0x30, 0x4b, 0x13, 0xcd, 0x7e,
	0xce, 0x59, 0x3d, 0xea, 0xb7, 0x35 };

enumError WC24SelfTest (void)
{
	int all_ok = 1;

	// FIPS-197 Appendix B: AES-128 known-answer test.
	uint8_t key[16] = { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88,
		0x09, 0xcf, 0x4f, 0x3c };
	uint8_t block[16] = { 0x32, 0x43, 0xf6, 0xa8, 0x88, 0x5a, 0x30, 0x8d, 0x31, 0x31, 0x98, 0xa2,
		0xe0, 0x37, 0x07, 0x34 };
	const uint8_t expect[16] = { 0x39, 0x25, 0x84, 0x1d, 0x02, 0xdc, 0x09, 0xfb, 0xdc, 0x11, 0x85,
		0x97, 0x19, 0x6a, 0x0b, 0x32 };
	aes128_ctx_t ctx;
	AES128_Init (&ctx, key);
	AES128_EncryptBlock (&ctx, block);
	int aes_ecb_ok = !memcmp (block, expect, 16);
	printf ("AES-128 ECB block (FIPS-197 KAT):     %s\n", aes_ecb_ok ? "PASS" : "FAIL");
	all_ok &= aes_ecb_ok;

	// NIST SP800-38A F.4.1: AES-128-OFB known-answer test (this is the
	// mode real WC24 files actually use).
	uint8_t ofb_key[16] = { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88,
		0x09, 0xcf, 0x4f, 0x3c };
	uint8_t ofb_iv[16] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
		0x0c, 0x0d, 0x0e, 0x0f };
	uint8_t ofb_pt[32] = { 0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11,
		0x73, 0x93, 0x17, 0x2a, 0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c, 0x9e, 0xb7, 0x6f,
		0xac, 0x45, 0xaf, 0x8e, 0x51 };
	const uint8_t ofb_expect[32] = { 0x3b, 0x3f, 0xd9, 0x2e, 0xb7, 0x2d, 0xad, 0x20, 0x33, 0x34,
		0x49, 0xf8, 0xe8, 0x3c, 0xfb, 0x4a, 0x77, 0x89, 0x50, 0x8d, 0x16, 0x91, 0x8f, 0x03, 0xf5,
		0x3c, 0x52, 0xda, 0xc5, 0x4e, 0xd8, 0x25 };
	AES128_OFB_Crypt (ofb_key, ofb_iv, ofb_pt, 32);
	int ofb_ok = !memcmp (ofb_pt, ofb_expect, 32);
	printf ("AES-128-OFB (NIST SP800-38A F.4.1 KAT): %s\n", ofb_ok ? "PASS" : "FAIL");
	all_ok &= ofb_ok;
	AES128_OFB_Crypt (ofb_key, ofb_iv, ofb_pt, 32); // OFB is its own inverse
	int ofb_rt_ok = !memcmp (ofb_pt,
		(uint8_t[32]) { 0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11,
			0x73, 0x93, 0x17, 0x2a, 0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c, 0x9e, 0xb7,
			0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51 },
		32);
	printf ("AES-128-OFB round-trip:                 %s\n", ofb_rt_ok ? "PASS" : "FAIL");
	all_ok &= ofb_rt_ok;

	// RSA-SHA1 PKCS#1v1.5 sign, checked against a fixed openssl-produced
	// signature, plus a self-verify round-trip.
	rsa_key_t rsakey;
	int rsa_load_ok
		= RSA_LoadPrivateKeyPEM (&rsakey, (const uint8_t *)kat_rsa_pem, strlen (kat_rsa_pem));
	printf ("RSA PKCS#1 PEM parse:                   %s\n", rsa_load_ok ? "PASS" : "FAIL");
	all_ok &= rsa_load_ok;

	if (rsa_load_ok)
	{
		uint8_t sig[64];
		const char *msg = "AES-RSA-KAT";
		int sign_ok = RSA_SignSHA1 (&rsakey, (const uint8_t *)msg, strlen (msg), sig);
		int sign_match = sign_ok && !memcmp (sig, kat_rsa_sig, 64);
		printf ("RSA-SHA1 sign (matches openssl output): %s\n", sign_match ? "PASS" : "FAIL");
		all_ok &= sign_match;

		rsa_key_t pubonly = rsakey;
		memset (&pubonly.d, 0, sizeof (pubonly.d));
		int verify_ok = RSA_VerifySHA1 (&pubonly, (const uint8_t *)msg, strlen (msg), kat_rsa_sig);
		printf ("RSA-SHA1 verify:                        %s\n", verify_ok ? "PASS" : "FAIL");
		all_ok &= verify_ok;
	}

	return all_ok ? ERR_OK : ERR_INVALID_DATA;
}

//-----------------------------------------------------------------------------

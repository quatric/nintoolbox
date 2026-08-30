#ifndef SZS_LIB_WC24_H
#define SZS_LIB_WC24_H 1

#include "types.h"

// WiiConnect24 file encrypt/decrypt, matching the reference RiiConnect24
// wc24-tools logic: AES-128-OFB for the payload and an RSA-SHA1 PKCS#1 v1.5
// signature for integrity (there is no HMAC anywhere in the real format).
//
// Key arguments accept the same three forms the reference tools do:
// a 32-character hex string, a 16-byte key file, or a 544-byte
// wc24pubk.mod-style blob carrying the AES key at offset 512.

enumError WC24DecryptFile (ccp infile, ccp outfile, ccp keyarg);
enumError WC24EncryptFile (ccp infile, ccp outfile, ccp keyarg, ccp rsa_pem_path, ccp ivarg);
// Runs the AES/RSA known-answer tests; prints one line per check.
// Returns ERR_OK when every check passes.
enumError WC24SelfTest (void);

#endif

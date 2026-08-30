// wwc24crypt - WC24 (Wii Connect24) file encrypt/decrypt utility.
//
// Thin front end: the implementation lives in lib-wc24.c and is shared with
// `wszst WC24DECRYPT` / `wszst WC24ENCRYPT`, so both entry points behave
// identically. AES-128-OFB payload + RSA-SHA1 PKCS#1 v1.5 signature, both
// native to this project -- no external crypto library or subprocess.

#include <stdio.h>
#include <string.h>
#include "lib-std.h"
#include "lib-wc24.h"

static void usage (ccp prog)
{
	printf ("wwc24crypt - WC24 (Wii Connect24) file encrypt/decrypt utility\n"
			"AES-128-OFB (not CBC), RSA-SHA1 signature (not HMAC).\n"
			"The same operations are available as 'wszst WC24DECRYPT' and\n"
			"'wszst WC24ENCRYPT'.\n\n"
			"Usage:\n"
			"  %s selftest\n"
			"  %s wc24-decrypt <infile> <outfile> <key-hex32 | keyfile>\n"
			"  %s wc24-encrypt <infile> <outfile> <key-hex32 | keyfile> "
			"<rsa-private-key.pem> [iv-hex32 | ivfile]\n",
		prog, prog, prog);
}

int main (int argc, char **argv)
{
	if (argc < 2)
	{
		usage (argv[0]);
		return ERR_SYNTAX;
	}

	if (!strcmp (argv[1], "selftest"))
		return WC24SelfTest ();

	if (!strcmp (argv[1], "wc24-decrypt"))
	{
		if (argc != 5)
		{
			usage (argv[0]);
			return ERR_SYNTAX;
		}
		return WC24DecryptFile (argv[2], argv[3], argv[4]);
	}

	if (!strcmp (argv[1], "wc24-encrypt"))
	{
		if (argc != 6 && argc != 7)
		{
			usage (argv[0]);
			return ERR_SYNTAX;
		}
		return WC24EncryptFile (argv[2], argv[3], argv[4], argv[5], argc == 7 ? argv[6] : 0);
	}

	usage (argv[0]);
	return ERR_SYNTAX;
}

bool DefineIntVar (VarMap_t *vm, ccp varname, int value)
{
	return false;
}

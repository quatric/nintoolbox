#include "lib-aes.h"
#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static uint32_t be32 (const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int parse_hex16 (const char *hex, uint8_t out[16])
{
	for (int i = 0; i < 16; i++)
	{
		unsigned int hi, lo;
		if (sscanf (hex + 2 * i, "%1x", &hi) != 1 || sscanf (hex + 2 * i + 1, "%1x", &lo) != 1)
			return -1;
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return 0;
}

// Test parsing of keys.txt and verify common key and count
static int test_keys_txt (const char *tp_dir, uint8_t sample_key[16], char sample_title[256])
{
	char path[1024];
	snprintf (path, sizeof (path), "%s/keys.txt", tp_dir);

	FILE *f = fopen (path, "r");
	if (!f)
	{
		fprintf (stderr, "FAIL: cannot open %s\n", path);
		return -1;
	}

	int common_key_found = 0;
	int disc_key_count = 0;
	char line[512];

	while (fgets (line, sizeof (line), f))
	{
		char *p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '#' || *p == ';' || !*p)
			continue;

		char hex[33] = "";
		int hlen = 0;
		while (p[hlen] && isxdigit ((unsigned char)p[hlen]) && hlen < 32)
		{
			hex[hlen] = p[hlen];
			hlen++;
		}
		if (hlen != 32)
			continue;
		hex[32] = '\0';

		char *hash = strchr (p, '#');
		char comment[256] = "";
		if (hash)
		{
			char *c = hash + 1;
			while (*c == ' ' || *c == '\t')
				c++;
			char *endc = c + strlen (c);
			while (endc > c && (endc[-1] == '\r' || endc[-1] == '\n' || endc[-1] == ' ' || endc[-1] == '\t'))
				*--endc = '\0';
			snprintf (comment, sizeof (comment), "%s", c);
		}

		if (strcasecmp (hex, "D7B00402659BA2ABD2CB0DB27FA2B656") == 0)
			common_key_found = 1;
		else if (hash && strlen (comment) > 0)
		{
			disc_key_count++;
			if (disc_key_count == 10)
			{
				parse_hex16 (hex, sample_key);
				snprintf (sample_title, 256, "%s", comment);
			}
		}
	}
	fclose (f);

	if (!common_key_found)
	{
		fprintf (stderr, "FAIL: Wii U common key not found in %s\n", path);
		return -1;
	}
	if (disc_key_count < 492)
	{
		fprintf (stderr, "FAIL: expected >= 492 disc keys in %s, got %d\n", path, disc_key_count);
		return -1;
	}

	printf("  keys.txt: validated common key and %d retail disc keys\n", disc_key_count);
	return 0;
}

// Test wiiu_keys directory count and exact 16-byte file size
static int test_wiiu_keys_dir (const char *tp_dir)
{
	char dirpath[1024];
	snprintf (dirpath, sizeof (dirpath), "%s/wiiu_keys", tp_dir);

	DIR *d = opendir (dirpath);
	if (!d)
	{
		fprintf (stderr, "FAIL: cannot open directory %s\n", dirpath);
		return -1;
	}

	int key_count = 0;
	struct dirent *de;
	while ((de = readdir (d)))
	{
		if (de->d_name[0] == '.')
			continue;
		const size_t len = strlen (de->d_name);
		if (len < 5 || strcmp (de->d_name + len - 4, ".key") != 0)
			continue;

		char filepath[1024];
		snprintf (filepath, sizeof (filepath), "%s/%s", dirpath, de->d_name);
		struct stat st;
		if (stat (filepath, &st) != 0 || !S_ISREG (st.st_mode) || st.st_size != 16)
		{
			fprintf (stderr, "FAIL: invalid key file %s (size %lld != 16)\n", filepath, (long long)st.st_size);
			closedir (d);
			return -1;
		}
		key_count++;
	}
	closedir (d);

	if (key_count != 492)
	{
		fprintf (stderr, "FAIL: expected 492 key files in %s, got %d\n", dirpath, key_count);
		return -1;
	}

	printf("  wiiu_keys: validated 492 individual 16-byte .key files\n");
	return 0;
}

// Test TOC probing: encrypt a valid TOC block with sample_key, then probe against keys.txt
static int test_toc_probe (const char *tp_dir, const uint8_t sample_key[16])
{
	uint8_t plaintext[16] = { 0xcc, 0xa6, 0xe6, 0x7b, 0x00, 0x01, 0x02, 0x03,
							  0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b };
	uint8_t enc_toc[16];
	memcpy (enc_toc, plaintext, 16);

	aes128_ctx_t ctx;
	AES128_Init (&ctx, sample_key);
	AES128_EncryptBlock (&ctx, enc_toc);

	// Now probe keys.txt
	char path[1024];
	snprintf (path, sizeof (path), "%s/keys.txt", tp_dir);
	FILE *f = fopen (path, "r");
	if (!f)
		return -1;

	int matched = 0;
	uint8_t found_key[16] = { 0 };
	char line[512];

	while (fgets (line, sizeof (line), f))
	{
		char *p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '#' || *p == ';' || !*p)
			continue;

		char hex[33] = "";
		int hlen = 0;
		while (p[hlen] && isxdigit ((unsigned char)p[hlen]) && hlen < 32)
		{
			hex[hlen] = p[hlen];
			hlen++;
		}
		if (hlen != 32)
			continue;
		hex[32] = '\0';

		uint8_t cand_key[16];
		if (parse_hex16 (hex, cand_key) != 0)
			continue;

		aes128_ctx_t dctx;
		AES128_Init (&dctx, cand_key);
		uint8_t dec_block[16];
		memcpy (dec_block, enc_toc, 16);
		AES128_DecryptBlock (&dctx, dec_block);

		if (be32 (dec_block) == 0xcca6e67bu)
		{
			matched = 1;
			memcpy (found_key, cand_key, 16);
			break;
		}
	}
	fclose (f);

	if (!matched)
	{
		fprintf (stderr, "FAIL: TOC probe did not match sample key in keys.txt\n");
		return -1;
	}
	if (memcmp (found_key, sample_key, 16) != 0)
	{
		fprintf (stderr, "FAIL: TOC probe matched wrong key\n");
		return -1;
	}

	// Negative test: random unmapped key
	uint8_t bogus_key[16] = { 0xfe, 0xed, 0xfa, 0xce, 0x11, 0x22, 0x33, 0x44,
							  0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc };
	memcpy (enc_toc, plaintext, 16);
	AES128_Init (&ctx, bogus_key);
	AES128_EncryptBlock (&ctx, enc_toc);

	f = fopen (path, "r");
	if (!f)
		return -1;
	matched = 0;
	while (fgets (line, sizeof (line), f))
	{
		char *p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '#' || *p == ';' || !*p)
			continue;
		char hex[33] = "";
		int hlen = 0;
		while (p[hlen] && isxdigit ((unsigned char)p[hlen]) && hlen < 32)
		{
			hex[hlen] = p[hlen];
			hlen++;
		}
		if (hlen != 32)
			continue;
		hex[32] = '\0';
		uint8_t cand_key[16];
		if (parse_hex16 (hex, cand_key) != 0)
			continue;
		aes128_ctx_t dctx;
		AES128_Init (&dctx, cand_key);
		uint8_t dec_block[16];
		memcpy (dec_block, enc_toc, 16);
		AES128_DecryptBlock (&dctx, dec_block);
		if (be32 (dec_block) == 0xcca6e67bu)
		{
			matched = 1;
			break;
		}
	}
	fclose (f);

	if (matched)
	{
		fprintf (stderr, "FAIL: TOC probe false positive on unmapped key\n");
		return -1;
	}

	printf("  TOC probe: verified AES-128 TOC decryption match & false-positive rejection\n");
	return 0;
}

int main (int argc, char **argv)
{
	const char *tp_dir = argc > 1 ? argv[1] : "third_party";

	uint8_t sample_key[16];
	char sample_title[256] = "";

	if (test_keys_txt (tp_dir, sample_key, sample_title) != 0)
		return 1;
	if (test_wiiu_keys_dir (tp_dir) != 0)
		return 1;
	if (test_toc_probe (tp_dir, sample_key) != 0)
		return 1;

	return 0;
}

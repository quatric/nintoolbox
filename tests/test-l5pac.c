// Regression test for Level-5 / Armor Project PAC archive container (lib-l5pac.c).
#include "lib-std.h"
#include "lib-l5pac.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

static void test_l5pac_parse_synthetic (void)
{
	printf ("test_l5pac_parse_synthetic... ");

	u8 buf[512];
	memset (buf, 0, sizeof (buf));

	// Entry 1: hello.txt, size 13, alloc_size 0x60
	const u8 file1[] = "Hello world!\n";
	const u32 sz1 = (u32)sizeof (file1);
	memcpy (buf, "hello.txt\0", 10);
	write_le32 (buf + 0x40, 0x50);
	write_le32 (buf + 0x44, sz1);
	write_le32 (buf + 0x48, 0x60);
	memcpy (buf + 0x50, file1, sz1);

	// Entry 2: number.bin, size 4, alloc_size 0x60 (at offset 0x60)
	const u8 file2[] = { 1, 2, 3, 4 };
	const u32 sz2 = 4;
	memcpy (buf + 0x60, "number.bin\0", 11);
	write_le32 (buf + 0x60 + 0x40, 0x50);
	write_le32 (buf + 0x60 + 0x44, sz2);
	write_le32 (buf + 0x60 + 0x48, 0x60);
	memcpy (buf + 0x60 + 0x50, file2, sz2);

	// Terminal sentinel at offset 0xc0
	write_le32 (buf + 0xc0 + 0x40, 0x50);
	write_le32 (buf + 0xc0 + 0x44, 0xffffffff);
	write_le32 (buf + 0xc0 + 0x48, 0xffffffff);

	const u32 total_sz = 0xc0 + 0x50;

	assert (IsL5Pac (buf, total_sz, total_sz));

	l5pac_t pac;
	enumError err = L5PacParse (&pac, buf, total_sz, total_sz);
	assert (err == ERR_OK);
	assert (pac.n == 2);

	assert (!strcmp (pac.e[0].name, "hello.txt"));
	assert (pac.e[0].offset == 0x50);
	assert (pac.e[0].size == sz1);
	assert (pac.e[0].alloc_size == 0x60);

	assert (!strcmp (pac.e[1].name, "number.bin"));
	assert (pac.e[1].offset == 0xb0);
	assert (pac.e[1].size == sz2);
	assert (pac.e[1].alloc_size == 0x60);

	L5PacFree (&pac);

	// The advertised file size cannot substitute for missing buffer bytes.
	assert (L5PacParse (&pac, buf, 0xb0, total_sz) == ERR_INVALID_DATA);
	assert (pac.n == 0 && !pac.e);

	// A stride includes the header and must contain the entire payload.
	write_le32 (buf + 0x48, 0x50);
	assert (!IsL5Pac (buf, total_sz, total_sz));

	// A large stride must not wrap the next-header bounds check.
	write_le32 (buf + 0x48, 0xfffffff0);
	assert (IsL5Pac (buf, total_sz, 0));
	assert (L5PacParse (&pac, buf, total_sz, 0) == ERR_INVALID_DATA);
	assert (pac.n == 0 && !pac.e);
	printf ("ok\n");
}

int main (void)
{
	test_l5pac_parse_synthetic ();
	return 0;
}

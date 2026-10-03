// Regression test for Capcom CPAC multi-section archive container (lib-cpac.c).
#include "lib-std.h"
#include "lib-cpac.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

static void test_cpac_parse_synthetic (void)
{
	printf ("test_cpac_parse_synthetic... ");

	// Outer header: 32 bytes (4 sections)
	u8 buf[1024];
	memset (buf, 0, sizeof (buf));

	const u32 head_len = 32;
	write_le32 (buf, head_len);

	// Section 0: BKEY section (offset 32)
	// table_len = 48 (32 bytes header + 1 record of 16 bytes)
	const u32 sec0_off = head_len;
	const u32 sec0_tbl_len = 48;
	const u8 file1[] = "MEMBER_FILE_1";
	const u8 file2[] = "MEMBER_FILE_2";
	const u32 sz1 = sizeof (file1);
	const u32 sz2 = sizeof (file2);

	write_le32 (buf + sec0_off, 24);
	write_le32 (buf + sec0_off + 4, 2);
	write_le32 (buf + sec0_off + 8, 0x424b4559); // BKEY
	write_le32 (buf + sec0_off + 12, 24);
	write_le32 (buf + sec0_off + 16, 0x42444154); // BDAT
	write_le32 (buf + sec0_off + 20, sec0_tbl_len);

	// Record at offset 32 from section start
	write_le32 (buf + sec0_off + 32, 0);
	write_le32 (buf + sec0_off + 36, sz1 | 0x80000000); // with LZ11 bit
	write_le32 (buf + sec0_off + 40, sz1);
	write_le32 (buf + sec0_off + 44, sz2);

	memcpy (buf + sec0_off + sec0_tbl_len, file1, sz1);
	memcpy (buf + sec0_off + sec0_tbl_len + sz1, file2, sz2);

	const u32 sec0_sz = sec0_tbl_len + sz1 + sz2;
	write_le32 (buf + 4, sec0_sz);

	// Section 1: PKEY section (offset sec0_off + sec0_sz)
	const u32 sec1_off = sec0_off + sec0_sz;
	const u32 sec1_tbl_len = 40; // 32 header + 2 descriptors (8 bytes)

	write_le32 (buf + 8, sec1_off);
	write_le32 (buf + 12, sec1_tbl_len + 32 + 512);

	write_le32 (buf + sec1_off, 24);
	write_le32 (buf + sec1_off + 4, 2);
	write_le32 (buf + sec1_off + 8, 0x504b4559); // PKEY
	write_le32 (buf + sec1_off + 12, 24);
	write_le32 (buf + sec1_off + 16, 0x50444154); // PDAT
	write_le32 (buf + sec1_off + 20, sec1_tbl_len);

	// Descriptor 0: 16 colors (w0=0x1000), bank 0 (w1=0)
	write_le16 (buf + sec1_off + 32, 0x1000);
	write_le16 (buf + sec1_off + 34, 0);

	// Descriptor 1: 256 colors (w0=0x0100), bank 1 (w1=1)
	write_le16 (buf + sec1_off + 36, 0x0100);
	write_le16 (buf + sec1_off + 38, 1);

	const u32 total_sz = sec1_off + sec1_tbl_len + 32 + 512;

	assert (IsCpac (buf, total_sz, total_sz));
	assert (CpacHeadSize (buf, total_sz) == head_len);

	cpac_t cpac;
	enumError err = CpacParse (&cpac, buf, total_sz, total_sz);
	assert (err == ERR_OK);
	assert (cpac.n == 4);

	assert (cpac.e[0].section == 0 && cpac.e[0].is_lz11 == true && cpac.e[0].size == sz1);
	assert (cpac.e[1].section == 0 && cpac.e[1].is_lz11 == false && cpac.e[1].size == sz2);
	assert (cpac.e[2].section == 1 && cpac.e[2].is_palette == true && cpac.e[2].size == 32);
	assert (cpac.e[3].section == 1 && cpac.e[3].is_palette == true && cpac.e[3].size == 512);

	CpacFree (&cpac);

	// A zero file size uses the supplied buffer length.
	assert (CpacParse (&cpac, buf, total_sz, 0) == ERR_OK);
	assert (cpac.n == 4);
	CpacFree (&cpac);

	// Truncated section data must not produce entries outside the buffer.
	assert (CpacParse (&cpac, buf, sec1_off, total_sz) == ERR_OK);
	assert (cpac.n == 2);
	CpacFree (&cpac);

	// An overflowing section offset must be skipped without dereferencing it.
	write_le32 (buf + 8, 0xfffffff0);
	assert (CpacParse (&cpac, buf, total_sz, total_sz) == ERR_OK);
	assert (cpac.n == 2);
	CpacFree (&cpac);

	// A table may not extend beyond its own section.
	write_le32 (buf + sec0_off + 20, 0xfffffff0);
	assert (CpacParse (&cpac, buf, total_sz, total_sz) == ERR_NOTHING_TO_DO);
	CpacFree (&cpac);
	printf ("ok\n");
}

int main (void)
{
	test_cpac_parse_synthetic ();
	return 0;
}

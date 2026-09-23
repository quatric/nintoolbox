// Regression for the AFS reader (lib-afs.c). AFS is a Sega/CRI-style flat
// archive format reused by Spike Co.'s engine for Dragon Ball Z: Budokai
// Tenkaichi 3 (wzs3us0/1/2.afs on the retail disc). The layout below was
// confirmed against a retail sample by direct hexdump before writing the
// parser; the test builds an equivalent synthetic archive in memory instead
// of shipping copyrighted retail data in the repo.
#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-afs.h"
#include <string.h>

// Builds a minimal 2-entry AFS archive with a name/date metadata table,
// mirroring the structure observed in the retail sample.
static u8 *build_synthetic_afs (size_t *out_size)
{
	const uint n = 2;
	const u32 entry0_off = 0x800, entry0_size = 0x10;
	const u32 entry1_off = 0x1000, entry1_size = 0x20;
	const u32 meta_off = 0x1800;
	const u32 meta_size = n * 48;
	const u32 total = meta_off + meta_size;

	u8 *buf = CALLOC (1, total);
	memcpy (buf, "AFS\0", 4);
	wr_le32 (buf + 4, n);
	wr_le32 (buf + 8, entry0_off);
	wr_le32 (buf + 12, entry0_size);
	wr_le32 (buf + 16, entry1_off);
	wr_le32 (buf + 20, entry1_size);
	wr_le32 (buf + 24, meta_off);	// footer descriptor
	wr_le32 (buf + 28, meta_size);

	u8 *rec0 = buf + meta_off;
	strcpy ((char *)rec0, "boot_texture_WII_.d");
	wr_le16 (rec0 + 32, 2005);	// year
	wr_le16 (rec0 + 34, 10);	// month
	wr_le16 (rec0 + 36, 21);	// day

	u8 *rec1 = buf + meta_off + 48;
	strcpy ((char *)rec1, "loa");
	wr_le16 (rec1 + 32, 2005);
	wr_le16 (rec1 + 34, 11);
	wr_le16 (rec1 + 36, 7);

	*out_size = total;
	return buf;
}

static int check_synthetic_sample (void)
{
	size_t size = 0;
	u8 *data = build_synthetic_afs (&size);

	afs_t afs;
	enumError err = ScanAFS (&afs, data, size);
	if (err)
	{
		FREE (data);
		return 2;
	}

	if (afs.n_entries != 2)
	{
		ResetAFS (&afs);
		FREE (data);
		return 3;
	}

	static const struct { u32 offset, size; } expect[] = {
		{ 0x800, 0x10 },
		{ 0x1000, 0x20 },
	};
	for (uint i = 0; i < 2; i++)
	{
		if (afs.entries[i].offset != expect[i].offset || afs.entries[i].size != expect[i].size)
		{
			ResetAFS (&afs);
			FREE (data);
			return 10 + i;
		}
	}

	if (afs.meta_offset != 0x1800 || afs.meta_size != 96)
	{
		ResetAFS (&afs);
		FREE (data);
		return 20;
	}

	if (strcmp (afs.entries[0].name, "boot_texture_WII_.d") || afs.entries[0].year != 2005
		|| afs.entries[0].month != 10 || afs.entries[0].day != 21)
	{
		ResetAFS (&afs);
		FREE (data);
		return 21;
	}

	if (strcmp (afs.entries[1].name, "loa") || afs.entries[1].year != 2005
		|| afs.entries[1].month != 11 || afs.entries[1].day != 7)
	{
		ResetAFS (&afs);
		FREE (data);
		return 22;
	}

	ResetAFS (&afs);
	FREE (data);
	return 0;
}

static int check_rejects_garbage (void)
{
	static const u8 junk[16] = { 'J', 'U', 'N', 'K', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	afs_t afs;
	if (!ScanAFS (&afs, junk, sizeof (junk)))
	{
		ResetAFS (&afs);
		return 1;
	}
	return 0;
}

static int check_rejects_truncated_table (void)
{
	// Valid magic and a file count whose entry table runs past the buffer.
	static const u8 hdr[8] = { 'A', 'F', 'S', 0, 0x40, 0, 0, 0 }; // 0x40 entries, no data
	afs_t afs;
	if (!ScanAFS (&afs, hdr, sizeof (hdr)))
	{
		ResetAFS (&afs);
		return 1;
	}
	return 0;
}

int main (void)
{
	struct
	{
		ccp name;
		int (*fn) (void);
	} tests[] = {
		{ "AFS synthetic sample header/entry/metadata parse", check_synthetic_sample },
		{ "AFS rejects non-AFS input", check_rejects_garbage },
		{ "AFS rejects an out-of-bounds entry table", check_rejects_truncated_table },
	};
	int rc = 0;
	for (uint i = 0; i < sizeof (tests) / sizeof (*tests); i++)
	{
		int r = tests[i].fn ();
		printf ("%s %s (rc=%d)\n", r ? "FAIL" : "ok  ", tests[i].name, r);
		if (r)
			rc = 1;
	}
	return rc;
}

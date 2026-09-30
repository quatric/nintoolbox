// Regression tests for Monolith Soft Soma Bringer formats (.obp, .bgp, .dad, .pcs)
#include "lib-soma.h"
#include "lib-nintendo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef malloc
#undef calloc
#undef realloc
#undef free
#undef strdup

void trace_free (ccp f, ccp p, uint l, void *v)
{
	(void)f; (void)p; (void)l;
	free (v);
}
void *trace_malloc (ccp f, ccp p, uint l, size_t n)
{
	(void)f; (void)p; (void)l;
	return malloc (n);
}
void *trace_calloc (ccp f, ccp p, uint l, size_t n, size_t s)
{
	(void)f; (void)p; (void)l;
	return calloc (n, s);
}
void *trace_realloc (ccp f, ccp p, uint l, void *v, size_t n)
{
	(void)f; (void)p; (void)l;
	return realloc (v, n);
}
void dclib_free (void *v) { free (v); }
void *dclib_malloc (size_t n) { return malloc (n); }
void *dclib_calloc (size_t n, size_t s) { return calloc (n, s); }
void *dclib_realloc (void *v, size_t n) { return realloc (v, n); }
char *dclib_strdup (ccp s) { return s ? strdup (s) : 0; }

static int failures;
#define CHECK(expr)                                                                                \
	do                                                                                             \
	{                                                                                              \
		if (!(expr))                                                                               \
		{                                                                                          \
			fprintf (stderr, "FAIL line %d: %s\n", __LINE__, #expr);                               \
			failures++;                                                                            \
		}                                                                                          \
	} while (0)

int main (void)
{
	// 1. OBP1 test
	{
		u8 obp[48] = { 0 };
		memcpy (obp, "OBP1", 4);
		wr_le16 (obp + 4, 1);
		wr_le16 (obp + 6, 2);
		wr_le16 (obp + 8, 32);  // width
		wr_le16 (obp + 10, 32); // height
		wr_le16 (obp + 12, 16);
		wr_le16 (obp + 14, 32); // palette bytes = 16 colors
		CHECK (IsSomaObp (obp, sizeof (obp), sizeof (obp)));
		CHECK (DecodeSomaObp_Text (stdout, obp, sizeof (obp), sizeof (obp)) == ERR_OK);

		// Negative checks
		CHECK (!IsSomaObp (0, 0, 0));
		CHECK (!IsSomaObp (obp, 10, 10));
		obp[0] = 'X';
		CHECK (!IsSomaObp (obp, sizeof (obp), sizeof (obp)));
	}

	// 2. BGP1 test
	{
		u8 bgp[48] = { 0 };
		memcpy (bgp, "BGP1", 4);
		wr_le32 (bgp + 4, sizeof (bgp));
		wr_le16 (bgp + 8, 256);
		wr_le16 (bgp + 10, 192);
		wr_le32 (bgp + 12, 0x1234);
		wr_le32 (bgp + 16, 24);
		wr_le32 (bgp + 20, 32);
		CHECK (IsSomaBgp (bgp, sizeof (bgp), sizeof (bgp)));
		CHECK (DecodeSomaBgp_Text (stdout, bgp, sizeof (bgp), sizeof (bgp)) == ERR_OK);

		// Negative checks
		CHECK (!IsSomaBgp (0, 0, 0));
		CHECK (!IsSomaBgp (bgp, 10, 10));
		wr_le32 (bgp + 4, 1000);
		CHECK (!IsSomaBgp (bgp, sizeof (bgp), sizeof (bgp)));
	}

	// 3. DAD decompression test (synthetic stream)
	{
		// Construct synthetic DAD payload:
		// "DAD\x01" + uncompressed size (20)
		// Flag byte: 0b00000011 -> 2 literals, then 1 match:
		// literals: 'A', 'B'
		// match: dist = 2, length = 6 -> repeats "ABABABAB"
		u8 dad_buf[64] = { 0 };
		memcpy (dad_buf, "DAD\x01", 4);
		wr_le32 (dad_buf + 4, 8); // uncompressed size = 8
		// Stream:
		// byte 8: flag byte = 0x03 (bits: 1, 1, 0, ...)
		// byte 9: 'A'
		// byte 10: 'B'
		// byte 11: b1 = 2 (dist = 2)
		// byte 12: b2 = 3 (length = 3 + 3 = 6) -> 2 literals + 6 = 8 bytes total!
		dad_buf[8] = 0x03;
		dad_buf[9] = 'A';
		dad_buf[10] = 'B';
		dad_buf[11] = 0x02;
		dad_buf[12] = 0x03;

		CHECK (IsSomaDad (dad_buf, 13));

		u8 out[16] = { 0 };
		CHECK (DecompressSomaDad (out, sizeof (out), dad_buf, 13) == ERR_OK);
		CHECK (!memcmp (out, "ABABABAB", 8));

		u8 *alloc_out = 0;
		size_t alloc_sz = 0;
		CHECK (DecompressSomaDad_Alloc (&alloc_out, &alloc_sz, dad_buf, 13) == ERR_OK);
		CHECK (alloc_sz == 8);
		CHECK (!memcmp (alloc_out, "ABABABAB", 8));
		free (alloc_out);

		// Truncation / corruption tests
		CHECK (DecompressSomaDad (out, 4, dad_buf, 13) == ERR_INVALID_DATA); // buffer too small
		CHECK (DecompressSomaDad (out, sizeof (out), dad_buf, 10) == ERR_INVALID_DATA); // truncated input
		CHECK (!IsSomaDad (0, 0));
	}

	// 4. PCS layout sequence test
	{
		u8 pcs[128] = { 0 };
		memcpy (pcs, "pcs\0", 4);
		wr_le32 (pcs + 4, sizeof (pcs));
		wr_le32 (pcs + 8, 1); // 1 component
		wr_le32 (pcs + 16, 32); // offset 32 to component

		// Component at offset 32
		u8 *pcn = pcs + 32;
		memcpy (pcn, "pcn\0", 4);
		wr_le32 (pcn + 16, 96);
		wr_le16 (pcn + 30, 1); // 1 track
		wr_le32 (pcn + 32, 48); // relative offset 48 -> offset 80 for track

		// Track at offset 80
		u8 *trk = pcs + 80;
		memcpy (trk, "pos\0", 4);
		wr_le32 (trk + 4, 48);
		wr_le32 (trk + 12, 2); // 2 keyframes
		// keyframe 0: frame=0, x=10 (40960), y=0, z=0
		wr_le32 (trk + 16, 0);
		wr_le32 (trk + 20, 40960);
		// keyframe 1: frame=5 (20480), x=20 (81920), y=0, z=0
		wr_le32 (trk + 32, 20480);
		wr_le32 (trk + 36, 81920);

		CHECK (IsSomaPcs (pcs, sizeof (pcs)));
		CHECK (DecodeSomaPcs_Text (stdout, pcs, sizeof (pcs)) == ERR_OK);

		// Negative checks
		CHECK (!IsSomaPcs (0, 0));
		CHECK (!IsSomaPcs (pcs, 12));
		pcs[0] = 'z';
		CHECK (!IsSomaPcs (pcs, sizeof (pcs)));
	}

	printf ("SOMA regressions: %d failures\n", failures);
	return failures != 0;
}

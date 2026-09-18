#include "lib-nintendo.h"
#include "lib-effn.h"
#include "lib-pctl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void trace_free (const char *func, const char *file, unsigned int line, void *ptr);
extern void *trace_malloc (const char *func, const char *file, unsigned int line, size_t size);
extern void *trace_calloc (
	const char *func, const char *file, unsigned int line, size_t nmemb, size_t size);
#define free(p) trace_free (__FUNCTION__, __FILE__, __LINE__, (p))
#define malloc(s) trace_malloc (__FUNCTION__, __FILE__, __LINE__, (s))
#define calloc(n, s) trace_calloc (__FUNCTION__, __FILE__, __LINE__, (n), (s))

// Constructs a synthetic minimal valid VFXB (.ptcl) archive
static u8 *build_synthetic_ptcl (size_t *out_size)
{
	size_t total = 512;
	u8 *buf = (u8 *)calloc (total, 1);

	// Header (32 bytes)
	memcpy (buf, "VFXB", 4);
	wr_le32 (buf + 4, 0);       // padding
	wr_le16 (buf + 8, 1);       // graphics_api_version
	wr_le16 (buf + 10, 22);     // vfx_version
	wr_le16 (buf + 12, 0xFEFF); // BOM
	buf[14] = 12;               // alignment
	buf[15] = 64;               // target_offset
	wr_le32 (buf + 16, 32);     // header_size
	wr_le16 (buf + 20, 0);      // flag
	wr_le16 (buf + 22, 64);     // block_offset
	wr_le32 (buf + 24, 0);      // padding2
	wr_le32 (buf + 28, (u32)total); // file_size

	// 32-byte Name at offset 32
	snprintf ((char *)buf + 32, 32, "TestParticleSystem");

	// Section at offset 64: ESTA (Emitter List)
	u8 *esta = buf + 64;
	memcpy (esta, "ESTA", 4);
	wr_le32 (esta + 4, 32);       // section_size
	wr_le32 (esta + 8, 32);       // subsection_offset -> offset 96 (ESET)
	wr_le32 (esta + 12, 0xFFFFFFFF); // next_section_offset = none
	wr_le32 (esta + 20, 0xFFFFFFFF); // binary_data_offset = none
	wr_le32 (esta + 28, 1);       // subsection_count = 1 ESET

	// Child section at offset 96: ESET (Emitter Set)
	u8 *eset = buf + 96;
	memcpy (eset, "ESET", 4);
	wr_le32 (eset + 4, 32);       // section_size
	wr_le32 (eset + 8, 128);      // subsection_offset -> offset 224 (EMTR)
	wr_le32 (eset + 12, 0xFFFFFFFF); // next_section_offset = none
	wr_le32 (eset + 20, 32);      // binary_data_offset -> offset 128 (payload)
	wr_le32 (eset + 28, 1);       // subsection_count = 1 EMTR

	// ESET binary data at offset 128 (64 bytes name at +16)
	u8 *eset_bin = buf + 128;
	snprintf ((char *)eset_bin + 16, 64, "FireEmitterSet");
	wr_le32 (eset_bin + 80, 1); // emitter count

	// Child section at offset 224: EMTR (Emitter)
	u8 *emtr = buf + 224;
	memcpy (emtr, "EMTR", 4);
	wr_le32 (emtr + 4, 96);       // section_size
	wr_le32 (emtr + 8, 0xFFFFFFFF);  // subsection_offset = none
	wr_le32 (emtr + 12, 0xFFFFFFFF); // next_section_offset = none
	wr_le32 (emtr + 20, 32);      // binary_data_offset -> offset 256
	wr_le32 (emtr + 28, 0);

	// EMTR binary data at offset 256
	u8 *emtr_bin = buf + 256;
	wr_le32 (emtr_bin, 0x1);      // flag
	wr_le32 (emtr_bin + 4, 12345);// random seed
	snprintf ((char *)emtr_bin + 16, 64, "SparksEmitter");

	*out_size = total;
	return buf;
}

// Constructs a synthetic valid EFFN archive wrapping the ptcl
static u8 *build_synthetic_effn (const u8 *ptcl, size_t ptcl_size, size_t *out_size)
{
	size_t align = 4096;
	size_t total = align + ptcl_size;
	u8 *buf = (u8 *)calloc (total, 1);

	// Header (16 bytes)
	memcpy (buf, "EFFN", 4);
	wr_le32 (buf + 4, 131072); // version
	wr_le16 (buf + 8, 1);      // num_effects
	wr_le16 (buf + 10, 1);     // num_external_models
	wr_le16 (buf + 12, 1);     // multi_part_effects
	wr_le16 (buf + 14, 1);     // header_chunk_align (4096)

	// Entry 0 (16 bytes at offset 16)
	u8 *ent = buf + 16;
	wr_le16 (ent, 1);          // kind
	wr_le16 (ent + 2, 0);      // unknown
	wr_le32 (ent + 4, 200);    // emitter_set_id
	wr_le32 (ent + 8, 1);      // external_model_idx (1-based)
	wr_le16 (ent + 12, 1);     // variant_start_idx (1-based)
	wr_le16 (ent + 14, 1);     // variant_count

	// Variant 0 (4 bytes at offset 32)
	u8 *var = buf + 32;
	wr_le16 (var, 10);         // start_frame
	wr_le16 (var + 2, 201);    // emitter_set_id

	// Model flag 0 (1 byte at offset 36)
	buf[36] = 5;

	// Strings starting at offset 37:
	// entry_names[0] = "ef_smash_fire"
	// model_names[0] = "smash_sword"
	// bone_names[0]  = "arm_r"
	u8 *str = buf + 37;
	strcpy ((char *)str, "ef_smash_fire");
	str += strlen ("ef_smash_fire") + 1;
	strcpy ((char *)str, "smash_sword");
	str += strlen ("smash_sword") + 1;
	strcpy ((char *)str, "arm_r");
	str += strlen ("arm_r") + 1;

	// Append PTCL at aligned offset 4096
	memcpy (buf + align, ptcl, ptcl_size);

	*out_size = total;
	return buf;
}

int main (void)
{
	printf ("Running test-effn-ptcl...\n");

	size_t ptcl_size = 0;
	u8 *ptcl = build_synthetic_ptcl (&ptcl_size);
	if (!ptcl || !ptcl_size)
	{
		printf ("FAIL: failed to build synthetic ptcl\n");
		return 1;
	}

	if (!IsPCTL (ptcl, ptcl_size))
	{
		printf ("FAIL: IsPCTL returned false\n");
		return 2;
	}

	size_t effn_size = 0;
	u8 *effn = build_synthetic_effn (ptcl, ptcl_size, &effn_size);
	if (!effn || !effn_size)
	{
		printf ("FAIL: failed to build synthetic effn\n");
		return 3;
	}

	if (!IsEFFN (effn, effn_size))
	{
		printf ("FAIL: IsEFFN returned false\n");
		return 4;
	}

	// Test DecodeEFFN_Text
	char txt_buf[4096] = {0};
	FILE *mf = fmemopen (txt_buf, sizeof (txt_buf), "w");
	if (mf)
	{
		enumError err = DecodeEFFN_Text (mf, effn, effn_size);
		fclose (mf);
		if (err)
		{
			printf ("FAIL: DecodeEFFN_Text failed: %d\n", err);
			return 5;
		}
		if (!strstr (txt_buf, "ef_smash_fire") || !strstr (txt_buf, "smash_sword") ||
			!strstr (txt_buf, "arm_r") || !strstr (txt_buf, "TestParticleSystem"))
		{
			printf ("FAIL: DecodeEFFN_Text missing expected strings:\n%s\n", txt_buf);
			return 6;
		}
	}

	// Write synthetic files to /tmp
	FILE *f1 = fopen ("/tmp/test_sample.ptcl", "wb");
	fwrite (ptcl, 1, ptcl_size, f1);
	fclose (f1);

	FILE *f2 = fopen ("/tmp/test_sample.eff", "wb");
	fwrite (effn, 1, effn_size, f2);
	fclose (f2);

	// Test ExtractEFFNArchive
	enumError err = ExtractEFFNArchive ("/tmp/test_sample.eff", "/tmp/test_eff_d");
	if (err)
	{
		printf ("FAIL: ExtractEFFNArchive failed: %d\n", err);
		return 7;
	}

	// Verify NamcoFile.json and particle.ptcl exist
	FILE *check_j = fopen ("/tmp/test_eff_d/NamcoFile.json", "r");
	if (!check_j)
	{
		printf ("FAIL: NamcoFile.json was not created\n");
		return 8;
	}
	char jcontent[4096] = {0};
	fread (jcontent, 1, sizeof (jcontent) - 1, check_j);
	fclose (check_j);

	if (!strstr (jcontent, "ef_smash_fire") || !strstr (jcontent, "smash_sword") ||
		!strstr (jcontent, "arm_r") || !strstr (jcontent, "\"EmitterSet_ID\": 200"))
	{
		printf ("FAIL: NamcoFile.json content mismatch:\n%s\n", jcontent);
		return 9;
	}

	FILE *check_p = fopen ("/tmp/test_eff_d/particle.ptcl", "rb");
	if (!check_p)
	{
		printf ("FAIL: particle.ptcl was not created\n");
		return 10;
	}
	fclose (check_p);

	// Test CreateEFFNArchive roundtrip
	err = CreateEFFNArchive ("/tmp/test_eff_d", "/tmp/test_repack.eff");
	if (err)
	{
		printf ("FAIL: CreateEFFNArchive failed: %d\n", err);
		return 11;
	}

	FILE *repack_f = fopen ("/tmp/test_repack.eff", "rb");
	if (!repack_f)
	{
		printf ("FAIL: repack.eff not created\n");
		return 12;
	}
	u8 repack_hdr[16];
	fread (repack_hdr, 1, 16, repack_f);
	fclose (repack_f);

	if (memcmp (repack_hdr, "EFFN", 4) != 0)
	{
		printf ("FAIL: repacked file is missing EFFN magic\n");
		return 13;
	}

	// Test ExtractPCTLArchive
	err = ExtractPCTLArchive ("/tmp/test_sample.ptcl", "/tmp/test_ptcl_d");
	if (err)
	{
		printf ("FAIL: ExtractPCTLArchive failed: %d\n", err);
		return 14;
	}

	FILE *check_hdr = fopen ("/tmp/test_ptcl_d/PtclHeader.txt", "r");
	if (!check_hdr)
	{
		printf ("FAIL: PtclHeader.txt was not created\n");
		return 15;
	}
	char hdr_str[1024] = {0};
	fread (hdr_str, 1, sizeof (hdr_str) - 1, check_hdr);
	fclose (check_hdr);

	if (!strstr (hdr_str, "TestParticleSystem"))
	{
		printf ("FAIL: PtclHeader.txt missing archive name:\n%s\n", hdr_str);
		return 16;
	}

	FILE *check_emtr = fopen ("/tmp/test_ptcl_d/FireEmitterSet/SparksEmitter/EmitterData.bin", "rb");
	if (!check_emtr)
	{
		printf ("FAIL: EmitterData.bin was not created in hierarchy\n");
		return 17;
	}
	fclose (check_emtr);

	// Test CreatePCTLArchive
	err = CreatePCTLArchive ("/tmp/test_ptcl_d", "/tmp/test_repack.ptcl");
	if (err)
	{
		printf ("FAIL: CreatePCTLArchive failed: %d\n", err);
		return 18;
	}

	free (ptcl);
	free (effn);

	printf ("RESULT: ok (all EFFN and PCTL tests passed)\n");
	return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <glob.h>

#include "lib-cvspr.h"

#undef malloc
#undef free

static int test_synthetic_sprite(void)
{
	printf("Testing synthetic Castlevania sprite generation and decode...\n");

	// Build a valid synthetic sprite file
	u8 buf[256];
	memset(buf, 0, sizeof(buf));

	u32 part_off = 0x30;
	u32 hit_off = part_off + 16;   // 1 part
	u32 frame_off = hit_off + 8;   // 1 hitbox
	u32 fdelay_off = frame_off + 12; // 1 frame
	u32 anim_off = fdelay_off + 8; // 1 delay
	u32 footer_off = anim_off + 8; // 1 anim
	u32 file_sz = footer_off + 16; // 16 bytes footer = 0x30 + 16 + 8 + 12 + 8 + 8 + 16 = 0x72 = 114 bytes

	// Header (LE)
	buf[0] = 0x0d; buf[1] = 0xf0; buf[2] = 0xef; buf[3] = 0xbe;
	*(u32 *)(buf + 4) = part_off;
	*(u32 *)(buf + 8) = hit_off;
	*(u32 *)(buf + 12) = frame_off;
	*(u32 *)(buf + 16) = fdelay_off;
	*(u32 *)(buf + 20) = anim_off;
	*(u32 *)(buf + 32) = footer_off;
	*(u32 *)(buf + 36) = 1; // 1 frame
	*(u32 *)(buf + 40) = 1; // 1 anim
	*(u32 *)(buf + 44) = file_sz;

	// Part 0
	*(s16 *)(buf + part_off + 0) = -16;
	*(s16 *)(buf + part_off + 2) = -16;
	*(u16 *)(buf + part_off + 4) = 0;  // gx
	*(u16 *)(buf + part_off + 6) = 0;  // gy
	*(u16 *)(buf + part_off + 8) = 32; // w
	*(u16 *)(buf + part_off + 10) = 32; // h
	buf[part_off + 12] = 0; // page
	buf[part_off + 13] = 0; // flip
	buf[part_off + 14] = 0; // pal

	// Hitbox 0
	*(s16 *)(buf + hit_off + 0) = -8;
	*(s16 *)(buf + hit_off + 2) = -8;
	*(u16 *)(buf + hit_off + 4) = 16;
	*(u16 *)(buf + hit_off + 6) = 16;

	// Frame 0
	*(u16 *)(buf + frame_off + 0) = 0;
	buf[frame_off + 2] = 1; // 1 hitbox
	buf[frame_off + 3] = 1; // 1 part
	*(u32 *)(buf + frame_off + 4) = 0; // first hitbox offset
	*(u32 *)(buf + frame_off + 8) = 0; // first part offset

	// FrameDelay 0
	*(u16 *)(buf + fdelay_off + 0) = 0; // frame index
	*(u16 *)(buf + fdelay_off + 2) = 5; // delay
	*(u32 *)(buf + fdelay_off + 4) = 0;

	// Animation 0
	*(u32 *)(buf + anim_off + 0) = 1; // 1 frame
	*(u32 *)(buf + anim_off + 4) = 0; // first delay offset

	// Verification
	assert(IsCastlevaniaSprite(buf, file_sz, file_sz) == true);
	assert(IsCastlevaniaSprite(buf, file_sz, file_sz + 1) == false);
	assert(IsCastlevaniaSprite(buf, 20, 20) == false);

	// Test text decode
	FILE *tmp = tmpfile();
	assert(tmp != NULL);
	enumError err = DecodeCastlevaniaSprite_Text(tmp, buf, file_sz);
	assert(err == ERR_OK);

	rewind(tmp);
	char line[256];
	bool found_magic = false;
	bool found_part = false;
	while (fgets(line, sizeof(line), tmp))
	{
		if (strstr(line, "0xBEEFF00D"))
			found_magic = true;
		if (strstr(line, "[[part]]"))
			found_part = true;
	}
	fclose(tmp);
	assert(found_magic == true);
	assert(found_part == true);

	printf("Synthetic test passed!\n");
	return 0;
}

static int test_retail_corpus(const char *pattern)
{
	glob_t g;
	if (glob(pattern, 0, NULL, &g) != 0)
		return 0;

	int tested = 0;
	for (size_t i = 0; i < g.gl_pathc; i++)
	{
		FILE *f = fopen(g.gl_pathv[i], "rb");
		if (!f) continue;
		fseek(f, 0, SEEK_END);
		long sz = ftell(f);
		rewind(f);

		u8 *mem = (u8 *)malloc(sz);
		assert(mem != NULL);
		size_t rd = fread(mem, 1, sz, f);
		fclose(f);
		assert(rd == (size_t)sz);

		if (IsCastlevaniaSprite(mem, sz, sz))
		{
			FILE *nullf = tmpfile();
			enumError err = DecodeCastlevaniaSprite_Text(nullf, mem, sz);
			assert(err == ERR_OK);
			fclose(nullf);
			tested++;
		}
		free(mem);
	}
	globfree(&g);
	return tested;
}

int main(void)
{
	test_synthetic_sprite();

	int dos_tested = test_retail_corpus("/tmp/dos/extracted/files/so/*.dat");
	if (dos_tested > 0)
		printf("Verified %d retail Dawn of Sorrow sprite files cleanly.\n", dos_tested);

	int ooe_tested = test_retail_corpus("/tmp/ooe/extracted/files/so/*.dat");
	if (ooe_tested > 0)
		printf("Verified %d retail Order of Ecclesia sprite files cleanly.\n", ooe_tested);

	printf("Castlevania Sprite tests: ALL PASSED\n");
	return 0;
}

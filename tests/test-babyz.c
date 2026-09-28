// Regression for Imagine: Party Babyz's "!Ce" LZSS container and the
// .wsp/.msk formats it wraps (lib-babyz.c). All test data is synthetic --
// see lib-babyz.h for how each layout was reverse-engineered against the
// retail disc's main.dol and real sample files.
#include "lib-std.h"
#include "lib-babyz.h"
#include "lib-image.h"
#include <string.h>
#include <stdio.h>

// Builds an all-literal LZSS bitstream for 'plain' (control bytes 0xff,
// i.e. every bit says "next byte is a literal") -- the simplest valid
// encoding, sufficient to exercise the decoder's literal path plus the
// control-byte refill logic.
static u8 *lzss_encode_literal (const u8 *plain, uint plain_size, uint *out_size)
{
	uint n_ctrl = (plain_size + 7) / 8;
	u8 *out = MALLOC (n_ctrl + plain_size);
	uint o = 0, p = 0;
	while (p < plain_size)
	{
		out[o++] = 0xff;
		for (uint k = 0; k < 8 && p < plain_size; k++)
			out[o++] = plain[p++];
	}
	*out_size = o;
	return out;
}

static u8 *build_container (const u8 *plain, uint plain_size, uint *out_size)
{
	uint comp_size;
	u8 *comp = lzss_encode_literal (plain, plain_size, &comp_size);

	u8 *buf = MALLOC (12 + comp_size);
	buf[0] = 0x21;
	buf[1] = 0x43;
	buf[2] = 0x65;
	buf[3] = 0x87;
	write_le32 (buf + 4, comp_size);
	write_le32 (buf + 8, plain_size);
	memcpy (buf + 12, comp, comp_size);
	FREE (comp);

	*out_size = 12 + comp_size;
	return buf;
}

static int check_roundtrip (void)
{
	static const char text[]
		= "// Items : Dummies, Trigzones, Splines, Cameras\nGENERATED FILE, DO NOT EDIT.\n";
	uint plain_size = (uint)strlen (text);

	uint file_size;
	u8 *file = build_container ((const u8 *)text, plain_size, &file_size);

	if (!IsBabyzWiz (file, file_size, file_size))
	{
		FREE (file);
		return 1;
	}
	if (GetDecompressedSizeBabyzWiz (file, file_size) != plain_size)
	{
		FREE (file);
		return 1;
	}

	u8 *dec = MALLOC (plain_size);
	size_t written = 0;
	enumError err = DecompressBabyzWiz (file, file_size, dec, plain_size, &written, "test", false);
	int rc = err != ERR_OK || written != plain_size || memcmp (dec, text, plain_size) != 0;

	FREE (dec);
	FREE (file);
	return rc;
}

static int check_rejects_garbage (void)
{
	static const u8 garbage[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
	return IsBabyzWiz (garbage, sizeof (garbage), sizeof (garbage)) != 0;
}

static int check_rejects_truncated (void)
{
	// Valid magic, but the announced compressed size runs past the file.
	u8 hdr[12] = { 0x21, 0x43, 0x65, 0x87 };
	write_le32 (hdr + 4, 0x10000); // comp_size, way bigger than the file
	write_le32 (hdr + 8, 0x10000); // decomp_size
	return IsBabyzWiz (hdr, sizeof (hdr), sizeof (hdr)) != 0;
}

// A decompressed .wsp payload: GX header (format/width/height + 0x14
// bytes reserved) followed by IMG_RGB565-tiled pixel data (4x4 blocks,
// 16 bits/pixel -- one full tile's worth is the smallest valid image).
static int check_wsp (void)
{
	const u32 format = IMG_RGB565, width = 4, height = 4;
	uint img_size = 0;
	CalcImageGeometry ((image_format_t)format, width, height, 0, 0, 0, 0, &img_size);

	uint dec_size = 0x20 + img_size;
	u8 *dec = CALLOC (dec_size, 1);
	write_le32 (dec, format);
	write_le32 (dec + 4, width);
	write_le32 (dec + 8, height);
	for (uint i = 0; i < img_size; i++)
		dec[0x20 + i] = (u8)(i * 37); // arbitrary but deterministic pixel bytes

	u32 rf, rw, rh;
	int rc = !IsBabyzWsp (dec, dec_size, &rf, &rw, &rh);
	rc |= rf != format || rw != width || rh != height;

	char tmp[] = "/tmp/babyz_test_wsp_XXXXXX.png";
	int fd = mkstemps (tmp, 4);
	if (fd >= 0)
		close (fd);
	rc |= ExportBabyzWspPng (dec, dec_size, tmp) != ERR_OK;
	unlink (tmp);

	FREE (dec);
	return rc;
}

// A decompressed .msk payload: u32 width, u32 height, 16 reserved bytes,
// then a width*height 0/1 mask (GX IMG_I8 tiled, 8x4 blocks).
static int check_msk (void)
{
	const u32 width = 8, height = 4; // exactly one I8 tile, no padding
	uint dec_size = 24 + width * height;
	u8 *dec = CALLOC (dec_size, 1);
	write_le32 (dec, width);
	write_le32 (dec + 4, height);
	for (u32 i = 0; i < width * height; i++)
		dec[24 + i] = i & 1;

	u32 rw, rh;
	int rc = !IsBabyzMsk (dec, dec_size, &rw, &rh);
	rc |= rw != width || rh != height;

	char tmp[] = "/tmp/babyz_test_msk_XXXXXX.png";
	int fd = mkstemps (tmp, 4);
	if (fd >= 0)
		close (fd);
	rc |= ExportBabyzMskPng (dec, dec_size, tmp) != ERR_OK;
	unlink (tmp);

	FREE (dec);
	return rc;
}

static int check_msk_rejects_wrong_size (void)
{
	u8 dec[24 + 10]; // announces 8x8=64 bytes of mask but only has 10
	memset (dec, 0, sizeof (dec));
	write_le32 (dec, 8);
	write_le32 (dec + 4, 8);
	return IsBabyzMsk (dec, sizeof (dec), 0, 0) != 0;
}

// A .cam payload: BE u32 count, then count 3x4 BE-float matrices.
static int check_cam (void)
{
	const u32 count = 2;
	uint size = 4 + count * 48;
	u8 *data = CALLOC (size, 1);
	write_be32 (data, count);
	for (u32 k = 0; k < count * 12; k++)
		write_bef4 (data + 4 + k * 4, (float)k * 0.5f - 3.0f);

	u32 rc_count;
	int rc = !IsBabyzCam (data, size, &rc_count);
	rc |= rc_count != count;

	char *buf = 0;
	size_t buf_size = 0;
	FILE *f = open_memstream (&buf, &buf_size);
	rc |= DecodeBabyzCam_Text (f, data, size) != ERR_OK;
	fclose (f);
	rc |= !buf || !strstr (buf, "count = 2") || !strstr (buf, "keyframe[1]");
	if (buf)
		RegisterAlloc (__FUNCTION__, __FILE__, __LINE__, buf, buf_size, false);
	FREE (buf);

	FREE (data);
	return rc;
}

static int check_cam_rejects_wrong_size (void)
{
	u8 data[4 + 10]; // announces 1 keyframe (48 bytes) but only has 10
	memset (data, 0, sizeof (data));
	write_be32 (data, 1);
	return IsBabyzCam (data, sizeof (data), 0) != 0;
}

// A .snd payload: 64-byte records, 60-byte NUL-padded ASCII name + a
// constant 4-byte tag.
static int check_snd (void)
{
	static const char *names[] = { "baby-minijeu_doudou1", "080_baby_kiss3" };
	const u32 count = 2;
	u8 *dec = CALLOC (count * 64, 1);
	for (u32 i = 0; i < count; i++)
	{
		u8 *rec = dec + i * 64;
		strcpy ((char *)rec, names[i]);
		write_be32 (rec + 60, 0x30000000);
	}

	u32 rc_count;
	int rc = !IsBabyzSnd (dec, count * 64, &rc_count);
	rc |= rc_count != count;

	char *buf = 0;
	size_t buf_size = 0;
	FILE *f = open_memstream (&buf, &buf_size);
	rc |= DecodeBabyzSnd_Text (f, dec, count * 64) != ERR_OK;
	fclose (f);
	rc |= !buf || !strstr (buf, "baby-minijeu_doudou1") || !strstr (buf, "080_baby_kiss3");
	if (buf)
		RegisterAlloc (__FUNCTION__, __FILE__, __LINE__, buf, buf_size, false);
	FREE (buf);

	FREE (dec);
	return rc;
}

static int check_snd_rejects_non_ascii (void)
{
	u8 dec[64];
	memset (dec, 0, sizeof (dec));
	dec[0] = 0xff; // not printable ASCII
	return IsBabyzSnd (dec, sizeof (dec), 0) != 0;
}

static int check_snd_rejects_unpadded_tail (void)
{
	u8 dec[64];
	memset (dec, 0, sizeof (dec));
	strcpy ((char *)dec, "ok");
	dec[10] = 'x'; // garbage after the NUL, before the tag -- not zero-padded
	return IsBabyzSnd (dec, sizeof (dec), 0) != 0;
}

// A .spt payload: BE u32 count, then count opaque 74-byte records.
static int check_spt (void)
{
	const u32 count = 3;
	uint size = 4 + count * 74;
	u8 *dec = CALLOC (size, 1);
	write_be32 (dec, count);

	u32 rc_count;
	int rc = !IsBabyzSpt (dec, size, &rc_count);
	rc |= rc_count != count;

	FREE (dec);
	return rc;
}

static int check_spt_rejects_wrong_size (void)
{
	u8 dec[4 + 10]; // announces 1 record (74 bytes) but only has 10
	memset (dec, 0, sizeof (dec));
	write_be32 (dec, 1);
	return IsBabyzSpt (dec, sizeof (dec), 0) != 0;
}

int main (void)
{
	struct
	{
		ccp name;
		int (*fn) (void);
	} tests[] = {
		{ "Babyz \"!Ce\" LZSS round-trip", check_roundtrip },
		{ "Babyz rejects non-container input", check_rejects_garbage },
		{ "Babyz rejects a truncated container", check_rejects_truncated },
		{ "Babyz .wsp GX texture parse + PNG export", check_wsp },
		{ "Babyz .msk GX-I8 mask parse + PNG export", check_msk },
		{ "Babyz .msk rejects a short mask", check_msk_rejects_wrong_size },
		{ "Babyz .cam keyframe track parse + text dump", check_cam },
		{ "Babyz .cam rejects a short track", check_cam_rejects_wrong_size },
		{ "Babyz .snd cue table parse + text dump", check_snd },
		{ "Babyz .snd rejects non-ASCII name", check_snd_rejects_non_ascii },
		{ "Babyz .snd rejects non-zero padding", check_snd_rejects_unpadded_tail },
		{ "Babyz .spt parameter table parse", check_spt },
		{ "Babyz .spt rejects a short table", check_spt_rejects_wrong_size },
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

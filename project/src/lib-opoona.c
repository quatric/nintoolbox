// SPDX-License-Identifier: GPL-2.0+
// Opoona (Wii, ArtePiazza) character animation formats: the .mol per-character
// manifest and the .mot skeletal-animation clip.
//
// No public documentation of these formats exists anywhere (XeNTaX, GBAtemp,
// Models-Resource and romhacking.net were all checked and are empty on this
// title). Everything here was reverse-engineered from scratch against the
// retail USA disc's own character/ files -- see lib-opoona.h for exactly
// what is and is not understood.

#include "lib-opoona.h"
#include "lib-nintendo.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

//-----------------------------------------------------------------------------
///////////////////////////////   .mol manifest   ////////////////////////////
//-----------------------------------------------------------------------------

// Record layout (32 bytes, confirmed byte-exact against on-disk sibling
// .mot file sizes for m003/m005/m009's manifests):
//   u32 offset;    // little-endian, cumulative; meaning not further pinned
//                  // down (probably an offset into some concatenated
//                  // resource blob this disc doesn't ship standalone)
//   u32 size;      // little-endian; exactly matches the real byte size of
//                  // the sibling file named below, when name != "NULL"
//   char name[16]; // ASCII, NUL-padded; literal "NULL" marks an empty slot
//   u8  reserved[8]; // always zero in every sample seen
#define OPOONA_MOL_HEADER_SIZE 8
#define OPOONA_MOL_RECORD_SIZE 32
#define OPOONA_MOL_NAME_LEN 16

static int opoona_mol_name_ok (const u8 *name)
{
	if (!memcmp (name, "NULL", 4) && !name[4])
		return 1; // literal empty-slot placeholder

	int len = 0;
	while (len < OPOONA_MOL_NAME_LEN && name[len])
	{
		u8 c = name[len];
		int ok = c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c >= '0' && c <= '9' || c == '_'
			|| c == '.' || c == '-';
		if (!ok)
			return 0;
		len++;
	}
	if (!len || len == OPOONA_MOL_NAME_LEN)
		return 0; // empty or not NUL-terminated within the field
	for (int i = len; i < OPOONA_MOL_NAME_LEN; i++)
		if (name[i])
			return 0; // junk after the NUL
	return 1;
}

// Counts the leading run of structurally valid records starting right after
// the 8-byte header. Returns the count (>=0); a caller decides how many are
// required for a positive identification.
static int opoona_mol_scan (const u8 *data, size_t size, size_t file_size, int max_out)
{
	int count = 0;
	size_t off = OPOONA_MOL_HEADER_SIZE;
	while (off + OPOONA_MOL_RECORD_SIZE <= size)
	{
		const u8 *rec = data + off;
		const u8 *name = rec + 8;
		const u8 *resv = rec + 24;

		int is_null = !memcmp (name, "NULL", 4) && !name[4];
		if (!opoona_mol_name_ok (name))
			break;

		u32 rsize = rd_le32 (rec + 4);
		if (is_null ? rsize != 0 : rsize == 0)
			break;

		int resv_zero = 1;
		for (int i = 0; i < 8; i++)
			if (resv[i])
			{
				resv_zero = 0;
				break;
			}
		if (!resv_zero)
			break;

		count++;
		off += OPOONA_MOL_RECORD_SIZE;
		if (max_out && count >= max_out)
			break;
	}
	return count;
}

int IsOpoonaMOL (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < OPOONA_MOL_HEADER_SIZE + OPOONA_MOL_RECORD_SIZE)
		return 0;

	int count = opoona_mol_scan (data, size, file_size, 4);
	// Require at least 2 structurally valid records in a row: cheap magic-
	// less formats need more than one coincidental match to avoid false
	// positives on unrelated binary data.
	if (count >= 2)
		return 1;
	// A short FILETYPE-probe prefix legitimately might not fit even 2
	// records; excuse that only when the caller told us it's truncated.
	return size < file_size && count >= 1;
}

enumError DecodeOpoonaMOL_Text (FILE *f, const u8 *data, size_t size)
{
	if (!f)
		return EINVAL;
	if (!IsOpoonaMOL (data, size, size))
		return EINVAL;

	fprintf (f, "# Opoona character manifest (.mol)\n");
	fprintf (f, "# reverse-engineered structure; record offset/size fields are\n");
	fprintf (f, "# read but their exact runtime meaning is not fully confirmed\n");

	size_t off = OPOONA_MOL_HEADER_SIZE;
	int idx = 0;
	while (off + OPOONA_MOL_RECORD_SIZE <= size)
	{
		const u8 *rec = data + off;
		const u8 *name = rec + 8;
		if (!opoona_mol_name_ok (name))
		{
			fprintf (f, "record[%d]: <does not match the record shape, stopping>\n", idx);
			break;
		}

		u32 offset = rd_le32 (rec);
		u32 rsize = rd_le32 (rec + 4);
		int is_null = !memcmp (name, "NULL", 4) && !name[4];

		char nm[OPOONA_MOL_NAME_LEN + 1];
		memcpy (nm, name, OPOONA_MOL_NAME_LEN);
		nm[OPOONA_MOL_NAME_LEN] = 0;

		if (is_null)
			fprintf (f, "record[%d]: NULL (empty slot)\n", idx);
		else
			fprintf (f, "record[%d]: name=%s offset=0x%x size=%u\n", idx, nm, offset, rsize);

		idx++;
		off += OPOONA_MOL_RECORD_SIZE;
	}
	fprintf (f, "records: %d\n", idx);
	return ERR_OK;
}

//-----------------------------------------------------------------------------
////////////////////////////////   .mot clip   ////////////////////////////////
//-----------------------------------------------------------------------------

// Header (big-endian, ~0x70 bytes -- confirmed fields only, the rest of the
// header is read but not claimed to be understood):
//   0x04 float duration;      // clip length in frames
//   0x08 float frame_rate;    // constant 30.0 in every sample seen
//   0x0c u32   bone_count_p1; // (bone/track count) + 1; confirmed exactly
//                             // equal to the bind-pose array's record
//                             // count, across a 600-file corpus sample
//   0x28 u32   table_ptr;     // second table pointer; NOT understood (see
//                             // lib-opoona.h) -- read and reported raw only
//   0x2c u32   footer_ptr;    // always == file_size - 32 in every sample
//                             // seen (600-file corpus check)
//
// Followed at 0x70 by bone_count_p1-1 bind-pose records, 80 bytes each:
//   0x00 float scale[3];
//   0x0c u8    reserved[40];  // zero in every bind-pose sample seen
//   0x34 float translate[3];
//   0x40 float quat[4];       // x,y,z,w; unit magnitude in every sample seen
#define OPOONA_MOT_HEADER_SIZE 0x70
#define OPOONA_MOT_BONE_SIZE 80
#define OPOONA_MOT_FOOTER_SIZE 32

static float opoona_be_f32 (const u8 *p)
{
	u32 u = rd_be32 (p);
	float f;
	memcpy (&f, &u, sizeof (f));
	return f;
}

// Unit-quaternion check used both to validate the structural probe and to
// find where the bind-pose array ends (bone_count_p1 - 1 records are
// expected to all pass; this is the same check used throughout the
// investigation to independently recover the bone count from raw bytes).
static int opoona_quat_ok (const u8 *rec)
{
	float x = opoona_be_f32 (rec + 0x40);
	float y = opoona_be_f32 (rec + 0x44);
	float z = opoona_be_f32 (rec + 0x48);
	float w = opoona_be_f32 (rec + 0x4c);
	float mag2 = x * x + y * y + z * z + w * w;
	return mag2 > 0.9f && mag2 < 1.1f;
}

int IsOpoonaMOT (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < OPOONA_MOT_HEADER_SIZE + OPOONA_MOT_BONE_SIZE)
		return 0;

	float duration = opoona_be_f32 (data + 0x04);
	float frame_rate = opoona_be_f32 (data + 0x08);
	// The frame-rate float is a cheap, strong discriminator: constant 30.0
	// in every one of hundreds of sampled files.
	if (frame_rate < 29.999f || frame_rate > 30.001f)
		return 0;
	if (!(duration >= 0.0f) || duration > 100000.0f) // reject NaN/garbage
		return 0;

	u32 bone_count_p1 = rd_be32 (data + 0x0c);
	if (!bone_count_p1 || bone_count_p1 > 4096)
		return 0;
	u32 bone_count = bone_count_p1 - 1;

	size_t need = OPOONA_MOT_HEADER_SIZE + (size_t)bone_count * OPOONA_MOT_BONE_SIZE;
	if (need > size)
		return size < file_size; // truncated FILETYPE-probe prefix

	// Independently recover the bone count by scanning for the run of
	// unit-quaternion records; this must match the header field exactly,
	// which is the strongest structural confirmation available (verified
	// as a 100%-consistent relationship across a 600-file corpus).
	u32 scanned = 0;
	size_t off = OPOONA_MOT_HEADER_SIZE;
	while (scanned < bone_count && opoona_quat_ok (data + off))
	{
		scanned++;
		off += OPOONA_MOT_BONE_SIZE;
	}
	if (scanned != bone_count)
		return 0;

	// Footer relationship, only meaningful once the whole file is in hand.
	if (size == file_size && file_size >= OPOONA_MOT_FOOTER_SIZE)
	{
		u32 footer_ptr = rd_be32 (data + file_size - OPOONA_MOT_FOOTER_SIZE + 4);
		if (footer_ptr != file_size - OPOONA_MOT_FOOTER_SIZE)
			return 0;
	}
	return 1;
}

enumError DecodeOpoonaMOT_Text (FILE *f, const u8 *data, size_t size)
{
	if (!f)
		return EINVAL;
	if (!IsOpoonaMOT (data, size, size))
		return EINVAL;

	float duration = opoona_be_f32 (data + 0x04);
	float frame_rate = opoona_be_f32 (data + 0x08);
	u32 bone_count_p1 = rd_be32 (data + 0x0c);
	u32 bone_count = bone_count_p1 - 1;
	u32 table_ptr = rd_be32 (data + 0x28);
	u32 footer_ptr = rd_be32 (data + 0x2c);

	fprintf (f, "# Opoona animation clip (.mot)\n");
	fprintf (f, "# bind-pose skeleton is fully decoded below; any animation-\n");
	fprintf (f, "# curve data present is reported as an unparsed byte range,\n");
	fprintf (f, "# never decoded (its layout is not understood -- see header)\n");
	fprintf (f, "duration: %g\n", duration);
	fprintf (f, "frame_rate: %g\n", frame_rate);
	fprintf (f, "bones: %u\n", bone_count);
	fprintf (f, "table_ptr: 0x%x (unresolved, raw value only)\n", table_ptr);
	fprintf (f, "footer_ptr: 0x%x\n", footer_ptr);

	size_t off = OPOONA_MOT_HEADER_SIZE;
	for (u32 i = 0; i < bone_count; i++, off += OPOONA_MOT_BONE_SIZE)
	{
		const u8 *rec = data + off;
		float scale[3], translate[3], quat[4];
		for (int k = 0; k < 3; k++)
			scale[k] = opoona_be_f32 (rec + k * 4);
		for (int k = 0; k < 3; k++)
			translate[k] = opoona_be_f32 (rec + 0x34 + k * 4);
		for (int k = 0; k < 4; k++)
			quat[k] = opoona_be_f32 (rec + 0x40 + k * 4);

		fprintf (f, "bone[%u]: scale=%g,%g,%g translate=%g,%g,%g quat=%g,%g,%g,%g\n", i, scale[0],
			scale[1], scale[2], translate[0], translate[1], translate[2], quat[0], quat[1], quat[2],
			quat[3]);
	}

	size_t pool_start = off;
	size_t pool_end = size >= OPOONA_MOT_FOOTER_SIZE ? size - OPOONA_MOT_FOOTER_SIZE : size;
	if (pool_end > pool_start)
		fprintf (f, "animation_curve_pool: 0x%zx bytes at offset 0x%zx (NOT decoded)\n",
			pool_end - pool_start, pool_start);

	return ERR_OK;
}

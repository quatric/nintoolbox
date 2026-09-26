#include "lib-nusktb.h"
#include "lib-std.h"

// SSBH SKEL (.nusktb), little-endian. Reference: ultimate-research/ssbh_lib
// ssbh_lib/src/formats/skel.rs (Skel::V10). Same container convention as
// this codebase's other SSBH decoders, and the same array offsets
// lib-numsh.c's ParseNUMSHBSkinned() already reads from a sibling .nusktb
// (verified there against 151 retail skeletons):
//
//   0x00  "HBSS" (or "SSBH"), then a u64 at 0x04 (0x40 on every retail file)
//   0x10  "LEKS" ("SKEL" byte-reversed, or literal "SKEL"), u16 major, u16 minor
//   0x18  bone_entries:          SsbhArray<SkelBoneEntry>
//   0x28  world_transforms:      SsbhArray<Matrix4x4>
//   0x38  inv_world_transforms:  SsbhArray<Matrix4x4>
//   0x48  transforms:            SsbhArray<Matrix4x4>  (relative to parent)
//   0x58  inv_transforms:        SsbhArray<Matrix4x4>
//
// Each SsbhArray field is a u64 relative offset (relative to the field's own
// position) followed by a u64 count; all five arrays share bone_entries's own
// count (they're parallel, one entry per bone).
//
// A SkelBoneEntry is 0x10 bytes, relative to the bone_entries array's own
// element base:
//
//   0x00  name:          SsbhString (u64 relative offset to a NUL-terminated
//                        string)
//   0x08  index:         u16
//   0x0a  parent_index:  s16 (-1 if none)
//   0x0c  flags.unk1:        u8
//   0x0d  flags.billboard_type: u8
//   0x0e  -- 2 bytes padding --
//
// A Matrix4x4 is 64 bytes: 16 little-endian floats. lib-numsh.c's
// nsh_mat_to_joint() already established (and validated against real files)
// that the translation lives at float indices 12/13/14, i.e. byte offsets
// 0x30/0x34/0x38 within the matrix -- that's what's printed as each bone's
// world-space position here.

#define NUSKTB_SUBHDR_OFF 0x10
#define NUSKTB_BONE_ENTRY_SIZE 0x10
#define NUSKTB_MATRIX_SIZE 0x40
#define NUSKTB_MAX_BONES 0x1000

static const ccp nusktb_billboard_name[] = {
	[0] = "Disabled",
	[1] = "XAxisViewPointAligned",
	[2] = "YAxisViewPointAligned",
	[3] = "Unk3",
	[4] = "XYAxisViewPointAligned",
	[6] = "YAxisViewPlaneAligned",
	[8] = "XYAxisViewPlaneAligned",
};

bool IsNUSKTB (const u8 *data, size_t size)
{
	if (!data || size < NUSKTB_SUBHDR_OFF + 8)
		return false;
	if (memcmp (data, "HBSS", 4) && memcmp (data, "SSBH", 4))
		return false;
	return !memcmp (data + NUSKTB_SUBHDR_OFF, "LEKS", 4)
		|| !memcmp (data + NUSKTB_SUBHDR_OFF, "SKEL", 4);
}

// Reads a NUL-terminated string at the relative offset stored at 'field_off' (an
// SsbhString field), bounds-checked against 'size'. Leaves 'dest' empty and returns
// false on a null or out-of-range offset instead of aborting the whole decode.
static bool read_ssbh_string (char *dest, uint destsz, const u8 *data, size_t size, u64 field_off)
{
	dest[0] = 0;
	if (field_off + 8 > size)
		return false;
	const u64 rel = rd_le64 (data + field_off);
	if (!rel)
		return false;
	const u64 str_off = field_off + rel;
	if (str_off < field_off || str_off >= size) // 64-bit wrap guard + bounds
		return false;

	const u8 *p = data + str_off;
	const size_t max_len = size - str_off;
	size_t len = 0;
	while (len < max_len && p[len])
		len++;
	const uint n = len < destsz - 1 ? (uint)len : destsz - 1;
	memcpy (dest, p, n);
	dest[n] = 0;
	return true;
}

static float read_f32 (const u8 *data, u64 off)
{
	float v;
	memcpy (&v, data + off, 4);
	return v;
}

// Reads one SsbhArray field (u64 relative offset + u64 count) at 'field_off'. Returns
// false (both outputs 0) if the field itself doesn't fit, the offset is null, or it
// overflows/wraps -- same discipline as the rest of this decoder.
static bool read_ssbh_array (
	const u8 *data, size_t size, u64 field_off, u64 *out_base, u64 *out_count)
{
	*out_base = *out_count = 0;
	if (field_off + 16 > size)
		return false;
	const u64 rel = rd_le64 (data + field_off);
	const u64 count = rd_le64 (data + field_off + 8);
	if (!rel)
		return false;
	const u64 base = field_off + rel;
	if (base < field_off) // 64-bit wrap guard
		return false;
	*out_base = base;
	*out_count = count;
	return true;
}

enumError DecodeNUSKTB_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsNUSKTB (data, size))
		return ERR_INVALID_DATA;

	const u16 major = rd_le16 (data + NUSKTB_SUBHDR_OFF + 4);
	const u16 minor = rd_le16 (data + NUSKTB_SUBHDR_OFF + 6);

	u64 bone_base, bone_count;
	if (!read_ssbh_array (data, size, NUSKTB_SUBHDR_OFF + 8, &bone_base, &bone_count))
	{
		fprintf (out,
			"#NUSKTB\nversion = %u.%u\nbone_count = 0\n\n"
			"[bones]\n  <no bone array>\n",
			major, minor);
		return ERR_OK;
	}

	u64 world_base, world_count;
	read_ssbh_array (data, size, NUSKTB_SUBHDR_OFF + 0x18, &world_base, &world_count);

	fprintf (out,
		"#NUSKTB\n"
		"version = %u.%u\n"
		"bone_count = %llu\n\n"
		"[bones]\n",
		major, minor, (unsigned long long)bone_count);

	if (bone_count > NUSKTB_MAX_BONES)
	{
		fprintf (out, "  <bone_count implausible>\n");
		return ERR_OK;
	}

	const bool have_world = world_base && world_count >= bone_count;

	for (u64 i = 0; i < bone_count; i++)
	{
		const u64 entry_off = bone_base + i * NUSKTB_BONE_ENTRY_SIZE;
		if (entry_off < bone_base || entry_off + NUSKTB_BONE_ENTRY_SIZE > size)
		{
			fprintf (out, "  [%llu] <entry out of bounds>\n", (unsigned long long)i);
			break;
		}

		char name[256];
		read_ssbh_string (name, sizeof (name), data, size, entry_off);
		const u16 index = rd_le16 (data + entry_off + 8);
		const s16 parent_index = (s16)rd_le16 (data + entry_off + 0x0a);
		const u8 billboard = data[entry_off + 0x0d];
		ccp billboard_name = billboard < sizeof (nusktb_billboard_name) / sizeof (ccp)
				&& nusktb_billboard_name[billboard]
			? nusktb_billboard_name[billboard]
			: 0;

		fprintf (out, "  [%llu] %s\n    index = %u, parent = ", (unsigned long long)i,
			name[0] ? name : "<unnamed>", index);
		if (parent_index < 0)
			fprintf (out, "<none>");
		else
			fprintf (out, "%d", parent_index);
		if (billboard_name)
			fprintf (out, ", billboard = %s\n", billboard_name);
		else
			fprintf (out, ", billboard = ?%u\n", billboard);

		if (have_world)
		{
			const u64 mtx_off = world_base + i * NUSKTB_MATRIX_SIZE;
			if (mtx_off + NUSKTB_MATRIX_SIZE > size)
				fprintf (out, "    position = <out of bounds>\n");
			else
				fprintf (out, "    position = (%g, %g, %g)\n",
					(double)read_f32 (data, mtx_off + 0x30),
					(double)read_f32 (data, mtx_off + 0x34),
					(double)read_f32 (data, mtx_off + 0x38));
		}
	}

	return ERR_OK;
}

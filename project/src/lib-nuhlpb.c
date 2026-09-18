#include "lib-nuhlpb.h"
#include "lib-std.h"

// SSBH HLPB (.nuhlpb), little-endian. Reference: ultimate-research/ssbh_lib
// ssbh_lib/src/formats/hlpb.rs (Hlpb::V11). Same container conventions as
// this codebase's other SSBH decoders.
//
//   0x00  "HBSS" (or "SSBH"), then a u64 at 0x04 (0x40 on every retail file)
//   0x10  "BPLH" ("HLPB" byte-reversed, or literal "HLPB"), u16 major, u16 minor
//   0x18  aim_constraints:     SsbhArray<AimConstraint>
//   0x28  orient_constraints:  SsbhArray<OrientConstraint>
//   0x38  constraint_indices:  SsbhArray<u32> -- per constraint (ordered by
//                              application), the index into whichever of the
//                              two arrays above constraint_types (below)
//                              says it belongs to
//   0x48  constraint_types:    SsbhArray<u32> -- 0 = Aim, 1 = Orient
//
// An AimConstraint is 0x90 bytes, relative to its array's own element base:
//
//   0x00  name:             SsbhString
//   0x08  aim_bone_name1:   SsbhString
//   0x10  aim_bone_name2:   SsbhString
//   0x18  aim_type1:        SsbhString (always "DEFAULT")
//   0x20  aim_type2:        SsbhString (always "DEFAULT")
//   0x28  target_bone_name1: SsbhString
//   0x30  target_bone_name2: SsbhString
//   0x38  unk1: u32 (always 0), unk2: u32 (always 1)
//   0x40  aim:  Vector3 (3x f32) -- local axis to constrain, usually X+ (1,0,0)
//   0x4c  up:   Vector3
//   0x58  quat1: Vector4 (4x f32)
//   0x68  quat2: Vector4
//   0x78  unk17..unk22: 6x f32 (always 0)
//
// An OrientConstraint is 0x70 bytes:
//
//   0x00  name:              SsbhString
//   0x08  parent_bone_name1: SsbhString
//   0x10  parent_bone_name2: SsbhString
//   0x18  source_bone_name:  SsbhString
//   0x20  target_bone_name:  SsbhString
//   0x28  unk_type: u32 (0, 1 or 2 -- usually 1 or 2)
//   0x2c  constraint_axes: Vector3 -- per-axis source/target interpolation factor
//   0x38  quat1: Vector4
//   0x48  quat2: Vector4
//   0x58  range_min: Vector3 (always -180,-180,-180)
//   0x64  range_max: Vector3 (always 180,180,180)

#define NUHLPB_SUBHDR_OFF 0x10
#define NUHLPB_AIM_SIZE 0x90
#define NUHLPB_ORIENT_SIZE 0x70
#define NUHLPB_MAX_LIST 65536

bool IsNUHLPB (const u8 *data, size_t size)
{
	if (!data || size < NUHLPB_SUBHDR_OFF + 8)
		return false;
	if (memcmp (data, "HBSS", 4) && memcmp (data, "SSBH", 4))
		return false;
	return !memcmp (data + NUHLPB_SUBHDR_OFF, "BPLH", 4)
		|| !memcmp (data + NUHLPB_SUBHDR_OFF, "HLPB", 4);
}

// Reads a NUL-terminated string at the relative offset stored at 'field_off' (an
// SsbhString field), bounds-checked against 'size'. Leaves 'dest' empty and returns
// false on a null or out-of-range offset instead of aborting the whole decode.
static bool read_ssbh_string (
	char *dest, uint destsz, const u8 *data, size_t size, u64 field_off)
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

static void print_vec3 (FILE *out, ccp label, const u8 *data, u64 off)
{
	fprintf (out, "%s(%g, %g, %g)", label, (double)read_f32 (data, off),
		(double)read_f32 (data, off + 4), (double)read_f32 (data, off + 8));
}

// Reads one SsbhArray field (u64 relative offset + u64 count) at 'field_off'. Returns
// false (both outputs 0) if the field itself doesn't fit, the offset is null, or it
// overflows/wraps.
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
	if (base < field_off)
		return false;
	*out_base = base;
	*out_count = count;
	return true;
}

enumError DecodeNUHLPB_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsNUHLPB (data, size))
		return ERR_INVALID_DATA;

	const u16 major = rd_le16 (data + NUHLPB_SUBHDR_OFF + 4);
	const u16 minor = rd_le16 (data + NUHLPB_SUBHDR_OFF + 6);

	fprintf (out, "#NUHLPB\nversion = %u.%u\n\n", major, minor);

	u64 aim_base, aim_count;
	const bool have_aim
		= read_ssbh_array (data, size, NUHLPB_SUBHDR_OFF + 8, &aim_base, &aim_count);

	fprintf (out, "[aim_constraints]\n");
	if (!have_aim || aim_count > NUHLPB_MAX_LIST)
		fprintf (out, "  <none>\n");
	else for (u64 i = 0; i < aim_count; i++)
	{
		const u64 e = aim_base + i * NUHLPB_AIM_SIZE;
		if (e < aim_base || e + NUHLPB_AIM_SIZE > size)
		{
			fprintf (out, "  [%llu] <out of bounds>\n", (unsigned long long)i);
			break;
		}
		char name[128], bone1[128], bone2[128], target1[128], target2[128];
		read_ssbh_string (name, sizeof (name), data, size, e);
		read_ssbh_string (bone1, sizeof (bone1), data, size, e + 8);
		read_ssbh_string (bone2, sizeof (bone2), data, size, e + 0x10);
		read_ssbh_string (target1, sizeof (target1), data, size, e + 0x28);
		read_ssbh_string (target2, sizeof (target2), data, size, e + 0x30);

		fprintf (out, "  [%llu] %s\n"
			"    aim_bones = %s, %s\n"
			"    target_bones = %s, %s\n    ",
			(unsigned long long)i, name[0] ? name : "<unnamed>",
			bone1[0] ? bone1 : "<none>", bone2[0] ? bone2 : "<none>",
			target1[0] ? target1 : "<none>", target2[0] ? target2 : "<none>");
		print_vec3 (out, "aim = ", data, e + 0x40);
		fprintf (out, ", ");
		print_vec3 (out, "up = ", data, e + 0x4c);
		fprintf (out, "\n");
	}

	u64 orient_base, orient_count;
	const bool have_orient
		= read_ssbh_array (data, size, NUHLPB_SUBHDR_OFF + 0x18, &orient_base, &orient_count);

	fprintf (out, "\n[orient_constraints]\n");
	if (!have_orient || orient_count > NUHLPB_MAX_LIST)
		fprintf (out, "  <none>\n");
	else for (u64 i = 0; i < orient_count; i++)
	{
		const u64 e = orient_base + i * NUHLPB_ORIENT_SIZE;
		if (e < orient_base || e + NUHLPB_ORIENT_SIZE > size)
		{
			fprintf (out, "  [%llu] <out of bounds>\n", (unsigned long long)i);
			break;
		}
		char name[128], parent1[128], parent2[128], source[128], target[128];
		read_ssbh_string (name, sizeof (name), data, size, e);
		read_ssbh_string (parent1, sizeof (parent1), data, size, e + 8);
		read_ssbh_string (parent2, sizeof (parent2), data, size, e + 0x10);
		read_ssbh_string (source, sizeof (source), data, size, e + 0x18);
		read_ssbh_string (target, sizeof (target), data, size, e + 0x20);
		const u32 unk_type = rd_le32 (data + e + 0x28);

		fprintf (out, "  [%llu] %s\n"
			"    parent_bones = %s, %s\n"
			"    source_bone = %s, target_bone = %s, unk_type = %u\n    ",
			(unsigned long long)i, name[0] ? name : "<unnamed>",
			parent1[0] ? parent1 : "<none>", parent2[0] ? parent2 : "<none>",
			source[0] ? source : "<none>", target[0] ? target : "<none>", unk_type);
		print_vec3 (out, "constraint_axes = ", data, e + 0x2c);
		fprintf (out, "\n");
	}

	u64 idx_base, idx_count, type_base, type_count;
	const bool have_idx
		= read_ssbh_array (data, size, NUHLPB_SUBHDR_OFF + 0x28, &idx_base, &idx_count);
	const bool have_type
		= read_ssbh_array (data, size, NUHLPB_SUBHDR_OFF + 0x38, &type_base, &type_count);

	fprintf (out, "\n[constraint_order]\n");
	if (!have_idx || !have_type || idx_count != type_count || idx_count > NUHLPB_MAX_LIST)
	{
		fprintf (out, "  <none>\n");
		return ERR_OK;
	}

	for (u64 i = 0; i < idx_count; i++)
	{
		const u64 idx_off = idx_base + i * 4, type_off = type_base + i * 4;
		if (idx_off + 4 > size || type_off + 4 > size)
		{
			fprintf (out, "  [%llu] <out of bounds>\n", (unsigned long long)i);
			break;
		}
		const u32 idx = rd_le32 (data + idx_off);
		const u32 type = rd_le32 (data + type_off);
		fprintf (out, "  [%llu] %s[%u]\n", (unsigned long long)i,
			type == 0 ? "aim_constraints" : type == 1 ? "orient_constraints" : "?", idx);
	}

	return ERR_OK;
}

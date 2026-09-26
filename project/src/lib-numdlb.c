#include "lib-numdlb.h"
#include "lib-std.h"

// SSBH MODL (.numdlb / .nusrcmdlb), little-endian. Reference:
// ultimate-research/ssbh_lib ssbh_lib/src/formats/modl.rs (Modl::V17). Same
// container conventions as this codebase's other SSBH decoders.
//
//   0x00  "HBSS" (or "SSBH"), then a u64 at 0x04 (0x40 on every retail file)
//   0x10  "LDOM" ("MODL" byte-reversed, or literal "MODL"), u16 major, u16 minor
//   0x18  model_name:           SsbhString
//   0x20  skeleton_file_name:   SsbhString
//   0x28  material_file_names:  SsbhArray<SsbhString>
//   0x38  animation_file_name:  RelPtr64<SsbhString> -- a pointer to a second
//                               SsbhString field elsewhere in the file, so
//                               reading it takes two hops: the field itself
//                               (relative to 0x38), then that target's own
//                               relative offset (relative to itself) to the
//                               actual string bytes. Null (0) if there's no
//                               associated .nuanmb.
//   0x40  mesh_file_name:       SsbhString8 (read identically to SsbhString)
//   0x48  entries:              SsbhArray<ModlEntry>
//
// A ModlEntry is 0x18 bytes, relative to the entries array's own element base:
//
//   0x00  mesh_object_name:      SsbhString
//   0x08  mesh_object_subindex:  u64
//   0x10  material_label:       SsbhString

#define NUMDLB_SUBHDR_OFF 0x10
#define NUMDLB_ENTRY_SIZE 0x18
#define NUMDLB_MAX_LIST 65536

bool IsNUMDLB (const u8 *data, size_t size)
{
	if (!data || size < NUMDLB_SUBHDR_OFF + 8)
		return false;
	if (memcmp (data, "HBSS", 4) && memcmp (data, "SSBH", 4))
		return false;
	return !memcmp (data + NUMDLB_SUBHDR_OFF, "LDOM", 4)
		|| !memcmp (data + NUMDLB_SUBHDR_OFF, "MODL", 4);
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

// Reads the two-hop RelPtr64<SsbhString> at 'field_off' (Modl's animation_file_name):
// first hop lands on an SsbhString field, whose own relative offset (relative to
// itself) is then followed to the actual string bytes.
static bool read_indirect_ssbh_string (
	char *dest, uint destsz, const u8 *data, size_t size, u64 field_off)
{
	dest[0] = 0;
	if (field_off + 8 > size)
		return false;
	const u64 rel = rd_le64 (data + field_off);
	if (!rel)
		return false;
	const u64 inner_field_off = field_off + rel;
	if (inner_field_off < field_off || inner_field_off >= size)
		return false;
	return read_ssbh_string (dest, destsz, data, size, inner_field_off);
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

enumError DecodeNUMDLB_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsNUMDLB (data, size))
		return ERR_INVALID_DATA;

	const u16 major = rd_le16 (data + NUMDLB_SUBHDR_OFF + 4);
	const u16 minor = rd_le16 (data + NUMDLB_SUBHDR_OFF + 6);

	char model_name[256], skeleton_name[256], mesh_name[256], anim_name[256];
	read_ssbh_string (model_name, sizeof (model_name), data, size, NUMDLB_SUBHDR_OFF + 8);
	read_ssbh_string (skeleton_name, sizeof (skeleton_name), data, size, NUMDLB_SUBHDR_OFF + 0x10);
	read_ssbh_string (mesh_name, sizeof (mesh_name), data, size, NUMDLB_SUBHDR_OFF + 0x30);
	read_indirect_ssbh_string (anim_name, sizeof (anim_name), data, size, NUMDLB_SUBHDR_OFF + 0x28);

	fprintf (out,
		"#NUMDLB\n"
		"version = %u.%u\n"
		"model_name = %s\n"
		"skeleton_file_name = %s\n"
		"mesh_file_name = %s\n"
		"animation_file_name = %s\n\n",
		major, minor, model_name[0] ? model_name : "<none>",
		skeleton_name[0] ? skeleton_name : "<none>", mesh_name[0] ? mesh_name : "<none>",
		anim_name[0] ? anim_name : "<none>");

	u64 mat_base, mat_count;
	fprintf (out, "[material_file_names]\n");
	if (read_ssbh_array (data, size, NUMDLB_SUBHDR_OFF + 0x18, &mat_base, &mat_count)
		&& mat_count <= NUMDLB_MAX_LIST)
	{
		for (u64 i = 0; i < mat_count; i++)
		{
			const u64 field_off = mat_base + i * 8;
			if (field_off < mat_base || field_off + 8 > size)
			{
				fprintf (out, "  [%llu] <out of bounds>\n", (unsigned long long)i);
				break;
			}
			char name[256];
			read_ssbh_string (name, sizeof (name), data, size, field_off);
			fprintf (out, "  [%llu] %s\n", (unsigned long long)i, name[0] ? name : "<unnamed>");
		}
	}
	else
		fprintf (out, "  <none>\n");

	u64 entry_base, entry_count;
	fprintf (out, "\n[mesh_object_materials]\n");
	if (!read_ssbh_array (data, size, NUMDLB_SUBHDR_OFF + 0x38, &entry_base, &entry_count)
		|| entry_count > NUMDLB_MAX_LIST)
	{
		fprintf (out, "  <none>\n");
		return ERR_OK;
	}

	for (u64 i = 0; i < entry_count; i++)
	{
		const u64 entry_off = entry_base + i * NUMDLB_ENTRY_SIZE;
		if (entry_off < entry_base || entry_off + NUMDLB_ENTRY_SIZE > size)
		{
			fprintf (out, "  [%llu] <out of bounds>\n", (unsigned long long)i);
			break;
		}
		char obj_name[256], mat_label[256];
		read_ssbh_string (obj_name, sizeof (obj_name), data, size, entry_off);
		const u64 subindex = rd_le64 (data + entry_off + 8);
		read_ssbh_string (mat_label, sizeof (mat_label), data, size, entry_off + 0x10);

		fprintf (out, "  [%llu] %s[%llu] -> %s\n", (unsigned long long)i,
			obj_name[0] ? obj_name : "<unnamed>", (unsigned long long)subindex,
			mat_label[0] ? mat_label : "<unnamed>");
	}

	return ERR_OK;
}

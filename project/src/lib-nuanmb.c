#include "lib-nuanmb.h"
#include "lib-std.h"

// SSBH ANIM (.nuanmb), little-endian. Reference: ultimate-research/ssbh_lib
// ssbh_lib/src/formats/anim.rs (Anim::V12/V20/V21). Same container
// conventions as this codebase's other SSBH decoders.
//
//   0x00  "HBSS" (or "SSBH"), then a u64 at 0x04 (0x40 on every retail file)
//   0x10  "MINA" ("ANIM" byte-reversed, or literal "ANIM"), u16 major, u16 minor
//
// Version 1.2 (older flat track list), fields from 0x18:
//   0x00  name:               SsbhString
//   0x08  unk1:               f32
//   0x0c  final_frame_index:  f32
//   0x10  unk2:               f32
//   0x14  unk3:               f32
//   0x18  tracks:             SsbhArray<TrackV1>
//   0x28  buffers:            SsbhArray<SsbhByteBuffer>
//
// A TrackV1 is 0x20 bytes: name (SsbhString), track_type (u64: 0 Transform,
// 2 UvTransform, 5 Visibility), properties (SsbhArray<Property>).
// A Property is 0x10 bytes: name (SsbhString), buffer_index (u64 -- indexes
// the file-level 'buffers' array above).
//
// Version 2.0/2.1 (group -> node -> track hierarchy), fields from 0x18:
//   0x00  final_frame_index:  f32
//   0x04  unk1: u16, unk2: u16
//   0x08  name:               SsbhString
//   0x10  groups:             SsbhArray<Group>
//   0x20  buffer:             SsbhByteBuffer
//   0x30  unk_data:           UnkData -- version 2.1 only (two SsbhArrays,
//                             not decoded further: the reference itself
//                             marks their element types' fields TODO)
//
// A Group is 0x18 bytes: group_type (u64: 1 Transform, 2 Visibility,
// 4 Material, 5 Camera), nodes (SsbhArray<Node>).
// A Node is 0x18 bytes: name (SsbhString), tracks (SsbhArray<TrackV2>).
// A TrackV2 is 0x20 bytes:
//   0x00  name:             SsbhString
//   0x08  track_type:       u8 (1 Transform, 2 UvTransform, 3 Float,
//                           5 PatternIndex, 8 Boolean, 9 Vector4)
//   0x09  compression_type: u8 (1 Direct, 2 ConstTransform, 4 Compressed,
//                           5 Constant)
//   0x0a  -- 2 bytes padding --
//   0x0c  frame_count:      u32
//   0x10  transform_flags:  u32 (bit 0 override_translation, bit 1
//                           override_rotation, bit 2 override_scale, bit 3
//                           override_compensate_scale)
//   0x14  data_offset:      u32 (into the file-level 'buffer' above)
//   0x18  data_size:        u64
//
// The keyframe bytes at [data_offset, data_offset+data_size) within 'buffer'
// (version 2.x) or within 'buffers[property.buffer_index]' (version 1.2) are
// a compression scheme documented only in the companion ssbh_data crate, not
// ssbh_lib -- out of scope here, same as this codebase's other embedded
// binary-blob formats. This decoder only reports where they are.

#define NUANMB_SUBHDR_OFF 0x10
// Where the version-specific fields begin, i.e. right after the sub-magic and
// the major/minor u16 pair -- matches the "fields from 0x18" comment blocks
// above verbatim (their listed offsets are relative to this).
#define NUANMB_FIELDS_OFF (NUANMB_SUBHDR_OFF + 8)
#define NUANMB_TRACKV1_SIZE 0x20
#define NUANMB_PROPERTY_SIZE 0x10
#define NUANMB_GROUP_SIZE 0x18
#define NUANMB_NODE_SIZE 0x18
#define NUANMB_TRACKV2_SIZE 0x20
#define NUANMB_MAX_LIST 65536

static const ccp nuanmb_track_type_v1_name (u64 v)
{
	switch (v)
	{
		case 0: return "Transform";
		case 2: return "UvTransform";
		case 5: return "Visibility";
		default: return "?";
	}
}

static const ccp nuanmb_track_type_v2_name (u8 v)
{
	switch (v)
	{
		case 1: return "Transform";
		case 2: return "UvTransform";
		case 3: return "Float";
		case 5: return "PatternIndex";
		case 8: return "Boolean";
		case 9: return "Vector4";
		default: return "?";
	}
}

static const ccp nuanmb_compression_type_name (u8 v)
{
	switch (v)
	{
		case 1: return "Direct";
		case 2: return "ConstTransform";
		case 4: return "Compressed";
		case 5: return "Constant";
		default: return "?";
	}
}

static const ccp nuanmb_group_type_name (u64 v)
{
	switch (v)
	{
		case 1: return "Transform";
		case 2: return "Visibility";
		case 4: return "Material";
		case 5: return "Camera";
		default: return "?";
	}
}

bool IsNUANMB (const u8 *data, size_t size)
{
	if (!data || size < NUANMB_SUBHDR_OFF + 8)
		return false;
	if (memcmp (data, "HBSS", 4) && memcmp (data, "SSBH", 4))
		return false;
	return !memcmp (data + NUANMB_SUBHDR_OFF, "MINA", 4)
		|| !memcmp (data + NUANMB_SUBHDR_OFF, "ANIM", 4);
}

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
	if (str_off < field_off || str_off >= size)
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

static float read_f32 (const u8 *data, u64 off)
{
	float v;
	memcpy (&v, data + off, 4);
	return v;
}

static void print_trackv2 (FILE *out, const u8 *data, size_t size, u64 e, u64 idx, ccp indent)
{
	char name[128];
	read_ssbh_string (name, sizeof (name), data, size, e);
	const u8 track_type = data[e + 8];
	const u8 compression_type = data[e + 9];
	const u32 frame_count = rd_le32 (data + e + 0x0c);
	const u32 transform_flags = rd_le32 (data + e + 0x10);
	const u32 data_offset = rd_le32 (data + e + 0x14);
	const u64 data_size = rd_le64 (data + e + 0x18);

	fprintf (out, "%s[%llu] %s\n", indent, (unsigned long long)idx, name[0] ? name : "<unnamed>");
	fprintf (out, "%s  type = %s, compression = %s, frame_count = %u\n", indent,
		nuanmb_track_type_v2_name (track_type), nuanmb_compression_type_name (compression_type),
		frame_count);
	if (transform_flags)
		fprintf (out, "%s  transform_flags = 0x%x (override_translation=%d,"
			" override_rotation=%d, override_scale=%d, override_compensate_scale=%d)\n",
			indent, transform_flags, transform_flags & 1, transform_flags >> 1 & 1,
			transform_flags >> 2 & 1, transform_flags >> 3 & 1);
	fprintf (out, "%s  data = offset %u, size %llu\n", indent, data_offset,
		(unsigned long long)data_size);
}

static enumError decode_v12 (FILE *out, const u8 *data, size_t size)
{
	char name[128];
	read_ssbh_string (name, sizeof (name), data, size, NUANMB_FIELDS_OFF);
	const float final_frame_index = read_f32 (data, NUANMB_FIELDS_OFF + 0x0c);

	fprintf (out, "name = %s\nfinal_frame_index = %g\n\n",
		name[0] ? name : "<unnamed>", (double)final_frame_index);

	u64 trk_base, trk_count;
	fprintf (out, "[tracks]\n");
	if (!read_ssbh_array (data, size, NUANMB_FIELDS_OFF + 0x18, &trk_base, &trk_count)
		|| trk_count > NUANMB_MAX_LIST)
	{
		fprintf (out, "  <none>\n");
		return ERR_OK;
	}

	for (u64 i = 0; i < trk_count; i++)
	{
		const u64 e = trk_base + i * NUANMB_TRACKV1_SIZE;
		if (e < trk_base || e + NUANMB_TRACKV1_SIZE > size)
		{
			fprintf (out, "  [%llu] <out of bounds>\n", (unsigned long long)i);
			break;
		}
		char tname[128];
		read_ssbh_string (tname, sizeof (tname), data, size, e);
		const u64 track_type = rd_le64 (data + e + 8);
		fprintf (out, "  [%llu] %s (type = %s)\n", (unsigned long long)i,
			tname[0] ? tname : "<unnamed>", nuanmb_track_type_v1_name (track_type));

		u64 prop_base, prop_count;
		if (read_ssbh_array (data, size, e + 0x10, &prop_base, &prop_count)
			&& prop_count <= NUANMB_MAX_LIST)
		{
			for (u64 p = 0; p < prop_count; p++)
			{
				const u64 pe = prop_base + p * NUANMB_PROPERTY_SIZE;
				if (pe < prop_base || pe + NUANMB_PROPERTY_SIZE > size)
				{
					fprintf (out, "    [%llu] <out of bounds>\n", (unsigned long long)p);
					break;
				}
				char pname[128];
				read_ssbh_string (pname, sizeof (pname), data, size, pe);
				const u64 buffer_index = rd_le64 (data + pe + 8);
				fprintf (out, "    [%llu] %s -> buffers[%llu]\n", (unsigned long long)p,
					pname[0] ? pname : "<unnamed>", (unsigned long long)buffer_index);
			}
		}
	}

	return ERR_OK;
}

static enumError decode_v2x (FILE *out, const u8 *data, size_t size)
{
	const float final_frame_index = read_f32 (data, NUANMB_FIELDS_OFF);
	char name[128];
	read_ssbh_string (name, sizeof (name), data, size, NUANMB_FIELDS_OFF + 8);

	fprintf (out, "name = %s\nfinal_frame_index = %g\n\n",
		name[0] ? name : "<unnamed>", (double)final_frame_index);

	u64 grp_base, grp_count;
	fprintf (out, "[groups]\n");
	if (!read_ssbh_array (data, size, NUANMB_FIELDS_OFF + 0x10, &grp_base, &grp_count)
		|| grp_count > NUANMB_MAX_LIST)
	{
		fprintf (out, "  <none>\n");
		return ERR_OK;
	}

	for (u64 g = 0; g < grp_count; g++)
	{
		const u64 ge = grp_base + g * NUANMB_GROUP_SIZE;
		if (ge < grp_base || ge + NUANMB_GROUP_SIZE > size)
		{
			fprintf (out, "  [%llu] <out of bounds>\n", (unsigned long long)g);
			break;
		}
		const u64 group_type = rd_le64 (data + ge);
		fprintf (out, "  [%llu] %s\n", (unsigned long long)g, nuanmb_group_type_name (group_type));

		u64 node_base, node_count;
		if (!read_ssbh_array (data, size, ge + 8, &node_base, &node_count)
			|| node_count > NUANMB_MAX_LIST)
			continue;

		for (u64 n = 0; n < node_count; n++)
		{
			const u64 ne = node_base + n * NUANMB_NODE_SIZE;
			if (ne < node_base || ne + NUANMB_NODE_SIZE > size)
			{
				fprintf (out, "    [%llu] <out of bounds>\n", (unsigned long long)n);
				break;
			}
			char nname[128];
			read_ssbh_string (nname, sizeof (nname), data, size, ne);
			fprintf (out, "    [%llu] %s\n", (unsigned long long)n, nname[0] ? nname : "<unnamed>");

			u64 trk_base, trk_count;
			if (!read_ssbh_array (data, size, ne + 8, &trk_base, &trk_count)
				|| trk_count > NUANMB_MAX_LIST)
				continue;

			for (u64 t = 0; t < trk_count; t++)
			{
				const u64 te = trk_base + t * NUANMB_TRACKV2_SIZE;
				if (te < trk_base || te + NUANMB_TRACKV2_SIZE > size)
				{
					fprintf (out, "      [%llu] <out of bounds>\n", (unsigned long long)t);
					break;
				}
				print_trackv2 (out, data, size, te, t, "      ");
			}
		}
	}

	return ERR_OK;
}

enumError DecodeNUANMB_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsNUANMB (data, size))
		return ERR_INVALID_DATA;

	const u16 major = rd_le16 (data + NUANMB_SUBHDR_OFF + 4);
	const u16 minor = rd_le16 (data + NUANMB_SUBHDR_OFF + 6);

	fprintf (out, "#NUANMB\nversion = %u.%u\n", major, minor);

	if (major == 1 && minor == 2)
		return decode_v12 (out, data, size);
	if (major == 2 && (minor == 0 || minor == 1))
		return decode_v2x (out, data, size);

	fprintf (out, "<unsupported version>\n");
	return ERR_OK;
}

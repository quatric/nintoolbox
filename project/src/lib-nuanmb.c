#include "lib-nuanmb.h"
#include "lib-std.h"

#include <math.h>

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
// (version 2.x) are decoded below, ported from SSBHLib's SSBHAnimTrackDecoder
// (SsbhAnimTrackDecoder.cs / AnimTrackTransform.cs): Direct stores every
// frame back to back, Constant/ConstTransform store one, Compressed stores a
// 16-byte header (u16 unk, u16 flags, u16 default-data offset, u16 bits per
// entry, i32 compressed-data offset, i32 frame count) followed by per-channel
// (start f32, end f32, bit-count u64) items, default values and an
// LSB-first bitstream. Version 1.2 buffers hold the same layouts per
// property, but SSBHLib's own decoder never implemented them, so this
// decoder reports their table and leaves the bytes alone, too.

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

static void print_trackv2 (FILE *out, const u8 *data, size_t size, u64 e, u64 idx, ccp indent,
	u64 buf_base, u64 buf_size, int have_buf); // fwd: needs the payload block

//--- keyframe payload decode (SSBHAnimTrackDecoder port) ---------------------
// Track payload kinds (low byte of the V2 track flags) and compression modes
// (high byte) mirror SSBHLib's AnimTrackFlags: 1 Transform, 2 UvTransform,
// 3 Float, 5 PatternIndex, 8 Boolean, 9 Vector4; 1 Direct, 2 ConstTransform,
// 4 Compressed, 5 Constant.

#define NUANMB_MAX_FRAMES 100000

typedef struct
{
	const u8 *d;
	u64 base;
	u64 span;
	u64 byte;
	int bit;
	int fail;
} abit_t;

// LSB-first bit reader. SSBHLib's SsbhParser.ReadBits assembles value bit i
// at position i in every reachable case, which is what this does directly.
static u32 abit_read (abit_t *b, u64 nbits)
{
	u32 v = 0;
	if (nbits > 32)
	{
		b->fail = 1;
		return 0;
	}
	for (u64 i = 0; i < nbits; i++)
	{
		if (b->byte >= b->span)
		{
			b->fail = 1;
			return 0;
		}
		v |= (u32)(((b->d[b->base + b->byte] >> b->bit) & 1u) << i);
		if (++b->bit == 8)
		{
			b->bit = 0;
			b->byte++;
		}
	}
	return v;
}

static float anim_lerp (float a, float b, float t)
{
	if (t <= 0.0f)
		return a;
	if (t >= 1.0f)
		return b;
	float v = a + (b - a) * t;
	return v != v ? 0.0f : v;
}

static void anim_f32 (FILE *out, const u8 *data, u64 off)
{
	fprintf (out, "%.9g", (double)read_f32 (data, off));
}

// One Direct value at [off, end); returns bytes consumed, or -1 when the
// value does not fit. Field order matches ReadDirect in the reference.
static int anim_direct (FILE *out, const u8 *data, u64 off, u64 end, int ttype)
{
	switch (ttype)
	{
	case 1: // Transform: 9 floats, then compensate-scale i32
		if (end - off < 44)
			return -1;
		fputs ("pos=(", out);
		anim_f32 (out, data, off + 28); fputs (", ", out);
		anim_f32 (out, data, off + 32); fputs (", ", out);
		anim_f32 (out, data, off + 36);
		fputs (") rot=(", out);
		anim_f32 (out, data, off + 12); fputs (", ", out);
		anim_f32 (out, data, off + 16); fputs (", ", out);
		anim_f32 (out, data, off + 20); fputs (", ", out);
		anim_f32 (out, data, off + 24);
		fputs (") scale=(", out);
		anim_f32 (out, data, off); fputs (", ", out);
		anim_f32 (out, data, off + 4); fputs (", ", out);
		anim_f32 (out, data, off + 8);
		fprintf (out, ") compensate=%d", (int)rd_le32 (data + off + 40));
		return 44;

	case 2: // UvTransform texture block: 4 floats, then i32
		if (end - off < 20)
			return -1;
		fputc ('[', out);
		for (int c = 0; c < 4; c++)
		{
			if (c)
				fputs (", ", out);
			anim_f32 (out, data, off + (u64)c * 4);
		}
		fprintf (out, ", %d]", (int)rd_le32 (data + off + 16));
		return 20;

	case 3: // Float
		if (end - off < 4)
			return -1;
		anim_f32 (out, data, off);
		return 4;

	case 5: // PatternIndex
		if (end - off < 4)
			return -1;
		fprintf (out, "%d", (int)rd_le32 (data + off));
		return 4;

	case 8: // Boolean
		if (end - off < 1)
			return -1;
		fputs (data[off] ? "true" : "false", out);
		return 1;

	case 9: // Vector4
		if (end - off < 16)
			return -1;
		fputc ('(', out);
		for (int c = 0; c < 4; c++)
		{
			if (c)
				fputs (", ", out);
			anim_f32 (out, data, off + (u64)c * 4);
		}
		fputc (')', out);
		return 16;

	default:
		return -1;
	}
}

static void anim_transform_frame (FILE *out, const float *v, int compensate)
{
	fprintf (out, "pos=(%.9g, %.9g, %.9g) rot=(%.9g, %.9g, %.9g, %.9g)"
		" scale=(%.9g, %.9g, %.9g) compensate=%d",
		(double)v[7], (double)v[8], (double)v[9],
		(double)v[3], (double)v[4], (double)v[5], (double)v[6],
		(double)v[0], (double)v[1], (double)v[2], compensate);
}

// Compressed Transform / Vector4 payloads (DecompressTransform /
// DecompressValues in the reference). Scale-item gating, the uniform-scale
// quirk ((flags & 3) == 2 parses no scale items at all) and the quaternion-W
// reconstruction (sqrt + sign bit) are all ported as-is.
static void anim_compressed (FILE *out, const u8 *data, u64 base, u64 span,
	int ttype, ccp indent)
{
	if (span < 16)
	{
		fprintf (out, "%s  <payload too short for a compressed header>\n", indent);
		return;
	}
	const u16 flags = rd_le16 (data + base + 2);
	const u64 def_rel = rd_le16 (data + base + 4);
	const u16 bits_per_entry = rd_le16 (data + base + 6);
	const u64 comp_rel = (u64)(int32_t)rd_le32 (data + base + 8);
	const int32_t frames = (int32_t)rd_le32 (data + base + 12);
	if (frames < 0 || def_rel > span || comp_rel > span)
	{
		fprintf (out, "%s  <invalid compressed header>\n", indent);
		return;
	}
	const int is_transform = ttype == 1;
	const int is_boolean = ttype == 8;
	if (is_boolean)
	{
		// ReadBooleans in the reference: FrameCount entries of
		// BitsPerEntry bits from the compressed-data offset.
		abit_t bb;
		bb.d = data;
		bb.base = base + comp_rel;
		bb.span = span > comp_rel ? span - comp_rel : 0;
		bb.byte = 0;
		bb.bit = 0;
		bb.fail = 0;
		u64 nframes = frames < 0 ? 0 : (u64)frames;
		if (nframes > NUANMB_MAX_FRAMES)
		{
			fprintf (out, "%s  <frame count %u exceeds the %u-frame display cap>\n",
				indent, (unsigned)frames, NUANMB_MAX_FRAMES);
			return;
		}
		for (u64 f = 0; f < nframes && !bb.fail; f++)
		{
			const u32 v = abit_read (&bb, bits_per_entry);
			if (bb.fail)
				break;
			fprintf (out, "%s  [%llu] %s\n", indent,
				(unsigned long long)f, v == 1 ? "true" : "false");
		}
		if (bb.fail)
			fprintf (out, "%s  <truncated bitstream>\n", indent);
		return;
	}
	const int nitems = is_transform ? 9 : 4;
	if (!is_boolean && 16 + (u64)nitems * 16 > span)
	{
		fprintf (out, "%s  <payload too short for %d items>\n", indent, nitems);
		return;
	}

	float starts[9], ends[9];
	u64 counts[9];
	for (int k = 0; k < nitems; k++)
	{
		const u64 io = base + 16 + (u64)k * 16;
		memcpy (&starts[k], data + io, 4);
		memcpy (&ends[k], data + io + 4, 4);
		counts[k] = rd_le64 (data + io + 8);
		if (counts[k] > 31)
		{
			fprintf (out, "%s  <item %d claims %llu bits>\n", indent,
				k, (unsigned long long)counts[k]);
			return;
		}
	}

	const int ndef = is_transform ? 10 : 4;
	if (def_rel + (u64)ndef * 4 + (is_transform ? 4 : 0) > span)
	{
		fprintf (out, "%s  <payload too short for defaults>\n", indent);
		return;
	}
	float defs[10];
	for (int k = 0; k < ndef; k++)
		memcpy (&defs[k], data + base + def_rel + (u64)k * 4, 4);
	const int compensate = is_transform
		? (int)rd_le32 (data + base + def_rel + 40) : 0;

	abit_t b;
	b.d = data;
	b.base = base + comp_rel;
	b.span = span > comp_rel ? span - comp_rel : 0;
	b.byte = 0;
	b.bit = 0;
	b.fail = 0;

	u64 nframes = (u64)frames;
	if (nframes > NUANMB_MAX_FRAMES)
	{
		fprintf (out, "%s  <frame count %u exceeds the %u-frame display cap>\n",
			indent, (unsigned)frames, NUANMB_MAX_FRAMES);
		return;
	}

	for (u64 f = 0; f < nframes && !b.fail; f++)
	{
		float v[10];
		for (int k = 0; k < (is_transform ? 10 : 4); k++)
			v[k] = defs[k];

		for (int k = 0; k < nitems && !b.fail; k++)
		{
			if (is_transform)
			{
				const int scaletype = flags & 3;
				const int gated = (k == 0 && scaletype == 3)
					|| (k >= 0 && k <= 2 && scaletype == 1)
					|| (k > 2 && k <= 5 && (flags & 4))
					|| (k > 5 && k <= 8 && (flags & 8));
				if (!gated)
					continue;
			}
			if (!counts[k])
				continue;
			const u32 raw = abit_read (&b, counts[k]);
			u32 scale = 0;
			for (u64 s = 0; s < counts[k]; s++)
				scale |= (u32)1 << s;
			const float t = scale ? (float)raw / (float)scale : 0.0f;
			const float fv = anim_lerp (starts[k], ends[k], t);
			if (is_transform)
			{
				if ((flags & 3) == 3)
				{
					if (k == 0)
						v[0] = v[1] = v[2] = fv;
				}
				else if (k <= 2)
					v[k] = fv;
				else if (k <= 5)
					v[k] = fv; // rotation slots 3..5 map to v[3..5]
				else
					v[k + 1] = fv; // position slots 6..8 map to v[7..9]
			}
			else
				v[k] = fv;
		}
		if (b.fail)
			break;

		if (is_transform && (flags & 4))
		{
			const int wflip = (int)abit_read (&b, 1);
			if (b.fail)
				break;
			const float w2 = 1.0f - (v[3] * v[3] + v[4] * v[4] + v[5] * v[5]);
			const float w = sqrtf (w2 < 0.0f ? -w2 : w2);
			v[6] = wflip ? -w : w;
		}

		fprintf (out, "%s  [%llu] ", indent, (unsigned long long)f);
		if (is_transform)
			anim_transform_frame (out, v, compensate);
		else
			fprintf (out, "(%.9g, %.9g, %.9g, %.9g)",
				(double)v[0], (double)v[1], (double)v[2], (double)v[3]);
		fputc ('\n', out);
	}
	if (b.fail)
		fprintf (out, "%s  <truncated bitstream>\n", indent);
}

static void print_trackv2 (FILE *out, const u8 *data, size_t size, u64 e, u64 idx, ccp indent,
	u64 buf_base, u64 buf_size, int have_buf)
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

	// Keyframe payloads, decoded like SSBHAnimTrackDecoder.ReadTrack.
	if (!have_buf)
		fprintf (out, "%s  <no buffer to decode>\n", indent);
	else if ((u64)data_offset + data_size > buf_size || (u64)data_offset > buf_size)
		fprintf (out, "%s  <payload outside the buffer>\n", indent);
	else if (track_type == 2 && compression_type == 4
		|| track_type == 3 && compression_type == 4
		|| track_type == 5 && compression_type == 4)
		fprintf (out, "%s  <compressed %s payloads are TODO in the reference decoder>\n",
			indent, nuanmb_track_type_v2_name (track_type));
	else if (compression_type == 4
		&& (track_type == 1 || track_type == 8 || track_type == 9))
		anim_compressed (out, data, buf_base + data_offset, data_size,
			track_type, indent);
	else if (compression_type == 1 || compression_type == 2 || compression_type == 5)
	{
		const u64 frames = compression_type == 1 ? frame_count : 1;
		u64 off = buf_base + data_offset;
		const u64 end = off + data_size;
		for (u64 f = 0; f < frames; f++)
		{
			if (off >= end)
			{
				fprintf (out, "%s  <truncated after %llu frame(s)>\n", indent,
					(unsigned long long)f);
				break;
			}
			fprintf (out, "%s  [%llu] ", indent, (unsigned long long)f);
			const int used = anim_direct (out, data, off, end, track_type);
			if (used < 0)
			{
				fprintf (out, "<undecodable %s value>\n",
					nuanmb_track_type_v2_name (track_type));
				break;
			}
			fputc ('\n', out);
			off += (u64)used;
		}
	}
	else
		fprintf (out, "%s  <unknown compression %u>\n", indent, compression_type);
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

	// Version 1.2 keyframe buffers (one SsbhByteBuffer per entry: relative
	// offset + size). Listed for reference; the payload layouts inside are
	// per-property and not decoded, matching the reference decoder.
	u64 bbuf_base, bbuf_count;
	fprintf (out, "[buffers]\n");
	if (!read_ssbh_array (data, size, NUANMB_FIELDS_OFF + 0x28, &bbuf_base, &bbuf_count)
		|| bbuf_count > NUANMB_MAX_LIST)
		fprintf (out, "  <none>\n");
	else
	{
		for (u64 i = 0; i < bbuf_count; i++)
		{
			const u64 be = bbuf_base + i * 16;
			if (be < bbuf_base || be + 16 > size)
			{
				fprintf (out, "  [%llu] <out of bounds>\n", (unsigned long long)i);
				break;
			}
			const u64 rel = rd_le64 (data + be);
			const u64 count = rd_le64 (data + be + 8);
			if (!rel || be + rel + count < be + rel || be + rel + count > size)
				fprintf (out, "  [%llu] <out of bounds>\n", (unsigned long long)i);
			else
				fprintf (out, "  [%llu] size = %llu\n",
					(unsigned long long)i, (unsigned long long)count);
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

	// File-level keyframe buffer (SsbhByteBuffer: relative offset + size).
	u64 buf_base = 0, buf_size = 0;
	int have_buf = 0;
	{
		const u64 bf = NUANMB_FIELDS_OFF + 0x20;
		if (bf + 16 <= size)
		{
			const u64 rel = rd_le64 (data + bf);
			const u64 count = rd_le64 (data + bf + 8);
			if (rel && bf + rel + count >= bf + rel && bf + rel + count <= size)
			{
				buf_base = bf + rel;
				buf_size = count;
				have_buf = 1;
			}
		}
	}
	fprintf (out, "buffer = %s%llu bytes\n\n",
		have_buf ? "" : "<missing> ", (unsigned long long)buf_size);

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
				print_trackv2 (out, data, size, te, t, "      ",
					buf_base, buf_size, have_buf);
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

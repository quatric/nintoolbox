#include "lib-nurpdb.h"
#include "lib-std.h"

// SSBH NRPD (.nurpdb), little-endian. Reference: ultimate-research/ssbh_lib
// ssbh_lib/src/formats/nrpd.rs (Nrpd::V16). Same container conventions as
// this codebase's other SSBH decoders. See lib-nurpdb.h for this decoder's
// scope note -- the reference itself flags most of this format's nested
// structs as unconfirmed ("Unk*"/TODO), so only the parts it's confident
// about are decoded here.
//
//   0x00  "HBSS" (or "SSBH"), then a u64 at 0x04 (0x40 on every retail file)
//   0x10  "DPRN" ("NRPD" byte-reversed, or literal "NRPD"), u16 major, u16 minor
//   0x18  frame_buffers:      SsbhArray<SsbhEnum64<FrameBuffer>>
//   0x28  state_containers:   SsbhArray<SsbhEnum64<State>>
//   0x38  render_passes:      SsbhArray<RenderPassContainer>
//   0x48  unk_string_list1:   SsbhArray<StringPair>
//   0x58  unk_string_list2:   SsbhArray<SsbhEnum64<UnkItem2>>
//   0x68  unk_list:           SsbhArray<UnkItem1>
//   0x78  unk_width1, unk_height1, unk3..unk8: 8x u32
//   0x98  unk9: SsbhString
//   0xa0  unk_width2, unk_height2: 2x u32
//   0xa8  unk10: u64
//
// An SsbhEnum64<T> field is 16 bytes: a u64 relative offset (relative to the
// field's own position) to the variant's payload, then a u64 discriminant
// selecting which of T's variants it is. Every FrameBuffer/State variant
// this decoder names happens to start with a 'name: SsbhString' field, so
// reading just that one field at the payload's own offset 0 works
// regardless of which variant it is.
//
// A RenderPassContainer is 0x40 bytes, relative to render_passes's own
// element base:
//   0x00 name: SsbhString
//   0x08 unk1: SsbhArray<SsbhEnum64<RenderPassData>>  (count only, not decoded)
//   0x18 unk2: SsbhArray<SsbhEnum64<RenderPassData>>  (count only, not decoded)
//   0x28 unk3: SsbhEnum64<RenderPassUnkData>          (not decoded)
//   0x38 -- 8 bytes padding --
//
// A StringPair is 0x10 bytes: item1, item2 (both SsbhString).
// A UnkItem1 is 0x18 bytes: unk1 (SsbhString), unk2 (SsbhArray<UnkItem3>).
// A UnkItem3 is 0x10 bytes: name, value (both SsbhString).
// Every UnkItem2 variant starts with an SsbhString, same "read field 0" trick
// as FrameBuffer/State.

#define NURPDB_SUBHDR_OFF 0x10
#define NURPDB_ENUM64_SIZE 0x10
#define NURPDB_RENDERPASS_SIZE 0x40
#define NURPDB_STRINGPAIR_SIZE 0x10
#define NURPDB_UNKITEM1_SIZE 0x18
#define NURPDB_UNKITEM3_SIZE 0x10
#define NURPDB_MAX_LIST 65536

bool IsNURPDB (const u8 *data, size_t size)
{
	if (!data || size < NURPDB_SUBHDR_OFF + 8)
		return false;
	if (memcmp (data, "HBSS", 4) && memcmp (data, "SSBH", 4))
		return false;
	return !memcmp (data + NURPDB_SUBHDR_OFF, "DPRN", 4)
		|| !memcmp (data + NURPDB_SUBHDR_OFF, "NRPD", 4);
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

// Follows one SsbhEnum64 array element at 'field_off' (16 bytes: rel offset + u64
// discriminant) and reads the 'name' field (an SsbhString) at the payload's own
// offset 0 -- true for every FrameBuffer/State/UnkItem2 variant.
static void print_enum64_named (FILE *out, const u8 *data, size_t size, u64 field_off,
	u64 idx, const ccp *variant_names, uint n_variants)
{
	if (field_off + 16 > size)
	{
		fprintf (out, "  [%llu] <out of bounds>\n", (unsigned long long)idx);
		return;
	}
	const u64 rel = rd_le64 (data + field_off);
	const u64 discr = rd_le64 (data + field_off + 8);
	ccp variant = discr < n_variants && variant_names[discr] ? variant_names[discr] : "?";

	char name[128] = "";
	if (rel)
	{
		const u64 payload_off = field_off + rel;
		if (payload_off >= field_off)
			read_ssbh_string (name, sizeof (name), data, size, payload_off);
	}
	fprintf (out, "  [%llu] %s (%s)\n", (unsigned long long)idx,
		name[0] ? name : "<unnamed>", variant);
}

static const ccp nurpdb_framebuffer_variant[5]
	= { "Framebuffer0", "Framebuffer1", "UniformBuffer", "Framebuffer3", "Framebuffer4" };
static const ccp nurpdb_state_variant[4]
	= { "Sampler", "RasterizerState", "DepthState", "BlendState" };
static const ccp nurpdb_unkitem2_variant[5]
	= { "UnkItem20", "UnkItem21", "UnkItem22", 0, "UnkItem24" };

enumError DecodeNURPDB_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsNURPDB (data, size))
		return ERR_INVALID_DATA;

	const u16 major = rd_le16 (data + NURPDB_SUBHDR_OFF + 4);
	const u16 minor = rd_le16 (data + NURPDB_SUBHDR_OFF + 6);
	fprintf (out, "#NURPDB\nversion = %u.%u\n\n", major, minor);

	u64 base, count;

	fprintf (out, "[frame_buffers]\n");
	if (read_ssbh_array (data, size, NURPDB_SUBHDR_OFF + 8, &base, &count)
		&& count <= NURPDB_MAX_LIST)
	{
		for (u64 i = 0; i < count; i++)
			print_enum64_named (out, data, size, base + i * NURPDB_ENUM64_SIZE, i,
				nurpdb_framebuffer_variant, 5);
	}
	else
		fprintf (out, "  <none>\n");

	fprintf (out, "\n[state_containers]\n");
	if (read_ssbh_array (data, size, NURPDB_SUBHDR_OFF + 0x18, &base, &count)
		&& count <= NURPDB_MAX_LIST)
	{
		for (u64 i = 0; i < count; i++)
			print_enum64_named (out, data, size, base + i * NURPDB_ENUM64_SIZE, i,
				nurpdb_state_variant, 4);
	}
	else
		fprintf (out, "  <none>\n");

	fprintf (out, "\n[render_passes]\n");
	if (read_ssbh_array (data, size, NURPDB_SUBHDR_OFF + 0x28, &base, &count)
		&& count <= NURPDB_MAX_LIST)
	{
		for (u64 i = 0; i < count; i++)
		{
			const u64 e = base + i * NURPDB_RENDERPASS_SIZE;
			if (e < base || e + NURPDB_RENDERPASS_SIZE > size)
			{
				fprintf (out, "  [%llu] <out of bounds>\n", (unsigned long long)i);
				break;
			}
			char name[128];
			read_ssbh_string (name, sizeof (name), data, size, e);
			u64 b1, c1 = 0, b2, c2 = 0;
			read_ssbh_array (data, size, e + 0x08, &b1, &c1);
			read_ssbh_array (data, size, e + 0x18, &b2, &c2);
			fprintf (out, "  [%llu] %s (unk1_count=%llu, unk2_count=%llu)\n",
				(unsigned long long)i, name[0] ? name : "<unnamed>",
				(unsigned long long)c1, (unsigned long long)c2);
		}
	}
	else
		fprintf (out, "  <none>\n");

	fprintf (out, "\n[unk_string_list1]\n");
	if (read_ssbh_array (data, size, NURPDB_SUBHDR_OFF + 0x38, &base, &count)
		&& count <= NURPDB_MAX_LIST)
	{
		for (u64 i = 0; i < count; i++)
		{
			const u64 e = base + i * NURPDB_STRINGPAIR_SIZE;
			if (e < base || e + NURPDB_STRINGPAIR_SIZE > size)
			{
				fprintf (out, "  [%llu] <out of bounds>\n", (unsigned long long)i);
				break;
			}
			char item1[128], item2[128];
			read_ssbh_string (item1, sizeof (item1), data, size, e);
			read_ssbh_string (item2, sizeof (item2), data, size, e + 8);
			fprintf (out, "  [%llu] %s / %s\n", (unsigned long long)i,
				item1[0] ? item1 : "<unnamed>", item2[0] ? item2 : "<unnamed>");
		}
	}
	else
		fprintf (out, "  <none>\n");

	fprintf (out, "\n[unk_string_list2]\n");
	if (read_ssbh_array (data, size, NURPDB_SUBHDR_OFF + 0x48, &base, &count)
		&& count <= NURPDB_MAX_LIST)
	{
		for (u64 i = 0; i < count; i++)
			print_enum64_named (out, data, size, base + i * NURPDB_ENUM64_SIZE, i,
				nurpdb_unkitem2_variant, 5);
	}
	else
		fprintf (out, "  <none>\n");

	fprintf (out, "\n[unk_list]\n");
	if (read_ssbh_array (data, size, NURPDB_SUBHDR_OFF + 0x58, &base, &count)
		&& count <= NURPDB_MAX_LIST)
	{
		for (u64 i = 0; i < count; i++)
		{
			const u64 e = base + i * NURPDB_UNKITEM1_SIZE;
			if (e < base || e + NURPDB_UNKITEM1_SIZE > size)
			{
				fprintf (out, "  [%llu] <out of bounds>\n", (unsigned long long)i);
				break;
			}
			char name[128];
			read_ssbh_string (name, sizeof (name), data, size, e);
			fprintf (out, "  [%llu] %s\n", (unsigned long long)i, name[0] ? name : "<unnamed>");

			u64 sub_base, sub_count;
			if (read_ssbh_array (data, size, e + 8, &sub_base, &sub_count)
				&& sub_count <= NURPDB_MAX_LIST)
			{
				for (u64 j = 0; j < sub_count; j++)
				{
					const u64 se = sub_base + j * NURPDB_UNKITEM3_SIZE;
					if (se < sub_base || se + NURPDB_UNKITEM3_SIZE > size)
					{
						fprintf (out, "      [%llu] <out of bounds>\n", (unsigned long long)j);
						break;
					}
					char sname[128], svalue[128];
					read_ssbh_string (sname, sizeof (sname), data, size, se);
					read_ssbh_string (svalue, sizeof (svalue), data, size, se + 8);
					fprintf (out, "      [%llu] %s = %s\n", (unsigned long long)j,
						sname[0] ? sname : "<unnamed>", svalue[0] ? svalue : "<unnamed>");
				}
			}
		}
	}
	else
		fprintf (out, "  <none>\n");

	const u64 tail_off = NURPDB_SUBHDR_OFF + 0x68;
	if (tail_off + 0x38 <= size)
	{
		char unk9[128];
		read_ssbh_string (unk9, sizeof (unk9), data, size, tail_off + 0x20);
		fprintf (out, "\n"
			"unk_width1 = %u, unk_height1 = %u\n"
			"unk3..unk8 = %u, %u, %u, %u, %u, %u\n"
			"unk9 = %s\n"
			"unk_width2 = %u, unk_height2 = %u\n"
			"unk10 = %llu\n",
			rd_le32 (data + tail_off), rd_le32 (data + tail_off + 4),
			rd_le32 (data + tail_off + 8), rd_le32 (data + tail_off + 0xc),
			rd_le32 (data + tail_off + 0x10), rd_le32 (data + tail_off + 0x14),
			rd_le32 (data + tail_off + 0x18), rd_le32 (data + tail_off + 0x1c),
			unk9[0] ? unk9 : "<empty>",
			rd_le32 (data + tail_off + 0x28), rd_le32 (data + tail_off + 0x2c),
			(unsigned long long)rd_le64 (data + tail_off + 0x30));
	}

	return ERR_OK;
}

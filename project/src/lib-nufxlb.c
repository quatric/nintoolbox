#include "lib-nufxlb.h"
#include "lib-std.h"

// SSBH NUFX (.nufxlb), little-endian. Reference: ultimate-research/ssbh_lib
// ssbh_lib/src/formats/nufx.rs. Same container conventions as this codebase's
// other SSBH decoders (NUMSHB, NUSHDB, NUMATB, NUSKTB): every pointer is a
// 64-bit offset relative to the field that holds it.
//
//   0x00  "HBSS" (or "SSBH"), then a u64 at 0x04 (0x40 on every retail file)
//   0x10  "XFUN" ("NUFX" byte-reversed, or literal "NUFX"), u16 major, u16 minor
//   0x18  programs:         SsbhArray<ShaderProgramV0|V1>
//   0x28  unk_string_list:  SsbhArray<UnkItem> (not decoded further -- see
//                           DecodeNUFXLB_Text's scope note)
//
// A ShaderProgram entry, relative to the programs array's own element base:
// version 1.0 (V0) is 0x50 bytes, version 1.1 (V1) is 0x60 bytes (V1 adds a
// vertex_attributes array after the shader stages):
//
//   0x00  name:                 SsbhString
//   0x08  render_pass:          SsbhString
//   0x10  shaders (ShaderStages, six SsbhStrings):
//           0x10  vertex_shader
//           0x18  unk_shader1   (possibly tessellation-control)
//           0x20  unk_shader2   (possibly tessellation-evaluation)
//           0x28  geometry_shader
//           0x30  pixel_shader
//           0x38  compute_shader
//   0x40  vertex_attributes:    SsbhArray<VertexAttribute> -- V1 only
//   0x40 (V0) / 0x50 (V1)  material_parameters: SsbhArray<MaterialParameter>
//
// A VertexAttribute is 0x10 bytes: name (SsbhString), attribute_name (SsbhString).
//
// A MaterialParameter is 0x18 bytes: param_id (u64, one of lib-numatb.c's
// matl_param_names ids), parameter_name (SsbhString), then 8 bytes padding
// the reference's own reader explicitly skips.

#define NUFXLB_SUBHDR_OFF 0x10
#define NUFXLB_PROGRAM_SIZE_V0 0x50
#define NUFXLB_PROGRAM_SIZE_V1 0x60
#define NUFXLB_VATTR_SIZE 0x10
#define NUFXLB_MATPARAM_SIZE 0x18
#define NUFXLB_MAX_PROGRAMS 65536
#define NUFXLB_MAX_LIST 4096

static const ccp nufxlb_stage_name[6]
	= { "vertex", "unk1", "unk2", "geometry", "pixel", "compute" };

bool IsNUFXLB (const u8 *data, size_t size)
{
	if (!data || size < NUFXLB_SUBHDR_OFF + 8)
		return false;
	if (memcmp (data, "HBSS", 4) && memcmp (data, "SSBH", 4))
		return false;
	return !memcmp (data + NUFXLB_SUBHDR_OFF, "XFUN", 4)
		|| !memcmp (data + NUFXLB_SUBHDR_OFF, "NUFX", 4);
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

static void print_shader_stage (FILE *out, const u8 *data, size_t size, u64 field_off, ccp label)
{
	char name[256];
	if (read_ssbh_string (name, sizeof (name), data, size, field_off) && name[0])
		fprintf (out, "      %s: %s\n", label, name);
}

enumError DecodeNUFXLB_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsNUFXLB (data, size))
		return ERR_INVALID_DATA;

	const u16 major = rd_le16 (data + NUFXLB_SUBHDR_OFF + 4);
	const u16 minor = rd_le16 (data + NUFXLB_SUBHDR_OFF + 6);
	const bool v1 = major == 1 && minor == 1;
	const u64 program_size = v1 ? NUFXLB_PROGRAM_SIZE_V1 : NUFXLB_PROGRAM_SIZE_V0;

	u64 prog_base, prog_count;
	const bool have_programs
		= read_ssbh_array (data, size, NUFXLB_SUBHDR_OFF + 8, &prog_base, &prog_count);

	fprintf (out,
		"#NUFXLB\n"
		"version = %u.%u\n"
		"program_count = %llu\n\n"
		"[programs]\n",
		major, minor, have_programs ? (unsigned long long)prog_count : 0);

	if (!have_programs || prog_count > NUFXLB_MAX_PROGRAMS)
	{
		fprintf (out, "  <no program array>\n");
		return ERR_OK;
	}

	for (u64 i = 0; i < prog_count; i++)
	{
		const u64 entry_off = prog_base + i * program_size;
		if (entry_off < prog_base || entry_off + program_size > size)
		{
			fprintf (out, "  [%llu] <entry out of bounds>\n", (unsigned long long)i);
			break;
		}

		char name[256], render_pass[256];
		read_ssbh_string (name, sizeof (name), data, size, entry_off);
		read_ssbh_string (render_pass, sizeof (render_pass), data, size, entry_off + 8);

		fprintf (out, "  [%llu] %s\n    render_pass = %s\n    shaders:\n", (unsigned long long)i,
			name[0] ? name : "<unnamed>", render_pass[0] ? render_pass : "<unnamed>");
		for (uint s = 0; s < 6; s++)
			print_shader_stage (
				out, data, size, entry_off + 0x10 + (u64)s * 8, nufxlb_stage_name[s]);

		u64 attrs_field_off = 0, matparam_field_off;
		if (v1)
		{
			attrs_field_off = entry_off + 0x40;
			matparam_field_off = entry_off + 0x50;

			u64 attr_base, attr_count;
			if (read_ssbh_array (data, size, attrs_field_off, &attr_base, &attr_count)
				&& attr_count <= NUFXLB_MAX_LIST)
			{
				fprintf (out, "    vertex_attributes:\n");
				for (u64 a = 0; a < attr_count; a++)
				{
					const u64 attr_off = attr_base + a * NUFXLB_VATTR_SIZE;
					if (attr_off < attr_base || attr_off + NUFXLB_VATTR_SIZE > size)
					{
						fprintf (out, "      [%llu] <out of bounds>\n", (unsigned long long)a);
						break;
					}
					char aname[128], aattr[128];
					read_ssbh_string (aname, sizeof (aname), data, size, attr_off);
					read_ssbh_string (aattr, sizeof (aattr), data, size, attr_off + 8);
					fprintf (out, "      [%llu] %s -> %s\n", (unsigned long long)a,
						aname[0] ? aname : "<unnamed>", aattr[0] ? aattr : "<unnamed>");
				}
			}
		}
		else
			matparam_field_off = entry_off + 0x40;

		u64 mp_base, mp_count;
		if (read_ssbh_array (data, size, matparam_field_off, &mp_base, &mp_count)
			&& mp_count <= NUFXLB_MAX_LIST)
		{
			fprintf (out, "    material_parameters:\n");
			for (u64 m = 0; m < mp_count; m++)
			{
				const u64 mp_off = mp_base + m * NUFXLB_MATPARAM_SIZE;
				if (mp_off < mp_base || mp_off + NUFXLB_MATPARAM_SIZE > size)
				{
					fprintf (out, "      [%llu] <out of bounds>\n", (unsigned long long)m);
					break;
				}
				const u64 param_id = rd_le64 (data + mp_off);
				char pname[128];
				read_ssbh_string (pname, sizeof (pname), data, size, mp_off + 8);
				fprintf (out, "      [%llu] %s (id=%llu)\n", (unsigned long long)m,
					pname[0] ? pname : "<unnamed>", (unsigned long long)param_id);
			}
		}
	}

	return ERR_OK;
}

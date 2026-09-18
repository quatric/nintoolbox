#include "lib-numatb.h"
#include "lib-std.h"

// SSBH MATL (.numatb), little-endian. Reference: ultimate-research/ssbh_lib
// ssbh_lib/src/{lib.rs,arrays.rs,strings.rs,enums.rs,formats/matl.rs}. Same
// container conventions as this codebase's other SSBH decoders (NUMSHB,
// NUSHDB): every pointer is a 64-bit offset relative to the field that holds
// it.
//
//   0x00  "HBSS" (or "SSBH"), then a u64 at 0x04 (0x40 on every retail file)
//   0x10  "LTAM" ("MATL" byte-reversed, or literal "MATL"), u16 major, u16 minor
//   0x18  entries: SsbhArray<MatlEntry> -- one relative offset (u64) + count (u64)
//
// A MatlEntry is 0x20 bytes, relative to the array's own element base (same
// layout for both version 1.5 and 1.6):
//
//   0x00  material_label: SsbhString  (u64 relative offset to a NUL-terminated
//                                      string)
//   0x08  attributes:     SsbhArray<Attribute> (u64 relative offset + u64 count)
//   0x18  shader_label:   SsbhString
//
// An Attribute is 0x18 bytes, relative to the attributes array's own element base:
//
//   0x00  param_id: u64          -- see matl_param_names[] below
//   0x08  param: SsbhEnum64      -- u64 relative offset (relative to THIS field's
//                                   own position, i.e. entry_off+0x08) to the
//                                   value, then u64 data_type discriminant
//
// The value at that offset, sized/typed per data_type:
//
//   1  Float           f32                                              (4 bytes)
//   2  Boolean         u32 (0/1)                                        (4 bytes)
//   5  Vector4         4x f32 (x,y,z,w)                                 (16 bytes)
//   7  Color4f (Unk7)  4x f32 (r,g,b,a)                                 (16 bytes)
//   11 String          SsbhString (another relative-offset string)      (8 bytes)
//   14 Sampler         6x u32 (wraps/wrapt/wrapr/min/mag/filter_type),
//                      Color4f border, 2x u32 unk, f32 lod_bias,
//                      u32 max_anisotropy                               (0x38 bytes)
//   16 UvTransform     5x f32 (scale_u, scale_v, rotation, translate_u,
//                      translate_v)                                    (20 bytes)
//   17 BlendState      version-dependent (see blend_state_size below)
//   18 RasterizerState version-dependent (see rasterizer_state_size below)
//
// BlendState/RasterizerState differ in size between file version 1.5 and 1.6
// (matl.rs's BlendStateV15/V16, RasterizerStateV15/V16); only the leading
// fields this decoder prints (blend factors/operations, cull mode, fill mode)
// are common to both, at the same relative offsets.

#define NUMATB_SUBHDR_OFF 0x10
#define NUMATB_ENTRY_SIZE 0x20
#define NUMATB_ATTR_SIZE 0x18
#define NUMATB_MAX_ENTRIES 65536
#define NUMATB_MAX_ATTRS 4096

// Sorted by id (see matl.rs's ParamId enum) for binary search.
typedef struct { u32 id; ccp name; } matl_param_name_t;
static const matl_param_name_t matl_param_names[] = {
	{ 0, "Diffuse" },
	{ 1, "Specular" },
	{ 2, "Ambient" },
	{ 3, "BlendMap" },
	{ 4, "Transparency" },
	{ 5, "DiffuseMapLayer1" },
	{ 6, "CosinePower" },
	{ 7, "SpecularPower" },
	{ 8, "Fresnel" },
	{ 9, "Roughness" },
	{ 10, "EmissiveScale" },
	{ 11, "EnableDiffuse" },
	{ 12, "EnableSpecular" },
	{ 13, "EnableAmbient" },
	{ 14, "DiffuseMapLayer2" },
	{ 15, "EnableTransparency" },
	{ 16, "EnableOpacity" },
	{ 17, "EnableCosinePower" },
	{ 18, "EnableSpecularPower" },
	{ 19, "EnableFresnel" },
	{ 20, "EnableRoughness" },
	{ 21, "EnableEmissiveScale" },
	{ 22, "WorldMatrix" },
	{ 23, "ViewMatrix" },
	{ 24, "ProjectionMatrix" },
	{ 25, "WorldViewMatrix" },
	{ 26, "ViewInverseMatrix" },
	{ 27, "ViewProjectionMatrix" },
	{ 28, "WorldViewProjectionMatrix" },
	{ 29, "WorldInverseTransposeMatrix" },
	{ 30, "DiffuseMap" },
	{ 31, "SpecularMap" },
	{ 32, "AmbientMap" },
	{ 33, "EmissiveMap" },
	{ 34, "SpecularMapLayer1" },
	{ 35, "TransparencyMap" },
	{ 36, "NormalMap" },
	{ 37, "DiffuseCubeMap" },
	{ 38, "ReflectionMap" },
	{ 39, "ReflectionCubeMap" },
	{ 40, "RefractionMap" },
	{ 41, "AmbientOcclusionMap" },
	{ 42, "LightMap" },
	{ 43, "AnisotropicMap" },
	{ 44, "RoughnessMap" },
	{ 45, "ReflectionMask" },
	{ 46, "OpacityMask" },
	{ 47, "UseDiffuseMap" },
	{ 48, "UseSpecularMap" },
	{ 49, "UseAmbientMap" },
	{ 50, "UseEmissiveMap" },
	{ 51, "UseTranslucencyMap" },
	{ 52, "UseTransparencyMap" },
	{ 53, "UseNormalMap" },
	{ 54, "UseDiffuseCubeMap" },
	{ 55, "UseReflectionMap" },
	{ 56, "UseReflectionCubeMap" },
	{ 57, "UseRefractionMap" },
	{ 58, "UseAmbientOcclusionMap" },
	{ 59, "UseLightMap" },
	{ 60, "UseAnisotropicMap" },
	{ 61, "UseRoughnessMap" },
	{ 62, "UseReflectionMask" },
	{ 63, "UseOpacityMask" },
	{ 64, "DiffuseSampler" },
	{ 65, "SpecularSampler" },
	{ 66, "NormalSampler" },
	{ 67, "ReflectionSampler" },
	{ 68, "SpecularMapLayer2" },
	{ 69, "NormalMapLayer1" },
	{ 70, "NormalMapBc5" },
	{ 71, "NormalMapLayer2" },
	{ 72, "RoughnessMapLayer1" },
	{ 73, "RoughnessMapLayer2" },
	{ 74, "UseDiffuseUvTransform1" },
	{ 75, "UseDiffuseUvTransform2" },
	{ 76, "UseSpecularUvTransform1" },
	{ 77, "UseSpecularUvTransform2" },
	{ 78, "UseNormalUvTransform1" },
	{ 79, "UseNormalUvTransform2" },
	{ 80, "ShadowDepthBias" },
	{ 81, "ShadowMap0" },
	{ 82, "ShadowMap1" },
	{ 83, "ShadowMap2" },
	{ 84, "ShadowMap3" },
	{ 85, "ShadowMap4" },
	{ 86, "ShadowMap5" },
	{ 87, "ShadowMap6" },
	{ 88, "ShadowMap7" },
	{ 89, "CastShadow" },
	{ 90, "ReceiveShadow" },
	{ 91, "ShadowMapSampler" },
	{ 92, "Texture0" },
	{ 93, "Texture1" },
	{ 94, "Texture2" },
	{ 95, "Texture3" },
	{ 96, "Texture4" },
	{ 97, "Texture5" },
	{ 98, "Texture6" },
	{ 99, "Texture7" },
	{ 100, "Texture8" },
	{ 101, "Texture9" },
	{ 102, "Texture10" },
	{ 103, "Texture11" },
	{ 104, "Texture12" },
	{ 105, "Texture13" },
	{ 106, "Texture14" },
	{ 107, "Texture15" },
	{ 108, "Sampler0" },
	{ 109, "Sampler1" },
	{ 110, "Sampler2" },
	{ 111, "Sampler3" },
	{ 112, "Sampler4" },
	{ 113, "Sampler5" },
	{ 114, "Sampler6" },
	{ 115, "Sampler7" },
	{ 116, "Sampler8" },
	{ 117, "Sampler9" },
	{ 118, "Sampler10" },
	{ 119, "Sampler11" },
	{ 120, "Sampler12" },
	{ 121, "Sampler13" },
	{ 122, "Sampler14" },
	{ 123, "Sampler15" },
	{ 124, "CustomBuffer0" },
	{ 125, "CustomBuffer1" },
	{ 126, "CustomBuffer2" },
	{ 127, "CustomBuffer3" },
	{ 128, "CustomBuffer4" },
	{ 129, "CustomBuffer5" },
	{ 130, "CustomBuffer6" },
	{ 131, "CustomBuffer7" },
	{ 132, "CustomMatrix0" },
	{ 133, "CustomMatrix1" },
	{ 134, "CustomMatrix2" },
	{ 135, "CustomMatrix3" },
	{ 136, "CustomMatrix4" },
	{ 137, "CustomMatrix5" },
	{ 138, "CustomMatrix6" },
	{ 139, "CustomMatrix7" },
	{ 140, "CustomMatrix8" },
	{ 141, "CustomMatrix9" },
	{ 142, "CustomMatrix10" },
	{ 143, "CustomMatrix11" },
	{ 144, "CustomMatrix12" },
	{ 145, "CustomMatrix13" },
	{ 146, "CustomMatrix14" },
	{ 147, "CustomMatrix15" },
	{ 148, "CustomMatrix16" },
	{ 149, "CustomMatrix17" },
	{ 150, "CustomMatrix18" },
	{ 151, "CustomMatrix19" },
	{ 152, "CustomVector0" },
	{ 153, "CustomVector1" },
	{ 154, "CustomVector2" },
	{ 155, "CustomVector3" },
	{ 156, "CustomVector4" },
	{ 157, "CustomVector5" },
	{ 158, "CustomVector6" },
	{ 159, "CustomVector7" },
	{ 160, "CustomVector8" },
	{ 161, "CustomVector9" },
	{ 162, "CustomVector10" },
	{ 163, "CustomVector11" },
	{ 164, "CustomVector12" },
	{ 165, "CustomVector13" },
	{ 166, "CustomVector14" },
	{ 167, "CustomVector15" },
	{ 168, "CustomVector16" },
	{ 169, "CustomVector17" },
	{ 170, "CustomVector18" },
	{ 171, "CustomVector19" },
	{ 172, "CustomColor0" },
	{ 173, "CustomColor1" },
	{ 174, "CustomColor2" },
	{ 175, "CustomColor3" },
	{ 176, "CustomColor4" },
	{ 177, "CustomColor5" },
	{ 178, "CustomColor6" },
	{ 179, "CustomColor7" },
	{ 180, "CustomColor8" },
	{ 181, "CustomColor9" },
	{ 182, "CustomColor10" },
	{ 183, "CustomColor11" },
	{ 184, "CustomColor12" },
	{ 185, "CustomColor13" },
	{ 186, "CustomColor14" },
	{ 187, "CustomColor15" },
	{ 188, "CustomColor16" },
	{ 189, "CustomColor17" },
	{ 190, "CustomColor18" },
	{ 191, "CustomColor19" },
	{ 192, "CustomFloat0" },
	{ 193, "CustomFloat1" },
	{ 194, "CustomFloat2" },
	{ 195, "CustomFloat3" },
	{ 196, "CustomFloat4" },
	{ 197, "CustomFloat5" },
	{ 198, "CustomFloat6" },
	{ 199, "CustomFloat7" },
	{ 200, "CustomFloat8" },
	{ 201, "CustomFloat9" },
	{ 202, "CustomFloat10" },
	{ 203, "CustomFloat11" },
	{ 204, "CustomFloat12" },
	{ 205, "CustomFloat13" },
	{ 206, "CustomFloat14" },
	{ 207, "CustomFloat15" },
	{ 208, "CustomFloat16" },
	{ 209, "CustomFloat17" },
	{ 210, "CustomFloat18" },
	{ 211, "CustomFloat19" },
	{ 212, "CustomInteger0" },
	{ 213, "CustomInteger1" },
	{ 214, "CustomInteger2" },
	{ 215, "CustomInteger3" },
	{ 216, "CustomInteger4" },
	{ 217, "CustomInteger5" },
	{ 218, "CustomInteger6" },
	{ 219, "CustomInteger7" },
	{ 220, "CustomInteger8" },
	{ 221, "CustomInteger9" },
	{ 222, "CustomInteger10" },
	{ 223, "CustomInteger11" },
	{ 224, "CustomInteger12" },
	{ 225, "CustomInteger13" },
	{ 226, "CustomInteger14" },
	{ 227, "CustomInteger15" },
	{ 228, "CustomInteger16" },
	{ 229, "CustomInteger17" },
	{ 230, "CustomInteger18" },
	{ 231, "CustomInteger19" },
	{ 232, "CustomBoolean0" },
	{ 233, "CustomBoolean1" },
	{ 234, "CustomBoolean2" },
	{ 235, "CustomBoolean3" },
	{ 236, "CustomBoolean4" },
	{ 237, "CustomBoolean5" },
	{ 238, "CustomBoolean6" },
	{ 239, "CustomBoolean7" },
	{ 240, "CustomBoolean8" },
	{ 241, "CustomBoolean9" },
	{ 242, "CustomBoolean10" },
	{ 243, "CustomBoolean11" },
	{ 244, "CustomBoolean12" },
	{ 245, "CustomBoolean13" },
	{ 246, "CustomBoolean14" },
	{ 247, "CustomBoolean15" },
	{ 248, "CustomBoolean16" },
	{ 249, "CustomBoolean17" },
	{ 250, "CustomBoolean18" },
	{ 251, "CustomBoolean19" },
	{ 252, "UvTransform0" },
	{ 253, "UvTransform1" },
	{ 254, "UvTransform2" },
	{ 255, "UvTransform3" },
	{ 256, "UvTransform4" },
	{ 257, "UvTransform5" },
	{ 258, "UvTransform6" },
	{ 259, "UvTransform7" },
	{ 260, "UvTransform8" },
	{ 261, "UvTransform9" },
	{ 262, "UvTransform10" },
	{ 263, "UvTransform11" },
	{ 264, "UvTransform12" },
	{ 265, "UvTransform13" },
	{ 266, "UvTransform14" },
	{ 267, "UvTransform15" },
	{ 268, "DiffuseUvTransform1" },
	{ 269, "DiffuseUvTransform2" },
	{ 270, "SpecularUvTransform1" },
	{ 271, "SpecularUvTransform2" },
	{ 272, "NormalUvTransform1" },
	{ 273, "NormalUvTransform2" },
	{ 274, "DiffuseUvTransform" },
	{ 275, "SpecularUvTransform" },
	{ 276, "NormalUvTransform" },
	{ 277, "UseDiffuseUvTransform" },
	{ 278, "UseSpecularUvTransform" },
	{ 279, "UseNormalUvTransform" },
	{ 280, "BlendState0" },
	{ 281, "BlendState1" },
	{ 282, "BlendState2" },
	{ 283, "BlendState3" },
	{ 284, "BlendState4" },
	{ 285, "BlendState5" },
	{ 286, "BlendState6" },
	{ 287, "BlendState7" },
	{ 288, "BlendState8" },
	{ 289, "BlendState9" },
	{ 290, "BlendState10" },
	{ 291, "RasterizerState0" },
	{ 292, "RasterizerState1" },
	{ 293, "RasterizerState2" },
	{ 294, "RasterizerState3" },
	{ 295, "RasterizerState4" },
	{ 296, "RasterizerState5" },
	{ 297, "RasterizerState6" },
	{ 298, "RasterizerState7" },
	{ 299, "RasterizerState8" },
	{ 300, "RasterizerState9" },
	{ 301, "RasterizerState10" },
	{ 302, "ShadowColor" },
	{ 303, "EmissiveMapLayer1" },
	{ 304, "EmissiveMapLayer2" },
	{ 305, "AlphaTestFunc" },
	{ 306, "AlphaTestRef" },
	{ 307, "Texture16" },
	{ 308, "Texture17" },
	{ 309, "Texture18" },
	{ 310, "Texture19" },
	{ 311, "Sampler16" },
	{ 312, "Sampler17" },
	{ 313, "Sampler18" },
	{ 314, "Sampler19" },
	{ 315, "CustomVector20" },
	{ 316, "CustomVector21" },
	{ 317, "CustomVector22" },
	{ 318, "CustomVector23" },
	{ 319, "CustomVector24" },
	{ 320, "CustomVector25" },
	{ 321, "CustomVector26" },
	{ 322, "CustomVector27" },
	{ 323, "CustomVector28" },
	{ 324, "CustomVector29" },
	{ 325, "CustomVector30" },
	{ 326, "CustomVector31" },
	{ 327, "CustomVector32" },
	{ 328, "CustomVector33" },
	{ 329, "CustomVector34" },
	{ 330, "CustomVector35" },
	{ 331, "CustomVector36" },
	{ 332, "CustomVector37" },
	{ 333, "CustomVector38" },
	{ 334, "CustomVector39" },
	{ 335, "CustomVector40" },
	{ 336, "CustomVector41" },
	{ 337, "CustomVector42" },
	{ 338, "CustomVector43" },
	{ 339, "CustomVector44" },
	{ 340, "CustomVector45" },
	{ 341, "CustomVector46" },
	{ 342, "CustomVector47" },
	{ 343, "CustomVector48" },
	{ 344, "CustomVector49" },
	{ 345, "CustomVector50" },
	{ 346, "CustomVector51" },
	{ 347, "CustomVector52" },
	{ 348, "CustomVector53" },
	{ 349, "CustomVector54" },
	{ 350, "CustomVector55" },
	{ 351, "CustomVector56" },
	{ 352, "CustomVector57" },
	{ 353, "CustomVector58" },
	{ 354, "CustomVector59" },
	{ 355, "CustomVector60" },
	{ 356, "CustomVector61" },
	{ 357, "CustomVector62" },
	{ 358, "CustomVector63" },
	{ 359, "UseBaseColorMap" },
	{ 360, "UseMetallicMap" },
	{ 361, "BaseColorMap" },
	{ 362, "BaseColorMapLayer1" },
	{ 363, "MetallicMap" },
	{ 364, "MetallicMapLayer1" },
	{ 365, "DiffuseLightingAoOffset" },
};
#define MATL_PARAM_NAMES_COUNT (sizeof (matl_param_names) / sizeof (*matl_param_names))

static ccp matl_param_name (u64 id)
{
	uint lo = 0, hi = MATL_PARAM_NAMES_COUNT;
	while (lo < hi)
	{
		const uint mid = (lo + hi) / 2;
		if (matl_param_names[mid].id == id)
			return matl_param_names[mid].name;
		if (matl_param_names[mid].id < id)
			lo = mid + 1;
		else
			hi = mid;
	}
	return 0;
}

static const ccp matl_cull_mode_name[]	  = { "Back", "Front", "Disabled" };
static const ccp matl_fill_mode_name[]	  = { "Line", "Solid" };
static const ccp matl_wrap_mode_name[]	  = { "Repeat", "ClampToEdge", "MirroredRepeat", "ClampToBorder" };
static const ccp matl_min_filter_name[]  = { "Nearest", "LinearMipmapLinear", "LinearMipmapLinear2" };
static const ccp matl_mag_filter_name[]  = { "Nearest", "Linear", "Linear2" };
static const ccp matl_filter_type_name[] = { "Default", "Default2", "AnisotropicFiltering" };
static const ccp matl_blend_op_name[]	  = { "Add", "Subtract", "ReverseSubtract", "Minimum", "Maximum" };
static const ccp matl_blend_factor_name[] = {
	"Zero", "One", "SourceAlpha", "DestinationAlpha", "SourceColor", "DestinationColor",
	"OneMinusSourceAlpha", "OneMinusDestinationAlpha", "OneMinusSourceColor",
	"OneMinusDestinationColor", "SourceAlphaSaturate", "Source1Alpha", "Source1Color",
	"OneMinusSource1Alpha", "OneMinusSource1Color"
};

static ccp matl_enum_name (const ccp *table, uint table_len, u32 val, char *fallback, uint fallback_sz)
{
	if (val < table_len)
		return table[val];
	snprintf (fallback, fallback_sz, "?%u", val);
	return fallback;
}

bool IsNUMATB (const u8 *data, size_t size)
{
	if (!data || size < NUMATB_SUBHDR_OFF + 8)
		return false;
	if (memcmp (data, "HBSS", 4) && memcmp (data, "SSBH", 4))
		return false;
	return !memcmp (data + NUMATB_SUBHDR_OFF, "LTAM", 4)
		|| !memcmp (data + NUMATB_SUBHDR_OFF, "MATL", 4);
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

static void print_f32 (FILE *out, ccp label, const u8 *data, u64 off)
{
	fprintf (out, "%s%g", label, (double)read_f32 (data, off));
}

enumError DecodeNUMATB_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsNUMATB (data, size))
		return ERR_INVALID_DATA;

	const u16 major = rd_le16 (data + NUMATB_SUBHDR_OFF + 4);
	const u16 minor = rd_le16 (data + NUMATB_SUBHDR_OFF + 6);
	const bool v16 = major == 1 && minor == 6;

	const u64 array_field_off = NUMATB_SUBHDR_OFF + 8;
	if (array_field_off + 16 > size)
		return ERROR0 (ERR_INVALID_DATA, "NUMATB: file shorter than the fixed header\n");

	const u64 array_rel = rd_le64 (data + array_field_off);
	const u64 entry_count = rd_le64 (data + array_field_off + 8);

	fprintf (out, "#NUMATB\n"
		"version = %u.%u\n"
		"entry_count = %llu\n\n"
		"[materials]\n",
		major, minor, (unsigned long long)entry_count);

	if (!array_rel || entry_count > NUMATB_MAX_ENTRIES)
	{
		fprintf (out, "  <no material array>\n");
		return ERR_OK;
	}

	const u64 array_base = array_field_off + array_rel;
	if (array_base < array_field_off) // 64-bit wrap guard
		return ERROR0 (ERR_INVALID_DATA, "NUMATB: material array offset overflow\n");

	for (u64 i = 0; i < entry_count; i++)
	{
		const u64 entry_off = array_base + i * NUMATB_ENTRY_SIZE;
		if (entry_off < array_base || entry_off + NUMATB_ENTRY_SIZE > size)
		{
			fprintf (out, "  [%llu] <entry out of bounds>\n", (unsigned long long)i);
			break;
		}

		char material_label[256], shader_label[256];
		read_ssbh_string (material_label, sizeof (material_label), data, size, entry_off);
		read_ssbh_string (shader_label, sizeof (shader_label), data, size, entry_off + 0x18);

		fprintf (out, "  [%llu] %s\n    shader = %s\n",
			(unsigned long long)i, material_label[0] ? material_label : "<unnamed>",
			shader_label[0] ? shader_label : "<unnamed>");

		const u64 attr_array_field_off = entry_off + 0x08;
		const u64 attr_rel = rd_le64 (data + attr_array_field_off);
		const u64 attr_count = rd_le64 (data + attr_array_field_off + 8);
		if (!attr_rel || attr_count > NUMATB_MAX_ATTRS)
			continue;
		const u64 attr_base = attr_array_field_off + attr_rel;
		if (attr_base < attr_array_field_off)
			continue;

		for (u64 a = 0; a < attr_count; a++)
		{
			const u64 attr_off = attr_base + a * NUMATB_ATTR_SIZE;
			if (attr_off < attr_base || attr_off + NUMATB_ATTR_SIZE > size)
			{
				fprintf (out, "    [%llu] <attribute out of bounds>\n", (unsigned long long)a);
				break;
			}

			const u64 param_id = rd_le64 (data + attr_off);
			ccp param_name = matl_param_name (param_id);
			fprintf (out, "    [%llu] %s (id=%llu) = ", (unsigned long long)a,
				param_name ? param_name : "?", (unsigned long long)param_id);

			const u64 enum_field_off = attr_off + 0x08;
			const u64 val_rel = rd_le64 (data + enum_field_off);
			const u64 data_type = rd_le64 (data + enum_field_off + 8);
			if (!val_rel)
			{
				fprintf (out, "<none>\n");
				continue;
			}
			const u64 val_off = enum_field_off + val_rel;
			if (val_off < enum_field_off)
			{
				fprintf (out, "<offset overflow>\n");
				continue;
			}

			switch (data_type)
			{
			case 1: // Float
				if (val_off + 4 > size) { fprintf (out, "<out of bounds>\n"); break; }
				print_f32 (out, "", data, val_off);
				fprintf (out, "\n");
				break;

			case 2: // Boolean
				if (val_off + 4 > size) { fprintf (out, "<out of bounds>\n"); break; }
				fprintf (out, "%s\n", rd_le32 (data + val_off) ? "true" : "false");
				break;

			case 5: // Vector4
			case 7: // Color4f (Unk7)
				if (val_off + 16 > size) { fprintf (out, "<out of bounds>\n"); break; }
				print_f32 (out, "(", data, val_off); print_f32 (out, ", ", data, val_off + 4);
				print_f32 (out, ", ", data, val_off + 8); print_f32 (out, ", ", data, val_off + 12);
				fprintf (out, ")\n");
				break;

			case 11: // String (SsbhString, another relative-offset field)
			{
				char str[256];
				read_ssbh_string (str, sizeof (str), data, size, val_off);
				fprintf (out, "\"%s\"\n", str);
				break;
			}

			case 14: // Sampler
			{
				if (val_off + 0x38 > size) { fprintf (out, "<out of bounds>\n"); break; }
				char fb1[16], fb2[16], fb3[16], fb4[16], fb5[16], fb6[16];
				fprintf (out, "Sampler(wrap=%s/%s/%s, min=%s, mag=%s, filter=%s,"
					" lod_bias=%g, max_aniso=%u)\n",
					matl_enum_name (matl_wrap_mode_name, 4, rd_le32 (data + val_off), fb1, sizeof (fb1)),
					matl_enum_name (matl_wrap_mode_name, 4, rd_le32 (data + val_off + 4), fb2, sizeof (fb2)),
					matl_enum_name (matl_wrap_mode_name, 4, rd_le32 (data + val_off + 8), fb3, sizeof (fb3)),
					matl_enum_name (matl_min_filter_name, 3, rd_le32 (data + val_off + 12), fb4, sizeof (fb4)),
					matl_enum_name (matl_mag_filter_name, 3, rd_le32 (data + val_off + 16), fb5, sizeof (fb5)),
					matl_enum_name (matl_filter_type_name, 3, rd_le32 (data + val_off + 20), fb6, sizeof (fb6)),
					(double)read_f32 (data, val_off + 0x30),
					rd_le32 (data + val_off + 0x34));
				break;
			}

			case 16: // UvTransform
				if (val_off + 20 > size) { fprintf (out, "<out of bounds>\n"); break; }
				fprintf (out, "UvTransform(scale=%g/%g, rot=%g, translate=%g/%g)\n",
					(double)read_f32 (data, val_off),
					(double)read_f32 (data, val_off + 4),
					(double)read_f32 (data, val_off + 8),
					(double)read_f32 (data, val_off + 12),
					(double)read_f32 (data, val_off + 16));
				break;

			case 17: // BlendState (leading fields are the same layout in v1.5/v1.6)
			{
				if (val_off + 24 > size) { fprintf (out, "<out of bounds>\n"); break; }
				char fb1[16], fb2[16], fb3[16], fb4[16], fb5[16], fb6[16];
				fprintf (out, "BlendState(src_color=%s, color_op=%s, dst_color=%s,"
					" src_alpha=%s, alpha_op=%s, dst_alpha=%s)\n",
					matl_enum_name (matl_blend_factor_name, 15, rd_le32 (data + val_off), fb1, sizeof (fb1)),
					matl_enum_name (matl_blend_op_name, 5, rd_le32 (data + val_off + 4), fb2, sizeof (fb2)),
					matl_enum_name (matl_blend_factor_name, 15, rd_le32 (data + val_off + 8), fb3, sizeof (fb3)),
					matl_enum_name (matl_blend_factor_name, 15, rd_le32 (data + val_off + 12), fb4, sizeof (fb4)),
					matl_enum_name (matl_blend_op_name, 5, rd_le32 (data + val_off + 16), fb5, sizeof (fb5)),
					matl_enum_name (matl_blend_factor_name, 15, rd_le32 (data + val_off + 20), fb6, sizeof (fb6)));
				break;
			}

			case 18: // RasterizerState -- v1.6 leads with fill_mode then cull_mode;
			         // v1.5 is cull_mode only, so the fill_mode column is skipped for it.
			{
				const u64 cull_off = v16 ? val_off + 4 : val_off;
				if (cull_off + 4 > size) { fprintf (out, "<out of bounds>\n"); break; }
				char fb1[16], fb2[16];
				if (v16)
				{
					if (val_off + 4 > size) { fprintf (out, "<out of bounds>\n"); break; }
					fprintf (out, "RasterizerState(fill=%s, cull=%s)\n",
						matl_enum_name (matl_fill_mode_name, 2, rd_le32 (data + val_off), fb1, sizeof (fb1)),
						matl_enum_name (matl_cull_mode_name, 3, rd_le32 (data + cull_off), fb2, sizeof (fb2)));
				}
				else
					fprintf (out, "RasterizerState(cull=%s)\n",
						matl_enum_name (matl_cull_mode_name, 3, rd_le32 (data + cull_off), fb1, sizeof (fb1)));
				break;
			}

			default:
				fprintf (out, "<unknown param type %llu>\n", (unsigned long long)data_type);
				break;
			}
		}
	}

	return ERR_OK;
}

//--- MatLab-dialect XML export (DecodeNUMATB_XML) ----------------------------

static void xml_escape (FILE *out, ccp s, int attr)
{
	for (; *s; s++)
	{
		switch (*s)
		{
			case '&': fputs ("&amp;", out); break;
			case '<': fputs ("&lt;", out); break;
			case '>': fputs ("&gt;", out); break;
			case '"': fputs (attr ? "&quot;" : "\"", out); break;
			default: fputc (*s, out); break;
		}
	}
}

// Enum name where known, plain number otherwise (XmlSerializer prints unknown
// enum values numerically, so this keeps the output loadable by MatLab).
static void xml_enum (FILE *out, const ccp *table, uint table_len, u32 val)
{
	if (val < table_len)
		fputs (table[val], out);
	else
		fprintf (out, "%u", val);
}

static void xml_f32 (FILE *out, const u8 *data, u64 off)
{
	fprintf (out, "%.9g", (double)read_f32 (data, off));
}

static void xml_indent (FILE *out, int depth)
{
	for (int i = 0; i < depth; i++)
		fputs ("  ", out);
}

#define XML_LIB_HEAD \
	"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" \
	"<MaterialLibrary" \
	" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"" \
	" xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\""

static void xml_vector4_fields (FILE *out, const u8 *data, u64 val_off, int depth)
{
	static const char fld[4] = { 'X', 'Y', 'Z', 'W' };
	for (int c = 0; c < 4; c++)
	{
		xml_indent (out, depth);
		fprintf (out, "<%c>", fld[c]);
		xml_f32 (out, data, val_off + (u64)c * 4);
		fprintf (out, "</%c>\n", fld[c]);
	}
}

enumError DecodeNUMATB_XML (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsNUMATB (data, size))
		return ERR_INVALID_DATA;

	const u64 array_field_off = NUMATB_SUBHDR_OFF + 8;
	if (array_field_off + 16 > size)
		return ERROR0 (ERR_INVALID_DATA, "NUMATB: file shorter than the fixed header\n");

	const u64 array_rel = rd_le64 (data + array_field_off);
	const u64 entry_count = rd_le64 (data + array_field_off + 8);

	if (!array_rel || entry_count > NUMATB_MAX_ENTRIES || !entry_count)
	{
		fprintf (out, XML_LIB_HEAD " />\n");
		return ERR_OK;
	}

	const u64 array_base = array_field_off + array_rel;
	if (array_base < array_field_off)
		return ERROR0 (ERR_INVALID_DATA, "NUMATB: material array offset overflow\n");

	fprintf (out, XML_LIB_HEAD ">\n");

	for (u64 i = 0; i < entry_count; i++)
	{
		const u64 entry_off = array_base + i * NUMATB_ENTRY_SIZE;
		if (entry_off < array_base || entry_off + NUMATB_ENTRY_SIZE > size)
			break;

		char material_label[256], shader_label[256];
		read_ssbh_string (material_label, sizeof (material_label), data, size, entry_off);
		read_ssbh_string (shader_label, sizeof (shader_label), data, size, entry_off + 0x18);

		xml_indent (out, 1);
		fputs ("<Material shaderLabel=\"", out);
		xml_escape (out, shader_label, 1);
		fputs ("\" materialLabel=\"", out);
		xml_escape (out, material_label, 1);
		fputs ("\">\n", out);

		const u64 attr_array_field_off = entry_off + 0x08;
		const u64 attr_rel = rd_le64 (data + attr_array_field_off);
		const u64 attr_count = rd_le64 (data + attr_array_field_off + 8);
		if (!attr_rel || attr_count > NUMATB_MAX_ATTRS)
		{
			xml_indent (out, 1);
			fputs ("</Material>\n", out);
			continue;
		}
		const u64 attr_base = attr_array_field_off + attr_rel;
		if (attr_base < attr_array_field_off)
		{
			xml_indent (out, 1);
			fputs ("</Material>\n", out);
			continue;
		}

		for (u64 a = 0; a < attr_count; a++)
		{
			const u64 attr_off = attr_base + a * NUMATB_ATTR_SIZE;
			if (attr_off < attr_base || attr_off + NUMATB_ATTR_SIZE > size)
				break;

			const u64 param_id = rd_le64 (data + attr_off);
			ccp param_name = matl_param_name (param_id);

			xml_indent (out, 2);
			fputs ("<Parameter name=\"", out);
			if (param_name)
				xml_escape (out, param_name, 1);
			else
				fprintf (out, "%llu", (unsigned long long)param_id);
			fputs ("\">\n", out);

			const u64 enum_field_off = attr_off + 0x08;
			const u64 val_rel = rd_le64 (data + enum_field_off);
			const u64 data_type = rd_le64 (data + enum_field_off + 8);
			const u64 val_off = enum_field_off + val_rel;
			const int val_ok = val_rel && val_off >= enum_field_off;

			switch (data_type)
			{
			case 1: // Float
				xml_indent (out, 3);
				if (val_ok && val_off + 4 <= size)
				{
					fputs ("<Float>", out);
					xml_f32 (out, data, val_off);
					fputs ("</Float>\n", out);
				}
				else
					fputs ("<Float>0</Float>\n", out);
				break;

			case 2: // Boolean
				xml_indent (out, 3);
				fprintf (out, "<Bool>%s</Bool>\n",
					val_ok && val_off + 4 <= size && rd_le32 (data + val_off)
						? "true" : "false");
				break;

			case 5: // Vector4
			case 7: // Color4f
				xml_indent (out, 3);
				if (val_ok && val_off + 16 <= size)
				{
					fputs ("<Vector4>\n", out);
					xml_vector4_fields (out, data, val_off, 4);
					xml_indent (out, 3);
					fputs ("</Vector4>\n", out);
				}
				else
				{
					fputs ("<Vector4>\n", out);
					xml_indent (out, 4); fputs ("<X>0</X>\n", out);
					xml_indent (out, 4); fputs ("<Y>0</Y>\n", out);
					xml_indent (out, 4); fputs ("<Z>0</Z>\n", out);
					xml_indent (out, 4); fputs ("<W>0</W>\n", out);
					xml_indent (out, 3);
					fputs ("</Vector4>\n", out);
				}
				break;

			case 11: // String
			{
				char str[512];
				read_ssbh_string (str, sizeof (str), data, size, val_ok ? val_off : size);
				xml_indent (out, 3);
				fputs ("<String>\n", out);
				xml_indent (out, 4);
				fputs ("<Text>", out);
				xml_escape (out, str, 0);
				fputs ("</Text>\n", out);
				xml_indent (out, 3);
				fputs ("</String>\n", out);
				break;
			}

			case 14: // Sampler (0x38 bytes)
				xml_indent (out, 3);
				if (val_ok && val_off + 0x38 <= size)
				{
					fputs ("<Sampler>\n", out);
					static ccp const snames[6] = { "WrapS", "WrapT", "WrapR",
						"MinFilter", "MagFilter", "TextureFilteringType" };
					static const ccp * const stabs[6] = { matl_wrap_mode_name,
						matl_wrap_mode_name, matl_wrap_mode_name,
						matl_min_filter_name, matl_mag_filter_name,
						matl_filter_type_name };
					static const uint stablens[6] = { 4, 4, 4, 3, 3, 3 };
					for (int s = 0; s < 6; s++)
					{
						xml_indent (out, 4);
						fprintf (out, "<%s>", snames[s]);
						xml_enum (out, stabs[s], stablens[s],
							rd_le32 (data + val_off + (u64)s * 4));
						fprintf (out, "</%s>\n", snames[s]);
					}
					xml_indent (out, 4);
					fputs ("<BorderColor>\n", out);
					xml_vector4_fields (out, data, val_off + 24, 5);
					xml_indent (out, 4);
					fputs ("</BorderColor>\n", out);
					xml_indent (out, 4);
					fprintf (out, "<Unk11>%u</Unk11>\n", rd_le32 (data + val_off + 0x28));
					xml_indent (out, 4);
					fprintf (out, "<Unk12>%u</Unk12>\n", rd_le32 (data + val_off + 0x2c));
					xml_indent (out, 4);
					fputs ("<LodBias>", out);
					xml_f32 (out, data, val_off + 0x30);
					fputs ("</LodBias>\n", out);
					xml_indent (out, 4);
					fprintf (out, "<MaxAnisotropy>%u</MaxAnisotropy>\n",
						rd_le32 (data + val_off + 0x34));
					xml_indent (out, 3);
					fputs ("</Sampler>\n", out);
				}
				else
					fputs ("<Sampler />\n", out);
				break;

			case 16: // UvTransform (5 floats)
				xml_indent (out, 3);
				if (val_ok && val_off + 20 <= size)
				{
					fputs ("<UVtransform>\n", out);
					static const char uvd[5] = { 'X', 'Y', 'Z', 'W', 'V' };
					for (int c = 0; c < 5; c++)
					{
						xml_indent (out, 4);
						fprintf (out, "<%c>", uvd[c]);
						xml_f32 (out, data, val_off + (u64)c * 4);
						fprintf (out, "</%c>\n", uvd[c]);
					}
					xml_indent (out, 3);
					fputs ("</UVtransform>\n", out);
				}
				else
					fputs ("<UVtransform />\n", out);
				break;

			case 17: // BlendState (10 u32, MatLab's version-agnostic shape)
				xml_indent (out, 3);
				if (val_ok && val_off + 40 <= size)
				{
					fputs ("<BlendState>\n", out);
					xml_indent (out, 4); fputs ("<SourceColor>", out);
					xml_enum (out, matl_blend_factor_name, 15,
						rd_le32 (data + val_off));
					fputs ("</SourceColor>\n", out);
					xml_indent (out, 4);
					fprintf (out, "<Unk2>%u</Unk2>\n", rd_le32 (data + val_off + 4));
					xml_indent (out, 4); fputs ("<DestinationColor>", out);
					xml_enum (out, matl_blend_factor_name, 15,
						rd_le32 (data + val_off + 8));
					fputs ("</DestinationColor>\n", out);
					static ccp const bnames[7] = { "Unk4", "Unk5", "Unk6",
						"EnableAlphaSampleToCoverage", "Unk8", "Unk9", "Unk10" };
					for (int b = 0; b < 7; b++)
					{
						xml_indent (out, 4);
						fprintf (out, "<%s>%u</%s>\n", bnames[b],
							rd_le32 (data + val_off + 12 + (u64)b * 4), bnames[b]);
					}
					xml_indent (out, 3);
					fputs ("</BlendState>\n", out);
				}
				else
					fputs ("<BlendState />\n", out);
				break;

			case 18: // RasterizerState (MatLab's 8-field shape)
				xml_indent (out, 3);
				if (val_ok && val_off + 32 <= size)
				{
					fputs ("<RasterizerState>\n", out);
					xml_indent (out, 4); fputs ("<FillMode>", out);
					xml_enum (out, matl_fill_mode_name, 2, rd_le32 (data + val_off));
					fputs ("</FillMode>\n", out);
					xml_indent (out, 4); fputs ("<CullMode>", out);
					xml_enum (out, matl_cull_mode_name, 3,
						rd_le32 (data + val_off + 4));
					fputs ("</CullMode>\n", out);
					static ccp const rnames[6] = { "DepthBias", "Unk4", "Unk5",
						"Unk6", "Unk7", "Unk8" };
					static const int rfloat[6] = { 1, 1, 1, 0, 0, 1 };
					for (int r = 0; r < 6; r++)
					{
						xml_indent (out, 4);
						fprintf (out, "<%s>", rnames[r]);
						if (rfloat[r])
							xml_f32 (out, data, val_off + 8 + (u64)r * 4);
						else
							fprintf (out, "%u", rd_le32 (data + val_off + 8 + (u64)r * 4));
						fprintf (out, "</%s>\n", rnames[r]);
					}
					xml_indent (out, 3);
					fputs ("</RasterizerState>\n", out);
				}
				else
					fputs ("<RasterizerState />\n", out);
				break;

			default: // not representable in MatLab's dialect; keep it visible
				xml_indent (out, 3);
				fprintf (out, "<!-- unknown param type %llu -->\n",
					(unsigned long long)data_type);
				break;
			}

			xml_indent (out, 2);
			fputs ("</Parameter>\n", out);
		}

		xml_indent (out, 1);
		fputs ("</Material>\n", out);
	}

	fputs ("</MaterialLibrary>\n", out);
	return ERR_OK;
}

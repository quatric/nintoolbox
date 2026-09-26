#include "lib-nulstb.h"
#include "lib-std.h"

// SSBH NLST (.nulstb), little-endian. Reference: ultimate-research/ssbh_lib
// ssbh_lib/src/formats/nlst.rs (Nlst::V10). Same container conventions as
// this codebase's other SSBH decoders.
//
//   0x00  "HBSS" (or "SSBH"), then a u64 at 0x04 (0x40 on every retail file)
//   0x10  "TSLN" ("NLST" byte-reversed, or literal "NLST"), u16 major, u16 minor
//   0x18  file_names: SsbhArray<SsbhString>

#define NULSTB_SUBHDR_OFF 0x10
#define NULSTB_MAX_LIST 65536

bool IsNULSTB (const u8 *data, size_t size)
{
	if (!data || size < NULSTB_SUBHDR_OFF + 8)
		return false;
	if (memcmp (data, "HBSS", 4) && memcmp (data, "SSBH", 4))
		return false;
	return !memcmp (data + NULSTB_SUBHDR_OFF, "TSLN", 4)
		|| !memcmp (data + NULSTB_SUBHDR_OFF, "NLST", 4);
}

static bool read_ssbh_string (char *dest, uint destsz, const u8 *data, size_t size, u64 field_off)
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

enumError DecodeNULSTB_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsNULSTB (data, size))
		return ERR_INVALID_DATA;

	const u16 major = rd_le16 (data + NULSTB_SUBHDR_OFF + 4);
	const u16 minor = rd_le16 (data + NULSTB_SUBHDR_OFF + 6);

	const u64 array_field_off = NULSTB_SUBHDR_OFF + 8;
	if (array_field_off + 16 > size)
		return ERROR0 (ERR_INVALID_DATA, "NULSTB: file shorter than the fixed header\n");

	const u64 rel = rd_le64 (data + array_field_off);
	const u64 count = rd_le64 (data + array_field_off + 8);

	fprintf (out,
		"#NULSTB\n"
		"version = %u.%u\n"
		"file_count = %llu\n\n"
		"[file_names]\n",
		major, minor, (unsigned long long)count);

	if (!rel || count > NULSTB_MAX_LIST)
	{
		fprintf (out, "  <none>\n");
		return ERR_OK;
	}

	const u64 base = array_field_off + rel;
	if (base < array_field_off)
		return ERROR0 (ERR_INVALID_DATA, "NULSTB: file_names array offset overflow\n");

	for (u64 i = 0; i < count; i++)
	{
		const u64 field_off = base + i * 8;
		if (field_off < base || field_off + 8 > size)
		{
			fprintf (out, "  [%llu] <out of bounds>\n", (unsigned long long)i);
			break;
		}
		char name[256];
		read_ssbh_string (name, sizeof (name), data, size, field_off);
		fprintf (out, "  [%llu] %s\n", (unsigned long long)i, name[0] ? name : "<unnamed>");
	}

	return ERR_OK;
}

// SPDX-License-Identifier: GPL-2.0+
#include "lib-cnmt.h"
#include <string.h>

static const char *get_content_meta_type_name (u8 type)
{
	switch (type)
	{
		case 0x01: return "Application";
		case 0x02: return "Patch";
		case 0x03: return "AddOnContent";
		case 0x04: return "Delta";
		case 0x80: return "SystemProgram";
		case 0x81: return "SystemData";
		case 0x82: return "SystemUpdate";
		case 0x83: return "BootImagePackage";
		case 0x84: return "BootImagePackageSafe";
		default:   return "Unknown";
	}
}

static const char *get_content_type_name (u8 type)
{
	switch (type)
	{
		case 0: return "Meta";
		case 1: return "Program";
		case 2: return "Data";
		case 3: return "Control";
		case 4: return "HtmlDocument";
		case 5: return "LegalInformation";
		case 6: return "DeltaFragment";
		default: return "Unknown";
	}
}

bool IsCNMT (const u8 *data, size_t size)
{
	if (!data || size < 0x20)
		return false;

	const u16 ext_hdr_size = rd_le16 (data + 0x0E);
	const u16 content_count = rd_le16 (data + 0x10);
	const u16 content_meta_count = rd_le16 (data + 0x12);

	const u64 min_size = 0x20 + (u64)ext_hdr_size + (u64)content_count * 24;
	if (min_size > size)
		return false;

	// Loose sanity on counts
	if (content_count > 128 || content_meta_count > 128)
		return false;

	const u8 type = data[0x0C];
	if (type != 0x01 && type != 0x02 && type != 0x03 && type != 0x04 &&
		type != 0x80 && type != 0x81 && type != 0x82 && type != 0x83 && type != 0x84)
		return false;

	return true;
}

enumError DumpCNMT (FILE *out, const u8 *data, size_t size)
{
	if (!IsCNMT (data, size))
		return ERR_INVALID_DATA;

	const u64 title_id = rd_le64 (data + 0x00);
	const u32 version = rd_le32 (data + 0x08);
	const u8 type = data[0x0C];
	const u8 attributes = data[0x14];
	const u16 ext_hdr_size = rd_le16 (data + 0x0E);
	const u16 content_count = rd_le16 (data + 0x10);
	const u16 content_meta_count = rd_le16 (data + 0x12);
	const u32 req_sys_ver = rd_le32 (data + 0x18);

	fprintf (out, "# Nintendo Switch Content Metadata (CNMT)\n");
	fprintf (out, "TitleId:                        0x%016llx\n", (unsigned long long)title_id);
	fprintf (out, "Version:                        %u (0x%08x)\n", version, version);
	fprintf (out, "Type:                           %s (0x%02x)\n", get_content_meta_type_name (type), type);
	fprintf (out, "Attributes:                     0x%02x\n", attributes);
	fprintf (out, "RequiredDownloadSystemVersion:  0x%08x\n", req_sys_ver);
	fprintf (out, "ContentCount:                   %u\n", content_count);
	fprintf (out, "ContentMetaCount:               %u\n", content_meta_count);

	const size_t content_offset = 0x20 + ext_hdr_size;
	if (content_count > 0 && content_offset + (size_t)content_count * 24 <= size)
	{
		fprintf (out, "\n# Packaged Contents:\n");
		for (uint i = 0; i < content_count; i++)
		{
			const u8 *entry = data + content_offset + (size_t)i * 24;
			const u32 size_low = rd_le32 (entry + 0x10);
			const u16 size_high = rd_le16 (entry + 0x14);
			const u64 c_size = ((u64)size_high << 32) | size_low;
			const u8 c_type = entry[0x16];
			const u8 id_offset = entry[0x17];

			char nca_id_str[33];
			for (int j = 0; j < 16; j++)
				snprintf (nca_id_str + j * 2, 3, "%02x", entry[j]);
			nca_id_str[32] = 0;

			fprintf (out, "  [%u] NCA ID: %s.nca\n", i, nca_id_str);
			fprintf (out, "      Type:     %s (0x%02x)\n", get_content_type_name (c_type), c_type);
			fprintf (out, "      Size:     %llu bytes", (unsigned long long)c_size);
			if (c_size >= 1024 * 1024)
				fprintf (out, " (%.2f MB)", (double)c_size / (1024.0 * 1024.0));
			else if (c_size >= 1024)
				fprintf (out, " (%.2f KB)", (double)c_size / 1024.0);
			fprintf (out, "\n");
			if (id_offset)
				fprintf (out, "      IdOffset: %u\n", id_offset);
		}
	}

	return ERR_OK;
}

enumError SaveTextCNMT (ccp filename, const u8 *data, size_t size)
{
	FILE *fp = fopen (filename, "w");
	if (!fp)
		return ERR_CANT_CREATE;
	enumError err = DumpCNMT (fp, data, size);
	fclose (fp);
	return err;
}

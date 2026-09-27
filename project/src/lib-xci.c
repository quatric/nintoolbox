// SPDX-License-Identifier: GPL-2.0+
#include "lib-xci.h"
#include "lib-archive-util.h"
#include <string.h>

static const char *get_xci_rom_size_name (u8 rom_size)
{
	switch (rom_size)
	{
		case 0xFA: return "1 GB";
		case 0xF8: return "2 GB";
		case 0xF0: return "4 GB";
		case 0xE0: return "8 GB";
		case 0xE1: return "16 GB";
		case 0xE2: return "32 GB";
		default:   return "Unknown";
	}
}

bool IsXCI (const u8 *data, size_t size)
{
	if (!data || size < 0x200)
		return false;

	if (memcmp (data + XCI_HEAD_OFFSET, "HEAD", 4) != 0)
		return false;

	const u8 rom_size = data[XCI_HEAD_OFFSET + 0x0D];
	if (rom_size != 0xFA && rom_size != 0xF8 && rom_size != 0xF0 &&
		rom_size != 0xE0 && rom_size != 0xE1 && rom_size != 0xE2)
		return false;

	return true;
}

enumError ParseXCIHeader (xci_header_t *hdr, const u8 *data, size_t size)
{
	if (!IsXCI (data, size))
		return ERR_INVALID_DATA;

	const u8 *h = data + XCI_HEAD_OFFSET;
	hdr->magic = rd_le32 (h + 0x00);
	hdr->rom_area_start_page = rd_le32 (h + 0x04);
	hdr->backup_area_start_page = rd_le32 (h + 0x08);
	hdr->key_index = h + 0x0C ? *(h + 0x0C) : 0;
	hdr->rom_size = h[0x0D];
	hdr->version = h[0x0E];
	hdr->flags = h[0x0F];
	hdr->package_id = rd_le64 (h + 0x10);
	hdr->valid_data_end_page = rd_le32 (h + 0x18);
	hdr->partition_fs_header_address = rd_le64 (h + 0x30);
	hdr->partition_fs_header_size = rd_le64 (h + 0x38);
	memcpy (hdr->partition_fs_header_hash, h + 0x40, 32);

	return ERR_OK;
}

static void extract_hfs0_stream (FILE *fp, u64 hfs0_base, ccp dest_dir)
{
	if (fseeko (fp, (off_t)hfs0_base, SEEK_SET) != 0)
		return;

	u8 header[16];
	if (fread (header, 1, 16, fp) != 16 || memcmp (header, "HFS0", 4) != 0)
		return;

	const u32 file_count = rd_le32 (header + 4);
	const u32 str_size = rd_le32 (header + 8);
	if (file_count == 0 || file_count > 10000 || str_size == 0 || str_size > 0x1000000)
		return;

	const u64 entry_table_bytes = (u64)file_count * 64;
	u8 *entry_table = MALLOC (entry_table_bytes);
	if (!entry_table)
		return;

	if (fread (entry_table, 1, entry_table_bytes, fp) != entry_table_bytes)
	{
		FREE (entry_table);
		return;
	}

	char *string_table = MALLOC (str_size + 1);
	if (!string_table)
	{
		FREE (entry_table);
		return;
	}

	if (fread (string_table, 1, str_size, fp) != str_size)
	{
		FREE (string_table);
		FREE (entry_table);
		return;
	}
	string_table[str_size] = 0;

	const u64 header_end = 16 + entry_table_bytes + str_size;
	const u64 data_offset = (header_end + 511ULL) & ~511ULL;

	const size_t chunk_size = 1024 * 1024;
	u8 *chunk_buf = MALLOC (chunk_size);
	if (!chunk_buf)
	{
		FREE (string_table);
		FREE (entry_table);
		return;
	}

	for (uint i = 0; i < file_count; i++)
	{
		const u8 *entry = entry_table + (u64)i * 64;
		const u64 rel_offset = rd_le64 (entry);
		const u64 file_size = rd_le64 (entry + 8);
		const u32 name_offset = rd_le32 (entry + 16);

		if (name_offset >= str_size)
			continue;

		const char *name = string_table + name_offset;
		char clean_name[PATH_MAX];
		if (!name[0] || !OwnedNameOk (name))
			snprintf (clean_name, sizeof (clean_name), "file_%04u.bin", i);
		else
			snprintf (clean_name, sizeof (clean_name), "%s", name);

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest_dir, clean_name);

		char *slash = strrchr (out_path, '/');
		if (slash)
		{
			*slash = 0;
			CreatePath (out_path, true);
			*slash = '/';
		}

		if (testmode)
			continue;

		FILE *out_fp = fopen (out_path, "wb");
		if (!out_fp)
			continue;

		const u64 abs_offset = hfs0_base + data_offset + rel_offset;
		if (fseeko (fp, (off_t)abs_offset, SEEK_SET) != 0)
		{
			fclose (out_fp);
			continue;
		}

		u64 remaining = file_size;
		while (remaining > 0)
		{
			const size_t to_read = (remaining > chunk_size) ? chunk_size : (size_t)remaining;
			const size_t read_bytes = fread (chunk_buf, 1, to_read, fp);
			if (read_bytes == 0)
				break;
			fwrite (chunk_buf, 1, read_bytes, out_fp);
			remaining -= read_bytes;
		}
		fclose (out_fp);
	}

	FREE (chunk_buf);
	FREE (string_table);
	FREE (entry_table);
}

enumError ExtractXCIArchive (ccp arg, ccp basedir, uint depth)
{
	(void)depth;

	FILE *fp = fopen (arg, "rb");
	if (!fp)
		return ERR_CANT_OPEN;

	u8 head_buf[0x200];
	if (fread (head_buf, 1, 0x200, fp) != 0x200)
	{
		fclose (fp);
		return ERR_NOTHING_TO_DO;
	}

	if (!IsXCI (head_buf, 0x200))
	{
		fclose (fp);
		return ERR_NOTHING_TO_DO;
	}

	xci_header_t hdr;
	if (ParseXCIHeader (&hdr, head_buf, 0x200))
	{
		fclose (fp);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT XCI:%s (%s) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			arg, get_xci_rom_size_name (hdr.rom_size), dest);

	if (testmode)
	{
		fclose (fp);
		return ERR_OK;
	}

	// Read Root HFS0
	const u64 root_addr = hdr.partition_fs_header_address;
	if (fseeko (fp, (off_t)root_addr, SEEK_SET) != 0)
	{
		fclose (fp);
		return ERR_INVALID_DATA;
	}

	u8 root_hdr[16];
	if (fread (root_hdr, 1, 16, fp) != 16 || memcmp (root_hdr, "HFS0", 4) != 0)
	{
		fclose (fp);
		return ERR_INVALID_DATA;
	}

	const u32 part_count = rd_le32 (root_hdr + 4);
	const u32 part_str_sz = rd_le32 (root_hdr + 8);
	if (part_count == 0 || part_count > 100 || part_str_sz == 0 || part_str_sz > 0x10000)
	{
		fclose (fp);
		return ERR_INVALID_DATA;
	}

	const u64 part_table_bytes = (u64)part_count * 64;
	u8 *part_table = MALLOC (part_table_bytes);
	char *part_strings = MALLOC (part_str_sz + 1);
	if (!part_table || !part_strings)
	{
		FREE (part_table);
		FREE (part_strings);
		fclose (fp);
		return ERR_OUT_OF_MEMORY;
	}

	if (fread (part_table, 1, part_table_bytes, fp) != part_table_bytes ||
		fread (part_strings, 1, part_str_sz, fp) != part_str_sz)
	{
		FREE (part_table);
		FREE (part_strings);
		fclose (fp);
		return ERR_INVALID_DATA;
	}
	part_strings[part_str_sz] = 0;

	const u64 root_hdr_end = 16 + part_table_bytes + part_str_sz;
	const u64 root_data_offset = (root_hdr_end + 511ULL) & ~511ULL;

	for (uint i = 0; i < part_count; i++)
	{
		const u8 *entry = part_table + (u64)i * 64;
		const u64 rel_offset = rd_le64 (entry);
		const u32 name_offset = rd_le32 (entry + 16);

		if (name_offset >= part_str_sz)
			continue;

		const char *part_name = part_strings + name_offset;
		char part_dest[PATH_MAX];
		snprintf (part_dest, sizeof (part_dest), "%s/%s", dest, part_name);
		CreatePath (part_dest, true);

		const u64 part_hfs0_addr = root_addr + root_data_offset + rel_offset;
		extract_hfs0_stream (fp, part_hfs0_addr, part_dest);
	}

	FREE (part_table);
	FREE (part_strings);
	fclose (fp);
	return ERR_OK;
}

enumError DumpXCI (FILE *out, const u8 *data, size_t size)
{
	xci_header_t hdr;
	if (ParseXCIHeader (&hdr, data, size))
		return ERR_INVALID_DATA;

	fprintf (out, "# Nintendo Switch Game Cartridge Image (XCI / HEAD)\n");
	fprintf (out, "CartridgeSize:          %s (0x%02x)\n",
		get_xci_rom_size_name (hdr.rom_size), hdr.rom_size);
	fprintf (out, "PackageId:              0x%016llx\n", (unsigned long long)hdr.package_id);
	fprintf (out, "Version:                %u\n", hdr.version);
	fprintf (out, "Flags:                  0x%02x\n", hdr.flags);
	fprintf (out, "PartitionFsAddress:     0x%llx\n", (unsigned long long)hdr.partition_fs_header_address);
	fprintf (out, "PartitionFsSize:        0x%llx (%llu bytes)\n",
		(unsigned long long)hdr.partition_fs_header_size, (unsigned long long)hdr.partition_fs_header_size);

	return ERR_OK;
}

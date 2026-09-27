// SPDX-License-Identifier: GPL-2.0+
#include "lib-nro.h"
#include "lib-archive-util.h"
#include "lib-nacp.h"
#include "lib-romfs.h"
#include <string.h>

bool IsNRO (const u8 *data, size_t size)
{
	if (!data || size < 0x80)
		return false;

	if (memcmp (data + 0x10, "NRO0", 4) != 0)
		return false;

	const u32 total_size = rd_le32 (data + 0x18);
	if (total_size < 0x80 || total_size > size)
		return false;

	return true;
}

enumError ParseNROHeader (nro_header_t *hdr, const u8 *data, size_t size)
{
	if (!IsNRO (data, size))
		return ERR_INVALID_DATA;

	hdr->entrypoint_insn = rd_le32 (data + 0x00);
	hdr->mod0_offset = rd_le32 (data + 0x04);
	memcpy (hdr->padding, data + 0x08, 8);
	hdr->magic = rd_le32 (data + 0x10);
	hdr->version = rd_le32 (data + 0x14);
	hdr->size = rd_le32 (data + 0x18);
	hdr->flags = rd_le32 (data + 0x1C);

	hdr->text.offset = rd_le32 (data + 0x20);
	hdr->text.size = rd_le32 (data + 0x24);
	hdr->ro.offset = rd_le32 (data + 0x28);
	hdr->ro.size = rd_le32 (data + 0x2C);
	hdr->data.offset = rd_le32 (data + 0x30);
	hdr->data.size = rd_le32 (data + 0x34);

	hdr->bss_size = rd_le32 (data + 0x38);
	hdr->reserved = rd_le32 (data + 0x3C);
	memcpy (hdr->module_id, data + 0x40, 32);
	hdr->dso_handle_offset = rd_le32 (data + 0x60);
	hdr->reserved2 = rd_le32 (data + 0x64);
	hdr->embedded_aset.offset = rd_le32 (data + 0x68);
	hdr->embedded_aset.size = rd_le32 (data + 0x6C);

	return ERR_OK;
}

static bool FindASETTrailer (nro_aset_header_t *aset, size_t *aset_base, const u8 *data, size_t size, const nro_header_t *hdr)
{
	size_t off = 0;
	if (hdr->embedded_aset.offset > 0 && hdr->embedded_aset.offset + 0x38 <= size &&
		!memcmp (data + hdr->embedded_aset.offset, "ASET", 4))
	{
		off = hdr->embedded_aset.offset;
	}
	else if (hdr->size < size && hdr->size + 0x38 <= size &&
		!memcmp (data + hdr->size, "ASET", 4))
	{
		off = hdr->size;
	}
	else
	{
		return false;
	}

	*aset_base = off;
	aset->magic = rd_le32 (data + off);
	aset->version = rd_le32 (data + off + 4);
	aset->icon.offset = rd_le64 (data + off + 8);
	aset->icon.size = rd_le64 (data + off + 16);
	aset->nacp.offset = rd_le64 (data + off + 24);
	aset->nacp.size = rd_le64 (data + off + 32);
	aset->romfs.offset = rd_le64 (data + off + 40);
	aset->romfs.size = rd_le64 (data + off + 48);

	return true;
}

enumError ExtractNROArchive (ccp arg, ccp basedir, uint depth)
{
	(void)depth;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (!IsNRO (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nro_header_t hdr;
	if (ParseNROHeader (&hdr, raw, raw_size))
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NRO:%s -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);

	if (testmode)
	{
		FREE (raw);
		return ERR_OK;
	}

	// 1. Save segments
	char seg_path[PATH_MAX];
	if (hdr.text.offset + hdr.text.size <= raw_size && hdr.text.size > 0)
	{
		snprintf (seg_path, sizeof (seg_path), "%s/text.bin", dest);
		SaveFile (seg_path, 0, 0, raw + hdr.text.offset, hdr.text.size, 0);
	}
	if (hdr.ro.offset + hdr.ro.size <= raw_size && hdr.ro.size > 0)
	{
		snprintf (seg_path, sizeof (seg_path), "%s/rodata.bin", dest);
		SaveFile (seg_path, 0, 0, raw + hdr.ro.offset, hdr.ro.size, 0);
	}
	if (hdr.data.offset + hdr.data.size <= raw_size && hdr.data.size > 0)
	{
		snprintf (seg_path, sizeof (seg_path), "%s/data.bin", dest);
		SaveFile (seg_path, 0, 0, raw + hdr.data.offset, hdr.data.size, 0);
	}

	// 2. Check for ASET trailer
	nro_aset_header_t aset;
	size_t aset_base = 0;
	if (FindASETTrailer (&aset, &aset_base, raw, raw_size, &hdr))
	{
		// Icon
		if (aset.icon.size > 0 && aset_base + aset.icon.offset + aset.icon.size <= raw_size)
		{
			const u8 *icon_data = raw + aset_base + aset.icon.offset;
			const bool is_jpg = (aset.icon.size >= 3 && icon_data[0] == 0xFF && icon_data[1] == 0xD8);
			const bool is_png = (aset.icon.size >= 8 && !memcmp (icon_data, "\x89PNG\r\n\x1a\n", 8));
			snprintf (seg_path, sizeof (seg_path), "%s/icon.%s", dest,
				is_jpg ? "jpg" : (is_png ? "png" : "bin"));
			SaveFile (seg_path, 0, 0, icon_data, (uint)aset.icon.size, 0);
		}

		// NACP
		if (aset.nacp.size > 0 && aset_base + aset.nacp.offset + aset.nacp.size <= raw_size)
		{
			const u8 *nacp_data = raw + aset_base + aset.nacp.offset;
			snprintf (seg_path, sizeof (seg_path), "%s/control.nacp", dest);
			SaveFile (seg_path, 0, 0, nacp_data, (uint)aset.nacp.size, 0);

			if (IsNACP (nacp_data, (uint)aset.nacp.size))
			{
				nacp_t parsed_nacp;
				if (ParseNACP (&parsed_nacp, nacp_data, (uint)aset.nacp.size) == ERR_OK)
				{
					snprintf (seg_path, sizeof (seg_path), "%s/control.txt", dest);
					SaveTextNACP (&parsed_nacp, seg_path);
				}
			}
		}

		// RomFS
		if (aset.romfs.size > 0 && aset_base + aset.romfs.offset + aset.romfs.size <= raw_size)
		{
			const u8 *romfs_data = raw + aset_base + aset.romfs.offset;
			snprintf (seg_path, sizeof (seg_path), "%s/romfs.bin", dest);
			SaveFile (seg_path, 0, 0, romfs_data, (uint)aset.romfs.size, 0);

			// Extract embedded RomFS into romfs/ directory
			char romfs_dest[PATH_MAX];
			snprintf (romfs_dest, sizeof (romfs_dest), "%s/romfs", dest);
			ExtractROMFSArchive (seg_path, dest, depth + 1);
		}
	}

	FREE (raw);
	return ERR_OK;
}

enumError DumpNRO (FILE *out, const u8 *data, size_t size)
{
	nro_header_t hdr;
	if (ParseNROHeader (&hdr, data, size))
		return ERR_INVALID_DATA;

	fprintf (out, "# Nintendo Switch Executable (NRO0)\n");
	fprintf (out, "Version:             %u\n", hdr.version);
	fprintf (out, "TotalSize:           0x%x (%u bytes)\n", hdr.size, hdr.size);
	fprintf (out, "Flags:               0x%08x\n", hdr.flags);
	fprintf (out, "TextSegment:         offset=0x%08x, size=0x%08x (%u bytes)\n",
		hdr.text.offset, hdr.text.size, hdr.text.size);
	fprintf (out, "RoSegment:           offset=0x%08x, size=0x%08x (%u bytes)\n",
		hdr.ro.offset, hdr.ro.size, hdr.ro.size);
	fprintf (out, "DataSegment:         offset=0x%08x, size=0x%08x (%u bytes)\n",
		hdr.data.offset, hdr.data.size, hdr.data.size);
	fprintf (out, "BssSize:             0x%08x (%u bytes)\n", hdr.bss_size, hdr.bss_size);

	fprintf (out, "ModuleId:            ");
	for (int i = 0; i < 32; i++)
		fprintf (out, "%02x", hdr.module_id[i]);
	fprintf (out, "\n");

	if (hdr.dso_handle_offset)
		fprintf (out, "DsoHandleOffset:     0x%08x\n", hdr.dso_handle_offset);

	nro_aset_header_t aset;
	size_t aset_base = 0;
	if (FindASETTrailer (&aset, &aset_base, data, size, &hdr))
	{
		fprintf (out, "\n# Asset Section (ASET)\n");
		fprintf (out, "ASETVersion:         %u\n", aset.version);
		fprintf (out, "Icon:                offset=0x%llx, size=0x%llx (%llu bytes)\n",
			(unsigned long long)aset.icon.offset, (unsigned long long)aset.icon.size,
			(unsigned long long)aset.icon.size);
		fprintf (out, "NACP:                offset=0x%llx, size=0x%llx (%llu bytes)\n",
			(unsigned long long)aset.nacp.offset, (unsigned long long)aset.nacp.size,
			(unsigned long long)aset.nacp.size);
		fprintf (out, "RomFS:               offset=0x%llx, size=0x%llx (%llu bytes)\n",
			(unsigned long long)aset.romfs.offset, (unsigned long long)aset.romfs.size,
			(unsigned long long)aset.romfs.size);
	}

	return ERR_OK;
}

enumError SaveTextNRO (ccp filename, const u8 *data, size_t size)
{
	FILE *fp = fopen (filename, "w");
	if (!fp)
		return ERR_CANT_CREATE;
	enumError err = DumpNRO (fp, data, size);
	fclose (fp);
	return err;
}

// SPDX-License-Identifier: GPL-2.0+
#include "lib-nso.h"
#include "lib-archive-util.h"
#include "lib-sha256.h"
#include "lz4.h"
#include <string.h>

bool IsNSO (const u8 *data, size_t size)
{
	if (!data || size < 0x100)
		return false;

	if (memcmp (data, "NSO0", 4) != 0)
		return false;

	return true;
}

enumError ParseNSOHeader (nso_header_t *hdr, const u8 *data, size_t size)
{
	if (!IsNSO (data, size))
		return ERR_INVALID_DATA;

	hdr->magic = rd_le32 (data + 0x00);
	hdr->version = rd_le32 (data + 0x04);
	hdr->reserved = rd_le32 (data + 0x08);
	hdr->flags = rd_le32 (data + 0x0C);

	hdr->text.file_offset = rd_le32 (data + 0x10);
	hdr->text.memory_offset = rd_le32 (data + 0x14);
	hdr->text.decompressed_size = rd_le32 (data + 0x18);
	hdr->text.align_or_bss = rd_le32 (data + 0x1C);

	hdr->ro.file_offset = rd_le32 (data + 0x20);
	hdr->ro.memory_offset = rd_le32 (data + 0x24);
	hdr->ro.decompressed_size = rd_le32 (data + 0x28);
	hdr->ro.align_or_bss = rd_le32 (data + 0x2C);

	hdr->data.file_offset = rd_le32 (data + 0x30);
	hdr->data.memory_offset = rd_le32 (data + 0x34);
	hdr->data.decompressed_size = rd_le32 (data + 0x38);
	hdr->data.align_or_bss = rd_le32 (data + 0x3C);

	memcpy (hdr->module_id, data + 0x40, 32);
	hdr->text_comp_size = rd_le32 (data + 0x60);
	hdr->ro_comp_size = rd_le32 (data + 0x64);
	hdr->data_comp_size = rd_le32 (data + 0x68);

	memcpy (hdr->text_sha256, data + 0xA0, 32);
	memcpy (hdr->ro_sha256, data + 0xC0, 32);
	memcpy (hdr->data_sha256, data + 0xE0, 32);

	return ERR_OK;
}

static enumError DecompressAndSaveSegment (ccp dest_file, const u8 *data, size_t file_size,
	u32 file_offset, u32 comp_size, u32 decomp_size, bool is_compressed)
{
	if (file_offset + comp_size > file_size || decomp_size == 0)
		return ERR_INVALID_DATA;

	u8 *out_buf = MALLOC (decomp_size);
	if (!out_buf)
		return ERR_OUT_OF_MEMORY;

	if (is_compressed)
	{
		int res = LZ4_decompress_safe (
			(const char *)(data + file_offset), (char *)out_buf, (int)comp_size, (int)decomp_size);
		if (res < 0 || (u32)res != decomp_size)
		{
			FREE (out_buf);
			return ERR_INVALID_DATA;
		}
	}
	else
	{
		memcpy (out_buf, data + file_offset, decomp_size);
	}

	SaveFile (dest_file, 0, 0, out_buf, decomp_size, 0);
	FREE (out_buf);
	return ERR_OK;
}

enumError ExtractNSOArchive (ccp arg, ccp basedir, uint depth)
{
	(void)depth;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (!IsNSO (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nso_header_t hdr;
	if (ParseNSOHeader (&hdr, raw, raw_size))
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NSO:%s -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);

	if (testmode)
	{
		FREE (raw);
		return ERR_OK;
	}

	char out_path[PATH_MAX];
	const bool text_comp = (hdr.flags & 1) != 0;
	const bool ro_comp = (hdr.flags & 2) != 0;
	const bool data_comp = (hdr.flags & 4) != 0;

	// 1. Text segment (.text.bin)
	snprintf (out_path, sizeof (out_path), "%s/text.bin", dest);
	DecompressAndSaveSegment (out_path, raw, raw_size,
		hdr.text.file_offset, text_comp ? hdr.text_comp_size : hdr.text.decompressed_size,
		hdr.text.decompressed_size, text_comp);

	// 2. Ro segment (.rodata.bin)
	snprintf (out_path, sizeof (out_path), "%s/rodata.bin", dest);
	DecompressAndSaveSegment (out_path, raw, raw_size,
		hdr.ro.file_offset, ro_comp ? hdr.ro_comp_size : hdr.ro.decompressed_size,
		hdr.ro.decompressed_size, ro_comp);

	// 3. Data segment (.data.bin)
	snprintf (out_path, sizeof (out_path), "%s/data.bin", dest);
	DecompressAndSaveSegment (out_path, raw, raw_size,
		hdr.data.file_offset, data_comp ? hdr.data_comp_size : hdr.data.decompressed_size,
		hdr.data.decompressed_size, data_comp);

	FREE (raw);
	return ERR_OK;
}

enumError DumpNSO (FILE *out, const u8 *data, size_t size)
{
	nso_header_t hdr;
	if (ParseNSOHeader (&hdr, data, size))
		return ERR_INVALID_DATA;

	const bool text_comp = (hdr.flags & 1) != 0;
	const bool ro_comp = (hdr.flags & 2) != 0;
	const bool data_comp = (hdr.flags & 4) != 0;

	fprintf (out, "# Nintendo Switch Executable (NSO0)\n");
	fprintf (out, "Version:             %u\n", hdr.version);
	fprintf (out, "Flags:               0x%08x (TextComp=%s, RoComp=%s, DataComp=%s)\n",
		hdr.flags, text_comp ? "yes" : "no", ro_comp ? "yes" : "no", data_comp ? "yes" : "no");

	fprintf (out, "TextSegment:         file_off=0x%08x, mem_off=0x%08x, decomp_size=0x%08x (%u), comp_size=0x%08x (%u)\n",
		hdr.text.file_offset, hdr.text.memory_offset, hdr.text.decompressed_size, hdr.text.decompressed_size,
		text_comp ? hdr.text_comp_size : hdr.text.decompressed_size,
		text_comp ? hdr.text_comp_size : hdr.text.decompressed_size);

	fprintf (out, "RoSegment:           file_off=0x%08x, mem_off=0x%08x, decomp_size=0x%08x (%u), comp_size=0x%08x (%u)\n",
		hdr.ro.file_offset, hdr.ro.memory_offset, hdr.ro.decompressed_size, hdr.ro.decompressed_size,
		ro_comp ? hdr.ro_comp_size : hdr.ro.decompressed_size,
		ro_comp ? hdr.ro_comp_size : hdr.ro.decompressed_size);

	fprintf (out, "DataSegment:         file_off=0x%08x, mem_off=0x%08x, decomp_size=0x%08x (%u), comp_size=0x%08x (%u), bss_size=0x%08x (%u)\n",
		hdr.data.file_offset, hdr.data.memory_offset, hdr.data.decompressed_size, hdr.data.decompressed_size,
		data_comp ? hdr.data_comp_size : hdr.data.decompressed_size,
		data_comp ? hdr.data_comp_size : hdr.data.decompressed_size,
		hdr.data.align_or_bss, hdr.data.align_or_bss);

	fprintf (out, "ModuleId:            ");
	for (int i = 0; i < 32; i++)
		fprintf (out, "%02x", hdr.module_id[i]);
	fprintf (out, "\n");

	fprintf (out, "TextSHA256:          ");
	for (int i = 0; i < 32; i++)
		fprintf (out, "%02x", hdr.text_sha256[i]);
	fprintf (out, "\n");

	fprintf (out, "RoSHA256:            ");
	for (int i = 0; i < 32; i++)
		fprintf (out, "%02x", hdr.ro_sha256[i]);
	fprintf (out, "\n");

	fprintf (out, "DataSHA256:          ");
	for (int i = 0; i < 32; i++)
		fprintf (out, "%02x", hdr.data_sha256[i]);
	fprintf (out, "\n");

	return ERR_OK;
}

enumError SaveTextNSO (ccp filename, const u8 *data, size_t size)
{
	FILE *fp = fopen (filename, "w");
	if (!fp)
		return ERR_CANT_CREATE;
	enumError err = DumpNSO (fp, data, size);
	fclose (fp);
	return err;
}

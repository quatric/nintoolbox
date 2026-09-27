// SPDX-License-Identifier: GPL-2.0+
#include "lib-npdm.h"
#include <string.h>

bool IsNPDM (const u8 *data, size_t size)
{
	if (!data || size < 0x80)
		return false;

	if (memcmp (data, "META", 4) != 0)
		return false;

	const u32 aci_off = rd_le32 (data + 0x70);
	const u32 aci_sz = rd_le32 (data + 0x74);
	const u32 acid_off = rd_le32 (data + 0x78);
	const u32 acid_sz = rd_le32 (data + 0x7C);

	if (aci_off >= size || acid_off >= size)
		return false;

	if (aci_off + 4 <= size && memcmp (data + aci_off, "ACI0", 4) != 0)
		return false;

	(void)aci_sz;
	(void)acid_sz;
	return true;
}

enumError ParseNPDM (npdm_meta_t *meta, const u8 *data, size_t size)
{
	if (!IsNPDM (data, size))
		return ERR_INVALID_DATA;

	memset (meta, 0, sizeof (*meta));

	meta->flags = data[0x0C];
	meta->main_thread_priority = data[0x0E];
	meta->main_thread_core = data[0x0F];
	meta->system_resource_size = rd_le32 (data + 0x14);
	meta->version = rd_le32 (data + 0x18);
	meta->main_thread_stack_size = rd_le32 (data + 0x1C);

	memcpy (meta->name, data + 0x20, 16);
	meta->name[16] = 0;
	memcpy (meta->product_code, data + 0x30, 16);
	meta->product_code[16] = 0;

	meta->aci_offset = rd_le32 (data + 0x70);
	meta->aci_size = rd_le32 (data + 0x74);
	meta->acid_offset = rd_le32 (data + 0x78);
	meta->acid_size = rd_le32 (data + 0x7C);

	if (meta->aci_offset + 0x18 <= size && !memcmp (data + meta->aci_offset, "ACI0", 4))
		meta->program_id = rd_le64 (data + meta->aci_offset + 0x10);

	return ERR_OK;
}

enumError DumpNPDM (FILE *out, const u8 *data, size_t size)
{
	npdm_meta_t meta;
	if (ParseNPDM (&meta, data, size))
		return ERR_INVALID_DATA;

	const bool is_64bit = (meta.flags & 1) != 0;

	fprintf (out, "# Nintendo Switch Program Descriptor (NPDM / META)\n");
	if (meta.name[0])
		fprintf (out, "Name:                   %s\n", meta.name);
	if (meta.product_code[0])
		fprintf (out, "ProductCode:            %s\n", meta.product_code);
	fprintf (out, "ProgramId:              0x%016llx\n", (unsigned long long)meta.program_id);
	fprintf (out, "ProcessAddressSpace:    %s\n", is_64bit ? "64Bit" : "32Bit");
	fprintf (out, "Version:                %u\n", meta.version);
	fprintf (out, "MainThreadPriority:     %u\n", meta.main_thread_priority);
	fprintf (out, "MainThreadCore:         %u\n", meta.main_thread_core);
	fprintf (out, "MainThreadStackSize:    0x%x (%u bytes)\n",
		meta.main_thread_stack_size, meta.main_thread_stack_size);
	if (meta.system_resource_size)
		fprintf (out, "SystemResourceSize:     0x%x (%u bytes)\n",
			meta.system_resource_size, meta.system_resource_size);

	fprintf (out, "AciOffset:              0x%08x (size: 0x%x)\n", meta.aci_offset, meta.aci_size);
	fprintf (out, "AcidOffset:             0x%08x (size: 0x%x)\n", meta.acid_offset, meta.acid_size);

	return ERR_OK;
}

enumError SaveTextNPDM (ccp filename, const u8 *data, size_t size)
{
	FILE *fp = fopen (filename, "w");
	if (!fp)
		return ERR_CANT_CREATE;
	enumError err = DumpNPDM (fp, data, size);
	fclose (fp);
	return err;
}

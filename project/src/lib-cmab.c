#include "lib-cmab.h"
#include "lib-ctpk.h"

#include <string.h>

// CMAB header locations established by the OoT3D extractor's reader. The
// txpt table has one 0x18-byte record per embedded PICA texture.
#define CMAB_HEADER_SIZE 0x34
#define CMAB_TXPT_ENTRY_SIZE 0x18

static uint cmab_pica_format (uint format)
{
	switch (format)
	{
		case 0x14016752: return 0;  // RGBA8
		case 0x14016754: return 1;  // RGB8
		case 0x80346752: return 2;  // RGBA5551
		case 0x83636754: return 3;  // RGB565
		case 0x80336752: return 4;  // RGBA4
		case 0x14016758: return 5;  // LA8
		case 0x67606758: return 6;  // LA4
		case 0x14016757: return 7;  // L8
		case 0x14016756: return 8;  // A8
		case 0x67616757: return 10; // L4
		case 0x0000675a: return 12; // ETC1
		case 0x0000675b: return 13; // ETC1A4
	}
	return UINT_MAX;
}

enumError ScanCMAB (cmab_t *cmab, const u8 *data, uint size)
{
	if (!cmab || !data || size < CMAB_HEADER_SIZE || memcmp (data, "cmab", 4))
		return EINVAL;

	memset (cmab, 0, sizeof (*cmab));
	const u64 txpt = (u64)rd_le32 (data + 0x14) + rd_le32 (data + 0x30);
	if (txpt > size || size - txpt < 8 || memcmp (data + txpt, "txpt", 4))
		return EINVAL;

	const uint count = rd_le32 (data + txpt + 4);
	const u64 entries_end = txpt + 8 + (u64)count * CMAB_TXPT_ENTRY_SIZE;
	if (count > 0xffff || entries_end > size || size - entries_end < 8
		|| memcmp (data + entries_end, "strt", 4))
		return EINVAL;

	const uint name_count = rd_le32 (data + entries_end + 4);
	const uint name_table = rd_le32 (data + 0x18);
	if (name_count < count || name_table > size || (u64)name_table + 8 + (u64)name_count * 4 > size)
		return EINVAL;

	cmab->data = data;
	cmab->size = size;
	cmab->texture_count = count;
	cmab->texture_table = txpt + 8;
	cmab->name_table = name_table;
	cmab->name_count = name_count;
	cmab->data_base = rd_le32 (data + 0x1c);
	return ERR_OK;
}

enumError GetCMABEntry (const cmab_t *cmab, uint index, cmab_entry_t *entry)
{
	if (!cmab || !cmab->data || !entry || index >= cmab->texture_count)
		return EINVAL;
	memset (entry, 0, sizeof (*entry));

	const u8 *record = cmab->data + cmab->texture_table + index * CMAB_TXPT_ENTRY_SIZE;
	const uint data_size = rd_le32 (record);
	const u64 data_offset = (u64)cmab->data_base + rd_le32 (record + 0x10);
	const uint name_offset = rd_le32 (cmab->data + cmab->name_table + 8 + index * 4);
	const u64 name_start = (u64)cmab->name_table + 8 + (u64)cmab->name_count * 4 + name_offset;
	if (!data_size || data_offset > cmab->size || data_size > cmab->size - data_offset || name_start >= cmab->size)
		return EINVAL;

	const u8 *name = cmab->data + name_start;
	const u8 *nul = memchr (name, 0, cmab->size - name_start);
	if (!nul)
		return EINVAL;
	const size_t name_len = nul - name < sizeof (entry->name) - 1 ? nul - name : sizeof (entry->name) - 1;
	memcpy (entry->name, name, name_len);
	entry->name[name_len] = 0;

	entry->data = cmab->data + data_offset;
	entry->data_size = data_size;
	entry->width = rd_le16 (record + 8);
	entry->height = rd_le16 (record + 10);
	entry->format = rd_le32 (record + 12);
	entry->pica_format = cmab_pica_format (entry->format);
	entry->id = rd_le32 (record + 20);
	if (!entry->width || !entry->height || entry->pica_format == UINT_MAX)
		return EINVAL;
	return ERR_OK;
}

enumError DecodeCMABTexture_RGBA (
	u8 **dest, uint *width, uint *height, const cmab_entry_t *entry)
{
	if (!entry)
		return EINVAL;
	return DecodePicaTexture (
		dest, width, height, entry->data, entry->width, entry->height, entry->pica_format, entry->data_size);
}

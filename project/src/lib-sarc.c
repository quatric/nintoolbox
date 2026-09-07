// Nintendo SARC archive format -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

static inline u16 sarc16 (const nintendo_sarc_t *s, const u8 *p)
{
	return s->big_endian ? rd_be16 (p) : rd_le16 (p);
}
static inline u32 sarc32 (const nintendo_sarc_t *s, const u8 *p)
{
	return s->big_endian ? rd_be32 (p) : rd_le32 (p);
}

enumError ScanSARC (nintendo_sarc_t *sarc, const u8 *data, uint size)
{
	if (!sarc || !data || size < 0x20 || memcmp (data, "SARC", 4))
		return EINVAL;
	memset (sarc, 0, sizeof (*sarc));
	sarc->data = data;
	sarc->size = size;
	// The BOM is stored in the file's byte order, independently of host CPU.
	if (data[6] == 0xfe && data[7] == 0xff)
		sarc->big_endian = true;
	else if (data[6] == 0xff && data[7] == 0xfe)
		sarc->big_endian = false;
	else
		return EINVAL;
	const uint header_size = sarc16 (sarc, data + 4);
	const uint file_size = sarc32 (sarc, data + 8);
	sarc->data_offset = sarc32 (sarc, data + 0x0c);
	if (header_size < 0x14 || header_size > size || file_size > size
		|| sarc->data_offset > file_size || header_size + 12 > file_size
		|| memcmp (data + header_size, "SFAT", 4))
		return EINVAL;
	const uint sfat_size = sarc16 (sarc, data + header_size + 4);
	sarc->n_entries = sarc16 (sarc, data + header_size + 6);
	if (sfat_size < 12 || sarc->n_entries > (file_size - header_size - 12) / 16
		|| header_size + sfat_size + 16 * sarc->n_entries + 8 > file_size)
		return EINVAL;
	sarc->entries_offset = header_size + sfat_size;
	sarc->sfnt_offset = sarc->entries_offset + 16 * sarc->n_entries;
	if (memcmp (data + sarc->sfnt_offset, "SFNT", 4)
		|| sarc16 (sarc, data + sarc->sfnt_offset + 4) < 8)
		return EINVAL;
	return ERR_OK;
}

enumError GetSARCEntry (
	const nintendo_sarc_t *sarc, uint index, ccp *name, const u8 **data, uint *size)
{
	if (!sarc || !sarc->data || index >= sarc->n_entries)
		return EINVAL;
	const u8 *node = sarc->data + sarc->entries_offset + 16 * index;
	const u32 attr = sarc32 (sarc, node + 4);
	const uint begin = sarc32 (sarc, node + 8), end = sarc32 (sarc, node + 12);
	if (begin > end || end > sarc->size - sarc->data_offset)
		return EINVAL;
	if (name)
	{
		if (!(attr >> 24))
		{
			*name = 0;
		}
		else
		{
			const uint noff = sarc->sfnt_offset + 8 + 4 * (attr & 0x00ffffff);
			if (noff >= sarc->size || !memchr (sarc->data + noff, 0, sarc->size - noff))
				return EINVAL;
			*name = (ccp)sarc->data + noff;
		}
	}
	if (data)
		*data = sarc->data + sarc->data_offset + begin;
	if (size)
		*size = end - begin;
	return ERR_OK;
}

typedef struct sarc_sort_t
{
	const nintendo_sarc_entry_t *entry;
	u32 hash;
} sarc_sort_t;

static u32 hash_sarc_name (ccp name)
{
	u32 hash = 0;
	while (*name)
		hash = hash * 0x65 + (u8)*name++;
	return hash;
}

static int cmp_sarc_entry (const void *a, const void *b)
{
	const sarc_sort_t *sa = a, *sb = b;
	if (sa->hash != sb->hash)
		return sa->hash < sb->hash ? -1 : 1;
	return strcmp (sa->entry->name, sb->entry->name);
}

enumError CreateSARC (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries,
	uint n_entries, bool big_endian)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xffff)
		return EINVAL;
	sarc_sort_t *sorted = CALLOC (n_entries, sizeof (*sorted));
	if (!sorted)
		return ERR_CANT_CREATE;
	uint names_size = 8, data_size = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		if (!entries[i].name || !*entries[i].name || !entries[i].data)
		{
			FREE (sorted);
			return EINVAL;
		}
		const size_t name_len = strlen (entries[i].name) + 1;
		if (name_len > UINT_MAX || names_size > UINT_MAX - ((name_len + 3) & ~3u)
			|| data_size > UINT_MAX - entries[i].size)
		{
			FREE (sorted);
			return EFBIG;
		}
		sorted[i].entry = entries + i;
		sorted[i].hash = hash_sarc_name (entries[i].name);
		names_size += (name_len + 3) & ~3u;
		data_size += entries[i].size;
	}
	qsort (sorted, n_entries, sizeof (*sorted), cmp_sarc_entry);
	if (names_size > UINT_MAX - (0x20 + 16 * n_entries))
	{
		FREE (sorted);
		return EFBIG;
	}
	const uint tables_size = 0x20 + 16 * n_entries + names_size;
	const uint data_offset = (tables_size + 0xff) & ~0xffu;
	if (tables_size > UINT_MAX - 0xff || data_offset > UINT_MAX - data_size)
	{
		FREE (sorted);
		return EFBIG;
	}
	const uint total = data_offset + data_size;
	u8 *out = CALLOC (1, total);
	if (!out)
	{
		FREE (sorted);
		return ERR_CANT_CREATE;
	}
	void (*w16) (u8 *, u16) = big_endian ? wr_be16 : wr_le16;
	void (*w32) (u8 *, u32) = big_endian ? wr_be32 : wr_le32;
	memcpy (out, "SARC", 4);
	w16 (out + 4, 0x14);
	// Store the same BOM value in the file's byte order: FE FF means big,
	// FF FE means little in the raw byte stream.
	w16 (out + 6, 0xfeff);
	w32 (out + 8, total);
	w32 (out + 0x0c, data_offset);
	w16 (out + 0x10, 0x0100);
	memcpy (out + 0x14, "SFAT", 4);
	w16 (out + 0x18, 12);
	w16 (out + 0x1a, n_entries);
	w32 (out + 0x1c, 0x65);
	const uint sfnt = 0x20 + 16 * n_entries;
	memcpy (out + sfnt, "SFNT", 4);
	w16 (out + sfnt + 4, 8);
	uint name_pos = sfnt + 8, data_pos = data_offset;
	for (uint i = 0; i < n_entries; i++)
	{
		const nintendo_sarc_entry_t *entry = sorted[i].entry;
		u8 *node = out + 0x20 + 16 * i;
		w32 (node, sorted[i].hash);
		w32 (node + 4, 0x01000000 | ((name_pos - (sfnt + 8)) / 4));
		w32 (node + 8, data_pos - data_offset);
		memcpy (out + name_pos, entry->name, strlen (entry->name) + 1);
		name_pos += (strlen (entry->name) + 1 + 3) & ~3u;
		memcpy (out + data_pos, entry->data, entry->size);
		data_pos += entry->size;
		w32 (node + 12, data_pos - data_offset);
	}
	FREE (sorted);
	*dest = out;
	*dest_size = total;
	return ERR_OK;
}

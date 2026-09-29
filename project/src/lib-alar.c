// Jump Super Stars / Jump Ultimate Stars ALAR archives.
#include "lib-std.h"
#include "lib-nintendo.h"

// Both variants have a LE16 member count at +6. Type 2 uses fixed 16-byte
// records. Type 3's LE16 offset table points to records, followed by a
// two-byte name hash and a variable-length name; the record is BEFORE its
// name, not after it. All offsets are absolute file offsets.
enumError ReadALAREntry (alar_entry_t *entry, const u8 *src, uint src_size, uint index)
{
	if (!entry)
		return ERR_INVALID_DATA;
	memset (entry, 0, sizeof (*entry));
	if (!src || src_size < 16 || memcmp (src, "ALAR", 4))
		return ERR_INVALID_DATA;
	const uint count = rd_le16 (src + 6);
	if (!count || index >= count)
		return ERR_INVALID_DATA;
	uint record, name_end = 0, name_off = 0;
	const uint type = src[4];
	if (type == 2)
	{
		const uint table_end = 16 + count * 16;
		if (table_end > src_size)
			return ERR_INVALID_DATA;
		record = 16 + index * 16;
		const uint off = rd_le32 (src + record + 4);
		// Retail type-2 members carry a 36-byte prefix: two reserved bytes,
		// a 32-byte filename slot, and a two-byte hash. Legacy unnamed
		// containers are still readable, using generated names in the CLI.
		if (off >= table_end + 36 && off <= src_size && !src[off - 36] && !src[off - 35])
		{
			name_off = off - 34;
			name_end = off - 2;
		}
		if (off < table_end)
			return ERR_INVALID_DATA;
	}
	else if (type == 3)
	{
		const uint table_end = 18 + count * 2;
		if (table_end > src_size)
			return ERR_INVALID_DATA;
		name_end = rd_le16 (src + 16);
		record = rd_le16 (src + 18 + index * 2);
		if (name_end > src_size || record < table_end || record > name_end
			|| name_end - record < 19)
			return ERR_INVALID_DATA;
		name_off = record + 18;
	}
	else
		return ERR_INVALID_DATA;

	const uint off = rd_le32 (src + record + 4), size = rd_le32 (src + record + 8);
	if (off > src_size || size > src_size - off || (type == 3 && off < name_end))
		return ERR_INVALID_DATA;
	if (name_end)
	{
		const char *name = (const char *)src + name_off;
		if (!memchr (name, 0, name_end - name_off))
		{
			if (type == 3)
				return ERR_INVALID_DATA;
		}
		else if (*name)
			entry->name = name;
	}
	entry->data = src + off;
	entry->size = size;
	return ERR_OK;
}

// Compatibility API: return only the first member, using the same parser as
// archive extraction rather than mistaking a type-3 name table for records.
enumError DecodeALAR (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size)
		return ERR_INVALID_DATA;
	*dest = 0;
	*dest_size = 0;
	alar_entry_t entry;
	enumError err = ReadALAREntry (&entry, src, src_size, 0);
	if (err)
		return err;
	u8 *out = MALLOC (entry.size ? entry.size : 1);
	if (!out)
		return ERR_OUT_OF_MEMORY;
	if (entry.size)
		memcpy (out, entry.data, entry.size);
	*dest = out;
	*dest_size = entry.size;
	return ERR_OK;
}

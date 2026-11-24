// Electronic Arts Wii asset archive (BIGF) -- directory-tree CREATE glue.

#include "lib-std.h"
#include "lib-archive-util.h"
#include "lib-bigf.h"

// Repack a directory extracted by extract_bigf_file() back into an EA BIGF archive, in the layout of
// most retail archives (170 of the 215 BIGF files of SSX Blur re-create byte-exact from their members):
// header {"BIGF", u32 LE file size, u32 BE count, u32 BE end of the name table}, the table
// {u32 BE offset, u32 BE size, name + NUL} in directory-scan order, four zero bytes, then the members,
// each starting on a 128-byte boundary, zero padded, no padding after the last. The other 45 use older
// tool layouts ("L231" + 4 zero bytes before the data, or no gap at all, other alignments) that the
// extracted directory does not record.
enumError create_bigf_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;

	u64 table_size = 0;
	for (uint i = 0; !err && i < list.used; i++)
	{
		if (!list.entry[i].name || !OwnedNameOk (list.entry[i].name))
		{
			err = ERR_INVALID_DATA;
			break;
		}
		table_size += 8 + strlen (list.entry[i].name) + 1;
	}
	const uint header_size = 16, align = 128;
	const u64 table_end = header_size + table_size;
	const u64 data_off64 = (table_end + 4 + align - 1) & ~(u64)(align - 1);
	if (data_off64 > UINT_MAX)
		err = ERR_FILE_TOO_BIG;
	const u32 data_off = (u32)data_off64;

	u64 total_size = data_off;
	for (uint i = 0; !err && i < list.used; i++)
		total_size = (i ? (total_size + align - 1) & ~(u64)(align - 1) : total_size) + list.entry[i].size;
	if (!err && total_size > UINT_MAX)
		err = ERR_FILE_TOO_BIG;

	if (!err && !testmode)
	{
		u8 *data = CALLOC (1, total_size);
		if (!data)
			err = ERR_CANT_CREATE;
		else
		{
			memcpy (data, "BIGF", 4);
			wr_le32 (data + 4, (u32)total_size);
			wr_be32 (data + 8, list.used);
			wr_be32 (data + 12, (u32)table_end);

			u8 *table = data + header_size;
			u64 member_off = data_off;
			for (uint i = 0; i < list.used; i++)
			{
				const nintendo_sarc_entry_t *e = list.entry + i;
				if (i)
					member_off = (member_off + align - 1) & ~(u64)(align - 1);
				wr_be32 (table, (u32)member_off);
				wr_be32 (table + 4, e->size);
				table += 8;
				const uint nlen = strlen (e->name) + 1;
				memcpy (table, e->name, nlen);
				table += nlen;
				memcpy (data + member_off, e->data, e->size);
				member_off += e->size;
			}

			File_t F;
			err = CreateFileOpt (&F, true, dest, false, dest);
			if (F.f && fwrite (data, 1, total_size, F.f) != total_size)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %llu bytes failed: %s\n",
					(unsigned long long)total_size, dest);
			ResetFile (&F, opt_preserve);
			FREE (data);
		}
	}
	reset_sarc_build_list (&list);
	return err;
}

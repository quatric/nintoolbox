// Electronic Arts Wii asset archive (BIGF) -- directory-tree CREATE glue.

#include "lib-std.h"
#include "lib-archive-util.h"
#include "lib-bigf.h"


// Repack a directory extracted by extract_bigf_file() back into an EA BIGF
// archive.  This is a plain, working reconstruction (member data emitted in
// directory-scan order, packed with no inter-member padding) rather than a
// byte-exact retail reproduction -- no retail .big sample was available to
// derive the original member ordering/alignment rules from, unlike the SZS
// formats above.  extract -> create -> extract round-trips losslessly.
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
	// Retail terminates the variable-length table with a four-byte "L234"
	// marker and pads with zeros up to the (4-byte aligned) data offset.
	const uint header_size = 16;
	const u64 data_off64 = (header_size + table_size + 4 + 3) & ~(u64)3;
	if (data_off64 > UINT_MAX)
		err = ERR_FILE_TOO_BIG;
	const u32 data_off = (u32)data_off64;

	u64 total_size = data_off;
	for (uint i = 0; !err && i < list.used; i++)
		total_size += list.entry[i].size;
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
			wr_be32 (data + 12, data_off);

			u8 *table = data + header_size;
			u64 member_off = data_off;
			for (uint i = 0; i < list.used; i++)
			{
				const nintendo_sarc_entry_t *e = list.entry + i;
				wr_be32 (table, (u32)member_off);
				wr_be32 (table + 4, e->size);
				table += 8;
				const uint nlen = strlen (e->name) + 1;
				memcpy (table, e->name, nlen);
				table += nlen;
				memcpy (data + member_off, e->data, e->size);
				member_off += e->size;
			}
			memcpy (table, "L234", 4);

			File_t F;
			err = CreateFileOpt (&F, true, dest, false, dest);
			if (F.f && fwrite (data, 1, total_size, F.f) != total_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %llu bytes failed: %s\n",
					(unsigned long long)total_size, dest);
			ResetFile (&F, opt_preserve);
			FREE (data);
		}
	}
	reset_sarc_build_list (&list);
	return err;
}


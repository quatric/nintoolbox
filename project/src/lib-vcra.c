// Bandai Namco Museum Remix archive format (VCRA) -- directory-tree CREATE
// glue.

#include "lib-std.h"
#include "lib-archive-util.h"
#include "lib-vcra.h"


// Repack a directory extracted by extract_namco_vcra_file() into a Namco
// Museum Remix VCRA archive.
//
// VCRA has two entry layouts, selected by whether the word at 0x0C is
// non-zero: a 44-byte entry carrying a CRC, or a 64-byte entry without one
// whose table starts at 0x40. The writer emits the second, so the word at
// 0x0C stays zero and no CRC has to be invented for members the reader
// never checked one against.
enumError create_vcra_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;

	if (!err)
		for (uint i = 0; i < list.used; i++)
		{
			ccp name = list.entry[i].name ? list.entry[i].name : "";
			if (strlen (name) > 56)
			{
				err = ERROR0 (ERR_INVALID_DATA,
					"VCRA member name exceeds the format's 56-byte field: %s\n", name);
				break;
			}
		}

	u8 *out = 0;
	uint total = 0;
	if (!err)
	{
		const uint table = 64, entry_size = 64;
		uint off = table + list.used * entry_size;
		off = (off + 31) & ~31u;
		total = off;
		for (uint i = 0; i < list.used; i++)
			total = (total + list.entry[i].size + 31) & ~31u;

		out = CALLOC (1, total);
		if (!out)
			err = ERR_CANT_CREATE;
		else
		{
			memcpy (out, "VCRA", 4);
			wr_le32 (out + 4, list.used);
			wr_le32 (out + 8, total);

			for (uint i = 0; i < list.used; i++)
			{
				u8 *entry = out + table + i * entry_size;
				wr_le32 (entry, off);
				wr_le32 (entry + 4, list.entry[i].size);
				strncpy ((char *)entry + 8, list.entry[i].name ? list.entry[i].name : "", 56);
				if (list.entry[i].data && list.entry[i].size)
					memcpy (out + off, list.entry[i].data, list.entry[i].size);
				off = (off + list.entry[i].size + 31) & ~31u;
			}
		}
	}

	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, dest);
		if (F.f && fwrite (out, 1, total, F.f) != total)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", total, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (out);
	reset_sarc_build_list (&list);
	return err;
}


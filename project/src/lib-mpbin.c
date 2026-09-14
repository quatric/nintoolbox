// Hudson Soft Mario Party archive container (MPBIN) -- directory-tree
// CREATE glue.

#include "lib-std.h"
#include "lib-archive-util.h"
#include "lib-mpbin.h"


enumError create_mpbin_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	if (err)
	{
		reset_sarc_build_list (&list);
		return err;
	}

	uint num_files = list.used;
	uint header_table_size = 4 + 4 * num_files;
	header_table_size = (header_table_size + 3) & ~3u;

	uint cur_off = header_table_size;
	u32 *file_offsets = CALLOC (num_files, sizeof (u32));
	if (!file_offsets)
	{
		reset_sarc_build_list (&list);
		return ERR_CANT_CREATE;
	}

	for (uint i = 0; i < num_files; i++)
	{
		file_offsets[i] = cur_off;
		cur_off += 8 + list.entry[i].size;
		cur_off = (cur_off + 3) & ~3u;
	}

	u8 *out = CALLOC (1, cur_off);
	if (!out)
	{
		FREE (file_offsets);
		reset_sarc_build_list (&list);
		return ERR_CANT_CREATE;
	}

	wr_be32 (out + 0, num_files);
	for (uint i = 0; i < num_files; i++)
		wr_be32 (out + 4 + 4 * i, file_offsets[i]);

	for (uint i = 0; i < num_files; i++)
	{
		u8 *fh = out + file_offsets[i];
		wr_be32 (fh + 0, list.entry[i].size);
		wr_be32 (fh + 4, 0); // uncompressed
		if (list.entry[i].size > 0 && list.entry[i].data)
			memcpy (fh + 8, list.entry[i].data, list.entry[i].size);
	}
	FREE (file_offsets);
	reset_sarc_build_list (&list);

	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, dest);
		if (F.f && fwrite (out, 1, cur_off, F.f) != cur_off)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", cur_off, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (out);
	return err;
}


#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "lib-smash-arc.h"
#include "lib-zstd.h"
#include "zstd.h"

// 8-byte hash lookup entry
typedef struct hash_to_index_t
{
	u32 hash;
	u32 length_and_index; // length: 8 bits, index: 24 bits
} __attribute__((packed)) hash_to_index_t;

static inline u8 h2i_len (const hash_to_index_t *h)
{
	return (u8)(h->length_and_index & 0xff);
}

static inline u32 h2i_idx (const hash_to_index_t *h)
{
	return (h->length_and_index >> 8) & 0xffffff;
}

// SearchFileSystem header (20 bytes at search_offset)
typedef struct search_fs_header_t
{
	u64 size;
	u32 folder_count;
	u32 path_index_count;
	u32 path_count;
} __attribute__((packed)) search_fs_header_t;

// SearchListEntry (32 bytes)
typedef struct search_list_entry_t
{
	hash_to_index_t path;
	hash_to_index_t parent;
	hash_to_index_t file_name;
	hash_to_index_t ext;
} __attribute__((packed)) search_list_entry_t;

// V1 FileSystemHeader (68 bytes)
typedef struct fs_header_v1_t
{
	u32 table_size;
	u32 folder_count;
	u32 dir_offset_count1;
	u32 file_information_count;
	u32 sub_file_count1;
	u32 unk5;
	u32 hash_folder_count;
	u32 unk7;
	u32 dir_offset_count2;
	u32 sub_file_count2;
	u32 unk10;
	u32 unk11;
	u32 unk12;
	u32 unk13;
	u32 unk14;
	u32 unk15;
	u32 unk16;
} __attribute__((packed)) fs_header_v1_t;

// DirectoryList (52 bytes)
typedef struct dir_list_v1_t
{
	u32 full_path_hash;
	u32 full_path_length_and_index;
	u32 name_hash;
	u32 name_hash_length;
	u32 parent_folder_hash;
	u32 parent_folder_hash_length;
	u32 extra_dis_re;
	u32 extra_dis_re_length;
	s32 file_info_start_index;
	s32 file_info_count;
	s32 child_dir_start_index;
	s32 child_dir_count;
	u32 flags;
} __attribute__((packed)) dir_list_v1_t;

// DirectoryOffset (28 bytes)
typedef struct dir_offset_v1_t
{
	u32 offset_lo;
	u32 offset_hi;
	u32 decomp_size;
	u32 size;
	u32 file_start_index;
	u32 file_count;
	u32 redirect_index;
} __attribute__((packed)) dir_offset_v1_t;

// FileInformationV1 (40 bytes)
typedef struct file_info_v1_t
{
	u32 path;
	u32 directory_index;
	u32 extension;
	u32 file_table_flag;
	u32 parent;
	u32 unk5;
	u32 hash2;
	u32 unk6;
	u32 sub_file_index;
	u32 flags;
} __attribute__((packed)) file_info_v1_t;

// SubFileInfo (16 bytes)
typedef struct sub_file_info_v1_t
{
	u32 offset;
	u32 comp_size;
	u32 decomp_size;
	u32 flags;
} __attribute__((packed)) sub_file_info_v1_t;

// Extension dictionary mapping common CRC32 hashes to extensions
typedef struct ext_map_t
{
	u32 hash;
	ccp ext;
} ext_map_t;

static const ext_map_t s_known_exts[] = {
	{ 0x1729af51, ".nus3audio" },
	{ 0x8c004f33, ".nus3bank" },
	{ 0x09b83ac4, ".tonelabel" },
	{ 0xe7af4342, ".bntx" },
	{ 0x62029ad4, ".prc" },
	{ 0xbe1c9acb, ".msbt" },
	{ 0x5c156dbc, ".nutexb" },
	{ 0x236db83a, ".numshb" },
	{ 0xdab89279, ".numatb" },
	{ 0x67e93703, ".nusktb" },
	{ 0x0032c3e4, ".nuanmb" },
	{ 0x2f6d9b0b, ".eff" },
	{ 0xd671372b, ".lua" },
	{ 0xaa275aed, ".bin" },
	{ 0x08a45257, ".csv" },
	{ 0x31f4d863, ".xml" },
	{ 0x6b072545, ".json" },
	{ 0x7fe65393, ".arc" },
	{ 0x6b7f2928, ".wav" },
	{ 0xfd785a32, ".webm" },
	{ 0xb1c50c22, ".mp4" },
	{ 0, 0 }
};

static ccp ResolveKnownExt (u32 hash)
{
	for (const ext_map_t *p = s_known_exts; p->ext; p++)
	{
		if (p->hash == hash)
			return p->ext;
	}
	return 0;
}

bool IsSmashARC (const u8 *data, size_t size)
{
	if (!data || size < sizeof (smash_arc_header_t))
		return false;

	const smash_arc_header_t *hdr = (const smash_arc_header_t *)data;
	return le64 (&hdr->magic) == SMASH_ARC_MAGIC;
}

bool IsSmashARCFile (ccp filename)
{
	if (!filename)
		return false;

	FILE *f = fopen (filename, "rb");
	if (!f)
		return false;

	smash_arc_header_t hdr;
	const size_t n = fread (&hdr, 1, sizeof (hdr), f);
	fclose (f);

	return n == sizeof (hdr) && le64 (&hdr.magic) == SMASH_ARC_MAGIC;
}

// Recursively write a file using direct fseeko / fread / zstd decompression
static enumError ExtractOneSubFile (FILE *f_arc,
	u64 file_data_base,
	const dir_list_v1_t *dirs,
	uint num_dirs,
	const dir_offset_v1_t *dir_offsets,
	uint num_dir_offsets,
	const file_info_v1_t *file_infos,
	uint num_file_infos,
	const sub_file_info_v1_t *sub_files,
	uint num_sub_files,
	uint dir_idx,
	uint fi_idx,
	int region_index,
	ccp out_file,
	uint depth)
{
	if (depth > 10 || fi_idx >= num_file_infos)
		return ERR_INVALID_DATA;

	const file_info_v1_t *fi = &file_infos[fi_idx];

	// Check redirect flag (0x00300000)
	if ((fi->flags & 0x00300000) == 0x00300000)
	{
		if (fi->sub_file_index < num_sub_files)
		{
			const uint target_fi = sub_files[fi->sub_file_index].flags & 0xFFFFFF;
			return ExtractOneSubFile (f_arc, file_data_base, dirs, num_dirs,
				dir_offsets, num_dir_offsets, file_infos, num_file_infos,
				sub_files, num_sub_files, dir_idx, target_fi, region_index, out_file, depth + 1);
		}
		return ERR_INVALID_DATA;
	}

	if (fi->sub_file_index >= num_sub_files)
		return ERR_INVALID_DATA;

	const sub_file_info_v1_t *sf = &sub_files[fi->sub_file_index];
	if (dir_idx >= num_dirs)
		return ERR_INVALID_DATA;

	const uint doff_idx = dirs[dir_idx].full_path_length_and_index >> 8;
	if (doff_idx >= num_dir_offsets)
		return ERR_INVALID_DATA;

	const dir_offset_v1_t *d_off = &dir_offsets[doff_idx];

	// Handle regional redirection
	if ((fi->file_table_flag >> 8) > 0)
	{
		const uint reg_sf_idx = (fi->file_table_flag >> 8) + (uint)region_index;
		const uint reg_doff_idx = doff_idx + 1 + (uint)region_index;
		if (reg_sf_idx < num_sub_files && reg_doff_idx < num_dir_offsets)
		{
			sf = &sub_files[reg_sf_idx];
			d_off = &dir_offsets[reg_doff_idx];
		}
	}

	const u64 folder_offset = ((u64)d_off->offset_hi << 32) | d_off->offset_lo;
	const u64 abs_offset = file_data_base + folder_offset + ((u64)sf->offset << 2);
	const u32 comp_size = sf->comp_size;
	const u32 decomp_size = sf->decomp_size;

	if (!comp_size)
		return ERR_OK;

	if (testmode)
		return ERR_OK;

	if (fseeko (f_arc, (off_t)abs_offset, SEEK_SET) != 0)
		return ERR_READ_FAILED;

	u8 *comp_buf = MALLOC (comp_size);
	if (!comp_buf)
		return ERR_CANT_CREATE;

	if (fread (comp_buf, 1, comp_size, f_arc) != comp_size)
	{
		FREE (comp_buf);
		return ERR_READ_FAILED;
	}

	char *slash = strrchr ((char *)out_file, '/');
	if (slash)
	{
		*slash = 0;
		CreatePath (out_file, true);
		*slash = '/';
	}

	// Decompress if Zstd
	if (comp_size >= 4 && le32 (comp_buf) == ZSTD_MAGIC_LE)
	{
		if (decomp_size > 0)
		{
			u8 *decomp_buf = MALLOC (decomp_size);
			if (decomp_buf)
			{
				size_t ret = ZSTD_decompress (decomp_buf, decomp_size, comp_buf, comp_size);
				if (!ZSTD_isError (ret))
				{
					SaveFile (out_file, 0, 0, decomp_buf, (uint)ret, 0);
					FREE (decomp_buf);
					FREE (comp_buf);
					return ERR_OK;
				}
				FREE (decomp_buf);
			}
		}

		// Fallback: dynamic DecodeZSTD allocation
		u8 *decomp_buf = 0;
		uint written = 0;
		enumError zerr = DecodeZSTD (&decomp_buf, &written, comp_buf, comp_size);
		if (!zerr && decomp_buf)
		{
			SaveFile (out_file, 0, 0, decomp_buf, written, 0);
			FREE (decomp_buf);
			FREE (comp_buf);
			return ERR_OK;
		}
	}

	// Uncompressed / raw payload
	SaveFile (out_file, 0, 0, comp_buf, comp_size, 0);

	FREE (comp_buf);
	return ERR_OK;
}

static void get_dest_dir (char *dest, size_t dest_size, ccp arg, ccp basedir)
{
	if (opt_dest)
		snprintf (dest, dest_size, "%s", opt_dest);
	else if (basedir && *basedir)
		snprintf (dest, dest_size, "%s/%s.d", basedir, FindFilename(arg, 0));
	else
		snprintf (dest, dest_size, "%s.d", arg);
}

enumError ExtractSmashARC (ccp arg, ccp basedir, uint depth)
{
	if (!arg)
		return ERR_NOTHING_TO_DO;

	FILE *f_arc = fopen (arg, "rb");
	if (!f_arc)
		return ERR_NOTHING_TO_DO;

	smash_arc_header_t hdr;
	if (fread (&hdr, 1, sizeof (hdr), f_arc) != sizeof (hdr) || le64 (&hdr.magic) != SMASH_ARC_MAGIC)
	{
		fclose (f_arc);
		return ERR_NOTHING_TO_DO;
	}

	const u64 file_off = le64 (&hdr.file_section_offset);
	const u64 fs_off = le64 (&hdr.fs_offset);
	const u64 search_off = le64 (&hdr.search_offset);

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SMASH-ARC: %s -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);

	// 1. Read SearchFileSystem (paths and folders) if present
	search_fs_header_t s_hdr;
	hash_to_index_t *search_folder_lookup = 0;
	search_list_entry_t *search_folders = 0;
	hash_to_index_t *search_path_lookup = 0;
	u32 *search_path_indices = 0;
	search_list_entry_t *search_paths = 0;

	if (search_off > 0 && fseeko (f_arc, (off_t)search_off, SEEK_SET) == 0)
	{
		if (fread (&s_hdr, 1, sizeof (s_hdr), f_arc) == sizeof (s_hdr))
		{
			const u32 folder_cnt = s_hdr.folder_count;
			const u32 pidx_cnt = s_hdr.path_index_count;
			const u32 path_cnt = s_hdr.path_count;

			search_folder_lookup = MALLOC (folder_cnt * sizeof (hash_to_index_t));
			search_folders = MALLOC (folder_cnt * sizeof (search_list_entry_t));
			search_path_lookup = MALLOC (pidx_cnt * sizeof (hash_to_index_t));
			search_path_indices = MALLOC (pidx_cnt * sizeof (u32));
			search_paths = MALLOC (path_cnt * sizeof (search_list_entry_t));

			if (search_folder_lookup && search_folders && search_path_lookup
				&& search_path_indices && search_paths)
			{
				fread (search_folder_lookup, sizeof (hash_to_index_t), folder_cnt, f_arc);
				fread (search_folders, sizeof (search_list_entry_t), folder_cnt, f_arc);
				fread (search_path_lookup, sizeof (hash_to_index_t), pidx_cnt, f_arc);
				fread (search_path_indices, sizeof (u32), pidx_cnt, f_arc);
				fread (search_paths, sizeof (search_list_entry_t), path_cnt, f_arc);
			}
		}
	}

	// 2. Read FileSystem table (V1 retail base game or V2 compressed)
	if (fseeko (f_arc, (off_t)fs_off, SEEK_SET) != 0)
	{
		FREE (search_folder_lookup);
		FREE (search_folders);
		FREE (search_path_lookup);
		FREE (search_path_indices);
		FREE (search_paths);
		fclose (f_arc);
		return ERR_READ_FAILED;
	}

	fs_header_v1_t fs_hdr;
	if (fread (&fs_hdr, 1, sizeof (fs_hdr), f_arc) != sizeof (fs_hdr))
	{
		FREE (search_folder_lookup);
		FREE (search_folders);
		FREE (search_path_lookup);
		FREE (search_path_indices);
		FREE (search_paths);
		fclose (f_arc);
		return ERR_READ_FAILED;
	}

	// Seek to start of V1 DirectoryList table (0x1bd24 relative to fs_off)
	const off_t dir_list_pos = (off_t)(fs_off + 0x1bd24);

	if (fseeko (f_arc, dir_list_pos, SEEK_SET) != 0)
	{
		FREE (search_folder_lookup);
		FREE (search_folders);
		FREE (search_path_lookup);
		FREE (search_path_indices);
		FREE (search_paths);
		fclose (f_arc);
		return ERR_READ_FAILED;
	}

	dir_list_v1_t *dirs = MALLOC (fs_hdr.folder_count * sizeof (dir_list_v1_t));
	const uint total_dir_offsets = fs_hdr.dir_offset_count1 + fs_hdr.dir_offset_count2;
	dir_offset_v1_t *dir_offsets = MALLOC (total_dir_offsets * sizeof (dir_offset_v1_t));
	const uint total_file_infos = fs_hdr.file_information_count;
	file_info_v1_t *file_infos = MALLOC (total_file_infos * sizeof (file_info_v1_t));
	const uint total_sub_files = fs_hdr.sub_file_count1 + fs_hdr.sub_file_count2;
	sub_file_info_v1_t *sub_files = MALLOC (total_sub_files * sizeof (sub_file_info_v1_t));

	if (!dirs || !dir_offsets || !file_infos || !sub_files)
	{
		FREE (dirs);
		FREE (dir_offsets);
		FREE (file_infos);
		FREE (sub_files);
		FREE (search_folder_lookup);
		FREE (search_folders);
		FREE (search_path_lookup);
		FREE (search_path_indices);
		FREE (search_paths);
		fclose (f_arc);
		return ERR_CANT_CREATE;
	}

	// Read DirectoryList
	fread (dirs, sizeof (dir_list_v1_t), fs_hdr.folder_count, f_arc);

	// Read DirectoryOffset
	fread (dir_offsets, sizeof (dir_offset_v1_t), total_dir_offsets, f_arc);

	// Skip HashFolderCount
	fseeko (f_arc, (off_t)(8 * fs_hdr.hash_folder_count), SEEK_CUR);

	// Read FileInformationV1
	fread (file_infos, sizeof (file_info_v1_t), total_file_infos, f_arc);

	// Read SubFileInfo
	fread (sub_files, sizeof (sub_file_info_v1_t), total_sub_files, f_arc);

	// Extraction phase:
	// Iterate through every directory and its associated files
	for (uint d = 0; d < fs_hdr.folder_count; d++)
	{
		const dir_list_v1_t *dir = &dirs[d];
		if (dir->file_info_count <= 0)
			continue;

		char dir_name[PATH_MAX];
		snprintf (dir_name, sizeof (dir_name), "%s/dir_0x%08x", dest, dir->full_path_hash);

		for (int fi_rel = 0; fi_rel < dir->file_info_count; fi_rel++)
		{
			const uint fi_idx = (uint)(dir->file_info_start_index + fi_rel);
			if (fi_idx >= total_file_infos)
				continue;

			const file_info_v1_t *fi = &file_infos[fi_idx];
			ccp ext = ResolveKnownExt (fi->extension);
			char out_file[PATH_MAX];
			if (ext)
				snprintf (out_file, sizeof (out_file), "%s/file_0x%08x%s", dir_name, fi->path, ext);
			else
				snprintf (out_file, sizeof (out_file), "%s/file_0x%08x_0x%08x.bin", dir_name, fi->path, fi->extension);

			ExtractOneSubFile (f_arc, file_off,
				dirs, fs_hdr.folder_count,
				dir_offsets, total_dir_offsets,
				file_infos, total_file_infos,
				sub_files, total_sub_files,
				d, fi_idx, 0, out_file, 0);
		}
	}

	FREE (dirs);
	FREE (dir_offsets);
	FREE (file_infos);
	FREE (sub_files);
	FREE (search_folder_lookup);
	FREE (search_folders);
	FREE (search_path_lookup);
	FREE (search_path_indices);
	FREE (search_paths);
	fclose (f_arc);

	return ERR_OK;
}

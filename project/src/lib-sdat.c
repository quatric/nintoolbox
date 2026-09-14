// Nintendo DS Nitro SDAT sound archive -- pack/unpack.

#include "lib-std.h"
#include "lib-sdat.h"
#include "lib-sequence.h"
#include "lib-sound-archive-util.h"
#include <assert.h>
#include <dirent.h>
#include <sys/stat.h>

// Nintendo DS SDAT. Block-relative SYMB/INFO offsets and absolute FAT
// offsets match the conventions consumed by the vendored NDSScanner.cpp.
typedef struct sdat_item_t
{
	char *name;
	u8 *data;
	size_t size;
	int kind;
} sdat_item_t;

static int cmp_sdat_item (const void *a, const void *b)
{
	const sdat_item_t *aa = a, *bb = b;
	return aa->kind != bb->kind ? aa->kind - bb->kind : strcmp (aa->name, bb->name);
}

static void sdat_block_header (membuf_t *b, ccp magic)
{
	mb_append (b, magic, 4);
	mb_append_u32e (b, 0, true);
}

static size_t sdat_name_list (membuf_t *b, sdat_item_t *it, uint n, int kind)
{
	size_t list = b->size;
	uint count = 0, j = 0;
	for (uint i = 0; i < n; i++)
		if (it[i].kind == kind)
			count++;
	mb_append_u32e (b, count, true);
	size_t ptrs = b->size;
	for (uint i = 0; i < count; i++)
		mb_append_u32e (b, 0, true);
	for (uint i = 0; i < n; i++)
		if (it[i].kind == kind)
		{
			mb_put_u32e (b, ptrs + 4 * j++, b->size, true);
			mb_append (b, it[i].name, strlen (it[i].name) + 1);
		}
	mb_align (b, 4);
	return list;
}

static size_t sdat_info_list (
	membuf_t *b, sdat_item_t *it, uint n, int kind, uint n_bank, uint n_wave)
{
	size_t list = b->size;
	uint count = 0, j = 0, file_id = 0;
	for (uint i = 0; i < n; i++)
		if (it[i].kind == kind)
			count++;
	mb_append_u32e (b, count, true);
	size_t ptrs = b->size;
	for (uint i = 0; i < count; i++)
		mb_append_u32e (b, 0, true);
	for (uint i = 0; i < n; i++)
	{
		if (it[i].kind == kind)
		{
			mb_put_u32e (b, ptrs + 4 * j++, b->size, true);
			mb_append_u16e (b, file_id, true);
			mb_append_u16e (b, 0, true);
			if (kind == 0)
			{
				mb_append_u16e (b, n_bank ? 0 : 0xffff, true);
				const u8 v[6] = { 127, 64, 64, 0, 0, 0 };
				mb_append (b, v, 6);
			}
			else if (kind == 1)
				for (uint w = 0; w < 4; w++)
					mb_append_u16e (b, w < n_wave ? w : 0xffff, true);
		}
		file_id++;
	}
	return list;
}

enumError PackSDATDir (u8 **out_data, size_t *out_size, ccp input_dir)
{
	if (!out_data || !out_size)
		return ERR_INVALID_DATA;
	*out_data = 0;
	*out_size = 0;
	DIR *dir = opendir (input_dir);
	if (!dir)
		return ERROR0 (ERR_CANT_OPEN, "PackSDATDir: can't open directory '%s'\n", input_dir);
	sdat_item_t *it = 0;
	uint n = 0, cap = 0;
	enumError err = ERR_OK;
	struct dirent *ent;
	while (!err && (ent = readdir (dir)))
	{
		if (ent->d_name[0] == '.')
			continue;
		int kind = has_suffix (ent->d_name, ".sseq") ? 0
			: has_suffix (ent->d_name, ".sbnk")		 ? 1
			: has_suffix (ent->d_name, ".swar")		 ? 2
			: has_suffix (ent->d_name, ".txt")		 ? 0
													 : -1;
		if (kind < 0)
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s", input_dir, ent->d_name);
		struct stat st;
		if (stat (path, &st) || !S_ISREG (st.st_mode))
			continue;
		u8 *raw = 0;
		size_t raw_size = 0;
		err = LoadFileAlloc (path, 0, 0, &raw, &raw_size, 0, 0, 0, false);
		if (err)
			break;
		if (has_suffix (ent->d_name, ".txt"))
		{
			u8 *bin = 0;
			size_t bin_size = 0;
			err = AssembleSequence (&bin, &bin_size, (ccp)raw, SEQ_FMT_SSEQ);
			FREE (raw);
			raw = bin;
			raw_size = bin_size;
			if (err)
				break;
		}
		if (n == cap)
		{
			cap = cap ? cap * 2 : 16;
			it = REALLOC (it, cap * sizeof (*it));
		}
		it[n] = (sdat_item_t) { strip_ext_dup (ent->d_name), raw, raw_size, kind };
		n++;
	}
	closedir (dir);
	if (!err && !n)
		err = ERROR0 (
			ERR_INVALID_DATA, "PackSDATDir: no SSEQ/SBNK/SWAR assets found in '%s'\n", input_dir);
	if (!err)
	{
		qsort (it, n, sizeof (*it), cmp_sdat_item);
		uint counts[3] = { 0 };
		for (uint i = 0; i < n; i++)
			counts[it[i].kind]++;
		membuf_t symb, info, fat, file, out;
		mb_init (&symb);
		mb_init (&info);
		mb_init (&fat);
		mb_init (&file);
		mb_init (&out);
		sdat_block_header (&symb, "SYMB");
		size_t sp[8];
		for (int i = 0; i < 8; i++)
			sp[i] = mb_append_u32e (&symb, 0, true);
		for (int k = 0; k < 8; k++)
		{
			int kind = k == 0 ? 0 : k == 2 ? 1 : k == 3 ? 2 : 99;
			size_t p = sdat_name_list (&symb, it, n, kind);
			mb_put_u32e (&symb, sp[k], p, true);
		}
		mb_put_u32e (&symb, 4, symb.size, true);
		sdat_block_header (&info, "INFO");
		size_t ip[8];
		for (int i = 0; i < 8; i++)
			ip[i] = mb_append_u32e (&info, 0, true);
		for (int k = 0; k < 8; k++)
		{
			int kind = k == 0 ? 0 : k == 2 ? 1 : k == 3 ? 2 : 99;
			size_t p = sdat_info_list (&info, it, n, kind, counts[1], counts[2]);
			mb_put_u32e (&info, ip[k], p, true);
		}
		mb_align (&info, 4);
		mb_put_u32e (&info, 4, info.size, true);
		sdat_block_header (&fat, "FAT ");
		mb_append_u32e (&fat, n, true);
		size_t fe = fat.size;
		for (uint i = 0; i < n; i++)
		{
			mb_append_u32e (&fat, 0, true);
			mb_append_u32e (&fat, it[i].size, true);
			mb_append_u32e (&fat, 0, true);
			mb_append_u32e (&fat, 0, true);
		}
		mb_put_u32e (&fat, 4, fat.size, true);
		sdat_block_header (&file, "FILE");
		mb_append_u32e (&file, n, true);
		mb_append_u32e (&file, 0, true);
		mb_align (&file, 32);
		u8 zero[0x40] = { 0 };
		mb_append (&out, zero, sizeof (zero));
		membuf_t *blocks[4] = { &symb, &info, &fat, &file };
		size_t offs[4];
		for (int b = 0; b < 4; b++)
		{
			mb_align (&out, 32);
			offs[b] = out.size;
			mb_append (&out, blocks[b]->data, blocks[b]->size);
		}
		for (uint i = 0; i < n; i++)
		{
			mb_align (&out, 32);
			mb_put_u32e (&out, offs[2] + fe + 16 * i, out.size, true);
			mb_append (&out, it[i].data, it[i].size);
		}
		mb_put_u32e (&out, offs[3] + 4, out.size - offs[3], true);
		memcpy (out.data, "SDAT", 4);
		mb_put_u16e (&out, 4, 0xfeff, true);
		mb_put_u16e (&out, 6, 0x0100, true);
		mb_put_u32e (&out, 8, out.size, true);
		mb_put_u16e (&out, 12, 0x40, true);
		mb_put_u16e (&out, 14, 4, true);
		for (int b = 0; b < 4; b++)
		{
			mb_put_u32e (&out, 0x10 + 8 * b, offs[b], true);
			mb_put_u32e (&out, 0x14 + 8 * b, b == 3 ? out.size - offs[b] : blocks[b]->size, true);
		}
		*out_data = out.data;
		*out_size = out.size;
		mb_free (&symb);
		mb_free (&info);
		mb_free (&fat);
		mb_free (&file);
	}
	for (uint i = 0; i < n; i++)
	{
		FREE (it[i].name);
		FREE (it[i].data);
	}
	if (it)
		FREE (it);
	return err;
}

enumError UnpackSDAT (const u8 *data, size_t size, ccp out_dir)
{
	if (size < 0x40 || memcmp (data, "SDAT", 4))
		return ERROR0 (ERR_INVALID_DATA, "UnpackSDAT: invalid header\n");
	u32 fat = rd_u32e (data + 0x20, true);
	if (fat + 12 > size || memcmp (data + fat, "FAT ", 4))
		return ERROR0 (ERR_INVALID_DATA, "UnpackSDAT: missing FAT block\n");
	u32 n = rd_u32e (data + fat + 8, true);
	if (n > (size - fat - 12) / 16)
		return ERROR0 (ERR_INVALID_DATA, "UnpackSDAT: invalid FAT count\n");
	if (mkdir (out_dir, 0755) && errno != EEXIST)
		return ERROR0 (ERR_CANT_CREATE, "UnpackSDAT: can't create '%s'\n", out_dir);
	char **names = CALLOC (n, sizeof (*names));
	u32 symb = rd_u32e (data + 0x10, true), info = rd_u32e (data + 0x18, true);
	if (symb + 0x28 <= size && info + 0x28 <= size && !memcmp (data + symb, "SYMB", 4)
		&& !memcmp (data + info, "INFO", 4))
	{
		const int cats[3] = { 0, 2, 3 };
		for (int c = 0; c < 3; c++)
		{
			u32 sl = rd_u32e (data + symb + 8 + 4 * cats[c], true),
				il = rd_u32e (data + info + 8 + 4 * cats[c], true);
			if (sl > size - symb - 4 || il > size - info - 4)
				continue;
			u32 sc = rd_u32e (data + symb + sl, true), ic = rd_u32e (data + info + il, true),
				count = sc < ic ? sc : ic;
			if (count > (size - symb - sl - 4) / 4 || count > (size - info - il - 4) / 4)
				continue;
			for (u32 j = 0; j < count; j++)
			{
				u32 so = rd_u32e (data + symb + sl + 4 + 4 * j, true),
					io = rd_u32e (data + info + il + 4 + 4 * j, true);
				if (!so || !io || so >= size - symb || io + 2 > size - info)
					continue;
				u16 id = rd_u16e (data + info + io, true);
				if (id >= n)
					continue;
				size_t max = size - (symb + so), len = strnlen ((ccp)data + symb + so, max);
				if (len == max)
					continue;
				char *name = MALLOC (len + 1);
				memcpy (name, data + symb + so, len);
				name[len] = 0;
				for (char *p = name; *p; p++)
					if (*p == '/' || *p == '\\' || (unsigned char)*p < 32)
						*p = '_';
				names[id] = name;
			}
		}
	}
	for (u32 i = 0; i < n; i++)
	{
		u32 off = rd_u32e (data + fat + 12 + 16 * i, true),
			len = rd_u32e (data + fat + 16 + 16 * i, true);
		if (off > size || len > size - off)
			return ERROR0 (ERR_INVALID_DATA, "UnpackSDAT: invalid file %u bounds\n", i);
		ccp ext = len >= 4 && !memcmp (data + off, "SSEQ", 4) ? "sseq"
			: len >= 4 && !memcmp (data + off, "SBNK", 4)	  ? "sbnk"
			: len >= 4 && !memcmp (data + off, "SWAR", 4)	  ? "swar"
															  : "bin";
		char fallback[32];
		snprintf (fallback, sizeof (fallback), "file_%04u", i);
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s.%s", out_dir, names[i] ? names[i] : fallback, ext);
		FILE *f = fopen (path, "wb");
		if (!f)
			return ERROR0 (ERR_CANT_CREATE, "UnpackSDAT: can't create '%s'\n", path);
		bool bad = fwrite (data + off, 1, len, f) != len;
		fclose (f);
		if (bad)
			return ERR_WRITE_FAILED;
	}
	for (u32 i = 0; i < n; i++)
		if (names[i])
			FREE (names[i]);
	FREE (names);
	return ERR_OK;
}

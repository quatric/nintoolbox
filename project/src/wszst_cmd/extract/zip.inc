#include "lib-zip.h"

static inline u32 zip_le32 (const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

typedef struct zip_member_t
{
	const u8 *data;
	char *name;
	uint stored_size, size, crc, method;
	bool directory;
} zip_member_t;

static enumError copy_zip_member (File_t *file, const zip_member_t *member)
{
	enumError err = DecodeZIPMember (file ? file->f : 0, member->data, member->stored_size,
		member->size, member->crc, member->method);
	if (err == ERR_WRITE_FAILED && file)
		return FILEERROR1 (file, err, "Writing ZIP member failed: %s\n", member->name);
	return err;
}

static enumError extract_zip_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".zip") && !is_ext (arg, ".tszip"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, UINT_MAX, 0, 0, false);
	if (err)
		return err;
	// EOCD must terminate the file, including its optional comment.
	const size_t begin = raw_size > 0x10016 ? raw_size - 0x10016 : 0;
	const u8 *eocd = 0;
	for (size_t pos = raw_size >= 22 ? raw_size - 22 : 0; raw_size >= 22 && pos >= begin; pos--)
	{
		if (!memcmp (raw + pos, "PK\x05\x06", 4)
			&& pos + 22 + zip_le16 (raw + pos + 20) == raw_size)
		{
			eocd = raw + pos;
			break;
		}
		if (!pos)
			break;
	}
	if (!eocd || zip_le16 (eocd + 4) || zip_le16 (eocd + 6)
		|| zip_le16 (eocd + 8) != zip_le16 (eocd + 10))
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}
	const uint count = zip_le16 (eocd + 10);
	const u32 cd_size = zip_le32 (eocd + 12), cd_off = zip_le32 (eocd + 16);
	const size_t eocd_off = eocd - raw;
	if (count == 0xffff || cd_off > eocd_off || cd_size != eocd_off - cd_off)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}
	zip_member_t *members = count ? CALLOC (count, sizeof (*members)) : 0;
	if (count && !members)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	const u8 *entry = raw + cd_off;
	for (uint i = 0; !err && i < count; i++)
	{
		if ((size_t)(eocd - entry) < 46 || memcmp (entry, "PK\x01\x02", 4))
		{
			err = ERR_INVALID_DATA;
			break;
		}
		const uint name_len = zip_le16 (entry + 28);
		const size_t record_size = 46u + name_len + zip_le16 (entry + 30) + zip_le16 (entry + 32);
		const uint flags = zip_le16 (entry + 8), method = zip_le16 (entry + 10);
		const u32 local_off = zip_le32 (entry + 42);
		zip_member_t *member = members + i;
		if (record_size > (size_t)(eocd - entry) || !name_len || name_len >= PATH_MAX
			|| memchr (entry + 46, 0, name_len) || (flags & (1 | 0x20 | 0x40 | 0x2000))
			|| zip_le16 (entry + 34) || (method != 0 && method != 8) || local_off > cd_off
			|| cd_off - local_off < 30 || memcmp (raw + local_off, "PK\x03\x04", 4))
		{
			err = ERR_INVALID_DATA;
			break;
		}
		member->name = MALLOC (name_len + 1);
		if (!member->name)
		{
			err = ERR_OUT_OF_MEMORY;
			break;
		}
		memcpy (member->name, entry + 46, name_len);
		member->name[name_len] = 0;
		member->directory = member->name[name_len - 1] == '/';
		if (member->directory)
			member->name[name_len - 1] = 0;
		if (!valid_sarc_path (member->name)
			|| strlen (dest) + 1 + (basedir ? strlen (basedir) : 0) + name_len >= PATH_MAX)
		{
			err = ERR_INVALID_DATA;
			break;
		}
		member->stored_size = zip_le32 (entry + 20);
		member->size = zip_le32 (entry + 24);
		member->crc = zip_le32 (entry + 16);
		member->method = method;
		const u8 *local = raw + local_off;
		const size_t data_off
			= (size_t)local_off + 30 + zip_le16 (local + 26) + zip_le16 (local + 28);
		if (zip_le16 (local + 6) != flags || zip_le16 (local + 8) != method
			|| zip_le16 (local + 26) != name_len || data_off > cd_off
			|| memcmp (local + 30, entry + 46, name_len) || member->stored_size > cd_off - data_off
			|| member->size > NFMT_MAX_OUTPUT || (member->directory && member->size)
			|| (!(flags & 8)
				&& (zip_le32 (local + 14) != member->crc
					|| zip_le32 (local + 18) != member->stored_size
					|| zip_le32 (local + 22) != member->size)))
		{
			err = ERR_INVALID_DATA;
			break;
		}
		member->data = raw + data_off;
		err = copy_zip_member (0, member);
		entry += record_size;
	}
	if (!err && entry != eocd)
		err = ERR_INVALID_DATA;
	if (!err && (verbose >= 0 || testmode))
		fprintf (stdlog, "%sEXTRACT ZIP:%s (%u entries) -> %s/\n", testmode ? "WOULD " : "", arg,
			count, dest);
	for (uint i = 0; !err && !testmode && i < count; i++)
	{
		const zip_member_t *member = members + i;
		if (member->directory)
			continue; // Parent directories are created with their files.
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", member->name);
		File_t file;
		err = CreateFileOpt (&file, true, path, false, arg);
		if (!err && file.f)
			err = copy_zip_member (&file, member);
		const enumError close_err = ResetFile (&file, opt_preserve);
		if (close_err > err)
			err = close_err;
	}
	for (uint i = 0; i < count; i++)
		FREE (members[i].name);
	FREE (members);
	FREE (raw);
	if (!err && !testmode && count)
		err = extract_tree_complete (dest, depth + 1);
	return err;
}

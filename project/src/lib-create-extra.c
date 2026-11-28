// CREATE glue for flat archives that EXTRACT writes as one file per member.

#include "lib-std.h"
#include "lib-archive-util.h"
#include "lib-create-extra.h"

typedef enumError (*pack_func) (u8 **out, u64 *out_size, const nintendo_sarc_entry_t *e, uint n);

// AFS: "AFS\0", count, {offset,size}*count, {meta_offset,meta_size}, data on 0x800
// boundaries, then 48-byte {name[32], date[6], reserved} records. EXTRACT names members
// "<NNNN>_<name>" or "<NNNN>.bin"; the name is restored and the timestamps are zeroed.
static enumError pack_afs (u8 **out, u64 *out_size, const nintendo_sarc_entry_t *e, uint n)
{
	u64 pos = align_up (12 + (u64)n * 8, 0x800), total = pos;
	for (uint i = 0; i < n; i++)
		total = align_up (total + e[i].size, 0x800);
	const u64 meta = total, file_size = total + (u64)n * 48;
	if (file_size > UINT_MAX)
		return ERR_FILE_TOO_BIG;
	u8 *d = CALLOC (1, file_size);
	if (!d)
		return ERR_CANT_CREATE;
	memcpy (d, "AFS", 3);
	wr_le32 (d + 4, n);
	for (uint i = 0; i < n; i++)
	{
		wr_le32 (d + 8 + 8 * i, (u32)pos);
		wr_le32 (d + 12 + 8 * i, e[i].size);
		memcpy (d + pos, e[i].data, e[i].size);
		pos = align_up (pos + e[i].size, 0x800);
		ccp name = leaf_name (e[i].name);
		if (strlen (name) > 5 && isdigit ((u8)name[0]) && name[4] == '_')
			strncpy ((char *)d + meta + 48 * i, name + 5, 31);
	}
	wr_le32 (d + 8 + 8 * n, (u32)meta);
	wr_le32 (d + 12 + 8 * n, n * 48);
	*out = d, *out_size = file_size;
	return ERR_OK;
}

// 3DS Camera PACK: 0x40-byte {name[0x38], u32 offset, u32 size} records, one all-zero
// terminator record, then the members on 16-byte boundaries.
static enumError pack_ctrpack (u8 **out, u64 *out_size, const nintendo_sarc_entry_t *e, uint n)
{
	if (n > 4096)
		return ERR_INVALID_DATA;
	u64 pos = align_up ((u64)(n + 1) * 0x40, 16), total = pos;
	for (uint i = 0; i < n; i++)
	{
		if (!e[i].size || strchr (e[i].name, '/') || strlen (e[i].name) >= 0x38)
			return ERR_INVALID_DATA;
		total = align_up (total + e[i].size, 16);
	}
	if (total > UINT_MAX)
		return ERR_FILE_TOO_BIG;
	u8 *d = CALLOC (1, total);
	if (!d)
		return ERR_CANT_CREATE;
	for (uint i = 0; i < n; i++)
	{
		strcpy ((char *)d + 0x40 * i, e[i].name);
		wr_le32 (d + 0x40 * i + 0x38, (u32)pos);
		wr_le32 (d + 0x40 * i + 0x3c, e[i].size);
		memcpy (d + pos, e[i].data, e[i].size);
		pos = align_up (pos + e[i].size, 16);
	}
	*out = d, *out_size = total;
	return ERR_OK;
}

// JUPITER-PCK: u32 header length (8 + 4n), u32 count, u32 sizes[n], payloads back to back.
// Members are packed in name order (EXTRACT writes file_NNN<ext>).
static enumError pack_jupiter (u8 **out, u64 *out_size, const nintendo_sarc_entry_t *e, uint n)
{
	if (!n || n > 1024)
		return ERR_INVALID_DATA;
	const u64 hdr = 8 + 4 * (u64)n;
	u64 total = hdr;
	for (uint i = 0; i < n; i++)
	{
		if (!e[i].size)
			return ERR_INVALID_DATA;
		total += e[i].size;
	}
	if (total > UINT_MAX)
		return ERR_FILE_TOO_BIG;
	u8 *d = MALLOC (total);
	if (!d)
		return ERR_CANT_CREATE;
	wr_le32 (d, (u32)hdr);
	wr_le32 (d + 4, n);
	u64 pos = hdr;
	for (uint i = 0; i < n; i++)
	{
		wr_le32 (d + 8 + 4 * i, e[i].size);
		memcpy (d + pos, e[i].data, e[i].size);
		pos += e[i].size;
	}
	*out = d, *out_size = total;
	return ERR_OK;
}

static const struct
{
	ccp ext, label;
	pack_func pack;
} extra_formats[] = {
	{ ".afs", "AFS", pack_afs },
	{ ".pack", "CTR-PACK", pack_ctrpack },
	{ ".pck", "JUPITER-PCK", pack_jupiter },
	{ 0 },
};

ccp create_extra_name (ccp ext)
{
	for (uint i = 0; ext && extra_formats[i].ext; i++)
		if (!strcasecmp (ext, extra_formats[i].ext))
			return extra_formats[i].label;
	return 0;
}

bool create_extra_dir (ccp ext, ccp source, ccp dest, enumError *err)
{
	pack_func pack = 0;
	for (uint i = 0; ext && extra_formats[i].ext; i++)
		if (!strcasecmp (ext, extra_formats[i].ext))
			pack = extra_formats[i].pack;
	if (!pack)
		return false;
	sarc_build_list_t list = { 0 };
	*err = collect_sarc_dir (&list, source, "");
	if (!*err && !list.used)
		*err = ERR_NOTHING_TO_DO;
	if (!*err && !testmode)
	{
		u8 *data = 0;
		u64 size = 0;
		*err = pack (&data, &size, list.entry, list.used);
		if (!*err)
		{
			File_t F;
			*err = CreateFileOpt (&F, true, dest, false, dest);
			if (F.f && fwrite (data, 1, size, F.f) != size)
				*err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %llu bytes failed: %s\n",
					(unsigned long long)size, dest);
			ResetFile (&F, opt_preserve);
		}
		FREE (data);
	}
	reset_sarc_build_list (&list);
	return true;
}

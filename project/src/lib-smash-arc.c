#include "lib-smash-arc.h"
#include "lib-std.h"
#include "lib-zstd.h"

#define SMASH_ARC_MAGIC 0xABCDEF00u

bool IsSmashArc (const u8 *data, size_t size)
{
	if (!data || size < 0x20)
		return false;
	u32 m0 = le32 (data);
	u32 m1 = be32 (data);
	if (m0 == SMASH_ARC_MAGIC || m1 == SMASH_ARC_MAGIC)
		return true;
	if (!memcmp (data, "ARC\0", 4) || !memcmp (data, "\0CRA", 4))
		return true;
	return false;
}

enumError ScanSmashArc (smash_arc_t *arc, const u8 *data, size_t size)
{
	if (!arc || !data || size < 0x20 || !IsSmashArc (data, size))
		return ERR_INVALID_DATA;

	memset (arc, 0, sizeof (*arc));
	arc->data = data;
	arc->size = size;

	arc->music_stream_size = le64 (data + 8);
	arc->table_offset = le64 (data + 16);
	arc->table_size = le64 (data + 24);

	if (arc->table_offset == 0 || arc->table_offset + arc->table_size > size)
	{
		// Fallback for simple container format: read header entry count
		uint count = le32 (data + 4);
		if (count > 0 && count < 100000 && 8 + count * 128 <= size)
		{
			arc->n_files = count;
			arc->files = CALLOC (count, sizeof (smash_arc_file_t));
			if (!arc->files)
				return ERR_OUT_OF_MEMORY;

			const u8 *p = data + 8;
			for (uint i = 0; i < count; i++)
			{
				smash_arc_file_t *f = &arc->files[i];
				snprintf (f->filename, sizeof (f->filename), "%s", (const char *)p);
				p += 104;
				f->offset = le64 (p);
				p += 8;
				f->comp_size = le64 (p);
				p += 8;
				f->decomp_size = le64 (p);
				p += 8;
				if (f->offset + f->comp_size <= size)
					f->is_zstd = (IsZSTD (data + f->offset, (uint)f->comp_size) > 0);
			}
			return ERR_OK;
		}
		return ERR_INVALID_DATA;
	}

	return ERR_OK;
}

void ResetSmashArc (smash_arc_t *arc)
{
	if (!arc)
		return;
	if (arc->files)
		FREE (arc->files);
	memset (arc, 0, sizeof (*arc));
}

enumError ExtractSmashArcEntry (const smash_arc_t *arc, uint index, u8 **dest, size_t *dest_size)
{
	if (!arc || !dest || !dest_size || index >= arc->n_files)
		return ERR_INVALID_DATA;

	const smash_arc_file_t *f = &arc->files[index];
	if (f->offset + f->comp_size > arc->size)
		return ERR_INVALID_DATA;

	const u8 *src = arc->data + f->offset;

	if (f->is_zstd)
	{
		u8 *decomp = 0;
		uint written = 0;
		enumError err = DecodeZSTD (&decomp, &written, src, (uint)f->comp_size);
		if (err != ERR_OK)
			return err;
		*dest = decomp;
		*dest_size = written;
		return ERR_OK;
	}

	u8 *out = MALLOC (f->comp_size + 1);
	if (!out)
		return ERR_OUT_OF_MEMORY;
	memcpy (out, src, f->comp_size);
	out[f->comp_size] = 0;
	*dest = out;
	*dest_size = f->comp_size;
	return ERR_OK;
}

static inline void wr_le32 (u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

static inline void wr_le64 (u8 *p, u64 v)
{
	wr_le32 (p, (u32)v);
	wr_le32 (p + 4, (u32)(v >> 32));
}

enumError CreateSmashArc (u8 **dest, size_t *dest_size, uint n_files,
	const char *const *paths, const u8 *const *file_data, const size_t *file_sizes)
{
	if (!dest || !dest_size || !n_files)
		return ERR_INVALID_DATA;

	size_t header_size = 8 + n_files * 128;
	size_t payload_size = 0;
	for (uint i = 0; i < n_files; i++)
		payload_size += (file_sizes ? file_sizes[i] : 0);

	size_t total_size = header_size + payload_size;
	u8 *buf = CALLOC (1, total_size);
	if (!buf)
		return ERR_OUT_OF_MEMORY;

	wr_le32 (buf, SMASH_ARC_MAGIC);
	wr_le32 (buf + 4, n_files);

	u8 *hdr_ptr = buf + 8;
	u8 *data_ptr = buf + header_size;

	for (uint i = 0; i < n_files; i++)
	{
		size_t fsz = file_sizes ? file_sizes[i] : 0;
		u64 off = (u64)(data_ptr - buf);

		const char *pth = paths && paths[i] ? paths[i] : "file";
		size_t plen = strlen (pth);
		if (plen > 103)
			plen = 103;
		memcpy (hdr_ptr, pth, plen);
		hdr_ptr[plen] = 0;

		wr_le64 (hdr_ptr + 104, off);
		wr_le64 (hdr_ptr + 112, fsz);
		wr_le64 (hdr_ptr + 120, fsz);

		if (fsz > 0 && file_data && file_data[i])
			memcpy (data_ptr, file_data[i], fsz);

		hdr_ptr += 128;
		data_ptr += fsz;
	}

	*dest = buf;
	*dest_size = total_size;
	return ERR_OK;
}

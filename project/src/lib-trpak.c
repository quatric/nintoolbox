#include "lib-trpak.h"
#include "lib-std.h"

// See lib-trpak.h for the format background. This file only ever needs the four FlatBuffers
// wire-format primitives below (root offset, table+vtable field lookup, indirected vector
// element, and scalar/vector-of-scalar vectors) -- TRPAK's schema is small enough that every
// field access can be hardcoded to its known vtable slot rather than writing a generic
// FlatBuffers reader. Every hop is resolved at 64-bit width and bounds-checked against 'size'
// before the *next* hop is allowed to use it, so a hostile root offset, soffset, vtable size,
// field offset, or vector length can only ever fail closed, never read or wrap out of bounds.

#define TRPAK_MAX_ENTRIES (1u << 20) // sane upper bound; real archives have at most a few thousand

static bool fb_u16 (const u8 *data, size_t size, u64 pos, u16 *out)
{
	if (pos + 2 > size)
		return false;
	*out = rd_le16 (data + pos);
	return true;
}

static bool fb_u32 (const u8 *data, size_t size, u64 pos, u32 *out)
{
	if (pos + 4 > size)
		return false;
	*out = rd_le32 (data + pos);
	return true;
}

static bool fb_u64 (const u8 *data, size_t size, u64 pos, u64 *out)
{
	if (pos + 8 > size)
		return false;
	*out = rd_le64 (data + pos);
	return true;
}

// Resolves the file's root table: a plain u32 offset at byte 0, relative to byte 0.
static bool fb_root (const u8 *data, size_t size, u64 *out_table_pos)
{
	u32 off;
	if (!fb_u32 (data, size, 0, &off))
		return false;
	if (off < 4 || off >= size) // must land strictly inside the buffer, past its own 4 bytes
		return false;
	*out_table_pos = off;
	return true;
}

// Resolves field number VTABLE_SLOT (4, 6, 8, ... per FlatBuffers' field-index*2+4 convention)
// of the table at TABLE_POS to the absolute byte position of its value. That value is the field
// itself for a scalar field (inline in the table), or a further uoffset for a vector/table
// field -- the caller picks which via fb_vector()/fb_indirect(). Returns false if the field is
// absent (vtable too short, or an explicit zero slot) or any offset along the way is invalid.
static bool fb_field (const u8 *data, size_t size, u64 table_pos, u16 vtable_slot, u64 *out_pos)
{
	s32 soffset;
	u32 raw;
	if (!fb_u32 (data, size, table_pos, &raw))
		return false;
	soffset = (s32)raw;

	// vtable_pos = table_pos - soffset; do the subtraction at signed 64-bit width so a hostile
	// soffset can't wrap the address space before the bounds check below catches it.
	const s64 vtable_pos_s = (s64)table_pos - (s64)soffset;
	if (vtable_pos_s < 0 || (u64)vtable_pos_s >= size)
		return false;
	const u64 vtable_pos = (u64)vtable_pos_s;

	u16 vtable_size;
	if (!fb_u16 (data, size, vtable_pos, &vtable_size))
		return false;
	if (vtable_slot >= vtable_size) // field not present in this (possibly older-schema) vtable
		return false;

	u16 field_rel;
	if (!fb_u16 (data, size, vtable_pos + vtable_slot, &field_rel))
		return false;
	if (!field_rel) // explicit "default value" encoding
		return false;

	const u64 field_pos = table_pos + field_rel; // field_rel is u16, table_pos < size: no overflow
	if (field_pos >= size)
		return false;
	*out_pos = field_pos;
	return true;
}

// Resolves a vector field at FIELD_POS (which holds a uoffset relative to itself pointing at
// the vector's u32 length prefix) to its element count and the absolute start of its data.
static bool fb_vector (
	const u8 *data, size_t size, u64 field_pos, uint elem_size, u64 *out_data_start, u32 *out_count)
{
	u32 off;
	if (!fb_u32 (data, size, field_pos, &off))
		return false;
	const u64 vec_pos = field_pos + off;

	u32 count;
	if (!fb_u32 (data, size, vec_pos, &count))
		return false;
	if (count > TRPAK_MAX_ENTRIES)
		return false;

	const u64 data_start = vec_pos + 4;
	const u64 need = (u64)count * elem_size; // both operands already bounded, no overflow
	if (data_start + need > size)
		return false;

	*out_data_start = data_start;
	*out_count = count;
	return true;
}

// Resolves the J'th element of an object vector (each element is itself a uoffset, relative to
// its own position) to the absolute position of the table it points at.
static bool fb_indirect (const u8 *data, size_t size, u64 elem_pos, u64 *out_pos)
{
	u32 off;
	if (!fb_u32 (data, size, elem_pos, &off))
		return false;
	const u64 pos = elem_pos + off;
	if (pos >= size)
		return false;
	*out_pos = pos;
	return true;
}

bool IsTRPAK (const u8 *data, size_t size)
{
	if (!data || size < 8)
		return false;
	u64 root_pos;
	if (!fb_root (data, size, &root_pos))
		return false;

	// A valid root table must at least resolve its own vtable; that alone rejects almost all
	// non-FlatBuffers data (the leading 4 bytes of most other formats are either ASCII magic,
	// too small a value, or too large to double as a plausible root offset).
	u64 dummy;
	return fb_field (data, size, root_pos, 4, &dummy) || fb_field (data, size, root_pos, 6, &dummy);
}

enumError ScanTRPAK (trpak_t *trpak, const u8 *data, uint size)
{
	if (!trpak || !data)
		return ERR_INVALID_DATA;
	memset (trpak, 0, sizeof (*trpak));

	if (!IsTRPAK (data, size))
		return ERROR0 (ERR_INVALID_DATA, "TRPAK: not a recognizable FlatBuffers root table\n");

	u64 root_pos;
	if (!fb_root (data, size, &root_pos))
		return ERROR0 (ERR_INVALID_DATA, "TRPAK: root offset out of bounds\n");

	u64 hashes_field, files_field;
	const bool have_hashes = fb_field (data, size, root_pos, 4, &hashes_field);
	const bool have_files = fb_field (data, size, root_pos, 6, &files_field);

	u64 hashes_start = 0, files_start = 0;
	u32 hashes_count = 0, files_count = 0;
	if (have_hashes && !fb_vector (data, size, hashes_field, 8, &hashes_start, &hashes_count))
		return ERROR0 (ERR_INVALID_DATA, "TRPAK: 'hashes' vector out of bounds\n");
	if (have_files && !fb_vector (data, size, files_field, 4, &files_start, &files_count))
		return ERROR0 (ERR_INVALID_DATA, "TRPAK: 'files' vector out of bounds\n");

	// The reference loader (TRPAK.cs Load()) treats a hashes/files length mismatch as a fatal
	// corruption, not something to silently truncate around -- do the same rather than guessing
	// which array to trust.
	if (hashes_count != files_count)
		return ERROR0 (ERR_INVALID_DATA, "TRPAK: 'hashes' (%u) and 'files' (%u) length mismatch\n",
			hashes_count, files_count);

	const uint n = files_count;
	trpak_entry_t *entries = n ? CALLOC (n, sizeof (*entries)) : 0;
	if (n && !entries)
		return ERR_OUT_OF_MEMORY;

	for (uint i = 0; i < n; i++)
	{
		entries[i].hash = rd_le64 (data + hashes_start + (u64)i * 8);

		u64 elem_pos = files_start + (u64)i * 4;
		u64 file_pos;
		if (!fb_indirect (data, size, elem_pos, &file_pos))
		{
			FREE (entries);
			return ERROR0 (ERR_INVALID_DATA, "TRPAK: files[%u] offset out of bounds\n", i);
		}

		u64 fp;
		if (fb_field (data, size, file_pos, 4, &fp) && fp < size)
			entries[i].unused = data[fp];
		entries[i].compression = 255; // File.CompressionType's schema default
		if (fb_field (data, size, file_pos, 6, &fp) && fp < size)
			entries[i].compression = data[fp];
		if (fb_field (data, size, file_pos, 8, &fp) && fp < size)
			entries[i].unk1 = data[fp];
		if (fb_field (data, size, file_pos, 10, &fp))
			fb_u64 (data, size, fp, &entries[i].decompressed_size);

		u64 data_field;
		if (fb_field (data, size, file_pos, 12, &data_field))
		{
			u64 dstart;
			u32 dcount;
			if (!fb_vector (data, size, data_field, 1, &dstart, &dcount))
			{
				FREE (entries);
				return ERROR0 (ERR_INVALID_DATA, "TRPAK: files[%u].data vector out of bounds\n", i);
			}
			entries[i].data_offset = dstart;
			entries[i].data_size = dcount;
		}
	}

	trpak->data = data;
	trpak->size = size;
	trpak->n_entries = n;
	trpak->entries = entries;
	return ERR_OK;
}

void ResetTRPAK (trpak_t *trpak)
{
	if (!trpak)
		return;
	FREE (trpak->entries);
	memset (trpak, 0, sizeof (*trpak));
}

static ccp trpak_compression_name (u8 c)
{
	switch (c)
	{
		case 3:
			return "OODLE";
		case 255:
			return "NONE";
		default:
			return "UNKNOWN";
	}
}

enumError DecodeTRPAK_Text (FILE *out, const trpak_t *trpak)
{
	if (!out || !trpak || !trpak->data)
		return ERR_INVALID_DATA;

	fprintf (out,
		"#TRPAK\n"
		"# tr Package -- decoded by "
		"wszst"
		"\n"
		"# entry_count=%u\n\n",
		trpak->n_entries);

	for (uint i = 0; i < trpak->n_entries; i++)
	{
		const trpak_entry_t *e = trpak->entries + i;
		fprintf (out,
			"[%u]\n"
			"hash             = 0x%016llx\n"
			"compression      = %s (%u)\n"
			"decompressed_size = %llu\n"
			"data_offset      = %llu\n"
			"data_size        = %u\n\n",
			i, (unsigned long long)e->hash, trpak_compression_name (e->compression), e->compression,
			(unsigned long long)e->decompressed_size, (unsigned long long)e->data_offset,
			e->data_size);
	}

	return ERR_OK;
}

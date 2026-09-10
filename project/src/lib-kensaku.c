#include "lib-std.h"
#include "lib-kensaku.h"
#include "lib-lz10.h"
#include <string.h>
#include <errno.h>

#define KENSAKU_PRES_HEADER 0x80
#define KENSAKU_ENTRY_SIZE 0x20
#define KENSAKU_MAX_ENTRIES 0x100000

// Read a NUL-terminated string that lives inside blob[0..size). Returns the
// length copied (excluding the terminator); dest is always NUL-terminated.
static uint kensaku_cstr (char *dest, uint dest_size, const u8 *blob, uint size, u32 off)
{
	uint n = 0;
	if (off < size)
		while (off + n < size && blob[off + n] && n + 1 < dest_size)
		{
			dest[n] = (char)blob[off + n];
			n++;
		}
	dest[n] = 0;
	return n;
}

void ResetKensaku (kensaku_t *k)
{
	if (!k)
		return;
	FREE (k->blob);
	FREE (k->entries);
	FREE (k->names);
	memset (k, 0, sizeof (*k));
}

enumError ScanKensakuRZ (kensaku_t *k, const u8 *data, uint size)
{
	if (!k || !data || size < 4)
		return EINVAL;
	if (data[0] != 0x10 && data[0] != 0x11)
		return EINVAL;
	memset (k, 0, sizeof (*k));

	u8 *blob = 0;
	uint blob_size = 0;
	enumError err = DecodeLZ10LZ11 (&blob, &blob_size, data, size);
	if (err)
		return err;

	if (blob_size < KENSAKU_PRES_HEADER + 4 || memcmp (blob, "Pres", 4))
	{
		FREE (blob);
		return EINVAL;
	}

	// The u32 at 0x80 is the byte offset where the member data begins, i.e.
	// the end of the 0x20-byte entry table that starts right after the header.
	const u32 table_end = rd_le32 (blob + KENSAKU_PRES_HEADER);
	if (table_end < KENSAKU_PRES_HEADER || table_end > blob_size
		|| (table_end - KENSAKU_PRES_HEADER) % KENSAKU_ENTRY_SIZE)
	{
		FREE (blob);
		return EINVAL;
	}
	const uint n = (table_end - KENSAKU_PRES_HEADER) / KENSAKU_ENTRY_SIZE;
	if (!n || n > KENSAKU_MAX_ENTRIES)
	{
		FREE (blob);
		return EINVAL;
	}

	kensaku_entry_t *entries = CALLOC (n, sizeof (*entries));
	// A generous upper bound: three path components per entry can never
	// exceed the whole decompressed blob, plus separators and terminators.
	char *names = CALLOC (1, (size_t)blob_size + (size_t)n * 8 + 16);
	if (!entries || !names)
	{
		FREE (blob);
		FREE (entries);
		FREE (names);
		return ERR_CANT_CREATE;
	}
	uint name_pos = 0;
	uint used = 0;

	for (uint i = 0; i < n; i++)
	{
		const u8 *rec = blob + KENSAKU_PRES_HEADER + (size_t)i * KENSAKU_ENTRY_SIZE;
		const u32 offset = rd_le32 (rec + 0x00);
		const u32 fsize = rd_le32 (rec + 0x04);
		const u32 name_off = rd_le32 (rec + 0x08);
		const u32 name_elements = rd_le32 (rec + 0x0c);

		// The .bms skips entries whose data offset is zero (directory markers
		// / unused slots).
		if (!offset)
			continue;
		if ((u64)offset + fsize > blob_size)
			continue;
		if (name_off + 12 > blob_size)
			continue;

		// The name descriptor: three offsets, each relative to PRES_OFF (0).
		const u32 str_name = rd_le32 (blob + name_off + 0x00);
		const u32 str_ext = rd_le32 (blob + name_off + 0x04);
		const u32 str_folder = rd_le32 (blob + name_off + 0x08);

		char nm[PATH_MAX] = "", ex[PATH_MAX] = "", fd[PATH_MAX] = "";
		if (name_elements >= 1)
			kensaku_cstr (nm, sizeof (nm), blob, blob_size, str_name);
		if (name_elements >= 2)
			kensaku_cstr (ex, sizeof (ex), blob, blob_size, str_ext);
		if (name_elements >= 3)
			kensaku_cstr (fd, sizeof (fd), blob, blob_size, str_folder);

		char rel[PATH_MAX];
		if (*fd && *ex)
			snprintf (rel, sizeof (rel), "%s/%s.%s", fd, nm, ex);
		else if (*ex)
			snprintf (rel, sizeof (rel), "%s.%s", nm, ex);
		else if (*nm)
			snprintf (rel, sizeof (rel), "%s", nm);
		else
			snprintf (rel, sizeof (rel), "file_%04u.bin", i);

		entries[used].name = names + name_pos;
		const uint rl = strlen (rel);
		memcpy (names + name_pos, rel, rl + 1);
		name_pos += rl + 1;

		entries[used].offset = offset;
		entries[used].size = fsize;
		used++;
	}

	k->blob = blob;
	k->blob_size = blob_size;
	k->entries = entries;
	k->n_entries = used;
	k->names = names;
	k->compression = data[0];
	return ERR_OK;
}

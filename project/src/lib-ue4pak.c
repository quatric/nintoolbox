#include "lib-ue4pak.h"
#include "lib-std.h"
#include "lib-zstd.h"
#include <zlib.h>

bool IsUE4Pak (const u8 *data, size_t size)
{
	if (!data || size < 44)
		return false;

	// The FPakInfo footer is located near the end of the file.
	// Search the last 512 bytes for the UE4 PAK magic: 0x5A6F12E1.
	const size_t probe_len = size < 512 ? size : 512;
	const u8 *probe_start = data + size - probe_len;

	for (size_t i = 0; i + 4 <= probe_len; i++)
	{
		if (le32 (probe_start + i) == UE4_PAK_MAGIC)
		{
			const u8 *m = probe_start + i;
			if (m + 24 <= data + size)
			{
				uint version = le32 (m + 4);
				u64 idx_off = le64 (m + 8);
				u64 idx_sz = le64 (m + 16);
				if (version <= 15 && idx_off + idx_sz <= size && idx_off > 0 && idx_sz > 0)
					return true;
			}
		}
	}
	return false;
}

static const u8 *read_fstring (const u8 *p, const u8 *end, char *out, size_t out_max)
{
	if (p + 4 > end)
	{
		if (out && out_max > 0)
			out[0] = 0;
		return end;
	}

	s32 len = (s32)le32 (p);
	p += 4;

	if (len == 0)
	{
		if (out && out_max > 0)
			out[0] = 0;
		return p;
	}

	if (len > 0)
	{
		// ASCII string with trailing null
		if (p + len > end)
		{
			if (out && out_max > 0)
				out[0] = 0;
			return end;
		}
		if (out && out_max > 0)
		{
			size_t copy_len = (size_t)len < out_max ? (size_t)len : out_max - 1;
			memcpy (out, p, copy_len);
			out[copy_len] = 0;
			// Strip trailing null if included in len
			if (copy_len > 0 && out[copy_len - 1] == 0)
				out[copy_len - 1] = 0;
		}
		p += len;
	}
	else
	{
		// UTF-16LE string: len is negative
		size_t u16_chars = (size_t)(-len);
		if (p + u16_chars * 2 > end)
		{
			if (out && out_max > 0)
				out[0] = 0;
			return end;
		}
		if (out && out_max > 0)
		{
			size_t o = 0;
			for (size_t i = 0; i < u16_chars && o + 1 < out_max; i++)
			{
				u16 ch = le16 (p + i * 2);
				if (ch == 0)
					break;
				out[o++] = (ch < 128) ? (char)ch : '?';
			}
			out[o] = 0;
		}
		p += u16_chars * 2;
	}
	return p;
}

enumError ScanUE4Pak (ue4_pak_t *pak, const u8 *data, size_t size)
{
	if (!pak || !data || size < 44)
		return ERR_INVALID_DATA;

	memset (pak, 0, sizeof (*pak));
	pak->data = data;
	pak->size = size;

	// Locate footer
	const size_t probe_len = size < 512 ? size : 512;
	const u8 *probe_start = data + size - probe_len;
	const u8 *footer_magic = 0;

	for (size_t i = 0; i + 4 <= probe_len; i++)
	{
		if (le32 (probe_start + i) == UE4_PAK_MAGIC)
		{
			const u8 *m = probe_start + i;
			if (m + 24 <= data + size)
			{
				uint version = le32 (m + 4);
				u64 idx_off = le64 (m + 8);
				u64 idx_sz = le64 (m + 16);
				if (version <= 15 && idx_off + idx_sz <= size && idx_off > 0)
				{
					footer_magic = m;
					break;
				}
			}
		}
	}

	if (!footer_magic)
		return ERR_INVALID_DATA;

	pak->version = le32 (footer_magic + 4);
	pak->index_offset = le64 (footer_magic + 8);
	pak->index_size = le64 (footer_magic + 16);
	memcpy (pak->comp_methods[0], "None", 5);

	// Read compression method names if present (version >= 7)
	if (pak->version >= 7)
	{
		const u8 *methods_ptr = footer_magic + 24 + 20; // past hash
		if (pak->version >= 8)
			methods_ptr += 1; // skip bFrozenIndex

		for (uint m = 0; m < 4; m++)
		{
			if (methods_ptr + (m + 1) * 32 <= data + size)
			{
				memcpy (pak->comp_methods[m + 1], methods_ptr + m * 32, 31);
				pak->comp_methods[m + 1][31] = 0;
			}
		}
	}

	if (pak->index_offset + pak->index_size > size)
		return ERR_INVALID_DATA;

	const u8 *p = data + pak->index_offset;
	const u8 *end = p + pak->index_size;

	p = read_fstring (p, end, pak->mount_point, sizeof (pak->mount_point));
	if (p + 4 > end)
		return ERR_INVALID_DATA;

	pak->n_entries = le32 (p);
	p += 4;

	if (pak->n_entries == 0 || pak->n_entries > 500000)
		return ERR_INVALID_DATA;

	pak->entries = CALLOC (pak->n_entries, sizeof (ue4_pak_entry_t));
	if (!pak->entries)
		return ERR_OUT_OF_MEMORY;

	for (uint i = 0; i < pak->n_entries; i++)
	{
		ue4_pak_entry_t *e = &pak->entries[i];
		char fname_buf[PATH_MAX];
		p = read_fstring (p, end, fname_buf, sizeof (fname_buf));
		if (p >= end)
			break;

		e->filename = STRDUP (fname_buf);
		if (p + 24 > end)
			break;

		e->offset = le64 (p);
		p += 8;
		e->size = le64 (p);
		p += 8;
		e->uncompressed_size = le64 (p);
		p += 8;

		if (pak->version < 7)
		{
			if (p + 4 > end)
				break;
			e->compression_method = le32 (p);
			p += 4;
		}
		else
		{
			if (p + 4 > end)
				break;
			e->compression_method = le32 (p);
			p += 4;
		}

		if (p + 20 > end)
			break;
		memcpy (e->hash, p, 20);
		p += 20;

		if (e->compression_method != 0)
		{
			if (p + 4 > end)
				break;
			e->block_count = le32 (p);
			p += 4;

			if (e->block_count > 0 && e->block_count < 100000 && p + e->block_count * 16 <= end)
			{
				e->blocks = CALLOC (e->block_count, sizeof (ue4_pak_block_t));
				for (uint b = 0; b < e->block_count; b++)
				{
					e->blocks[b].comp_start = le64 (p);
					p += 8;
					e->blocks[b].comp_end = le64 (p);
					p += 8;
				}
			}
		}

		if (p + 1 > end)
			break;
		e->encrypted = *p++;

		if (p + 4 > end)
			break;
		e->block_size = le32 (p);
		p += 4;

		if (e->compression_method > 0 && e->compression_method <= 4)
			snprintf (e->method_name, sizeof (e->method_name), "%s",
				pak->comp_methods[e->compression_method]);
		else if (e->compression_method == 0)
			snprintf (e->method_name, sizeof (e->method_name), "None");
		else
			snprintf (e->method_name, sizeof (e->method_name), "Method_%u", e->compression_method);
	}

	return ERR_OK;
}

void ResetUE4Pak (ue4_pak_t *pak)
{
	if (!pak)
		return;
	if (pak->entries)
	{
		for (uint i = 0; i < pak->n_entries; i++)
		{
			if (pak->entries[i].filename)
				FREE (pak->entries[i].filename);
			if (pak->entries[i].blocks)
				FREE (pak->entries[i].blocks);
		}
		FREE (pak->entries);
	}
	memset (pak, 0, sizeof (*pak));
}

enumError ExtractUE4PakEntry (const ue4_pak_t *pak, uint index, u8 **dest, size_t *dest_size)
{
	if (!pak || !dest || !dest_size || index >= pak->n_entries)
		return ERR_INVALID_DATA;

	const ue4_pak_entry_t *e = &pak->entries[index];
	if (e->uncompressed_size == 0)
	{
		*dest = CALLOC (1, 1);
		*dest_size = 0;
		return ERR_OK;
	}

	u8 *out = MALLOC (e->uncompressed_size + 1);
	if (!out)
		return ERR_OUT_OF_MEMORY;

	if (e->compression_method == 0)
	{
		// Uncompressed: in standard UE4 PAK, the file data payload at e->offset
		// begins after an FPakEntry header (53 bytes for V3-V11).
		uint hdr_size = 53;
		u64 src_off = e->offset + hdr_size;

		if (src_off + e->uncompressed_size <= pak->size)
			memcpy (out, pak->data + src_off, e->uncompressed_size);
		else if (e->offset + e->uncompressed_size <= pak->size)
			memcpy (out, pak->data + e->offset, e->uncompressed_size);
		else
		{
			FREE (out);
			return ERR_INVALID_DATA;
		}
		out[e->uncompressed_size] = 0;
		*dest = out;
		*dest_size = e->uncompressed_size;
		return ERR_OK;
	}

	// Compressed entry: iterate compression blocks
	bool is_zstd = !strcasecmp (e->method_name, "Zstd") || e->compression_method == 2;
	size_t written_total = 0;

	for (uint b = 0; b < e->block_count; b++)
	{
		u64 cstart = e->blocks[b].comp_start;
		u64 cend = e->blocks[b].comp_end;
		if (cstart >= cend || cend > pak->size)
		{
			FREE (out);
			return ERR_INVALID_DATA;
		}

		size_t clen = (size_t)(cend - cstart);
		const u8 *csrc = pak->data + cstart;
		size_t dst_off = (size_t)b * e->block_size;
		size_t expected_dst = e->block_size;
		if (dst_off + expected_dst > e->uncompressed_size)
			expected_dst = e->uncompressed_size - dst_off;

		if (is_zstd)
		{
			uint block_written = 0;
			enumError zerr = DecodeZSTDpart (out + dst_off, (uint)expected_dst, &block_written, csrc, (uint)clen);
			if (zerr != ERR_OK)
			{
				FREE (out);
				return zerr;
			}
			written_total += block_written;
		}
		else
		{
			// Zlib deflate
			uLongf dlen = (uLongf)expected_dst;
			int zres = uncompress (out + dst_off, &dlen, csrc, (uLong)clen);
			if (zres != Z_OK)
			{
				// Retry with raw inflate in case no zlib header
				z_stream strm;
				memset (&strm, 0, sizeof (strm));
				strm.next_in = (Bytef *)csrc;
				strm.avail_in = (uInt)clen;
				strm.next_out = out + dst_off;
				strm.avail_out = (uInt)expected_dst;
				if (inflateInit2 (&strm, -15) == Z_OK)
				{
					inflate (&strm, Z_FINISH);
					inflateEnd (&strm);
					written_total += strm.total_out;
				}
				else
				{
					FREE (out);
					return ERR_INVALID_DATA;
				}
			}
			else
				written_total += dlen;
		}
	}

	out[e->uncompressed_size] = 0;
	*dest = out;
	*dest_size = e->uncompressed_size;
	return ERR_OK;
}

enumError CreateUE4Pak (u8 **dest, size_t *dest_size, const char *mount_point,
	uint n_files, const char *const *rel_paths, const u8 *const *file_data, const size_t *file_sizes)
{
	if (!dest || !dest_size || (!n_files && rel_paths))
		return ERR_INVALID_DATA;

	const char *mp = mount_point && *mount_point ? mount_point : "../../../MarioAndLuigi/Content/";
	const uint version = 8; // UE4.27 standard version
	const uint hdr_size = 57;

	// Calculate total size needed
	size_t data_payload_size = 0;
	for (uint i = 0; i < n_files; i++)
		data_payload_size += hdr_size + (file_sizes ? file_sizes[i] : 0);

	size_t index_size = 4 + strlen (mp) + 1 + 4;
	for (uint i = 0; i < n_files; i++)
		index_size += 4 + strlen (rel_paths[i]) + 1 + 8 + 8 + 8 + 4 + 20 + 1 + 4;

	size_t footer_size = 221;
	size_t total_size = data_payload_size + index_size + footer_size;

	u8 *buf = CALLOC (1, total_size);
	if (!buf)
		return ERR_OUT_OF_MEMORY;

	u8 *p = buf;
	u64 *offsets = CALLOC (n_files, sizeof (u64));

	// Write data payloads
	for (uint i = 0; i < n_files; i++)
	{
		offsets[i] = (u64)(p - buf);
		u8 *entry_hdr = p;
		p += hdr_size;
		size_t fsz = file_sizes ? file_sizes[i] : 0;
		if (fsz > 0 && file_data && file_data[i])
		{
			memcpy (p, file_data[i], fsz);
			p += fsz;
		}

		// Write local FPakEntry header
		u8 *eh = entry_hdr;
		CF_W64 (eh + 0, offsets[i]);
		CF_W64 (eh + 8, fsz);
		CF_W64 (eh + 16, fsz);
		CF_W32 (eh + 24, 0); // Method 0 = None
		memset (eh + 28, 0, 20); // Hash
		eh[48] = 0; // encrypted
		CF_W32 (eh + 49, 65536); // block_size
	}

	u64 index_offset = (u64)(p - buf);

	// Write Index table
	s32 mp_len = (s32)(strlen (mp) + 1);
	CF_W32 (p, mp_len);
	p += 4;
	memcpy (p, mp, mp_len);
	p += mp_len;

	CF_W32 (p, n_files);
	p += 4;

	for (uint i = 0; i < n_files; i++)
	{
		s32 flen = (s32)(strlen (rel_paths[i]) + 1);
		CF_W32 (p, flen);
		p += 4;
		memcpy (p, rel_paths[i], flen);
		p += flen;

		size_t fsz = file_sizes ? file_sizes[i] : 0;
		CF_W64 (p, offsets[i]);
		p += 8;
		CF_W64 (p, fsz);
		p += 8;
		CF_W64 (p, fsz);
		p += 8;
		CF_W32 (p, 0); // compression_method = 0
		p += 4;
		memset (p, 0, 20); // hash
		p += 20;
		*p++ = 0; // encrypted = 0
		CF_W32 (p, 65536); // block_size
		p += 4;
	}

	u64 real_index_size = (u64)(p - (buf + index_offset));
	FREE (offsets);

	// Write Footer (FPakInfo)
	u8 *footer = buf + total_size - footer_size;
	// EncryptionKeyGuid (16 bytes zero)
	footer[16] = 0; // bEncryptedIndex
	CF_W32 (footer + 17, UE4_PAK_MAGIC);
	CF_W32 (footer + 21, version);
	CF_W64 (footer + 25, index_offset);
	CF_W64 (footer + 33, real_index_size);
	memset (footer + 41, 0, 20); // IndexHash
	footer[61] = 0; // bFrozenIndex

	// CompressionMethods array (4 * 32 bytes)
	memcpy (footer + 62, "None\0", 5);
	memcpy (footer + 62 + 32, "Zlib\0", 5);
	memcpy (footer + 62 + 64, "Zstd\0", 5);
	memcpy (footer + 62 + 96, "Oodle\0", 6);

	*dest = buf;
	*dest_size = total_size;
	return ERR_OK;
}

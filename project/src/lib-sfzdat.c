#include "lib-std.h"
#include "lib-sfzdat.h"
#include <string.h>
#include <errno.h>

//-----------------------------------------------------------------------------
///////////////	    Star Fox Zero DAT (Wii U, "DAT\0")		///////////////
//-----------------------------------------------------------------------------

// Layout ported from aluigi's public QuickBMS script (star_fox_zero_dat.bms),
// all big-endian:
//
//   'DAT\0'
//   u32 files
//   u32 offsets_off      -> u32 offset[files]      (absolute into the file)
//   u32 exts_off         -> per-file type/extension string
//   u32 names_off        -> u32 name_stride, then per-file name string
//   u32 sizes_off        -> u32 size[files]
//   u32 hashmap_off      -> hash lookup table, not needed for extraction
//
// The BMS reads the extension and name sections as back-to-back
// NUL-terminated strings.  That is only *incidentally* right: this is
// PlatinumGames' DAT container, whose two string sections are fixed-stride
// record arrays -- 4 bytes per extension, and `name_stride` bytes per name
// (that is exactly what the script's unexplained `get DUMMY long` before the
// names is).  Sequential string reads happen to land correctly whenever the
// stored strings fill their record, which holds for the 3-character
// extensions this game uses but not for names.  So both sections are read
// here as strided records, with a packed-string fallback if the strided
// interpretation does not fit inside the section.
//
// Entry names are emitted as "<ext>/<name>", matching the script.
enumError ScanSFZDAT (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || size < 32 || memcmp (data, "DAT\0", 4))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u32 files = rd_be32 (data + 4);
	const u32 off_offsets = rd_be32 (data + 8);
	const u32 off_exts = rd_be32 (data + 12);
	const u32 off_names = rd_be32 (data + 16);
	const u32 off_sizes = rd_be32 (data + 20);

	if (!files || files > 0x40000)
		return EINVAL;
	if ((u64)off_offsets + (u64)files * 4 > size)
		return EINVAL;
	if ((u64)off_sizes + (u64)files * 4 > size)
		return EINVAL;
	if (off_exts >= size || (u64)off_names + 4 > size)
		return EINVAL;

	// Names are a fixed-stride array preceded by the stride.  Fall back to
	// packed NUL-terminated strings when the strided array does not fit.
	u32 name_stride = rd_be32 (data + off_names);
	const u32 names_base = off_names + 4;
	if (!name_stride || name_stride > 256
		|| (u64)names_base + (u64)files * name_stride > size)
		name_stride = 0;

	// Extensions are 4-byte records in every known file; same fallback.
	u32 ext_stride = 4;
	if ((u64)off_exts + (u64)files * 4 > size)
		ext_stride = 0;

	nintendo_sarc_entry_t *out = CALLOC (files, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint n = 0;
	uint ext_pos = off_exts, name_pos = names_base;
	for (u32 i = 0; i < files; i++)
	{
		char ext[64] = "", name[256] = "";

		if (ext_stride)
		{
			const uint base = off_exts + i * ext_stride;
			uint len = 0;
			while (len < ext_stride && data[base + len])
				len++;
			memcpy (ext, data + base, len);
			ext[len] = 0;
		}
		else
		{
			uint start = ext_pos;
			while (ext_pos < size && data[ext_pos])
				ext_pos++;
			if (ext_pos >= size)
				break;
			uint len = ext_pos - start;
			if (len >= sizeof (ext))
				len = sizeof (ext) - 1;
			memcpy (ext, data + start, len);
			ext[len] = 0;
			ext_pos++;
		}

		if (name_stride)
		{
			const uint base = names_base + i * name_stride;
			uint len = 0;
			while (len < name_stride && data[base + len])
				len++;
			if (len >= sizeof (name))
				len = sizeof (name) - 1;
			memcpy (name, data + base, len);
			name[len] = 0;
		}
		else
		{
			uint start = name_pos;
			while (name_pos < size && data[name_pos])
				name_pos++;
			if (name_pos >= size)
				break;
			uint len = name_pos - start;
			if (len >= sizeof (name))
				len = sizeof (name) - 1;
			memcpy (name, data + start, len);
			name[len] = 0;
			name_pos++;
		}

		const u32 foff = rd_be32 (data + off_offsets + i * 4);
		const u32 fsize = rd_be32 (data + off_sizes + i * 4);
		if ((u64)foff + fsize > size)
			continue;

		char full[352];
		if (*ext && OwnedNameOk (ext))
			snprintf (full, sizeof (full), "%s/%s", ext, name);
		else
			snprintf (full, sizeof (full), "%s", name);
		if (!OwnedNameOk (full))
			snprintf (full, sizeof (full), "%04u.bin", i);

		if (!OwnedEntryAdd (out, n, full, data + foff, fsize))
		{
			ResetOwnedEntries (out, n);
			return ERR_CANT_CREATE;
		}
		n++;
	}

	if (!n)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

// Inverse of ScanSFZDAT, writing the strided form of both string sections.
//
// The sixth header word points at PlatinumGames' hash-lookup section, which
// this tool never reads (extraction walks the name array directly) and whose
// hash function has not been recovered here.  Rather than emit a bogus one,
// the section is written as a well-formed *empty* map: the four offsets are
// present and internally consistent, and the bucket table is a single
// all-ones (== "no entry") slot.  Archives rebuilt by this function
// therefore round-trip through this tool, but are NOT asserted to satisfy a
// game-side hash lookup.
enumError CreateSFZDAT (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0x40000)
		return EINVAL;

	ccp *ext = MALLOC (n_entries * sizeof (ccp));
	ccp *name = MALLOC (n_entries * sizeof (ccp));
	char *extbuf = MALLOC (n_entries * 8);
	if (!ext || !name || !extbuf)
	{
		FREE (ext);
		FREE (name);
		FREE (extbuf);
		return ERR_CANT_CREATE;
	}

	uint name_stride = 1;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp n0 = entries[i].name ? entries[i].name : "";
		ccp slash = strrchr (n0, '/');
		char *e = extbuf + i * 8;
		if (slash)
		{
			uint len = (uint)(slash - n0);
			if (len > 7)
				len = 7;
			memcpy (e, n0, len);
			e[len] = 0;
			name[i] = slash + 1;
		}
		else
		{
			// No "<ext>/" prefix: derive the type from the suffix.
			ccp dot = strrchr (n0, '.');
			uint len = dot ? (uint)strlen (dot + 1) : 0;
			if (len > 7)
				len = 7;
			if (len)
				memcpy (e, dot + 1, len);
			e[len] = 0;
			name[i] = n0;
		}
		ext[i] = e;
		const uint nl = (uint)strlen (name[i]) + 1;
		if (nl > name_stride)
			name_stride = nl;
	}
	name_stride = (name_stride + 3) & ~3u;
	if (name_stride > 256)
	{
		FREE (ext);
		FREE (name);
		FREE (extbuf);
		return EINVAL;
	}

	const u32 off_offsets = 32;
	const u32 off_exts = off_offsets + n_entries * 4;
	const u32 off_names = off_exts + n_entries * 4;
	const u32 off_sizes = off_names + 4 + n_entries * name_stride;
	const u32 off_hash = off_sizes + n_entries * 4;
	const u32 hash_size = 16 + 4; // header + one empty bucket slot (padded)
	u64 total = (u64)off_hash + hash_size;
	total = (total + 15) & ~(u64)15;
	const u64 data_start = total;
	for (uint i = 0; i < n_entries; i++)
	{
		total += entries[i].size;
		total = (total + 15) & ~(u64)15;
	}
	if (total > NFMT_MAX_OUTPUT)
	{
		FREE (ext);
		FREE (name);
		FREE (extbuf);
		return EFBIG;
	}

	u8 *out = CALLOC (1, (size_t)total);
	if (!out)
	{
		FREE (ext);
		FREE (name);
		FREE (extbuf);
		return ERR_CANT_CREATE;
	}

	memcpy (out, "DAT\0", 4);
	wr_be32 (out + 4, n_entries);
	wr_be32 (out + 8, off_offsets);
	wr_be32 (out + 12, off_exts);
	wr_be32 (out + 16, off_names);
	wr_be32 (out + 20, off_sizes);
	wr_be32 (out + 24, off_hash);
	wr_be32 (out + 28, 0);

	wr_be32 (out + off_names, name_stride);
	u64 cur = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		wr_be32 (out + off_offsets + i * 4, (u32)cur);
		wr_be32 (out + off_sizes + i * 4, entries[i].size);
		uint el = (uint)strlen (ext[i]);
		if (el > 3)
			el = 3;
		memcpy (out + off_exts + i * 4, ext[i], el);
		uint nl = (uint)strlen (name[i]);
		if (nl > name_stride - 1)
			nl = name_stride - 1;
		memcpy (out + off_names + 4 + i * name_stride, name[i], nl);
		if (entries[i].size)
			memcpy (out + cur, entries[i].data, entries[i].size);
		cur += entries[i].size;
		cur = (cur + 15) & ~(u64)15;
	}

	// Empty hash map: preHashShift, then three section offsets relative to
	// the map, then one bucket slot holding -1 ("empty").
	wr_be32 (out + off_hash, 31);
	wr_be32 (out + off_hash + 4, 16);
	wr_be32 (out + off_hash + 8, 20);
	wr_be32 (out + off_hash + 12, 20);
	wr_be16 (out + off_hash + 16, 0xffff);

	FREE (ext);
	FREE (name);
	FREE (extbuf);
	*dest = out;
	*dest_size = (uint)total;
	return ERR_OK;
}


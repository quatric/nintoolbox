#include "lib-std.h"
#include "lib-archive-util.h"
#include "lib-gfa.h"
#include <string.h>
#include <errno.h>

enumError DecodeLZ10Raw (u8 *dest, uint dest_size, const u8 *src, uint src_size)
{
	if (!dest || !src)
		return EINVAL;
	uint sp = 0, dp = 0;
	while (dp < dest_size)
	{
		if (sp >= src_size)
			return EINVAL;
		u8 flags = src[sp++];
		for (uint bit = 0; bit < 8 && dp < dest_size; bit++, flags <<= 1)
		{
			if (!(flags & 0x80))
			{
				if (sp >= src_size)
					return EINVAL;
				dest[dp++] = src[sp++];
			}
			else
			{
				if (sp + 2 > src_size)
					return EINVAL;
				const u8 a = src[sp++], b = src[sp++];
				const uint len = (a >> 4) + 3, back = ((a & 15) << 8 | b) + 1;
				if (back > dp || len > dest_size - dp)
					return EINVAL;
				for (uint i = 0; i < len; i++, dp++)
					dest[dp] = dest[dp - back];
			}
		}
	}
	return ERR_OK;
}

enumError DecodeBPE (u8 *dest, uint dest_size, const u8 *src, uint src_size)
{
	if (!dest || !src)
		return EINVAL;
	uint sp = 0, dp = 0;

	while (dp < dest_size)
	{
		u8 table[256][2];
		for (uint i = 0; i < 256; i++)
		{
			table[i][0] = (u8)i;
			table[i][1] = 0;
		}
		bool paired[256];
		memset (paired, 0, sizeof (paired));

		uint c = 0;
		while (c < 256)
		{
			if (sp >= src_size)
				return EINVAL;
			uint marker = src[sp++];

			uint entries;
			if (marker > 127)
			{
				c += marker - 127;
				if (c == 256)
					break;
				entries = 1;
			}
			else
				entries = marker + 1;

			for (uint i = 0; i < entries && c < 256; i++, c++)
			{
				if (sp >= src_size)
					return EINVAL;
				const u8 lc = src[sp++];
				table[c][0] = lc;
				if (lc != (u8)c)
				{
					if (sp >= src_size)
						return EINVAL;
					table[c][1] = src[sp++];
					paired[c] = true;
				}
			}
		}

		if (sp + 2 > src_size)
			return EINVAL;
		uint block_len = (uint)src[sp] << 8 | src[sp + 1];
		sp += 2;

		u8 stack[512];
		uint sn = 0;
		while (block_len || sn)
		{
			u8 b;
			if (sn)
				b = stack[--sn];
			else
			{
				if (sp >= src_size)
					return EINVAL;
				b = src[sp++];
				block_len--;
			}

			if (paired[b])
			{
				if (sn + 2 > sizeof (stack))
					return EINVAL;
				stack[sn++] = table[b][1];
				stack[sn++] = table[b][0];
			}
			else
			{
				if (dp >= dest_size)
					return EINVAL;
				dest[dp++] = b;
			}
		}
	}
	return ERR_OK;
}

// Real byte-pair compression for one block: repeatedly finds the most
// frequent adjacent pair of symbols still representable by an unused byte
// value, and substitutes it, until no byte value is free or no pair repeats
// often enough to be worth its table entry. Mirrors DecodeBPE's model
// exactly: table[c] is either the identity (c itself) or a pair of two
// earlier symbols that expand recursively. 'work' is resized in place as
// substitutions shrink it. 'freq' is a caller-owned scratch buffer of
// 256*256 uints, reused across blocks to avoid repeated large allocations.
static uint CompressBPEBlock (
	u8 *work, uint work_len, u8 table0[256], u8 table1[256], bool paired[256], uint *freq)
{
	bool present[256];
	// Once a byte value is used as either a pair's own code or as one of its
	// two components, its meaning is fixed for the rest of this block: every
	// earlier table entry that referenced it (as a component) or every later
	// occurrence of it as a fresh literal relies on that single, final
	// meaning, since the decoder has only one flat table with no notion of
	// "at the time this entry was defined". Reassigning it later -- even
	// after it stops appearing literally in 'work', having been fully
	// absorbed into a higher-level substitution -- would silently change
	// what every earlier reference to it decodes to.
	bool locked[256];
	memset (locked, 0, sizeof (locked));
	for (;;)
	{
		memset (present, 0, sizeof (present));
		for (uint i = 0; i < work_len; i++)
			present[work[i]] = true;

		int free_code = -1;
		for (int c = 0; c < 256; c++)
			if (!present[c] && !locked[c])
			{
				free_code = c;
				break;
			}
		if (free_code < 0 || work_len < 2)
			break;

		memset (freq, 0, 256 * 256 * sizeof (uint));
		for (uint i = 0; i + 1 < work_len; i++)
			freq[work[i] * 256 + work[i + 1]]++;

		uint best_count = 0;
		uint best_a = 0, best_b = 0;
		for (uint a = 0; a < 256; a++)
			for (uint b = 0; b < 256; b++)
			{
				const uint cnt = freq[a * 256 + b];
				if (cnt > best_count)
				{
					best_count = cnt;
					best_a = a;
					best_b = b;
				}
			}

		// A new pair entry costs at least 2 table bytes (plus up to ~1 byte of
		// extra marker fragmentation); each substituted occurrence after the
		// first saves 1 byte, so this is a conservative break-even threshold.
		if (best_count < 4)
			break;

		table0[free_code] = (u8)best_a;
		table1[free_code] = (u8)best_b;
		paired[free_code] = true;
		locked[free_code] = true;
		locked[best_a] = true;
		locked[best_b] = true;

		uint w = 0;
		for (uint i = 0; i < work_len;)
		{
			if (i + 1 < work_len && work[i] == best_a && work[i + 1] == best_b)
			{
				work[w++] = (u8)free_code;
				i += 2;
			}
			else
				work[w++] = work[i++];
		}
		work_len = w;
	}
	return work_len;
}

enumError EncodeBPE (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src)
		return EINVAL;

	// BPE divides data into blocks of up to 0x7FFF bytes. For each block:
	//   1. Header: a run-length-coded 256-entry table. Marker <=127 introduces
	//      (marker+1) explicit entries: one byte each for an identity mapping
	//      (byte == the entry's own index), or two bytes for a pair mapping
	//      (the two symbols it expands to, each possibly itself a pair).
	//      Marker >127 skips (marker-127) further identity entries with no
	//      explicit bytes at all.
	//   2. Block length (be16): the number of symbol bytes in the compressed
	//      block body below (not the block's decompressed size).
	//   3. The block body: 'block length' symbol bytes, each either a literal
	//      byte or a table-assigned pair code, expanded recursively on decode.
	const uint max_block = 0x7FFF;
	uint est_size = src_size + (src_size / max_block + 1) * 2048 + 64;
	u8 *out = MALLOC (est_size);
	if (!out)
		return ERR_OUT_OF_MEMORY;

	uint *freq = MALLOC (256 * 256 * sizeof (uint));
	if (!freq)
	{
		FREE (out);
		return ERR_OUT_OF_MEMORY;
	}

	uint sp = 0, dp = 0;
	do
	{
		uint cur_block = src_size - sp;
		if (cur_block > max_block)
			cur_block = max_block;

		u8 *work = cur_block ? MALLOC (cur_block) : 0;
		if (cur_block && !work)
		{
			FREE (out);
			FREE (freq);
			return ERR_OUT_OF_MEMORY;
		}
		if (cur_block)
			memcpy (work, src + sp, cur_block);

		u8 table0[256], table1[256];
		bool paired[256];
		for (uint i = 0; i < 256; i++)
		{
			table0[i] = (u8)i;
			table1[i] = 0;
		}
		memset (paired, 0, sizeof (paired));

		const uint compressed_len
			= cur_block ? CompressBPEBlock (work, cur_block, table0, table1, paired, freq) : 0;

		// Emit the table as explicit runs of up to 128 entries each (marker =
		// count-1, one or two bytes per entry). The decoder's skip marker
		// (>127) always consumes one more explicit entry immediately after
		// the skip -- unless the skip lands exactly on index 256 -- so it
		// cannot stand alone as a plain "skip N identities" and is not worth
		// the complexity here; explicit identity entries cost only 1 byte
		// each anyway.
		for (uint c = 0; c < 256;)
		{
			uint run = 256 - c;
			if (run > 128)
				run = 128;
			out[dp++] = (u8)(run - 1);
			for (uint i = 0; i < run; i++)
			{
				out[dp++] = table0[c + i];
				if (table0[c + i] != (u8)(c + i))
					out[dp++] = table1[c + i];
			}
			c += run;
		}

		// Block length (big-endian 16-bit)
		out[dp++] = (u8)(compressed_len >> 8);
		out[dp++] = (u8)(compressed_len & 0xFF);

		if (compressed_len)
			memcpy (out + dp, work, compressed_len);
		dp += compressed_len;

		FREE (work);
		sp += cur_block;
	} while (sp < src_size);

	FREE (freq);
	*dest = out;
	*dest_size = dp;
	return ERR_OK;
}

void ResetGFA (gfa_t *gfa)
{
	if (!gfa)
		return;
	FREE (gfa->blob);
	FREE (gfa->entries);
	FREE (gfa->names);
	memset (gfa, 0, sizeof (*gfa));
}

enumError ScanGFA (gfa_t *gfa, const u8 *data, uint size)
{
	if (!gfa || !data || size < 0x1c || memcmp (data, "GFAC", 4))
		return EINVAL;
	memset (gfa, 0, sizeof (*gfa));

	const u32 info_off = rd_le32 (data + 0x0c);
	const u32 data_off = rd_le32 (data + 0x14);
	const u32 data_size = rd_le32 (data + 0x18);

	if ((u64)info_off + 4 > size || (u64)data_off + 16 > size)
		return EINVAL;
	if ((u64)data_off + data_size > size)
		return EINVAL;

	const u32 n = rd_le32 (data + info_off);
	if (!n || n > 0x100000 || (u64)info_off + 4 + (u64)n * 16 > size)
		return EINVAL;

	const u8 *gfcp = data + data_off;
	if (memcmp (gfcp, "GFCP", 4))
		return EINVAL;
	const u32 zip = rd_le32 (gfcp + 8);
	const u32 out_len = rd_le32 (gfcp + 12);
	const u32 zsize = rd_le32 (gfcp + 16);
	if (!out_len || out_len > NFMT_MAX_OUTPUT)
		return EFBIG;
	if ((u64)20 + zsize > data_size)
		return EINVAL;

	u8 *blob = MALLOC (out_len);
	if (!blob)
		return ERR_CANT_CREATE;
	enumError err;
	switch (zip)
	{
		case 1:
			err = DecodeBPE (blob, out_len, gfcp + 20, zsize);
			break;
		case 2:
		case 3:
			err = DecodeLZ10Raw (blob, out_len, gfcp + 20, zsize);
			break;
		default:
			err = EINVAL;
			break;
	}
	if (err)
	{
		FREE (blob);
		return err;
	}

	gfa_entry_t *entries = CALLOC (n, sizeof (*entries));
	char *names = CALLOC (1, size);
	if (!entries || !names)
	{
		FREE (blob);
		FREE (entries);
		FREE (names);
		return ERR_CANT_CREATE;
	}
	uint name_pos = 0;

	const u8 *rec = data + info_off + 4;
	for (uint i = 0; i < n; i++, rec += 16)
	{
		u32 name_off = rd_le32 (rec + 4) & 0x00ffffff;
		const u32 fsize = rd_le32 (rec + 8);
		u32 offset = rd_le32 (rec + 12);

		if (name_off >= size)
			name_off = 0;

		entries[i].name = names + name_pos;
		if (name_off)
		{
			uint j = name_off;
			while (j < size && data[j] && name_pos + 1 < size)
				names[name_pos++] = (char)data[j++];
		}
		names[name_pos++] = 0;

		entries[i].size = fsize;
		entries[i].offset = offset >= data_off ? offset - data_off : offset;
		if (fsize && (entries[i].offset > out_len || fsize > out_len - entries[i].offset))
		{
			entries[i].size = 0;
			entries[i].offset = 0;
		}
	}

	gfa->blob = blob;
	gfa->blob_size = out_len;
	gfa->entries = entries;
	gfa->n_entries = n;
	gfa->names = names;
	gfa->compression = zip;
	return ERR_OK;
}

// Read just the GFCP compression id (1=BPE, 2/3=raw LZ10) from an existing
// on-disk .gfa file's header, without decompressing its payload. Used so a
// re-CREATE can re-encode with the same scheme the original used instead of
// always forcing LZ10 (see the CreateGFA 'compression' parameter).
enumError PeekGFACompression (ccp path, uint *compression)
{
	if (!path || !compression)
		return EINVAL;

	FILE *f = fopen (path, "rb");
	if (!f)
		return ERR_CANT_OPEN;

	u8 head[0x1c];
	const size_t n = fread (head, 1, sizeof (head), f);
	if (n < sizeof (head) || memcmp (head, "GFAC", 4))
	{
		fclose (f);
		return EINVAL;
	}

	const u32 data_off = rd_le32 (head + 0x14);
	u8 gfcp[20];
	if (fseeko (f, data_off, SEEK_SET) || fread (gfcp, 1, sizeof (gfcp), f) < sizeof (gfcp)
		|| memcmp (gfcp, "GFCP", 4))
	{
		fclose (f);
		return EINVAL;
	}
	fclose (f);

	*compression = rd_le32 (gfcp + 8);
	return ERR_OK;
}

// GFA's 16-byte entry record carries an opaque 4-byte value at rec+0 that
// ScanGFA() never reads -- it just walks entries linearly by index -- but
// which is not a standard 0x65 SARC-style name hash (verified against
// retail files: computed hashes don't match, and entries aren't even sorted
// by it), so its exact meaning/derivation is unknown. Whatever it is, the
// game apparently *does* care about it: repacking an archive that zeroed it
// out (the previous behavior of CreateGFA, since 'entries' here has nowhere
// to carry it) reportedly crashes the game when it accesses the rebuilt
// content. Since we can't recompute it, the only safe option is to carry the
// existing value over unchanged for entries that already existed in the
// archive being replaced -- see ReadGFAHashHints() and its use in
// create_gfa_dir() (wszst_cmd/create_archive_formats.inc). New entries that have no prior record
// get 0, same as before; that's a pre-existing limitation, not a regression.
enumError ReadGFAHashHints (ccp path, ParamField_t *out)
{
	if (!path || !out)
		return EINVAL;

	FILE *f = fopen (path, "rb");
	if (!f)
		return ERR_CANT_OPEN;

	enumError err = EINVAL;
	u8 *data = 0;
	do
	{
		if (fseeko (f, 0, SEEK_END))
			break;
		const off_t fsize = ftello (f);
		if (fsize < 0x1c || fseeko (f, 0, SEEK_SET))
			break;

		u8 head[0x1c];
		if (fread (head, 1, sizeof (head), f) < sizeof (head) || memcmp (head, "GFAC", 4))
			break;

		const u32 info_off = rd_le32 (head + 0x0c);
		const u32 data_off = rd_le32 (head + 0x14);
		if ((u64)info_off + 4 > (u64)fsize || (u64)data_off > (u64)fsize)
			break;

		// Read everything up to the compressed data blob: that's the info
		// table plus the name table, all we need without decompressing.
		const uint head_size = data_off;
		data = MALLOC (head_size);
		if (!data || fseeko (f, 0, SEEK_SET) || fread (data, 1, head_size, f) < head_size)
			break;

		const u32 n = rd_le32 (data + info_off);
		if (!n || n > 0x100000 || (u64)info_off + 4 + (u64)n * 16 > head_size)
			break;

		const u8 *rec = data + info_off + 4;
		for (uint i = 0; i < n; i++, rec += 16)
		{
			const u32 hash = rd_le32 (rec + 0);
			u32 name_off = rd_le32 (rec + 4) & 0x00ffffff;
			if (!name_off || name_off >= head_size)
				continue;
			if (!memchr (data + name_off, 0, head_size - name_off))
				continue;
			InsertParamField (out, (ccp)data + name_off, false, hash, 0);
		}
		err = ERR_OK;
	} while (0);

	FREE (data);
	fclose (f);
	return err;
}

enumError CreateGFA (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries,
	uint n_entries, uint compression, const ParamField_t *hash_hint)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0x100000)
		return EINVAL;

	uint payload_size = 0;
	uint names_size = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		if (!entries[i].name)
			return EINVAL;
		names_size += strlen (entries[i].name) + 1;
		payload_size += entries[i].size;
	}

	u8 *payload = CALLOC (1, payload_size ? payload_size : 1);
	if (!payload)
		return ERR_CANT_CREATE;

	uint current_offset = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		if (entries[i].size)
		{
			memcpy (payload + current_offset, entries[i].data, entries[i].size);
			current_offset += entries[i].size;
		}
	}

	// Preserve whatever scheme the source archive actually used (see the
	// 'compression' parameter doc in lib-gfa.h): re-encoding a BPE archive
	// as LZ10 on every CREATE just because LZ10 was this function's original
	// hardcoded default silently changes every repacked file's compression
	// format even though nothing in its content changed.
	// Every retail .gfa this format has been checked against (2342/2342 in
	// one full game) uses BPE, never LZ10 -- LZ10 support exists only to
	// decode/round-trip files that happen to use it, not because it's a
	// realistic default. Default here to what real archives actually are.
	if (compression != 1 && compression != 2 && compression != 3)
		compression = 1; // no/invalid hint (e.g. brand new archive): default to BPE
	const bool use_bpe = compression == 1;

	u8 *zdata = 0;
	uint zsize = 0;
	enumError err = use_bpe ? EncodeBPE (&zdata, &zsize, payload, payload_size)
							: EncodeLZ10Raw (&zdata, &zsize, payload, payload_size);
	FREE (payload);
	if (err)
		return err;

	const uint info_off = 0x20;
	const uint names_off = info_off + 4 + 16 * n_entries;
	uint data_off = names_off + names_size;
	data_off = (data_off + 3) & ~3u;

	const uint data_size = 20 + zsize;
	const uint total_size = data_off + data_size;

	u8 *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (zdata);
		return ERR_CANT_CREATE;
	}

	memcpy (out, "GFAC", 4);
	wr_le32 (out + 0x0c, info_off);
	wr_le32 (out + 0x14, data_off);
	wr_le32 (out + 0x18, data_size);

	wr_le32 (out + info_off, n_entries);

	uint name_pos = 0;
	current_offset = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		const nintendo_sarc_entry_t *e = entries + i;
		u8 *rec = out + info_off + 4 + 16 * i;
		const ParamFieldItem_t *hint = hash_hint ? FindParamField (hash_hint, e->name) : 0;
		wr_le32 (rec + 0, hint ? hint->num : 0);
		wr_le32 (rec + 4, names_off + name_pos);
		wr_le32 (rec + 8, e->size);
		wr_le32 (rec + 12, data_off + current_offset);

		size_t nlen = strlen (e->name) + 1;
		memcpy (out + names_off + name_pos, e->name, nlen);
		name_pos += nlen;
		current_offset += e->size;
	}

	u8 *gfcp = out + data_off;
	memcpy (gfcp, "GFCP", 4);
	wr_le32 (gfcp + 8, compression);
	wr_le32 (gfcp + 12, payload_size);
	wr_le32 (gfcp + 16, zsize);
	memcpy (gfcp + 20, zdata, zsize);
	FREE (zdata);

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}

enumError create_gfa_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
	{
		// Re-encode with whatever compression the archive being replaced
		// already used (BPE vs raw LZ10), so a plain content edit doesn't
		// also silently flip the archive's compression format. No existing
		// 'dest' (a brand new archive) falls back to CreateGFA's own default.
		uint compression = 0;
		PeekGFACompression (dest, &compression);

		// See CreateGFA's 'hash_hint' doc in lib-gfa.h: rec+0 of each entry
		// carries an opaque value the game apparently needs but that isn't a
		// recomputable name hash, so it must be carried over from the
		// archive being replaced rather than left zeroed.
		ParamField_t hash_hint;
		InitializeParamField (&hash_hint);
		ReadGFAHashHints (dest, &hash_hint);
		err = CreateGFA (&data, &size, list.entry, list.used, compression, &hash_hint);
		ResetParamField (&hash_hint);
	}
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, dest);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

// Wii Virtual Console formats (CCF archive, romc/romchu ROM compression) --
// see lib-vc.h.

#include "lib-std.h"
#include "lib-vc.h"
#include "lib-nintendo.h"
#include <zlib.h>

static inline u32 vrd32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

//-----------------------------------------------------------------------------
///////////////		CCF archive				///////////////
//-----------------------------------------------------------------------------

// Layout per the WiiBrew "CCF archive" page: 32-byte header ("CCF\0" +
// 12 zero bytes + offset_multiplier(u32) + file_count(u32) + 8 zero bytes),
// then file_count 32-byte descriptors (name[20] + data_offset(u32, in units
// of offset_multiplier) + size(u32) + decompressed_size(u32)). Not yet
// cross-checked against a real CCF-magic file -- none has turned up in any
// real retail Wii VC WAD examined so far (Yoshi's Story N64 stores its ROM
// completely uncompressed with no CCF wrapper at all; Kirby 64 N64 uses
// "romc" compression -- see DecodeRomC() -- applied directly, again with no
// CCF wrapper). Struct layout cross-checked against TWO independent
// sources that agree byte-for-byte: the WiiBrew wiki's documented struct
// AND paulguy/ccf-tools' actual reference C source (ccfex.c: `struct
// ccffile { char filename[20]; uint offset; uint datasize; uint filesize;
// }`, decompressed via zlib's standard uncompress(), matching this file's
// inflateInit2(&zs,15) -- not raw deflate). That repo's later commit
// history ("Updated to handle different chunk sizes found in Switch SEGA
// AGES games") confirms CCF also appears on Switch, not just Wii VC -- a
// lead worth chasing if a Wii sample keeps not turning up. Bounds-checked
// the usual way, but flagged here as unverified-against-real-bytes until
// an actual CCF-magic file is found.
enumError ScanCCF (ccf_t *ccf, const u8 *data, uint size)
{
	if (!ccf || !data || size < 32 || memcmp (data, "CCF\0", 4))
		return EINVAL;

	const u32 mult = vrd32 (data + 16);
	const u32 n = vrd32 (data + 20);
	if (!mult || !n || n > 0x10000 || (u64)32 + (u64)n * 32 > size)
		return EINVAL;

	ccf_entry_t *entries = CALLOC (n, sizeof (*entries));
	if (!entries)
		return ERR_CANT_CREATE;

	for (u32 i = 0; i < n; i++)
	{
		const u8 *d = data + 32 + (size_t)i * 32;
		memcpy (entries[i].name, d, 20);
		entries[i].name[20] = 0;
		const u32 off_units = vrd32 (d + 20);
		const u32 fsize = vrd32 (d + 24);
		const u32 dsize = vrd32 (d + 28);
		const u64 off = (u64)off_units * mult;
		if (off + fsize > size)
		{
			FREE (entries);
			return EINVAL;
		}
		entries[i].data = data + off;
		entries[i].size = fsize;
		entries[i].decompressed_size = dsize;
	}

	memset (ccf, 0, sizeof (*ccf));
	ccf->data = data;
	ccf->size = size;
	ccf->n_entries = n;
	ccf->entries = entries;
	return ERR_OK;
}

void ResetCCF (ccf_t *ccf)
{
	if (!ccf)
		return;
	FREE (ccf->entries);
	memset (ccf, 0, sizeof (*ccf));
}

enumError DecodeCCFEntry (u8 **dest, uint *dest_size, const ccf_entry_t *entry)
{
	if (!dest || !dest_size || !entry || !entry->data)
		return EINVAL;

	if (entry->decompressed_size == entry->size)
	{
		// stored raw
		u8 *out = MALLOC (entry->size);
		if (!out)
			return ERR_CANT_CREATE;
		memcpy (out, entry->data, entry->size);
		*dest = out;
		*dest_size = entry->size;
		return ERR_OK;
	}

	// Zlib-compressed (standard zlib stream, per the WiiBrew doc's
	// "pass into ZLib's deflate()/zpipe" wording -- i.e. a plain
	// compress()-style stream with the normal 2-byte zlib header, not
	// raw deflate).
	u8 *out = MALLOC (entry->decompressed_size);
	if (!out)
		return ERR_CANT_CREATE;

	z_stream zs;
	memset (&zs, 0, sizeof (zs));
	zs.next_in = (Bytef *)entry->data;
	zs.avail_in = entry->size;
	zs.next_out = out;
	zs.avail_out = entry->decompressed_size;
	if (inflateInit2 (&zs, 15) != Z_OK)
	{
		FREE (out);
		return ERR_ERROR;
	}
	const int rc = inflate (&zs, Z_FINISH);
	const uint produced = entry->decompressed_size - zs.avail_out;
	inflateEnd (&zs);
	if (rc != Z_STREAM_END || produced != entry->decompressed_size)
	{
		FREE (out);
		return ERR_INVALID_DATA;
	}

	*dest = out;
	*dest_size = produced;
	return ERR_OK;
}

enumError CreateCCF (u8 **dest, uint *dest_size, const struct nintendo_sarc_entry_t *entries,
	uint n_entries, bool compress)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xFFFF)
		return EINVAL;

	const u32 mult = 32;
	const u32 table_size = 32 + (u32)n_entries * 32;
	const u32 data_start = (table_size + mult - 1) & ~(mult - 1);

	typedef struct
	{
		u8 *data;
		u32 stored_size;
		u32 decomp_size;
		bool is_alloced;
	} ccf_payload_t;

	ccf_payload_t *payloads = CALLOC (n_entries, sizeof (ccf_payload_t));
	if (!payloads)
		return ERR_CANT_CREATE;

	u32 current_offset = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		payloads[i].decomp_size = entries[i].size;
		payloads[i].stored_size = entries[i].size;
		payloads[i].data = (u8 *)entries[i].data;
		payloads[i].is_alloced = false;

		if (compress && entries[i].size > 32)
		{
			uLongf comp_bound = compressBound (entries[i].size);
			u8 *comp_buf = MALLOC (comp_bound);
			if (comp_buf)
			{
				if (compress2 (comp_buf, &comp_bound, entries[i].data, entries[i].size, 6) == Z_OK
					&& comp_bound < entries[i].size)
				{
					payloads[i].data = comp_buf;
					payloads[i].stored_size = (u32)comp_bound;
					payloads[i].is_alloced = true;
				}
				else
				{
					FREE (comp_buf);
				}
			}
		}
		current_offset = (current_offset + payloads[i].stored_size + mult - 1) & ~(mult - 1);
	}

	u8 *out = CALLOC (1, current_offset);
	if (!out)
	{
		for (uint i = 0; i < n_entries; i++)
			if (payloads[i].is_alloced)
				FREE (payloads[i].data);
		FREE (payloads);
		return ERR_CANT_CREATE;
	}

	// Header
	memcpy (out, "CCF\0", 4);
	out[16] = (u8)(mult >> 24);
	out[17] = (u8)(mult >> 16);
	out[18] = (u8)(mult >> 8);
	out[19] = (u8)mult;
	out[20] = (u8)(n_entries >> 24);
	out[21] = (u8)(n_entries >> 16);
	out[22] = (u8)(n_entries >> 8);
	out[23] = (u8)n_entries;

	// Descriptors + Data
	u32 file_off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		u8 *d = out + 32 + i * 32;
		ccp name = entries[i].name ? entries[i].name : "";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;
		strncpy ((char *)d, name, 20);

		u32 off_units = file_off / mult;
		d[20] = (u8)(off_units >> 24);
		d[21] = (u8)(off_units >> 16);
		d[22] = (u8)(off_units >> 8);
		d[23] = (u8)off_units;
		d[24] = (u8)(payloads[i].stored_size >> 24);
		d[25] = (u8)(payloads[i].stored_size >> 16);
		d[26] = (u8)(payloads[i].stored_size >> 8);
		d[27] = (u8)payloads[i].stored_size;
		d[28] = (u8)(payloads[i].decomp_size >> 24);
		d[29] = (u8)(payloads[i].decomp_size >> 16);
		d[30] = (u8)(payloads[i].decomp_size >> 8);
		d[31] = (u8)payloads[i].decomp_size;

		if (payloads[i].stored_size > 0 && payloads[i].data)
			memcpy (out + file_off, payloads[i].data, payloads[i].stored_size);

		file_off = (file_off + payloads[i].stored_size + mult - 1) & ~(mult - 1);
		if (payloads[i].is_alloced)
			FREE (payloads[i].data);
	}
	FREE (payloads);

	*dest = out;
	*dest_size = current_offset;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		romc / romchu ROM compression		///////////////
//-----------------------------------------------------------------------------

// N64 Virtual Console's own ROM compressor (real tool name "romc", per a
// 2008 GBAtemp thread -- "For encoding a rom for use in the latest versions
// of the Nintendo 64 Virtual Console emulator... Currently supported by
// games such as Cruis'n USA and Kirby 64"). Not every N64 VC title uses it
// -- verified on two real retail titles: Yoshi's Story (USA) stores its ROM
// completely uncompressed (a plain file literally named "rom" inside the
// emulator app's U8 archive, real N64 header, no compression at all), while
// Kirby 64 - The Crystal Shards (USA) has a file named "romc" in the same
// location that IS compressed with this format.
//
// Header (4 bytes, found by inspecting the real Kirby 64 "romc" file and
// cross-checked against Plombo/vcromclaim's romc.py, itself a from-scratch
// reimplementation predating any sample access here):
//   byte 0: number of 4MB units in the DECOMPRESSED size (the N64 VC
//           emulator always maps a fixed-size cart image regardless of the
//           real game's ROM size -- Kirby 64 is a real 12MB cart but
//           decompresses to a full 32MB image, byte 0 = 8).
//   bytes 1-2: unused/reserved in both samples (always 0 so far).
//   byte 3, low 2 bits: compression type. 1 = plain LZ77 (this fork's
//           already-verified DecodeLZ10LZ11, type 0x10 -- romc's own
//           stream has no embedded tag/size prefix the way a standalone
//           .lz10 file would, so one is synthesized here before handing
//           off to the shared decoder). 2 = "romchu" (LZ77 + a Huffman
//           second pass) -- NOT implemented; no real sample of type 2 has
//           been found yet, so this declines (EINVAL) rather than guess.
//
// Verified byte-exact on the real Kirby 64 sample: decompresses to exactly
// 33554432 bytes (8 * 4MB), starting with the real N64 magic (0x80371240)
// and the correct internal ROM name ("Kirby64") at the standard offset
// 0x20.
enumError DecodeRomC (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 4)
		return EINVAL;

	const uint units = src[0];
	const uint out_len = units * 4 * 1024 * 1024;
	const uint comp_type = src[3] & 3;
	if (!units || !out_len)
		return EINVAL;

	if (comp_type == 1)
	{
		// This is the same LZ77/LZ10 token stream this fork's own
		// DecodeLZ10LZ11() already decodes -- but that function derives its
		// output size from a *24-bit* header field (max 16MB), and a real
		// romc stream can decompress past that (Kirby 64's real sample is a
		// full 32MB image). romc's own 4-byte header already carries the
		// real size as a plain byte count (units*4MB, not clamped to 24
		// bits), so this can't just synthesize a standard LZ10 header and
		// hand off to the shared decoder -- an earlier attempt at exactly
		// that silently truncated the reconstructed size to its low 24 bits
		// (33554432 -> 131072) before this was caught. Duplicated token-
		// stream loop instead, identical to DecodeLZ10LZ11's LZ10 branch,
		// parametrized on the real 32-bit out_len.
		u8 *out = MALLOC (out_len);
		if (!out)
			return ERR_CANT_CREATE;
		uint sp = 4, dp = 0;
		while (dp < out_len)
		{
			if (sp >= src_size)
				goto invalid;
			u8 flags = src[sp++];
			for (uint bit = 0; bit < 8 && dp < out_len; bit++, flags <<= 1)
			{
				if (!(flags & 0x80))
				{
					if (sp >= src_size)
						goto invalid;
					out[dp++] = src[sp++];
				}
				else
				{
					if (sp + 2 > src_size)
						goto invalid;
					const u8 a = src[sp++], b = src[sp++];
					const uint len = (a >> 4) + 3;
					const uint back = ((uint)(a & 15) << 8 | b) + 1;
					if (back > dp || len > out_len - dp)
						goto invalid;
					for (uint k = 0; k < len; k++)
					{
						out[dp] = out[dp - back];
						dp++;
					}
				}
			}
		}
		*dest = out;
		*dest_size = out_len;
		return ERR_OK;
	invalid:
		FREE (out);
		return EINVAL;
	}

	// comp_type == 2 (romchu/Huffman) or anything else: not implemented.
	return EINVAL;
}

// Emit a type-1 romc stream. Literal-only LZ10 tokens are intentional: the
// format has no requirement that a back-reference be used, and this avoids
// the quadratic cost of the small-file LZ10 encoder on 32/64MB ROM images.
// The VC header expresses the output size only in whole 4MB units, so inputs
// that cannot be represented losslessly are rejected rather than padded.
enumError EncodeRomC (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	const uint unit = 4u * 1024 * 1024;
	if (!dest || !dest_size || !src || !src_size || src_size % unit || src_size / unit > 255)
		return EINVAL;
	const u64 total = 4ull + src_size + (src_size + 7ull) / 8;
	if (total > UINT_MAX)
		return EFBIG;
	u8 *out = MALLOC ((size_t)total);
	if (!out)
		return ERR_CANT_CREATE;
	out[0] = (u8)(src_size / unit);
	out[1] = out[2] = 0;
	out[3] = 1;
	uint sp = 0, dp = 4;
	while (sp < src_size)
	{
		out[dp++] = 0; // eight literal tokens
		uint n = src_size - sp < 8 ? src_size - sp : 8;
		memcpy (out + dp, src + sp, n);
		dp += n;
		sp += n;
	}
	*dest = out;
	*dest_size = dp;
	return ERR_OK;
}

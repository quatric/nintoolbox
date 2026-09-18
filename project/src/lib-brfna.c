// Wii BRFNA font-animation format -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

enumError EncodeBRFNA_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, uint cell_w, uint cell_h)
{
	if (!dest || !dest_size || !rgba || !width || !height)
		return ERR_SEMANTIC;
	*dest = 0;
	*dest_size = 0;

	if (!cell_w)
		cell_w = (width >= 16) ? 16 : width;
	if (!cell_h)
		cell_h = (height >= 16) ? 16 : height;
	uint cols = width / cell_w;
	uint rows = height / cell_h;
	if (!cols)
		cols = 1;
	if (!rows)
		rows = 1;
	uint n_chars = cols * rows;

	uint tw = (width + 3) & ~3u;
	uint th = (height + 3) & ~3u;
	uint tex_size = tw * th * 4;
	u8 *tex_data = CALLOC (1, tex_size);
	if (!tex_data)
		return ERR_OUT_OF_MEMORY;

	uint out_idx = 0;
	for (uint by = 0; by < th; by += 4)
	{
		for (uint bx = 0; bx < tw; bx += 4)
		{
			// 16 AR pairs
			for (uint y = 0; y < 4; y++)
			{
				for (uint x = 0; x < 4; x++)
				{
					uint px = bx + x;
					uint py = by + y;
					u8 r = 0, a = 0;
					if (px < width && py < height)
					{
						uint idx = (py * width + px) * 4;
						r = rgba[idx];
						a = rgba[idx + 3];
					}
					tex_data[out_idx++] = a;
					tex_data[out_idx++] = r;
				}
			}
			// 16 GB pairs
			for (uint y = 0; y < 4; y++)
			{
				for (uint x = 0; x < 4; x++)
				{
					uint px = bx + x;
					uint py = by + y;
					u8 g = 0, b = 0;
					if (px < width && py < height)
					{
						uint idx = (py * width + px) * 4;
						g = rgba[idx + 1];
						b = rgba[idx + 2];
					}
					tex_data[out_idx++] = g;
					tex_data[out_idx++] = b;
				}
			}
		}
	}

	u8 *packed_sheet = 0;
	uint packed_size = 0;
	enumError lz_err = EncodeLZ10LZ11 (&packed_sheet, &packed_size, tex_data, tex_size, false);
	FREE (tex_data);
	if (lz_err)
		return lz_err;

	uint comp_chunk_size = 4 + packed_size;
	uint tglp_size = (0x30 + comp_chunk_size + 3) & ~3u;
	uint cwdh_payload_size = n_chars * 3;
	uint cwdh_size = (0x10 + cwdh_payload_size + 3) & ~3u;
	uint cmap_size = 0x14;
	uint total_size = 0x10 + 0x20 + tglp_size + cwdh_size + cmap_size;

	u8 *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (packed_sheet);
		return ERR_OUT_OF_MEMORY;
	}

	// Header (16 bytes)
	memcpy (out, "RFNA", 4);
	wr_be16 (out + 4, 0xFEFF); // BOM
	wr_be16 (out + 6, 0x0104); // version
	wr_be32 (out + 8, total_size);
	wr_be16 (out + 12, 0x0010); // header size
	wr_be16 (out + 14, 0x0004); // sections count

	// FINF (32 bytes) at offset 0x10
	u8 *finf = out + 0x10;
	memcpy (finf, "FINF", 4);
	wr_be32 (finf + 4, 0x00000020);
	finf[8] = 0; // glyph
	finf[9] = (u8)cell_h; // line feed
	wr_be16 (finf + 10, 0); // alter char
	finf[12] = 0; // left space
	finf[13] = (u8)cell_w; // glyph width
	finf[14] = (u8)cell_w; // char width
	finf[15] = 0; // UTF-8 encoding

	uint tglp_off = 0x30;
	uint cwdh_off = tglp_off + tglp_size;
	uint cmap_off = cwdh_off + cwdh_size;

	wr_be32 (finf + 16, tglp_off + 8); // ptr to TGLP data
	wr_be32 (finf + 20, cwdh_off + 8); // ptr to CWDH data
	wr_be32 (finf + 24, cmap_off + 8); // ptr to CMAP data
	finf[28] = (u8)cell_h; // height
	finf[29] = (u8)cell_w; // width
	finf[30] = (u8)(cell_h > 2 ? cell_h - 2 : cell_h); // ascent
	finf[31] = 0; // reserved

	// TGLP at offset 0x30
	u8 *tglp = out + tglp_off;
	memcpy (tglp, "TGLP", 4);
	wr_be32 (tglp + 4, tglp_size);
	tglp[8] = (u8)cell_w;
	tglp[9] = (u8)cell_h;
	tglp[10] = (u8)(cell_h > 2 ? cell_h - 2 : cell_h); // baseline
	tglp[11] = (u8)cell_w; // max char width
	wr_be32 (tglp + 12, tex_size); // uncompressed sheet size
	wr_be16 (tglp + 16, 1); // sheet count
	wr_be16 (tglp + 18, 0x8006); // format = compressed RGBA8
	wr_be16 (tglp + 20, (u16)rows);
	wr_be16 (tglp + 22, (u16)cols);
	wr_be16 (tglp + 24, (u16)width);
	wr_be16 (tglp + 26, (u16)height);
	wr_be32 (tglp + 28, tglp_off + 0x30); // data offset

	wr_be32 (tglp + 0x30, packed_size); // 4-byte BE compressed size prefix
	memcpy (tglp + 0x34, packed_sheet, packed_size);
	FREE (packed_sheet);

	// CWDH at cwdh_off
	u8 *cwdh = out + cwdh_off;
	memcpy (cwdh, "CWDH", 4);
	wr_be32 (cwdh + 4, cwdh_size);
	wr_be16 (cwdh + 8, 0); // first index
	wr_be16 (cwdh + 10, (u16)(n_chars - 1)); // last index
	wr_be32 (cwdh + 12, 0); // next cwdh
	for (uint i = 0; i < n_chars; i++)
	{
		cwdh[16 + i * 3] = 0;
		cwdh[16 + i * 3 + 1] = (u8)cell_w;
		cwdh[16 + i * 3 + 2] = (u8)cell_w;
	}

	// CMAP at cmap_off
	u8 *cmap = out + cmap_off;
	memcpy (cmap, "CMAP", 4);
	wr_be32 (cmap + 4, cmap_size);
	wr_be16 (cmap + 8, 0x0020); // first char code
	wr_be16 (cmap + 10, (u16)(0x0020 + n_chars - 1)); // last char code
	wr_be16 (cmap + 12, 0); // direct mapping
	wr_be16 (cmap + 14, 0); // reserved
	wr_be32 (cmap + 16, 0); // next cmap
	wr_be32 (cmap + 20, 0); // index offset

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}
// [[brfna-compress]] Archived-font (.brfna) TGLP sheets whose sheetFormat has
// bit 0x8000 set store each sheet as a 4-byte-BE-size-prefixed compressed
// chunk instead of raw GX pixel data. This is a proprietary, undocumented
// codec RE'd from nw4r_fontcvtr.exe (no written spec exists anywhere in the
// SDK) -- decompiled via Ghidra and cross-checked against real retail
// samples (round-tripped through the actual Nintendo tool under Wine to get
// byte-exact ground truth, then independently verified by rendering decoded
// output and confirming legible glyph shapes). See the brfna_archived_font_
// format memory for the full RE trail. Three of the four opcodes are
// implemented (the fourth, 0x80, is a delta/predictive table encoder never
// observed on real pixel-sheet data and is treated as unsupported).

// nibble 0x10: classic byte-oriented LZSS. One control byte selects 8
// following operations MSB-first: 0-bit = literal byte, 1-bit = a 2-byte
// (length,distance) back-reference into the growing output itself
// (overlapping copies allowed, same as any textbook LZ77 variant).
static uint DecodeBRFNA_LZSS (const u8 *src, uint src_size, uint target, u8 *dest, uint dest_size)
{
	uint out = 0, in = 0;
	while (out < target && in < src_size)
	{
		u8 ctrl = src[in++];
		for (uint bit = 0; bit < 8 && out < target; bit++, ctrl <<= 1)
		{
			if (ctrl & 0x80)
			{
				if (in + 1 >= src_size)
					return out;
				const u8 b0 = src[in], b1 = src[in + 1];
				in += 2;
				const uint len = (b0 >> 4) + 3;
				const uint dist = (b1 | (b0 & 0xF) << 8) + 1;
				if (dist > out)
					return out;
				uint base = out - dist;
				for (uint k = 0; k < len && out < target; k++, out++)
				{
					if (out >= dest_size)
						return out;
					dest[out] = dest[base + k];
				}
			}
			else
			{
				if (in >= src_size || out >= dest_size)
					return out;
				dest[out++] = src[in++];
			}
		}
	}
	return out;
}

// nibble 0x30: simple RLE. Each control byte is either a literal-run count
// (bit7 clear: copy count+1 raw bytes) or a repeat-run (bit7 set: repeat the
// one following byte (count&0x7F)+3 times).
static uint DecodeBRFNA_RLE (const u8 *src, uint src_size, uint target, u8 *dest, uint dest_size)
{
	uint out = 0, in = 0;
	while (out < target && in < src_size)
	{
		const u8 b = src[in];
		if (b & 0x80)
		{
			if (in + 1 >= src_size)
				return out;
			const u8 val = src[in + 1];
			const uint count = (b & 0x7F) + 3;
			in += 2;
			for (uint k = 0; k < count && out < target; k++, out++)
			{
				if (out >= dest_size)
					return out;
				dest[out] = val;
			}
		}
		else
		{
			const uint count = b + 1;
			in++;
			for (uint k = 0; k < count && out < target; k++, out++)
			{
				if (in >= src_size || out >= dest_size)
					return out;
				dest[out] = src[in++];
			}
		}
	}
	return out;
}

// nibble 0x20: a canonical-Huffman-style bit-walk whose code tree is embedded
// directly in the token's own bytes (word-stride array at the token start,
// each entry packing a 6-bit "keep scanning" skip-length plus two branch-
// decision flag bits) rather than a separately transmitted table. The 32-bit
// bit-accumulator's source position is itself computed relative to the same
// token bytes (tok[0]+1)*2), so the whole thing is self-contained per call.
// low_nibble selects the output packing: exactly 8 means one decoded byte
// written per finalize event; any other value means two finalize events are
// packed into one output byte (low nibble first, high nibble second).
static uint DecodeBRFNA_Huffman (
	const u8 *tok, uint tok_size, uint target, u8 *dest, uint dest_size, u8 low_nibble)
{
	if (tok_size < 2)
		return 0;
	u8 bl = tok[1];
	bool half_pending = false;
	u8 half_val = 0;
	uint out = 0, cx = 1;
	uint bitptr = ((uint)tok[0] + 1) * 2 & 0xFFFF;

	while (out < target)
	{
		if ((u64)bitptr + 4 > tok_size)
			return out;
		u32 word = tok[bitptr] | tok[bitptr + 1] << 8 | tok[bitptr + 2] << 16
			| (u32)tok[bitptr + 3] << 24;
		bitptr += 4;
		for (uint b = 0; b < 32 && out < target; b++)
		{
			const uint bit = word >> 31 & 1;
			word <<= 1;
			const bool finalize = bit ? (bl & 0x40) != 0 : (bl & 0x80) != 0;
			const uint idx = cx * 2 + bit;
			if (idx >= tok_size)
				return out;
			if (!finalize)
			{
				bl = tok[idx];
				cx += (bl & 0x3F) + 1;
				continue;
			}
			const u8 val = tok[idx];
			if (low_nibble == 8)
			{
				if (out >= dest_size)
					return out;
				dest[out++] = val;
			}
			else if (half_pending)
			{
				if (out >= dest_size)
					return out;
				dest[out++] = half_val | val << 4;
				half_pending = false;
			}
			else
			{
				half_val = val;
				half_pending = true;
			}
			if (out >= target)
				return out;
			bl = tok[1];
			cx = 1;
		}
	}
	return out;
}

// Outer per-sheet dispatch: reads a 4-byte token (LE32), the top 3 bytes are
// this call's output-byte target, the low nibble of the low byte selects the
// codec. Every real sample checked so far uses exactly one token per sheet
// (the token's own target already equals the full sheet size), so this
// deliberately doesn't implement the ping-pong multi-token chaining the real
// decoder supports for the general case -- if a real file ever needs more
// than one token per sheet this returns false (caller treats as unsupported)
// rather than silently emitting a partially-decoded sheet.
bool DecompressBRFNASheet (const u8 *comp, uint comp_size, u8 *dest, uint dest_size)
{
	if (comp_size < 4)
		return false;
	const u32 word = comp[0] | comp[1] << 8 | comp[2] << 16 | (u32)comp[3] << 24;
	const uint target = word >> 8;
	const uint opcode = word & 0xF0;
	const uint low_nibble = word & 0xF;
	const u8 *payload = comp + 4;
	const uint payload_size = comp_size - 4;
	if (!target || target > dest_size)
		return false;

	uint produced;
	switch (opcode)
	{
		case 0x10:
			produced = DecodeBRFNA_LZSS (payload, payload_size, target, dest, dest_size);
			break;
		case 0x20:
			produced
				= DecodeBRFNA_Huffman (payload, payload_size, target, dest, dest_size, low_nibble);
			break;
		case 0x30:
			produced = DecodeBRFNA_RLE (payload, payload_size, target, dest, dest_size);
			break;
		default:
			return false; // 0x80 (delta table) or an escape/unknown opcode
	}
	return produced >= target;
}

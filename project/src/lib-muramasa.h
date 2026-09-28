// SPDX-License-Identifier: GPL-2.0+
// "Muramasa - The Demon Blade" (Wii) proprietary asset formats. No public
// documentation of any of these exists (checked XeNTaX, GBAtemp,
// Models-Resource, romhacking.net -- all empty on this title). Everything
// below was reverse-engineered against the retail disc: the container
// format by extracting files/ and cross-checking real samples of each
// extension, and the FCMP compression itself by disassembling the game's
// main.dol (USA v1.0, entry 0x8000403c) in Ghidra and decompiling the
// actual decoder at 0x802c09e0 (called, with the ring-buffer/history
// state block as param_1, from FUN_800976dc @ 0x800976dc, which in turn
// reads a loading-screen FCMP/FTEX blob picked by FUN_800973c0 based on
// the console's language setting).
//
// Three formats are covered:
//
// (1) A generic "FCMP" compressed container that wraps seven of the
//     on-disc extensions (".mbs", ".ftx", ".esb", ".nsb", ".abf", ".nms",
//     ".wbf"), each holding a fixed, distinct typed inner sub-blob tag.
//     Confirmed outer header, verified by re-decompressing every single
//     FCMP file on the retail disc (1454/1454, spanning all seven
//     extensions -- see fcmp_inner_map[] in lib-muramasa.c):
//       char magic[4];      // "FCMP" (fixed)
//       u32  decomp_size;   // LE. Exact decompressed payload size --
//                             // confirmed equal (not just >=) to the
//                             // actual LZSS output length in all 1454
//                             // samples.
//       u32  reserved;      // LE. Observed exactly 0x12340000 in every
//                             // sample checked -- looks like a fixed
//                             // sentinel/version constant, meaning still
//                             // not otherwise understood (never read by
//                             // the decoder at 0x802c09e0 beyond the
//                             // fixed-value check below).
//     -- 12 bytes total -- followed immediately, with NO further framing,
//     by the raw LZSS bitstream (see part below). There is no separate
//     "flag byte" in the header: what earlier black-box analysis took for
//     a 13th header byte ending in nibble 0xf is actually just the FIRST
//     control-flag byte of the LZSS stream itself (see below), which
//     happens to often end in 0xf on this disc simply because an all-or
//     mostly-literal opening run is common.
//
//     COMPRESSION: classic Haruhiko Okumura-style LZSS, byte-for-byte the
//     same scheme used in the reference `lzss.c` shared across countless
//     commercial titles, with parameters:
//       N (ring buffer / window size) = 4096, zero-initialized
//       F (max match length)          = 18
//       THRESHOLD                     = 2
//       initial window position       = N - F = 0xfee
//     Bitstream, read starting at header offset 12: a control byte is
//     read every time the previously-read control byte's 8 flag bits (LSB
//     first) are exhausted; bit==1 means "copy one literal byte from the
//     stream to output (and to the ring buffer)"; bit==0 means a 2-byte
//     match token follows: byte0 = low 8 bits of a 12-bit ring-buffer
//     offset, byte1's high nibble = high 4 bits of that offset, byte1's
//     low nibble = (match length - 3). The match is copied byte-by-byte
//     from the ring buffer (wrapping mod 4096) to output, and each copied
//     byte is also written back into the ring buffer at the current
//     write position (so overlapping self-referencing copies work).
//     Decoding stops once `decomp_size` output bytes have been produced.
//     See DecompressMuramasaFcmp() in lib-muramasa.c for the reference
//     implementation, verified against every FCMP file on the disc.
//
//     The inner sub-blob tag (first 4 bytes of the DECOMPRESSED payload,
//     not the compressed bytes) is a hard 1:1 mapping with the on-disc
//     extension, confirmed across every sample (no exceptions):
//       ".mbs" -> "FMBS" (642/642 samples)
//       ".ftx" -> "FTEX" (610/610 samples)
//       ".esb" -> "EMBP" (74/74 samples)
//       ".nsb" -> "NSBD" (60/60 samples)
//       ".abf" -> "MLIB" (57/57 samples)
//       ".nms" -> "NMSB" (10/10 samples)
//       ".wbf" -> "WOLD" (1/1 sample -- world-map data)
//
// (2) ".otb" -- "OTB " table. Confirmed outer header (only 2 real samples
//     exist on this disc -- both are the same underlying table, once
//     region-neutral and once under files/_US/, so the values below are
//     corroborating rather than independently confirming):
//       char magic[4];     // "OTB " (fixed, note trailing space)
//       u32  body_size;    // LE.
//       u32  header_size;  // LE. Confirmed body_size + header_size ==
//                            // file_size exactly in both samples (observed
//                            // value 0x260 / 608 in both).
//       u32  count;        // LE. Observed 0x7e (126) in both samples.
//     followed by a dense table (u16 offset-looking entries with many
//     0xffff "unused" slots, then what looks like a second, separate
//     table of BE-ish u32 values further in) that was NOT reverse-
//     engineered -- too few distinct samples (both essentially the same
//     table) to confidently pin down a per-entry record shape. Only the
//     header fields above are decoded/reported.
//
// (3) ".nsi" -- "NSI " sound info table. Confirmed header (1 sample on
//     this disc):
//       char magic[4];     // "NSI " (fixed, note trailing space)
//       u32  body_size;    // LE.
//       u32  tail_size;    // LE. Confirmed body_size + tail_size ==
//                            // file_size exactly.
//     followed by a sparse table of small LE u16/u32 values (mostly
//     zero-padded) that was NOT reverse-engineered -- only one real
//     sample exists on this disc, not enough to distinguish a per-entry
//     record shape from incidental zero padding. Only the header fields
//     above are decoded/reported.
#ifndef LIB_MURAMASA_H
#define LIB_MURAMASA_H 1

#include "lib-std.h"

//-----------------------------------------------------------------------------
// (1) "FCMP" compressed container wrapping ".mbs"/".ftx"/".esb"/".nsb"/
// ".abf"/".nms"/".wbf" -- outer header, inner sub-blob tag AND the LZSS
// compressed payload are all decoded (see above).
int IsMuramasaFcmp (const u8 *data, size_t size, size_t file_size);
enumError DecodeMuramasaFcmp_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

// Returns the exact decompressed size announced by the header, or 0 if
// 'data' is not a valid FCMP container.
u32 GetDecompressedSizeMuramasaFcmp (const void *data, size_t data_size);

// Decompress an FCMP container's LZSS payload.
//	returns:
//	    ERR_OK:           decompression completed, wrote exactly
//	                       'dest_buf_size' bytes
//	    ERR_WARNING:      silent==true: dest buffer too small or source
//	                       data exhausted early
//	    ERR_INVALID_DATA: invalid source data (bad magic/header, or
//	                       corrupt/truncated LZSS stream)
enumError DecompressMuramasaFcmp (const void *data, // source data (starting at "FCMP")
	size_t data_size, // size of 'data'
	void *dest_buf, // destination buffer (decompressed data)
	size_t dest_buf_size, // size of 'dest_buf' == wanted output size
	size_t *write_status, // NULL or returns number of bytes written
	ccp fname, // NULL or file name for error messages
	bool silent // true: don't print error messages
);

//-----------------------------------------------------------------------------
// (2) ".otb" "OTB " table -- header only, entry table not decoded.
int IsMuramasaOtb (const u8 *data, size_t size, size_t file_size);
enumError DecodeMuramasaOtb_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (3) ".nsi" "NSI " sound info table -- header only, entry table not decoded.
int IsMuramasaNsi (const u8 *data, size_t size, size_t file_size);
enumError DecodeMuramasaNsi_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // LIB_MURAMASA_H

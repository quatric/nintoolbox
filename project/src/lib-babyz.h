// SPDX-License-Identifier: GPL-2.0+
// "Imagine: Party Babyz" (Wii, Ubisoft, id6 R8BE41) proprietary asset
// container. No public documentation of this format exists (checked
// XeNTaX, GBAtemp, Models-Resource, romhacking.net -- all empty on this
// title). The same "!Ce\x87" magic and container layout also appear
// throughout Ubisoft's shared "Wizard" tooling used across the whole
// Imagine Wii shovelware line (Petz, Fashion Designer, etc.), but only
// this title's disc has been checked, so the format is documented here
// under this game's name rather than claimed as a general Ubisoft
// standard.
//
// Reverse-engineered by disassembling the retail disc's main.dol (USA,
// entry 0x8000403c) in Ghidra: the file-load path (FUN_800194d8, called
// e.g. from resource-loading helper FUN_800b4d98) reads a file's 12-byte
// header, and if the first word matches the magic below, hands the
// remaining bytes to the LZSS decoder at 0x8001987c -- decompiled and
// reimplemented as DecompressBabyzWiz() below. Confirmed by
// re-decompressing every single magic-tagged file extracted from the
// disc's files/babyz/ tree: 3865/3865 succeeded with the decompressed
// output landing at EXACTLY the header's announced size (0 mismatches),
// across all eleven extensions the container wraps.
//
// Container layout (12-byte header, all three fields little-endian
// despite the surrounding disc/executable being big-endian PowerPC --
// the loader explicitly byte-swaps them after reading):
//   u32 magic;         // bytes on disc: 21 43 65 87 (read as a big-endian
//                       // u32 this is the classic 0x21436587 endian-test
//                       // constant; stored/read LE here, so compare raw
//                       // bytes rather than a swapped constant)
//   u32 comp_size;     // LE. Size in bytes of the LZSS bitstream that
//                       // immediately follows the header. Confirmed
//                       // exact (not just an upper bound) in every
//                       // sample checked.
//   u32 decomp_size;   // LE. Exact decompressed output size -- confirmed
//                       // equal to the real LZSS output length in every
//                       // sample checked.
// -- 12 bytes total -- followed immediately by the raw LZSS bitstream,
// with no further framing.
//
// Extensions confirmed wrapped by this container (all magic-gated, no
// exceptions found among the disc's ~4200 files/babyz/* files):
//   .wiz  scene/level definition          .wsp  sprite/texture
//   .wsn  skeleton/motion data             .tan  texture animation
//   .wan  animation                        .msk  blend/morph mask
//   .spt  sound-effect data                .snd  sound bank
//   .wik  UI widget/menu panel             .lmc  "iLive" live-move capture
//   .eff  particle/visual effect
// Also wraps extensionless "_sys"-style generated-source files under
// files/babyz/env/*/.c and .h (plain ASCII once decompressed -- these
// were the easiest samples to eyeball-verify the algorithm against).
// NOT wrapped (no magic, different/no container): .cam (raw float
// camera keyframes), .spd (looks like raw/differently-encoded audio,
// high entropy, no header), .db (OLE/Thumbs.db), .wt/.thp/.mid/.tpl
// (standard/unrelated formats).
//
// COMPRESSION: classic Haruhiko Okumura-style LZSS, byte-for-byte the
// same reference scheme used in countless commercial titles (also seen
// in this codebase for Muramasa's FCMP container -- see lib-muramasa.h --
// but with a different match-length encoding, see below), with
// parameters:
//   N (ring buffer / window size) = 4096, space (0x20) initialized
//   F (max match length)          = 18
//   THRESHOLD                     = 3
//   initial window position       = N - F = 0xfee
// Bitstream: a control byte is read every time the previously-read
// control byte's 8 flag bits (LSB first) are exhausted; bit==1 means
// "copy one literal byte from the stream to output (and to the ring
// buffer)"; bit==0 means a 2-byte match token follows: byte0 = low 8
// bits of a 12-bit ring-buffer offset, byte1's high nibble = high 4 bits
// of that offset, byte1's low nibble = (match length - 3) (NOTE: unlike
// Muramasa's FCMP, which subtracts 2, this decoder's unrolled copy loop
// confirms the match length is nibble+3, range 3..18). The match is
// copied byte-by-byte from the ring buffer (wrapping mod 4096) to
// output, and each copied byte is also written back into the ring
// buffer at the current write position. Decoding stops once
// 'comp_size' input bytes have been consumed (not once a target output
// size is reached, matching the real decoder's loop condition). See
// DecompressBabyzWiz() below for the reference implementation, verified
// against every magic-tagged file on the disc.

#ifndef LIB_BABYZ_H
#define LIB_BABYZ_H 1

#include "lib-std.h"

//-----------------------------------------------------------------------------
// "!Ce\x87" LZSS compressed container -- outer header AND the LZSS
// compressed payload are both decoded (see above).
int IsBabyzWiz (const u8 *data, size_t size, size_t file_size);
enumError DecodeBabyzWiz_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

// Returns the exact decompressed size announced by the header, or 0 if
// 'data' is not a valid container.
u32 GetDecompressedSizeBabyzWiz (const void *data, size_t data_size);

// Decompress a container's LZSS payload.
//	returns:
//	    ERR_OK:           decompression completed, wrote exactly
//	                       'dest_buf_size' bytes
//	    ERR_WARNING:      silent==true: dest buffer too small or source
//	                       data exhausted early
//	    ERR_INVALID_DATA: invalid source data (bad magic/header, or
//	                       corrupt/truncated LZSS stream)
enumError DecompressBabyzWiz (const void *data, // source data (starting at the magic)
	size_t data_size, // size of 'data'
	void *dest_buf, // destination buffer (decompressed data)
	size_t dest_buf_size, // size of 'dest_buf' == wanted output size
	size_t *write_status, // NULL or returns number of bytes written
	ccp fname, // NULL or file name for error messages
	bool silent // true: don't print error messages
);

#endif // LIB_BABYZ_H

#ifndef LIB_ARIKA_H
#define LIB_ARIKA_H

#include "lib-nintendo.h"

//-----------------------------------------------------------------------------
// Arika archives: a "rom:\INFO.DAT" / "rom:\GAME.DAT" file pair used by
// Arika's DS/DSi titles -- Dr. Mario Online Rx, Dr. Mario Express, the
// original (DS) Endless Ocean, and (per its own non-encrypted variant, see
// below) Endless Ocean: Blue World.  INFO.DAT holds an obfuscated directory
// table; GAME.DAT holds the member payloads, each stored either raw or
// ALZ1-compressed.  Layout and algorithms per GBATEK's "DS Encrypted Arika
// Archives with ALZ1 compression" page.
//
// INFO.DAT header (all fields little-endian):
//   000h 10h   Title (e.g. "*Dr.Mario-DSi!!!"), doubles as the decryption
//              key; a title starting with 00h marks the file unencrypted --
//              this is also how Endless Ocean: Blue World's *ARK variant
//              (whose first 4 bytes are the magic 00h 'A' 'R' 'K') ends up
//              handled for free by the same decrypt-or-not check.
//   010h 14h   Zerofilled
//   024h 4     Sector size (0 defaults to 800h)
//   028h 4     Unknown, reportedly a version field
//   02Ch 4     Number of directory entries
//   030h N*30h Directory entries
// Directory entry (30h bytes):
//   000h 20h   Filename (ASCII, zero-padded; unused slots are all-zero)
//   020h 4     Size on disk in GAME.DAT, in bytes (== decompressed size when
//              the entry is stored raw)
//   024h 4     Offset in GAME.DAT, in sector units
//   028h 4     Size in sector units (informational, not needed to extract)
//   02Ch 4     Decompressed size in bytes
//
// Decryption (applied to bytes [10h, filesize) only, when byte 0 != 0):
//   buf[i] = ((buf[i] ror 4) xor FFh) - buf[i AND 0Fh]
// where the subtracted byte is always one of the untouched 16 title/key
// bytes at the very start of the file (the loop never touches those).

enumError DecryptArikaInfo (u8 *buf, uint size);

// Inverse of DecryptArikaInfo, derived algebraically from GBATEK's decrypt
// formula (nibble-swap is its own inverse, so only the add/subtract and the
// two xor's need un-doing): buf[i] = ror4( (buf[i] + key[i&0xF]) xor FFh ).
// A no-op when buf[0..0xF] (the key/title) starts with a 00h byte, matching
// the "unencrypted" convention DecryptArikaInfo reads on the other side.
enumError EncryptArikaInfo (u8 *buf, uint size);

// Arika ALZ1 compression: near-identical to classic LZSS (4096-byte ring
// buffer, initial write pointer FEEh, matches encoded as [dict_lsb,
// dict_msb<<4|(len-3)] with len 3..18), except the flag byte is inverted
// (0=compressed) and consumed LSB-first. Operates on the bitstream *after*
// the 4-byte "ALZ1" magic (or the 8-byte "ZALZ" header used by a couple of
// older titles, which shares the same bitstream per aluigi's arika.bms) --
// callers skip that header themselves, same convention as DecodeLZ10Raw.
enumError DecodeALZ1 (u8 *dest, uint dest_size, const u8 *src, uint src_size);
enumError EncodeALZ1 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

// Extracts both INFO.DAT and GAME.DAT into a flat entry list. Entries whose
// raw (uncompressed) GAME.DAT slice itself starts with an "RF2" tag are
// recursively split into their own sub-entries -- a nested grouping table
// Endless Ocean: Blue World uses for related assets (e.g. a character's rig
// plus its sub-meshes); see ScanArika()'s definition for the RF2 layout.
// Unrecognised/out-of-range entries are skipped rather than aborting the
// whole archive.
enumError ExtractArika (nintendo_sarc_entry_t **out_entries, uint *out_n_entries,
	const u8 *info_data, uint info_size, const u8 *game_data, uint game_size);

// Builds a fresh INFO.DAT/GAME.DAT pair from a flat entry list (no RF2
// re-nesting -- entries are written back exactly as given). TITLE becomes
// the 16-byte INFO.DAT key; pass one starting with a non-NUL byte to get an
// encrypted archive, or NULL/an empty string for an unencrypted one. When
// COMPRESS is set, each member is ALZ1-encoded and only kept compressed if
// that's actually smaller than storing it raw.
enumError CreateArika (u8 **dest_info, uint *dest_info_size, u8 **dest_game, uint *dest_game_size,
	const nintendo_sarc_entry_t *entries, uint n_entries, ccp title, bool compress);

#endif
